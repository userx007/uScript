#ifndef DDS_DRIVER_HPP
#define DDS_DRIVER_HPP

#include "ICommDriver.hpp"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/**
 * @brief The "driver side" of the DDS plugin — CONFIG storage translation,
 * the DDS.CMD intermediary command parsing, and per-topic pub/sub state —
 * so DdsPlugin (dds_plugin.hpp) can stay a thin, high-level shell, exactly
 * like MqttDriver is to MqttPlugin (see mqtt_driver.hpp's class doc
 * comment for the general shape this follows).
 *
 * Three-way split, per plugin architecture guideline #12:
 *   - **Protocol side**: `uDdsProtocol` (protocols/dds) — just the one
 *     generic IDL sample type every DDS.CMD topic is published/
 *     subscribed as, compiled via Cyclone DDS's own `idlc` — see
 *     protocols/dds/idl/ucmdexec_dds.idl's doc comment.
 *   - **Driver side**: this class. Wraps the Eclipse Cyclone DDS C API
 *     (`<dds/dds.h>`, target `CycloneDDS::ddsc`) — https://github.com/eclipse-cyclonedds/cyclonedds —
 *     instead of owning raw RTPS UDP sockets directly: one `dds_entity_t`
 *     participant per `DdsDriver`, one Cyclone topic+writer per PUBLISHed
 *     topic name, one Cyclone topic+reader per SUBSCRIBEd topic name.
 *     Cyclone's own DDSI-RTPS stack now supplies everything the old
 *     hand-rolled implementation used to do itself: SPDP/SEDP discovery,
 *     HEARTBEAT/ACKNACK reliability, DATA_FRAG fragmentation/reassembly,
 *     IPv4 *and* IPv6, and interop with any other real DDSI-RTPS
 *     implementation (OpenDDS, RTI Connext, FastDDS, ...) — not just
 *     against another instance of this plugin.
 *   - **Plugin side**: `DdsPlugin` — stores CONFIG (domain id, interface,
 *     participant name, ...), builds this class's Config from it, and
 *     supplies send()/receive() to `ucmdexec::generic_cmd()`/
 *     `generic_script()` as the `pfsend`/`pfrecv` override — see
 *     dds_plugin.cpp, mirroring mqtt_plugin.cpp exactly. This side is
 *     unchanged by the Cyclone DDS switch: CONFIG keys, DDS.CMD syntax,
 *     and the ini file format are all identical to before.
 *
 * Wire data model: every DDS.CMD topic — regardless of name — is
 * published/subscribed as the single generic IDL type
 * `ucmdexec_dds::GenericSample { string payload; }` (see
 * protocols/dds/idl/ucmdexec_dds.idl). This preserves the previous
 * driver's "unkeyed, topic-addressed-by-name-only, opaque string sample"
 * model (like an MQTT topic string) without depending on any
 * vendor-specific *per-topic* IDL code generation — one fixed type,
 * compiled once, covers every topic a script names at runtime. A
 * genuinely IDL-typed peer (e.g. a real NGVA subsystem publishing its own
 * generated type on the same topic name) will not type-match this driver
 * unless it also happens to use this exact `{ string payload; }` shape —
 * this is the same interop trade-off the previous implementation's class
 * doc comment documented for its own hand-rolled `{ string data; }` CDR
 * encoding, just now backed by a real DDS type system instead of manual
 * bytes.
 *
 * QoS mapping from Config (see open()'s doc comment for the full
 * per-field mapping onto Cyclone's XML domain configuration and QoS
 * API) — reliability, history depth and fragment size all carry over
 * from before; HEARTBEAT/ACKNACK timing, DATA_FRAG reassembly and SPDP/
 * SEDP handling are now entirely Cyclone's internal business rather than
 * something this class implements or tunes directly.
 */
