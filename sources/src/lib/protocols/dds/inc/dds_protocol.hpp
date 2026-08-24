#ifndef DDS_PROTOCOL_HPP
#define DDS_PROTOCOL_HPP

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

/**
 * @brief Pure OMG DDSI-RTPS 2.3 protocol codec (the wire format OpenDDS,
 * RTI Connext, CycloneDDS, FastDDS, ... all speak, and the interoperability
 * protocol NGVA/STANAG 4754 mandates on top of whichever DDS vendor is
 * used — see the "DDS – the data exchange mechanism" section of the NGVA
 * white paper this plugin was requested against).
 *
 * Same split as MqttProtocol (mqtt_protocol.hpp): this class only ever
 * builds/parses RTPS Messages, Submessages and the CDR-encoded
 * ParameterList payloads the two built-in discovery protocols (SPDP —
 * participant discovery — and SEDP — endpoint/topic discovery) carry. It
 * never touches a socket; that is entirely DdsDriver's (dds_driver.hpp)
 * job, the same way MqttTransport owns MqttProtocol's sockets.
 *
 * Scope of this codec (documented here once, referenced by DdsDriver and
 * the plugin's INFO text rather than repeated everywhere):
 *   - RTPS message header + submessage kinds: INFO_TS, DATA, DATA_FRAG,
 *     HEARTBEAT, ACKNACK, and the ParameterList payload both SPDP and
 *     SEDP DATA submessages carry.
 *   - Reliability: HEARTBEAT/ACKNACK are implemented at sample (not
 *     fragment) granularity — a partially-received fragmented sample is
 *     simply "still missing" until every fragment arrives, so it gets
 *     re-requested (and fully re-sent, all fragments) via the same
 *     ACKNACK path as a lost unfragmented sample. NACK_FRAG/
 *     HEARTBEAT_FRAG (which would let a reader ask for only the missing
 *     fragments of a large sample) are not implemented — see DdsDriver's
 *     class doc comment if per-fragment retransmission is ever needed.
 *   - No GAP submessage (a writer never proactively tells a reader "don't
 *     bother NACKing this range" — a reader that outlives a writer's
 *     history cache just keeps NACKing something the writer can no
 *     longer answer; harmless, just a periodic wasted ACKNACK).
 *   - IPv4 (LOCATOR_KIND_UDPv4) and IPv6 (LOCATOR_KIND_UDPv6) both
 *     supported by `Locator`; a given DdsDriver instance is single-stack
 *     (see dds_driver.hpp's Config::useIpv6) but the wire format itself
 *     doesn't care.
 *   - Unkeyed (NO_KEY) user topics: one sample in, one sample out, no DDS
 *     instance/key model — topics are addressed by name only, exactly
 *     like an MQTT topic string.
 *   - PL_CDR_LE / CDR_LE (little-endian) encapsulation only.
 */
class DdsProtocol
{
public:
    // -------------------------------------------------------------------
    // Core RTPS identifiers
    // -------------------------------------------------------------------
    using GuidPrefix = std::array<uint8_t, 12>;
    using EntityId   = std::array<uint8_t, 4>;

    struct Guid {
        GuidPrefix prefix{};
        EntityId   entityId{};
        bool operator==(const Guid& o) const { return prefix == o.prefix && entityId == o.entityId; }
        bool operator<(const Guid& o) const {
            return prefix != o.prefix ? (prefix < o.prefix) : (entityId < o.entityId);
        }
    };

    static constexpr uint32_t kLocatorKindInvalid = 0;
    static constexpr uint32_t kLocatorKindUdpV4    = 1;
    static constexpr uint32_t kLocatorKindUdpV6    = 2;

    struct Locator {
        uint32_t kind = kLocatorKindInvalid;
        uint32_t port = 0;
        std::array<uint8_t, 16> address{}; // IPv4 mapped into the last 4 bytes; IPv6 uses all 16

        bool valid() const { return (kind == kLocatorKindUdpV4 || kind == kLocatorKindUdpV6) && port != 0; }
        bool isV6() const { return kind == kLocatorKindUdpV6; }
        std::string toIpString() const; // "a.b.c.d" (v4) or the canonical "::"-form (v6)
        static Locator fromIpv4Port(const std::string& ip, uint16_t port);
        static Locator fromIpv6Port(const std::string& ip, uint16_t port);
        /// Auto-detects family by the presence of ':' in ip (crude but
        /// sufficient — IPv4 dotted-quad never contains one).
        static Locator fromIpPort(const std::string& ip, uint16_t port);
    };

