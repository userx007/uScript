#include "dds_protocol.hpp"

#include <cstring>
#include <cstdio>
#include <chrono>
#include <functional>

namespace
{
    // -----------------------------------------------------------------------
    // RTPS wire constants (OMG DDSI-RTPS 2.3)
    // -----------------------------------------------------------------------
    constexpr uint8_t  kRtpsMagic[4]      = { 'R', 'T', 'P', 'S' };
    constexpr uint8_t  kProtocolVersionMajor = 2;
    constexpr uint8_t  kProtocolVersionMinor = 3;
    // Not an OMG-registered vendor id (those are allocated per-implementation) —
    // any unassigned value works fine for wire interoperability, it is purely
    // informational. Chosen to be visually obvious in a packet capture.
    constexpr uint8_t  kVendorId[2]       = { 0x01, 0xF0 };

    constexpr uint8_t kSubmsgPad          = 0x01;
    constexpr uint8_t kSubmsgInfoTs       = 0x09;
    constexpr uint8_t kSubmsgData         = 0x15;

    constexpr uint8_t kFlagEndianLE       = 0x01; // bit0, every submessage
    constexpr uint8_t kDataFlagInlineQos  = 0x02; // bit1
    constexpr uint8_t kDataFlagDataPresent= 0x04; // bit2

    constexpr uint16_t kEncapCdrLe   = 0x0001;
    constexpr uint16_t kEncapPlCdrLe = 0x0003;

    // Parameter ids actually used by this codec (RTPS 2.3 table 9.13/9.14)
    constexpr uint16_t PID_PAD                        = 0x0000;
    constexpr uint16_t PID_SENTINEL                   = 0x0001;
    constexpr uint16_t PID_PARTICIPANT_LEASE_DURATION = 0x0002;
    constexpr uint16_t PID_TOPIC_NAME                 = 0x0005;
    constexpr uint16_t PID_TYPE_NAME                  = 0x0007;
    constexpr uint16_t PID_DEFAULT_UNICAST_LOCATOR    = 0x0031;
    constexpr uint16_t PID_METATRAFFIC_UNICAST_LOCATOR= 0x0032;
    constexpr uint16_t PID_RELIABILITY                = 0x001A;
    constexpr uint16_t PID_DOMAIN_ID                  = 0x000F;
    constexpr uint16_t PID_BUILTIN_ENDPOINT_SET        = 0x0058;
    constexpr uint16_t PID_PARTICIPANT_GUID           = 0x0050;
    constexpr uint16_t PID_ENDPOINT_GUID              = 0x005A;

    constexpr uint32_t kReliabilityBestEffort = 1;
    constexpr uint32_t kReliabilityReliable   = 2;

    // -----------------------------------------------------------------------
    // Minimal bounds-checked CDR (little-endian) reader/writer.
    // Everything this codec parses originates on the network, so every read
    // returns a success flag instead of assuming well-formed input.
    // -----------------------------------------------------------------------
    class CdrWriter
    {
    public:
        std::vector<uint8_t> buf;