class DdsDriver : public ICommDriver
{
public:
    struct Config {
        uint32_t domainId = 0;
        // Selects this participant's discovery port (RTPS "participant
        // index", RTPS spec 9.6.1.1) — maps to Discovery/ParticipantIndex
        // in the domain's Cyclone config (see open()'s doc comment). Give
        // co-located instances (e.g. several DDS:n plugin instances in one
        // process, or several processes on the same host/domain) distinct
        // values, same as before.
        uint32_t participantId = 0;
        bool useIpv6 = false;            // -> General/Transport = udp6 vs udp
        std::string ifaceAddress = "0.0.0.0"; // "0.0.0.0"/"::" = let Cyclone auto-select; else -> General/Interfaces/NetworkInterface
        // Used as the NetworkInterface selector when ifaceAddress is left
        // at its "auto" default — IPv4: an interface IP; IPv6: an
        // interface *name* (e.g. "eth0") — same convention as before.
        std::string multicastInterface;
        // Empty = Cyclone's own family default (239.255.0.1 for IPv4;
        // Cyclone picks a fixed IPv6 group itself — see Cyclone's
        // Discovery/SPDPMulticastAddress docs — so, unlike the previous
        // hand-rolled driver, IPv6 no longer *requires* this to be set,
        // though it still must match a non-Cyclone peer's configuration).
        std::string spdpMulticastGroup;
        // Carried in the participant's standard USER_DATA QoS (visible to
        // any DDSI-RTPS peer, not just this plugin) and read back for
        // DDS.CMD > LIST — see listParticipants()'s doc comment.
        std::string participantName = "uScript-DDS";
        uint8_t  ttl = 1;                    // -> General/MulticastTimeToLive
        uint32_t spdpPeriodMs = 2000;        // -> Discovery/SPDPInterval
        uint32_t leaseDurationSec = 20;      // -> Discovery/LeaseDuration
        bool     reliable = false;           // -> DDS_RELIABILITY_QOS on locally created writers/readers
        // No public per-entity QoS for this in Cyclone (it schedules
        // HEARTBEATs internally); kept only so an existing ini file /
        // CONFIG hb= argument doesn't start failing to parse. Accepted,
        // not applied — see open()'s doc comment.
        uint32_t heartbeatPeriodMs = 500;
        uint32_t historyDepth = 32;              // -> DDS_HISTORY_KEEP_LAST(historyDepth) QoS
        uint32_t fragmentThresholdBytes = 1300;  // -> General/FragmentSize; 0 leaves Cyclone's own default
        std::string strInstanceName;
    };

    struct DiscoveredParticipantView {
        std::string guidHex;
        std::string name;   // from the peer's USER_DATA QoS, if it set one (empty otherwise — not every DDS vendor does)
        double      ageSec = 0.0; // time since Cyclone's builtin-topic cache last refreshed this participant
    };
    struct DiscoveredEndpointView {
        std::string guidHex;
        std::string topic;
        std::string typeName;
        bool        isWriter = false;
        bool        reliable = false;
    };

    explicit DdsDriver(Config config);
    ~DdsDriver() override;

    /// Applies Config to a Cyclone domain (creating it — or attaching to
    /// an already-created one, see the doc comment in the .cpp — with
    /// config.domainId), creates this driver's DDS participant plus the
    /// three built-in discovery readers DDS.CMD > LIST reads from.
    bool open();
    void close();

    // ---- ICommDriver ----
    // See MqttDriver's identical rationale: CommScriptCommandInterpreter<DdsDriver>
    // requires DriverT to implement ICommDriver, and this plugin always
    // drives it through send()/receive() below instead — these three are
    // thin passthroughs for interface completeness.
    bool is_open() const override;
    CommDetails describeConnection(std::string_view xtra_params = {}) const override;
    ICommDriver::WriteResult tout_write(uint32_t u32WriteTimeout, std::span<const uint8_t> buffer,
                                         std::string_view xtra_params = {}) const override;
    ICommDriver::ReadResult tout_read(uint32_t u32ReadTimeout, std::span<uint8_t> buffer,
                                       const ICommDriver::ReadOptions& options,
                                       std::string_view xtra_params = {}) const override;

    /**
     * @brief The intermediary layer: parses one DDS.CMD argument line
     * (e.g. "PUBLISH sensors/temp 21.5", "SUBSCRIBE sensors/temp",
     * "UNSUBSCRIBE sensors/temp", "LIST"), performs the corresponding
     * Cyclone DDS operation, and — for PUBLISH/SUBSCRIBE — reports it to
     * the GUI comm-dump panel. Matches `CommScriptCommandInterpreter<DdsDriver>::
     * SendFunc`'s exact signature — see mqtt_driver.hpp's send() doc
     * comment for why plugins need this instead of the interpreter's own
     * automatic dump.
     */
    ICommDriver::WriteResult send(uint32_t u32WriteTimeout, std::span<const uint8_t> dataSpan,
                                   std::string_view xtra_params) const;