    // Well-known entity ids (RTPS spec 9.3.1.2 / 8.5.4.3)
    static const EntityId kEntityIdUnknown;
    static const EntityId kEntityIdParticipant;
    static const EntityId kEntityIdSedpPubAnnouncer;   // SEDP built-in publications writer
    static const EntityId kEntityIdSedpPubDetector;    // SEDP built-in publications reader
    static const EntityId kEntityIdSedpSubAnnouncer;   // SEDP built-in subscriptions writer
    static const EntityId kEntityIdSedpSubDetector;    // SEDP built-in subscriptions reader
    static const EntityId kEntityIdSpdpAnnouncer;      // SPDP built-in participant writer
    static const EntityId kEntityIdSpdpDetector;       // SPDP built-in participant reader

    // BuiltinEndpointSet bits (RTPS spec 8.5.3.3) — this driver advertises
    // (and only ever needs) the participant + SEDP publication/subscription pairs.
    static constexpr uint32_t kBepParticipantAnnouncer = 1u << 0;
    static constexpr uint32_t kBepParticipantDetector  = 1u << 1;
    static constexpr uint32_t kBepPublicationAnnouncer = 1u << 2;
    static constexpr uint32_t kBepPublicationDetector  = 1u << 3;
    static constexpr uint32_t kBepSubscriptionAnnouncer= 1u << 4;
    static constexpr uint32_t kBepSubscriptionDetector = 1u << 5;
    static constexpr uint32_t kBepAllUsed = kBepParticipantAnnouncer | kBepParticipantDetector |
                                             kBepPublicationAnnouncer | kBepPublicationDetector |
                                             kBepSubscriptionAnnouncer | kBepSubscriptionDetector;

    /// Derives a stable, non-built-in user EntityId from a topic name, so
    /// two independent processes publishing/subscribing to the same topic
    /// name agree on the same wire entity id without any extra handshake
    /// (mirrors how an MQTT topic string alone is enough to "address" a
    /// stream). kindByte is the low entity-kind octet — see
    /// kUserWriterKind/kUserReaderKind below.
    static constexpr uint8_t kUserWriterKind = 0x03; // NO_KEY, user-defined writer
    static constexpr uint8_t kUserReaderKind = 0x04; // NO_KEY, user-defined reader
    static EntityId makeUserEntityId(const std::string& topicName, uint8_t kindByte);

    // -------------------------------------------------------------------
    // Discovery payload models (already-decoded ParameterList content)
    // -------------------------------------------------------------------
    struct ParticipantInfo {
        GuidPrefix  prefix{};
        std::string participantName;
        Locator     metaUnicastLocator;   // where to send this participant's SEDP traffic
        Locator     userUnicastLocator;   // where to send this participant's user DATA traffic
        uint32_t    leaseDurationSec = 20;
        uint32_t    builtinEndpointSet = 0;
        uint32_t    domainId = 0;
    };

    struct EndpointInfo {
        Guid        guid;
        std::string topicName;
        std::string typeName;
        bool        isWriter = false;
        bool        reliable = false; // false == BEST_EFFORT
    };

    struct DataSubmessage {
        EntityId              readerId{};
        EntityId              writerId{};
        int64_t                seqNum = 0;
        std::vector<uint8_t>  serializedPayload; // encapsulation header stripped
        bool                  isPlCdr = false;    // true => payload is a ParameterList (SPDP/SEDP)
    };

    struct DataFragSubmessage {
        EntityId              readerId{};
        EntityId              writerId{};
        int64_t                writerSN = 0;
        uint32_t               fragmentStartingNum = 1; // 1-based index of the first fragment carried here
        uint16_t               fragmentsInSubmessage = 1;
        uint32_t               fragmentSize = 0;
        uint32_t               sampleSize = 0; // total size of the unfragmented serializedPayload
        std::vector<uint8_t>   fragmentData; // exactly fragmentSize bytes (fewer for the last fragment)
    };

    struct HeartbeatSubmessage {
        EntityId  readerId{};
        EntityId  writerId{};
        int64_t   firstSeqNum = 0; // oldest sample still in the writer's history
        int64_t   lastSeqNum = 0;  // newest sample the writer has sent
        int32_t   count = 0;
        bool      finalFlag = false; // true => reader need not ACKNACK if it has everything
    };

    struct AckNackSubmessage {
        EntityId               readerId{};
        EntityId               writerId{};
        std::vector<int64_t>   missingSeqNums; // absolute sequence numbers the reader is still missing
        int64_t                readerHasEverythingUpTo = 0; // highest contiguous seq the reader confirms received
        int32_t                count = 0;
        bool                   finalFlag = false;
    };

    /// One parsed RTPS Message's worth of submessages this codec cares
    /// about — see parseMessage() below. PAD/GAP/unknown submessage kinds
    /// are silently skipped (see class doc comment's GAP note).
    struct RtpsMessage {
        GuidPrefix srcPrefix{};
        std::vector<DataSubmessage>      data;
        std::vector<DataFragSubmessage>  dataFrags;
        std::vector<HeartbeatSubmessage> heartbeats;
        std::vector<AckNackSubmessage>   acknacks;
    };