        void u8(uint8_t v)  { buf.push_back(v); }
        void u16(uint16_t v){ buf.push_back(uint8_t(v)); buf.push_back(uint8_t(v >> 8)); }
        void u32(uint32_t v){ for (int i = 0; i < 4; ++i) buf.push_back(uint8_t(v >> (8*i))); }
        void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
        void bytes(std::span<const uint8_t> b) { buf.insert(buf.end(), b.begin(), b.end()); }
        void align4()
        {
            while (buf.size() % 4 != 0) buf.push_back(0);
        }
        // CDR string: uint32 length (incl. NUL) + chars + NUL, no extra
        // alignment beyond the natural 4-byte alignment of the length field
        // (callers align4() before calling this, matching parameter-list use).
        void cdrString(const std::string& s)
        {
            u32(static_cast<uint32_t>(s.size() + 1));
            bytes(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(s.data()), s.size()));
            u8(0);
            align4();
        }
        void guidPrefix(const DdsProtocol::GuidPrefix& g) { bytes(std::span<const uint8_t>(g.data(), g.size())); }
        void entityId(const DdsProtocol::EntityId& e)     { bytes(std::span<const uint8_t>(e.data(), e.size())); }
        void locator(const DdsProtocol::Locator& l)
        {
            i32(static_cast<int32_t>(l.kind));
            u32(l.port);
            bytes(std::span<const uint8_t>(l.address.data(), l.address.size()));
        }
    };

    class CdrReader
    {
    public:
        std::span<const uint8_t> data;
        size_t pos = 0;

        explicit CdrReader(std::span<const uint8_t> d) : data(d) {}

        size_t remaining() const { return pos <= data.size() ? data.size() - pos : 0; }

        bool u8(uint8_t& v)  { if (remaining() < 1) return false; v = data[pos]; pos += 1; return true; }
        bool u16(uint16_t& v){ if (remaining() < 2) return false; v = uint16_t(data[pos]) | (uint16_t(data[pos+1]) << 8); pos += 2; return true; }
        bool u32(uint32_t& v){
            if (remaining() < 4) return false;
            v = uint32_t(data[pos]) | (uint32_t(data[pos+1]) << 8) | (uint32_t(data[pos+2]) << 16) | (uint32_t(data[pos+3]) << 24);
            pos += 4; return true;
        }
        bool i32(int32_t& v) { uint32_t u; if (!u32(u)) return false; v = static_cast<int32_t>(u); return true; }
        bool skip(size_t n)  { if (remaining() < n) return false; pos += n; return true; }
        bool bytes(std::span<uint8_t> out) {
            if (remaining() < out.size()) return false;
            std::memcpy(out.data(), data.data() + pos, out.size());
            pos += out.size();
            return true;
        }
        void align4() { while (pos % 4 != 0) { if (remaining() < 1) { pos = data.size(); return; } ++pos; } }

        bool cdrString(std::string& out)
        {
            uint32_t len = 0;
            if (!u32(len) || len == 0 || len > remaining()) return false;
            out.assign(reinterpret_cast<const char*>(data.data() + pos), len - 1); // drop trailing NUL
            pos += len;
            align4();
            return true;
        }
        bool guidPrefix(DdsProtocol::GuidPrefix& g) { return bytes(std::span<uint8_t>(g.data(), g.size())); }
        bool entityId(DdsProtocol::EntityId& e)     { return bytes(std::span<uint8_t>(e.data(), e.size())); }
        bool locator(DdsProtocol::Locator& l)
        {
            int32_t kind = 0;
            if (!i32(kind)) return false;
            l.kind = static_cast<uint32_t>(kind);
            if (!u32(l.port)) return false;
            return bytes(std::span<uint8_t>(l.address.data(), l.address.size()));
        }
    };

    void writeParamHeader(CdrWriter& w, uint16_t pid, uint16_t len) { w.u16(pid); w.u16(len); }

    // Writes a parameter whose value is produced by fn(CdrWriter&) into a
    // scratch writer first, so its padded length can be computed and
    // written into the (pid,length) header before the value itself.
    template <typename Fn>
    void writeParam(CdrWriter& out, uint16_t pid, Fn&& fn)
    {
        CdrWriter scratch;
        fn(scratch);
        scratch.align4();
        writeParamHeader(out, pid, static_cast<uint16_t>(scratch.buf.size()));
        out.bytes(scratch.buf);
    }

    void writeSentinel(CdrWriter& out) { writeParamHeader(out, PID_SENTINEL, 0); }
} // namespace

// ---------------------------------------------------------------------------
// Well-known entity ids
// ---------------------------------------------------------------------------
const DdsProtocol::EntityId DdsProtocol::kEntityIdUnknown          = {0x00, 0x00, 0x00, 0x00};
const DdsProtocol::EntityId DdsProtocol::kEntityIdParticipant      = {0x00, 0x00, 0x01, 0xC1};
const DdsProtocol::EntityId DdsProtocol::kEntityIdSedpPubAnnouncer = {0x00, 0x00, 0x03, 0xC2};
const DdsProtocol::EntityId DdsProtocol::kEntityIdSedpPubDetector  = {0x00, 0x00, 0x03, 0xC7};
const DdsProtocol::EntityId DdsProtocol::kEntityIdSedpSubAnnouncer = {0x00, 0x00, 0x04, 0xC2};
const DdsProtocol::EntityId DdsProtocol::kEntityIdSedpSubDetector  = {0x00, 0x00, 0x04, 0xC7};
const DdsProtocol::EntityId DdsProtocol::kEntityIdSpdpAnnouncer    = {0x00, 0x01, 0x00, 0xC2};
const DdsProtocol::EntityId DdsProtocol::kEntityIdSpdpDetector     = {0x00, 0x01, 0x00, 0xC7};