    /**
     * @brief The other half: for a standalone "DDS.CMD <" (no preceding
     * SUBSCRIBE on this same '>'/'<' pair — see thread-local state's doc
     * comment in the .cpp), blocks on the most recently SUBSCRIBEd topic's
     * receive queue (fed by that topic's Cyclone reader listener — see
     * m_OnReaderDataAvailable()) until a sample arrives or the timeout
     * elapses (0 = block indefinitely / infinite timeout).
     * A PUBLISH has nothing to wait for (best-effort by default, no
     * synchronous ack even when reliable=true — Cyclone's ACKNACK
     * handshake happens asynchronously) so a "PUBLISHED" confirmation
     * string is returned immediately instead — mirrors MqttDriver::receive()'s
     * ack-vs-standalone split.
     */
    ICommDriver::ReadResult receive(uint32_t u32ReadTimeout, std::span<uint8_t> dataSpan,
                                     const ICommDriver::ReadOptions& options, std::string_view xtra_params) const;

    /// For DDS.INFO / DDS.CMD > LIST — a human-readable snapshot of every
    /// discovered participant and endpoint, read straight from Cyclone's
    /// builtin discovery topics (DCPSParticipant / DCPSPublication /
    /// DCPSSubscription) rather than a hand-maintained map. Thread-safe.
    std::vector<DiscoveredParticipantView> listParticipants() const;
    std::vector<DiscoveredEndpointView> listEndpoints() const;

private:
    Config m_config;

    // ---- Cyclone DDS entity handles ----
    // dds_entity_t is `int32_t` (see <dds/ddsc/dds_basic_types.h>); kept
    // as a plain alias here rather than pulling <dds/dds.h> into this
    // public header, the same way the previous socket-based driver kept
    // POSIX fds as plain `int` in its header and did all the actual
    // socket-API work in the .cpp only.
    using DdsEntity = int32_t;
    static constexpr DdsEntity kInvalidEntity = -1;

    DdsEntity m_domain = kInvalidEntity;          // only >=0 if *this* open() call created it — see open()'s doc comment
    DdsEntity m_participant = kInvalidEntity;
    DdsEntity m_biParticipantReader = kInvalidEntity;  // DDS_BUILTIN_TOPIC_DCPSPARTICIPANT
    DdsEntity m_biPublicationReader = kInvalidEntity;  // DDS_BUILTIN_TOPIC_DCPSPUBLICATION
    DdsEntity m_biSubscriptionReader = kInvalidEntity; // DDS_BUILTIN_TOPIC_DCPSSUBSCRIPTION

    std::string m_strIdentityLabel;
    std::string m_guidHex; // this participant's own GUID, hex — used to filter self out of listParticipants()

    struct LocalWriter {
        DdsEntity topic = kInvalidEntity;
        DdsEntity writer = kInvalidEntity;
    };
    struct LocalReader {
        DdsEntity topic = kInvalidEntity;
        DdsEntity reader = kInvalidEntity;
        mutable std::mutex queueMutex;
        mutable std::condition_variable queueCv;
        std::deque<std::string> queue;
    };

    // Guards the two maps' structure (insert/erase/lookup) — NOT a given
    // LocalReader's queue, which has its own mutex so a blocking receive()
    // on one topic never stalls PUBLISH/SUBSCRIBE/UNSUBSCRIBE on another.
    mutable std::mutex m_mutex;
    mutable std::map<std::string, LocalWriter> m_localWriters;                   // key: topic name
    mutable std::map<std::string, std::shared_ptr<LocalReader>> m_localReaders;  // key: topic name

    // Hand-off between a "DDS.CMD > SUBSCRIBE <topic>" send() and the receive()
    // call that follows it — see receive()'s doc comment. Instance-scoped
    // (per participant), *not* thread_local — a "DDS.CMD < &" deliberately
    // runs on a background OS thread so it doesn't block the script's main
    // thread, so the SUBSCRIBE (main thread) and the receive (background
    // thread) are never the same thread.
    mutable std::mutex m_activeTopicMutex;
    mutable std::string m_strActiveTopic;

    // ---- helpers (implemented in dds_driver.cpp) ----
    std::string m_BuildDomainConfigXml() const;
    DdsEntity m_EnsureLocalWriter(const std::string& topic) const;
    std::shared_ptr<LocalReader> m_EnsureLocalReader(const std::string& topic) const;

    bool m_Publish(const std::string& topic, const std::string& payload) const;
    bool m_Subscribe(const std::string& topic) const;
    bool m_Unsubscribe(const std::string& topic) const;
    std::string m_BuildListText() const;

    /// dds_on_data_available_fn callback (see <dds/ddsc/dds_public_listener.h>)
    /// registered on every local reader: drains whatever Cyclone just made
    /// available via dds_take() straight into that LocalReader's queue and
    /// wakes receive(). `arg` is the LocalReader* passed to dds_create_listener().
    static void m_OnReaderDataAvailable(DdsEntity reader, void* arg);
};

#endif // DDS_DRIVER_HPP