    // -------------------------------------------------------------------
    // Message / submessage builders — pure encode, no I/O
    // -------------------------------------------------------------------

    /// Wraps one or more already-built submessages (see below) into a
    /// complete RTPS Message with the standard header.
    static std::vector<uint8_t> buildMessage(const GuidPrefix& srcPrefix,
                                              const std::vector<std::vector<uint8_t>>& submessages);

    static std::vector<uint8_t> buildInfoTsSubmessageNow();

    /// serializedPayload is the already-CDR-encoded sample (user payload,
    /// or a ParameterList produced by buildSpdpPayload()/buildSedpPayload()).
    static std::vector<uint8_t> buildDataSubmessage(const EntityId& readerId, const EntityId& writerId,
                                                      int64_t seqNum, std::span<const uint8_t> serializedPayload,
                                                      bool isPlCdr);

    /// One DATA_FRAG submessage carrying exactly one fragment (see
    /// DataFragSubmessage — this codec always sends one fragment per
    /// submessage, never batches several; simpler, costs a few extra
    /// bytes of submessage-header overhead per fragment). `fragmentData`
    /// must be exactly `fragmentSize` bytes (`sampleSize` for the last
    /// fragment if it's short).
    static std::vector<uint8_t> buildDataFragSubmessage(const EntityId& readerId, const EntityId& writerId,
                                                          int64_t writerSN, uint32_t fragmentStartingNum,
                                                          uint32_t fragmentSize, uint32_t sampleSize,
                                                          std::span<const uint8_t> fragmentData);

    static std::vector<uint8_t> buildHeartbeatSubmessage(const EntityId& readerId, const EntityId& writerId,
                                                           int64_t firstSeqNum, int64_t lastSeqNum,
                                                           int32_t count, bool finalFlag);

    /// missingSeqNums need not be sorted or contiguous; encoded as an RTPS
    /// SequenceNumberSet (bitmapBase = the smallest value in the set, or
    /// readerHasEverythingUpTo+1 if missingSeqNums is empty; capped at the
    /// spec's 256-bit window — anything beyond base+255 is dropped and
    /// will simply be re-requested by a later ACKNACK once it enters the
    /// window).
    static std::vector<uint8_t> buildAckNackSubmessage(const EntityId& readerId, const EntityId& writerId,
                                                         int64_t readerHasEverythingUpTo,
                                                         const std::vector<int64_t>& missingSeqNums,
                                                         int32_t count, bool finalFlag);

    /// Builds the ParameterList payload of an SPDP ParticipantData sample
    /// (RTPS spec 8.5.3) — hand the result to buildDataSubmessage() with
    /// isPlCdr=true and reader/writerId = kEntityIdUnknown/kEntityIdSpdpAnnouncer.
    static std::vector<uint8_t> buildSpdpPayload(const ParticipantInfo& info);
    static bool parseSpdpPayload(std::span<const uint8_t> plCdrPayload, ParticipantInfo& out);

    /// Builds the ParameterList payload of a SEDP DiscoveredWriter/ReaderData
    /// sample (RTPS spec 8.5.4) describing one local endpoint.
    static std::vector<uint8_t> buildSedpPayload(const Guid& endpointGuid, const GuidPrefix& participantPrefix,
                                                   const std::string& topicName, const std::string& typeName,
                                                   bool reliable);
    static bool parseSedpPayload(std::span<const uint8_t> plCdrPayload, bool isWriterTopic, EndpointInfo& out);

    /// User-data CDR encoding: this driver treats topic samples as opaque
    /// byte strings (see class doc comment's "Unkeyed" note), encoded as a
    /// single CDR `string` (matches an IDL `struct { string data; }` — the
    /// simplest possible NGVA-style "@topic"'d struct — so a genuinely
    /// spec-following OpenDDS peer using that convention can decode it; a
    /// peer expecting a different IDL type will still receive the bytes,
    /// just needs its own type-specific decode).
    static std::vector<uint8_t> encodeUserPayload(const std::string& data);
    static bool decodeUserPayload(std::span<const uint8_t> serializedPayload, std::string& out);

    // -------------------------------------------------------------------
    // Message parser
    // -------------------------------------------------------------------

    /// Parses one complete RTPS Message (as received in a single UDP
    /// datagram — RTPS messages are always datagram-aligned on this
    /// transport) into every submessage kind this codec understands.
    /// Returns false only if the header itself is malformed; an
    /// individual unparseable submessage is skipped (logged by the
    /// caller) rather than aborting the whole datagram, matching how real
    /// RTPS stacks tolerate unknown/malformed submessages from newer peers.
    static bool parseMessage(std::span<const uint8_t> buf, RtpsMessage& out);

private:
    DdsProtocol() = delete;
};

#endif // DDS_PROTOCOL_HPP