// ---------------------------------------------------------------------------
// Locator helpers
// ---------------------------------------------------------------------------
std::string DdsProtocol::Locator::toIpString() const
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", address[12], address[13], address[14], address[15]);
    return std::string(buf);
}

DdsProtocol::Locator DdsProtocol::Locator::fromIpPort(const std::string& ip, uint16_t port)
{
    Locator l;
    l.kind = kLocatorKindUdpV4;
    l.port = port;
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (std::sscanf(ip.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
        l.address[12] = static_cast<uint8_t>(a);
        l.address[13] = static_cast<uint8_t>(b);
        l.address[14] = static_cast<uint8_t>(c);
        l.address[15] = static_cast<uint8_t>(d);
    }
    return l;
}

// ---------------------------------------------------------------------------
// EntityId derivation (topic name -> stable user entity id)
// ---------------------------------------------------------------------------
DdsProtocol::EntityId DdsProtocol::makeUserEntityId(const std::string& topicName, uint8_t kindByte)
{
    // FNV-1a over the topic name, folded into the 3-byte entity key —
    // deterministic across processes so independent publisher/subscriber
    // instances agree on the same wire entity id for the same topic name
    // without any handshake, the same role an MQTT topic string plays.
    uint32_t hash = 2166136261u;
    for (unsigned char c : topicName) {
        hash ^= c;
        hash *= 16777619u;
    }
    EntityId id;
    id[0] = static_cast<uint8_t>(hash >> 16);
    id[1] = static_cast<uint8_t>(hash >> 8);
    id[2] = static_cast<uint8_t>(hash);
    id[3] = kindByte;
    return id;
}

// ---------------------------------------------------------------------------
// Message / submessage builders
// ---------------------------------------------------------------------------
std::vector<uint8_t> DdsProtocol::buildMessage(const GuidPrefix& srcPrefix,
                                                const std::vector<std::vector<uint8_t>>& submessages)
{
    std::vector<uint8_t> out;
    out.reserve(20 + 64);
    out.insert(out.end(), kRtpsMagic, kRtpsMagic + 4);
    out.push_back(kProtocolVersionMajor);
    out.push_back(kProtocolVersionMinor);
    out.push_back(kVendorId[0]);
    out.push_back(kVendorId[1]);
    out.insert(out.end(), srcPrefix.begin(), srcPrefix.end());

    for (const auto& sm : submessages) {
        out.insert(out.end(), sm.begin(), sm.end());
    }
    return out;
}

std::vector<uint8_t> DdsProtocol::buildInfoTsSubmessageNow()
{
    // INFO_TS carrying the current wall-clock time is optional per the
    // spec but customary — some receivers use its absence as a signal of
    // a non-conformant sender, so it's included for every DATA we send.
    using namespace std::chrono;
    const auto now = system_clock::now().time_since_epoch();
    const int32_t sec = static_cast<int32_t>(duration_cast<seconds>(now).count());
    const uint32_t frac = static_cast<uint32_t>(
        (duration_cast<nanoseconds>(now).count() % 1000000000LL) * 4294967296LL / 1000000000LL);

    CdrWriter body;
    body.i32(sec);
    body.u32(frac);

    std::vector<uint8_t> sm;
    sm.push_back(kSubmsgInfoTs);
    sm.push_back(kFlagEndianLE);
    const uint16_t len = static_cast<uint16_t>(body.buf.size());
    sm.push_back(uint8_t(len)); sm.push_back(uint8_t(len >> 8));
    sm.insert(sm.end(), body.buf.begin(), body.buf.end());
    return sm;
}

std::vector<uint8_t> DdsProtocol::buildDataSubmessage(const EntityId& readerId, const EntityId& writerId,
                                                        int64_t seqNum, std::span<const uint8_t> serializedPayload,
                                                        bool isPlCdr)
{
    CdrWriter body;
    body.entityId(readerId);
    body.entityId(writerId);
    body.i32(static_cast<int32_t>(seqNum >> 32));
    body.u32(static_cast<uint32_t>(seqNum & 0xFFFFFFFFu));

    // encapsulation header + payload
    body.u16(isPlCdr ? kEncapPlCdrLe : kEncapCdrLe);
    body.u16(0); // options
    body.bytes(serializedPayload);
    body.align4();

    std::vector<uint8_t> sm;
    sm.push_back(kSubmsgData);
    sm.push_back(kFlagEndianLE | kDataFlagDataPresent);
    // placeholder length, patched below
    sm.push_back(0); sm.push_back(0);
    sm.push_back(0); sm.push_back(0); // extraFlags
    const uint16_t octetsToInlineQos = 16; // readerId(4)+writerId(4)+writerSN(8), no inlineQos
    sm.push_back(uint8_t(octetsToInlineQos)); sm.push_back(uint8_t(octetsToInlineQos >> 8));
    sm.insert(sm.end(), body.buf.begin(), body.buf.end());

    const uint16_t submsgLen = static_cast<uint16_t>(sm.size() - 4);
    sm[2] = uint8_t(submsgLen);
    sm[3] = uint8_t(submsgLen >> 8);
    return sm;
}

// ---------------------------------------------------------------------------
// SPDP (participant discovery) payload
// ---------------------------------------------------------------------------
std::vector<uint8_t> DdsProtocol::buildSpdpPayload(const ParticipantInfo& info)
{
    CdrWriter out;
    out.u16(kEncapPlCdrLe);
    out.u16(0);

    Guid participantGuid{ info.prefix, kEntityIdParticipant };
    writeParam(out, PID_PARTICIPANT_GUID, [&](CdrWriter& w) { w.guidPrefix(participantGuid.prefix); w.entityId(participantGuid.entityId); });
    writeParam(out, PID_DOMAIN_ID, [&](CdrWriter& w) { w.u32(info.domainId); });
    if (info.metaUnicastLocator.valid()) {
        writeParam(out, PID_METATRAFFIC_UNICAST_LOCATOR, [&](CdrWriter& w) { w.locator(info.metaUnicastLocator); });
    }
    if (info.userUnicastLocator.valid()) {
        writeParam(out, PID_DEFAULT_UNICAST_LOCATOR, [&](CdrWriter& w) { w.locator(info.userUnicastLocator); });
    }
    writeParam(out, PID_BUILTIN_ENDPOINT_SET, [&](CdrWriter& w) { w.u32(kBepAllUsed); });
    writeParam(out, PID_PARTICIPANT_LEASE_DURATION, [&](CdrWriter& w) { w.i32(static_cast<int32_t>(info.leaseDurationSec)); w.u32(0); });
    if (!info.participantName.empty()) {
        // Not a standard builtin PID (real vendor-specific PID is 0x0044 /
        // PID_ENTITY_NAME) — used here only for this driver's own INFO/LIST
        // output; harmless as an unknown-but-well-formed parameter to any
        // conformant peer, which must skip unrecognised PIDs (spec 9.6.2.2.1).
        writeParam(out, 0x0044, [&](CdrWriter& w) { w.cdrString(info.participantName); });
    }
    writeSentinel(out);
    return out.buf;
}

bool DdsProtocol::parseSpdpPayload(std::span<const uint8_t> plCdrPayload, ParticipantInfo& out)
{
    CdrReader r(plCdrPayload);
    uint16_t scheme = 0, options = 0;
    if (!r.u16(scheme) || !r.u16(options)) return false;
    (void)options;

    bool gotGuid = false;
    while (r.remaining() >= 4) {
        uint16_t pid = 0, len = 0;
        if (!r.u16(pid) || !r.u16(len)) return false;
        if (pid == PID_SENTINEL) break;
        if (r.remaining() < len) return false;
        const size_t valueStart = r.pos;

        switch (pid) {
            case PID_PARTICIPANT_GUID: {
                Guid g;
                if (r.guidPrefix(g.prefix) && r.entityId(g.entityId)) { out.prefix = g.prefix; gotGuid = true; }
                break;
            }
            case PID_DOMAIN_ID: r.u32(out.domainId); break;
            case PID_METATRAFFIC_UNICAST_LOCATOR: r.locator(out.metaUnicastLocator); break;
            case PID_DEFAULT_UNICAST_LOCATOR: r.locator(out.userUnicastLocator); break;
            case PID_BUILTIN_ENDPOINT_SET: r.u32(out.builtinEndpointSet); break;
            case PID_PARTICIPANT_LEASE_DURATION: {
                int32_t sec = 0; uint32_t frac = 0;
                if (r.i32(sec)) { r.u32(frac); out.leaseDurationSec = sec > 0 ? static_cast<uint32_t>(sec) : 20; }
                break;
            }
            case 0x0044: r.cdrString(out.participantName); break;
            default: break; // unknown PID — skip via the length-based seek below
        }

        r.pos = valueStart + len; // always resync — tolerates partially-parsed/unknown params
    }
    return gotGuid;
}

// ---------------------------------------------------------------------------
// SEDP (endpoint discovery) payload
// ---------------------------------------------------------------------------
std::vector<uint8_t> DdsProtocol::buildSedpPayload(const Guid& endpointGuid, const GuidPrefix& /*participantPrefix*/,
                                                     const std::string& topicName, const std::string& typeName,
                                                     bool reliable)
{
    CdrWriter out;
    out.u16(kEncapPlCdrLe);
    out.u16(0);

    writeParam(out, PID_ENDPOINT_GUID, [&](CdrWriter& w) { w.guidPrefix(endpointGuid.prefix); w.entityId(endpointGuid.entityId); });
    writeParam(out, PID_TOPIC_NAME, [&](CdrWriter& w) { w.cdrString(topicName); });
    writeParam(out, PID_TYPE_NAME, [&](CdrWriter& w) { w.cdrString(typeName); });
    writeParam(out, PID_RELIABILITY, [&](CdrWriter& w) {
        w.u32(reliable ? kReliabilityReliable : kReliabilityBestEffort);
        w.i32(0); w.u32(0); // max_blocking_time — unused by this driver (best-effort only)
    });
    writeSentinel(out);
    return out.buf;
}

bool DdsProtocol::parseSedpPayload(std::span<const uint8_t> plCdrPayload, bool isWriterTopic, EndpointInfo& out)
{
    out.isWriter = isWriterTopic;
    CdrReader r(plCdrPayload);
    uint16_t scheme = 0, options = 0;
    if (!r.u16(scheme) || !r.u16(options)) return false;
    (void)options;

    bool gotGuid = false, gotTopic = false;
    while (r.remaining() >= 4) {
        uint16_t pid = 0, len = 0;
        if (!r.u16(pid) || !r.u16(len)) return false;
        if (pid == PID_SENTINEL) break;
        if (r.remaining() < len) return false;
        const size_t valueStart = r.pos;

        switch (pid) {
            case PID_ENDPOINT_GUID:
                if (r.guidPrefix(out.guid.prefix) && r.entityId(out.guid.entityId)) gotGuid = true;
                break;
            case PID_TOPIC_NAME: gotTopic = r.cdrString(out.topicName); break;
            case PID_TYPE_NAME: r.cdrString(out.typeName); break;
            case PID_RELIABILITY: {
                uint32_t kind = 0;
                if (r.u32(kind)) out.reliable = (kind == kReliabilityReliable);
                break;
            }
            default: break;
        }
        r.pos = valueStart + len;
    }
    return gotGuid && gotTopic;
}

// ---------------------------------------------------------------------------
// User payload (opaque string sample)
// ---------------------------------------------------------------------------
std::vector<uint8_t> DdsProtocol::encodeUserPayload(const std::string& data)
{
    CdrWriter w;
    w.cdrString(data);
    return w.buf;
}

bool DdsProtocol::decodeUserPayload(std::span<const uint8_t> serializedPayload, std::string& out)
{
    CdrReader r(serializedPayload);
    return r.cdrString(out);
}

// ---------------------------------------------------------------------------
// Message parser
// ---------------------------------------------------------------------------
bool DdsProtocol::parseMessage(std::span<const uint8_t> buf, GuidPrefix& outSrcPrefix,
                                std::vector<DataSubmessage>& outData)
{
    outData.clear();

    if (buf.size() < 20) return false;
    if (buf[0] != kRtpsMagic[0] || buf[1] != kRtpsMagic[1] || buf[2] != kRtpsMagic[2] || buf[3] != kRtpsMagic[3]) {
        return false;
    }
    std::memcpy(outSrcPrefix.data(), buf.data() + 8, 12);

    size_t pos = 20;
    while (pos + 4 <= buf.size()) {
        const uint8_t submsgId = buf[pos];
        const uint8_t flags    = buf[pos + 1];
        const bool littleEndian = (flags & kFlagEndianLE) != 0;
        uint16_t submsgLen;
        if (littleEndian) {
            submsgLen = uint16_t(buf[pos + 2]) | (uint16_t(buf[pos + 3]) << 8);
        } else {
            submsgLen = uint16_t(buf[pos + 3]) | (uint16_t(buf[pos + 2]) << 8);
        }

        const size_t contentStart = pos + 4;
        size_t contentLen = submsgLen;
        if (submsgLen == 0) {
            // "rest of message" — legal only for the last submessage.
            contentLen = (contentStart <= buf.size()) ? (buf.size() - contentStart) : 0;
        }
        if (contentStart + contentLen > buf.size()) {
            break; // malformed/truncated — stop, keep whatever we already parsed
        }

        if (submsgId == kSubmsgData && littleEndian) {
            CdrReader r(buf.subspan(contentStart, contentLen));
            uint16_t extraFlags = 0, octetsToInlineQos = 0;
            DataSubmessage d;
            if (r.u16(extraFlags) && r.u16(octetsToInlineQos) &&
                r.entityId(d.readerId) && r.entityId(d.writerId)) {
                (void)extraFlags;
                int32_t snHigh = 0; uint32_t snLow = 0;
                if (r.i32(snHigh) && r.u32(snLow)) {
                    d.seqNum = (static_cast<int64_t>(snHigh) << 32) | snLow;

                    const bool hasInlineQos = (flags & kDataFlagInlineQos) != 0;
                    const bool hasData      = (flags & kDataFlagDataPresent) != 0;

                    // octetsToInlineQos is measured from right after that field
                    // itself; readerId+writerId+writerSN = 16 bytes already
                    // consumed, so skip whatever (if anything) sits beyond that
                    // before inlineQos/serializedPayload starts.
                    const size_t fixedPart = 16;
                    if (octetsToInlineQos > fixedPart) {
                        r.skip(octetsToInlineQos - fixedPart);
                    }
                    if (hasInlineQos) {
                        // Skip an inline QoS ParameterList we don't need — walk
                        // it via its own PID/length framing to find its end.
                        while (r.remaining() >= 4) {
                            uint16_t pid = 0, len = 0;
                            if (!r.u16(pid) || !r.u16(len)) break;
                            if (pid == PID_SENTINEL) break;
                            r.skip(len);
                        }
                    }
                    if (hasData && r.remaining() >= 4) {
                        uint16_t encapScheme = 0, encapOptions = 0;
                        r.u16(encapScheme);
                        r.u16(encapOptions);
                        d.isPlCdr = (encapScheme == kEncapPlCdrLe);
                        d.serializedPayload.assign(buf.begin() + contentStart + r.pos, buf.begin() + contentStart + contentLen);
                        outData.push_back(std::move(d));
                    }
                }
            }
        }
        // PAD and any other/unknown submessage kind are simply skipped —
        // only DATA carries content this driver cares about.
        (void)kSubmsgPad;

        pos = contentStart + contentLen;
        // Submessages are individually 4-byte aligned relative to the
        // message start; contentLen from a conformant sender already keeps
        // that true, but re-align defensively for a slightly malformed one.
        while (pos % 4 != 0 && pos < buf.size()) ++pos;

        if (submsgLen == 0) break; // consumed "rest of message"
    }

    return true;
}
