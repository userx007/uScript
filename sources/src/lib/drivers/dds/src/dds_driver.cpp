#include "dds_driver.hpp"
#include "uLogger.hpp"
#include "uGuiNotify.hpp"
#include "uString.hpp"

#include <dds/dds.h>
#include "ucmdexec_dds.h" // generated from protocols/dds/idl/ucmdexec_dds.idl — struct ucmdexec_dds_GenericSample

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>

#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LOG_HDR "DDS_DRV     |"

namespace
{
    constexpr const char* kPluginNameForDump = "DDS";
    constexpr uint32_t kBuiltinReadBatch = 64; // see listParticipants()/listEndpoints()'s doc comment on this cap

    std::string guidToHex(const dds_guid_t& g)
    {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (uint8_t b : g.v) oss << std::setw(2) << static_cast<int>(b);
        return oss.str();
    }

    /// Minimal XML text escaping for the handful of Config strings (iface
    /// name/address, multicast group, ...) that end up as attribute/element
    /// text in m_BuildDomainConfigXml()'s generated config document.
    std::string xmlEscape(const std::string& in)
    {
        std::string out;
        out.reserve(in.size());
        for (char c : in) {
            switch (c) {
                case '&':  out += "&amp;";  break;
                case '<':  out += "&lt;";   break;
                case '>':  out += "&gt;";   break;
                case '"':  out += "&quot;"; break;
                case '\'': out += "&apos;"; break;
                default:   out += c;        break;
            }
        }
        return out;
    }

    /// Crude but sufficient (same convention the previous socket-based
    /// driver used for its own IPv4-vs-IPv6 address detection): a dotted
    /// IPv4 quad or bracketed/colon IPv6 literal is an *address*; anything
    /// else (e.g. "eth0") is an interface *name*. Cyclone's NetworkInterface
    /// element accepts either, just as a different attribute.
    bool looksLikeIpLiteral(const std::string& s)
    {
        return s.find(':') != std::string::npos || s.find('.') != std::string::npos;
    }
} // namespace

// ---------------------------------------------------------------------------
// Construction / lifetime
// ---------------------------------------------------------------------------
DdsDriver::DdsDriver(Config config)
    : m_config(std::move(config))
{
    if (m_config.strInstanceName.empty()) {
        m_config.strInstanceName = kPluginNameForDump;
    }
}

DdsDriver::~DdsDriver()
{
    close();
}

// ---------------------------------------------------------------------------
// Domain configuration
// ---------------------------------------------------------------------------
/**
 * Builds the `<CycloneDDS><Domain>...</Domain></CycloneDDS>` document
 * handed to dds_create_domain() — see open()'s doc comment for why a
 * failed dds_create_domain() here isn't necessarily fatal.
 *
 * Field mapping (see Config's own per-field doc comments for the
 * rationale of each): useIpv6->General/Transport, ttl->General/
 * MulticastTimeToLive, fragmentThresholdBytes->General/FragmentSize,
 * ifaceAddress/multicastInterface->General/Interfaces/NetworkInterface,
 * spdpMulticastGroup->Discovery/SPDPMulticastAddress, spdpPeriodMs->
 * Discovery/SPDPInterval, leaseDurationSec->Discovery/LeaseDuration,
 * participantId->Discovery/ParticipantIndex. reliable/historyDepth are
 * *not* domain config — they're per-writer/reader QoS, set in
 * m_EnsureLocalWriter()/m_EnsureLocalReader() instead.
 */
std::string DdsDriver::m_BuildDomainConfigXml() const
{
    std::ostringstream xml;
    xml << "<CycloneDDS><Domain id=\"any\"><General>";
    xml << "<Transport>" << (m_config.useIpv6 ? "udp6" : "udp") << "</Transport>";
    xml << "<MulticastTimeToLive>" << static_cast<unsigned>(m_config.ttl) << "</MulticastTimeToLive>";
    if (m_config.fragmentThresholdBytes > 0) {
        xml << "<FragmentSize>" << m_config.fragmentThresholdBytes << "B</FragmentSize>";
    }

    const bool listensOnAll = (m_config.ifaceAddress == "0.0.0.0" || m_config.ifaceAddress == "::" ||
                                m_config.ifaceAddress.empty());
    const std::string ifaceSel = !listensOnAll ? m_config.ifaceAddress : m_config.multicastInterface;
    if (!ifaceSel.empty()) {
        const char* attr = looksLikeIpLiteral(ifaceSel) ? "address" : "name";
        xml << "<Interfaces><NetworkInterface " << attr << "=\"" << xmlEscape(ifaceSel) << "\"/></Interfaces>";
    }
    xml << "</General><Discovery>";
    if (!m_config.spdpMulticastGroup.empty()) {
        xml << "<SPDPMulticastAddress>" << xmlEscape(m_config.spdpMulticastGroup) << "</SPDPMulticastAddress>";
    }
    xml << "<SPDPInterval>" << m_config.spdpPeriodMs << "ms</SPDPInterval>";
    xml << "<LeaseDuration>" << m_config.leaseDurationSec << "s</LeaseDuration>";
    xml << "<ParticipantIndex>" << m_config.participantId << "</ParticipantIndex>";
    xml << "</Discovery></Domain></CycloneDDS>";
    return xml.str();
}

// ---------------------------------------------------------------------------
// open()/close()
// ---------------------------------------------------------------------------
/**
 * Cyclone's domain configuration is process-global per domain id: the
 * *first* dds_create_domain()/dds_create_participant() call for a given
 * id wins, and every later one (this instance's own re-open, or another
 * DdsDriver/plugin instance in the same process sharing that domain id)
 * just attaches to whatever config that first call established —
 * DDS_RETCODE_PRECONDITION_NOT_MET from dds_create_domain() here means
 * exactly that, and is not treated as a failure: dds_create_participant()
 * below still succeeds against the already-live domain. This mirrors the
 * previous driver's own "first participant on a domain/participant-id
 * pair wins the ports" behaviour, just at the process level instead of
 * the OS socket level.
 */
bool DdsDriver::open()
{
    if (is_open()) return true;

    const std::string xml = m_BuildDomainConfigXml();
    const DdsEntity domainRc = dds_create_domain(static_cast<dds_domainid_t>(m_config.domainId), xml.c_str());
    if (domainRc < 0) {
        if (-domainRc != DDS_RETCODE_PRECONDITION_NOT_MET) {
            LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Custom transport config for domain rejected ("); LOG_STRING(dds_strretcode(-domainRc));
                      LOG_STRING(") — continuing with whatever config this process already has for this domain id, if any"));
        }
        m_domain = kInvalidEntity; // we didn't create it (or it already existed) — see class doc comment; nothing of ours to tear down later
    } else {
        m_domain = domainRc;
    }

    dds_qos_t* pqos = dds_create_qos();
    if (!m_config.participantName.empty()) {
        dds_qset_userdata(pqos, m_config.participantName.data(), m_config.participantName.size());
    }
    m_participant = dds_create_participant(static_cast<dds_domainid_t>(m_config.domainId), pqos, nullptr);
    dds_delete_qos(pqos);
    if (m_participant < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("dds_create_participant failed: "); LOG_STRING(dds_strretcode(-m_participant)));
        m_participant = kInvalidEntity;
        return false;
    }

    dds_guid_t guid{};
    dds_get_guid(m_participant, &guid);
    m_guidHex = guidToHex(guid);

    // Builtin discovery readers for DDS.CMD > LIST — see listParticipants()/
    // listEndpoints()'s doc comments. Not fatal if these fail: PUBLISH/
    // SUBSCRIBE still work against a fully live participant either way.
    m_biParticipantReader  = dds_create_reader(m_participant, DDS_BUILTIN_TOPIC_DCPSPARTICIPANT, nullptr, nullptr);
    m_biPublicationReader  = dds_create_reader(m_participant, DDS_BUILTIN_TOPIC_DCPSPUBLICATION, nullptr, nullptr);
    m_biSubscriptionReader = dds_create_reader(m_participant, DDS_BUILTIN_TOPIC_DCPSSUBSCRIPTION, nullptr, nullptr);
    if (m_biParticipantReader < 0 || m_biPublicationReader < 0 || m_biSubscriptionReader < 0) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("One or more builtin discovery readers failed to open — "
                  "DDS.CMD > LIST will be incomplete, PUBLISH/SUBSCRIBE are unaffected"));
    }

    m_strIdentityLabel = "DDS domain=" + std::to_string(m_config.domainId) +
                          " participant_index=" + std::to_string(m_config.participantId) +
                          (m_config.useIpv6 ? " (IPv6)" : " (IPv4)") +
                          " guid=" + m_guidHex + " backend=CycloneDDS";

    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING(m_strIdentityLabel.c_str()));
    return true;
}

void DdsDriver::close()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto& [topic, w] : m_localWriters) {
        if (w.writer >= 0) dds_delete(w.writer);
        if (w.topic  >= 0) dds_delete(w.topic);
    }
    m_localWriters.clear();

    for (auto& [topic, r] : m_localReaders) {
        if (r->reader >= 0) dds_delete(r->reader);
        if (r->topic  >= 0) dds_delete(r->topic);
    }
    m_localReaders.clear();

    // dds_delete() on the participant cascades to every entity created
    // under it, including the three builtin discovery readers above.
    if (m_participant >= 0) {
        dds_delete(m_participant);
    }
    m_participant = kInvalidEntity;
    m_biParticipantReader = m_biPublicationReader = m_biSubscriptionReader = kInvalidEntity;

    // Deliberately NOT dds_delete()ing m_domain: Cyclone's domain entity is
    // process-wide, and another DdsDriver instance in this same process
    // (e.g. a second DDS:n plugin instance sharing this domain id) may
    // still be using it — see open()'s doc comment. Only this instance's
    // own participant (and everything under it, above) is ours to tear down.
    m_domain = kInvalidEntity;
    m_guidHex.clear();
}

bool DdsDriver::is_open() const
{
    return m_participant >= 0;
}

CommDetails DdsDriver::describeConnection(std::string_view xtra_params) const
{
    return commdump_details(CommFamily::NET, xtra_params.empty() ? m_strIdentityLabel : xtra_params);
}

ICommDriver::WriteResult DdsDriver::tout_write(uint32_t, std::span<const uint8_t> buffer, std::string_view) const
{
    // Thin passthrough for interface completeness — see class doc comment.
    WriteResult r;
    r.status = is_open() ? ICommDriver::Status::OPERATION_FAILED : ICommDriver::Status::PORT_ACCESS;
    (void)buffer;
    return r;
}

ICommDriver::ReadResult DdsDriver::tout_read(uint32_t, std::span<uint8_t>, const ICommDriver::ReadOptions&, std::string_view) const
{
    ReadResult r;
    r.status = ICommDriver::Status::OPERATION_FAILED;
    return r;
}

// ---------------------------------------------------------------------------
// Per-topic writer/reader lifecycle
// ---------------------------------------------------------------------------
DdsDriver::DdsEntity DdsDriver::m_EnsureLocalWriter(const std::string& topic) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_localWriters.find(topic);
    if (it != m_localWriters.end()) return it->second.writer;

    const DdsEntity topicEnt = dds_create_topic(m_participant, &ucmdexec_dds_GenericSample_desc,
                                                 topic.c_str(), nullptr, nullptr);
    if (topicEnt < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("dds_create_topic failed for '"); LOG_STRING(topic.c_str());
                  LOG_STRING("': "); LOG_STRING(dds_strretcode(-topicEnt)));
        return kInvalidEntity;
    }

    dds_qos_t* qos = dds_create_qos();
    if (m_config.reliable) {
        dds_qset_reliability(qos, DDS_RELIABILITY_RELIABLE, DDS_SECS(10));
    } else {
        dds_qset_reliability(qos, DDS_RELIABILITY_BEST_EFFORT, 0);
    }
    dds_qset_history(qos, DDS_HISTORY_KEEP_LAST, static_cast<int32_t>(std::max<uint32_t>(1, m_config.historyDepth)));

    const DdsEntity writerEnt = dds_create_writer(m_participant, topicEnt, qos, nullptr);
    dds_delete_qos(qos);
    if (writerEnt < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("dds_create_writer failed for '"); LOG_STRING(topic.c_str());
                  LOG_STRING("': "); LOG_STRING(dds_strretcode(-writerEnt)));
        dds_delete(topicEnt);
        return kInvalidEntity;
    }

    m_localWriters.emplace(topic, LocalWriter{topicEnt, writerEnt});
    return writerEnt;
}

void DdsDriver::m_OnReaderDataAvailable(DdsEntity reader, void* arg)
{
    auto* localReader = static_cast<LocalReader*>(arg);

    void* samples[kBuiltinReadBatch] = {};
    dds_sample_info_t infos[kBuiltinReadBatch];
    for (auto& s : samples) s = ucmdexec_dds_GenericSample__alloc();

    dds_return_t n;
    while ((n = dds_take(reader, samples, infos, kBuiltinReadBatch, kBuiltinReadBatch)) > 0) {
        {
            std::lock_guard<std::mutex> lock(localReader->queueMutex);
            for (dds_return_t i = 0; i < n; ++i) {
                if (!infos[i].valid_data) continue;
                auto* sample = static_cast<ucmdexec_dds_GenericSample*>(samples[i]);
                localReader->queue.emplace_back(sample->payload ? sample->payload : "");
            }
        }
        localReader->queueCv.notify_all();
        if (n < static_cast<dds_return_t>(kBuiltinReadBatch)) break;
    }

    for (auto& s : samples) ucmdexec_dds_GenericSample_free(s, DDS_FREE_ALL);
}

std::shared_ptr<DdsDriver::LocalReader> DdsDriver::m_EnsureLocalReader(const std::string& topic) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_localReaders.find(topic);
    if (it != m_localReaders.end()) return it->second;

    auto localReader = std::make_shared<LocalReader>();

    const DdsEntity topicEnt = dds_create_topic(m_participant, &ucmdexec_dds_GenericSample_desc,
                                                 topic.c_str(), nullptr, nullptr);
    if (topicEnt < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("dds_create_topic failed for '"); LOG_STRING(topic.c_str());
                  LOG_STRING("': "); LOG_STRING(dds_strretcode(-topicEnt)));
        m_localReaders.emplace(topic, localReader); // still register it — receive() needs a queue to wait on even if empty forever
        return localReader;
    }

    dds_qos_t* qos = dds_create_qos();
    if (m_config.reliable) {
        dds_qset_reliability(qos, DDS_RELIABILITY_RELIABLE, DDS_SECS(10));
    } else {
        dds_qset_reliability(qos, DDS_RELIABILITY_BEST_EFFORT, 0);
    }
    dds_qset_history(qos, DDS_HISTORY_KEEP_LAST, static_cast<int32_t>(std::max<uint32_t>(1, m_config.historyDepth)));

    // The listener is copied into the reader entity by dds_create_reader()
    // (standard DDS listener semantics) — this local dds_listener_t* is
    // deleted right after, same pattern as every Cyclone DDS example.
    dds_listener_t* listener = dds_create_listener(localReader.get());
    dds_lset_data_available(listener, &DdsDriver::m_OnReaderDataAvailable);
    const DdsEntity readerEnt = dds_create_reader(m_participant, topicEnt, qos, listener);
    dds_delete_listener(listener);
    dds_delete_qos(qos);

    if (readerEnt < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("dds_create_reader failed for '"); LOG_STRING(topic.c_str());
                  LOG_STRING("': "); LOG_STRING(dds_strretcode(-readerEnt)));
        dds_delete(topicEnt);
    } else {
        localReader->topic = topicEnt;
        localReader->reader = readerEnt;
    }

    m_localReaders.emplace(topic, localReader);
    return localReader;
}

bool DdsDriver::m_Publish(const std::string& topic, const std::string& payload) const
{
    const DdsEntity writer = m_EnsureLocalWriter(topic);
    if (writer < 0) return false;

    ucmdexec_dds_GenericSample sample;
    sample.payload = const_cast<char*>(payload.c_str()); // dds_write() serializes synchronously — no lifetime issue past this call

    const dds_return_t rc = dds_write(writer, &sample);
    if (rc != DDS_RETCODE_OK) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("dds_write failed for '"); LOG_STRING(topic.c_str());
                  LOG_STRING("': "); LOG_STRING(dds_strretcode(-rc)));
        return false;
    }

    if (gui_mode_active()) {
        gui_notify_comm_dump(m_config.strInstanceName, describeConnection(topic), CommDir::Tx,
                              reinterpret_cast<const uint8_t*>(payload.data()), static_cast<uint32_t>(payload.size()));
    }
    return true;
}

bool DdsDriver::m_Subscribe(const std::string& topic) const
{
    return m_EnsureLocalReader(topic) != nullptr;
}

bool DdsDriver::m_Unsubscribe(const std::string& topic) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_localReaders.find(topic);
    if (it == m_localReaders.end()) return false;
    if (it->second->reader >= 0) dds_delete(it->second->reader);
    if (it->second->topic  >= 0) dds_delete(it->second->topic);
    m_localReaders.erase(it);
    return true;
}

// ---------------------------------------------------------------------------
// DDS.CMD > LIST — builtin-topic discovery snapshot
// ---------------------------------------------------------------------------
std::vector<DdsDriver::DiscoveredParticipantView> DdsDriver::listParticipants() const
{
    std::vector<DiscoveredParticipantView> out;
    if (!is_open() || m_biParticipantReader < 0) return out;

    void* samples[kBuiltinReadBatch] = {};
    dds_sample_info_t infos[kBuiltinReadBatch];
    for (auto& s : samples) s = dds_alloc(sizeof(dds_builtintopic_participant_t));

    // dds_read() is non-destructive: DDS.CMD > LIST always sees Cyclone's
    // *current* live snapshot (this is the entire replacement for the
    // previous driver's hand-maintained, lease-expiry-pruned participants
    // map — Cyclone's own builtin-topic cache already is that map).
    // Capped at kBuiltinReadBatch entries — see class doc comment's scope note.
    const dds_return_t n = dds_read(m_biParticipantReader, samples, infos, kBuiltinReadBatch, kBuiltinReadBatch);
    const auto nowNs = dds_time();
    for (dds_return_t i = 0; i < n; ++i) {
        if (!infos[i].valid_data || infos[i].instance_state != DDS_ALIVE_INSTANCE_STATE) continue;
        auto* p = static_cast<dds_builtintopic_participant_t*>(samples[i]);
        const std::string guidHex = guidToHex(p->key);
        if (guidHex == m_guidHex) continue; // skip self

        DiscoveredParticipantView v;
        v.guidHex = guidHex;
        if (p->qos) {
            void* ud = nullptr;
            size_t udLen = 0;
            if (dds_qget_userdata(p->qos, &ud, &udLen) && ud != nullptr) {
                v.name.assign(static_cast<const char*>(ud), udLen);
                dds_free(ud);
            }
        }
        v.ageSec = static_cast<double>(nowNs - infos[i].source_timestamp) / 1e9;
        out.push_back(std::move(v));
    }
    for (auto& s : samples) dds_free(s);
    return out;
}

std::vector<DdsDriver::DiscoveredEndpointView> DdsDriver::listEndpoints() const
{
    std::vector<DiscoveredEndpointView> out;
    if (!is_open()) return out;

    const struct { DdsEntity reader; bool isWriter; } kBuiltinReaders[] = {
        { m_biPublicationReader,  true  },
        { m_biSubscriptionReader, false },
    };

    for (const auto& br : kBuiltinReaders) {
        if (br.reader < 0) continue;

        void* samples[kBuiltinReadBatch] = {};
        dds_sample_info_t infos[kBuiltinReadBatch];
        for (auto& s : samples) s = dds_alloc(sizeof(dds_builtintopic_endpoint_t));

        const dds_return_t n = dds_read(br.reader, samples, infos, kBuiltinReadBatch, kBuiltinReadBatch);
        for (dds_return_t i = 0; i < n; ++i) {
            if (!infos[i].valid_data || infos[i].instance_state != DDS_ALIVE_INSTANCE_STATE) continue;
            auto* e = static_cast<dds_builtintopic_endpoint_t*>(samples[i]);
            // Skip this instance's own endpoints — including its own three
            // builtin discovery readers, which are regular SEDP-announced
            // endpoints just like anyone else's — same self-filter as
            // listParticipants(); local_writers/local_readers in
            // m_BuildListText() already cover "our own" from m_localWriters/
            // m_localReaders directly.
            if (guidToHex(e->participant_key) == m_guidHex) continue;

            DiscoveredEndpointView v;
            v.guidHex = guidToHex(e->key);
            v.topic = e->topic_name ? e->topic_name : "";
            v.typeName = e->type_name ? e->type_name : "";
            v.isWriter = br.isWriter;
            if (e->qos) {
                dds_reliability_kind_t kind = DDS_RELIABILITY_BEST_EFFORT;
                dds_duration_t maxBlock = 0;
                if (dds_qget_reliability(e->qos, &kind, &maxBlock)) {
                    v.reliable = (kind == DDS_RELIABILITY_RELIABLE);
                }
            }
            out.push_back(std::move(v));
        }
        for (auto& s : samples) dds_free(s);
    }
    return out;
}

std::string DdsDriver::m_BuildListText() const
{
    std::ostringstream oss;

    const auto participants = listParticipants();
    oss << "participants=" << participants.size();
    for (const auto& p : participants) {
        oss << " | " << p.guidHex << " '" << p.name << "' age=" << p.ageSec << "s";
    }

    const auto endpoints = listEndpoints();
    size_t remoteWriters = 0, remoteReaders = 0;
    for (const auto& e : endpoints) (e.isWriter ? remoteWriters : remoteReaders)++;
    oss << " ; remote_writers=" << remoteWriters << " remote_readers=" << remoteReaders;
    for (const auto& e : endpoints) {
        oss << " " << e.topic << "[" << e.typeName << "]" << (e.isWriter ? "(W," : "(R,")
            << (e.reliable ? "reliable)" : "best_effort)");
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    oss << " ; local_writers=" << m_localWriters.size();
    for (const auto& [topic, w] : m_localWriters) { (void)w; oss << " " << topic; }
    oss << " ; local_readers=" << m_localReaders.size();
    for (const auto& [topic, r] : m_localReaders) { (void)r; oss << " " << topic; }

    return oss.str();
}

// ---------------------------------------------------------------------------
// Intermediary layer: DDS.CMD argument decomposition
// ---------------------------------------------------------------------------
namespace
{
    void tokenize(std::span<const uint8_t> dataSpan, std::vector<std::string>& outTokens)
    {
        outTokens.clear();
        size_t len = dataSpan.size();
        while (len > 0 && dataSpan[len - 1] == 0) --len; // strip trailing NUL, same convention as MqttDriver
        std::string text(reinterpret_cast<const char*>(dataSpan.data()), len);
        text = ustring::trim(text);

        size_t i = 0;
        const size_t n = text.size();
        while (i < n) {
            while (i < n && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
            if (i >= n) break;
            const size_t start = i;
            while (i < n && !std::isspace(static_cast<unsigned char>(text[i]))) ++i;
            outTokens.push_back(text.substr(start, i - start));
        }
    }
}

ICommDriver::WriteResult DdsDriver::send(uint32_t, std::span<const uint8_t> dataSpan, std::string_view xtra_params) const
{
    (void)xtra_params;
    WriteResult result;

    if (!is_open()) {
        result.status = ICommDriver::Status::PORT_ACCESS;
        return result;
    }

    std::vector<std::string> tokens;
    tokenize(dataSpan, tokens);
    if (tokens.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("DDS.CMD > requires a command: PUBLISH, SUBSCRIBE, UNSUBSCRIBE or LIST"));
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }

    std::string cmdKeyword = tokens[0];
    std::transform(cmdKeyword.begin(), cmdKeyword.end(), cmdKeyword.begin(),
                    [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    bool ok = false;
    if (cmdKeyword == "PUBLISH") {
        if (tokens.size() < 2) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("PUBLISH requires: <topic> [payload words...]"));
        } else {
            const std::string topic = tokens[1];
            std::string payload;
            for (size_t i = 2; i < tokens.size(); ++i) {
                if (i > 2) payload += ' ';
                payload += tokens[i];
            }
            ok = m_Publish(topic, payload);
        }
    } else if (cmdKeyword == "SUBSCRIBE") {
        if (tokens.size() != 2) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SUBSCRIBE requires exactly: <topic>"));
        } else {
            { std::lock_guard<std::mutex> lock(m_activeTopicMutex); m_strActiveTopic = tokens[1]; }
            ok = m_Subscribe(tokens[1]);
        }
    } else if (cmdKeyword == "UNSUBSCRIBE") {
        if (tokens.size() != 2) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("UNSUBSCRIBE requires exactly: <topic>"));
        } else {
            ok = m_Unsubscribe(tokens[1]);
            std::lock_guard<std::mutex> lock(m_activeTopicMutex);
            if (m_strActiveTopic == tokens[1]) m_strActiveTopic.clear();
        }
    } else if (cmdKeyword == "LIST") {
        ok = true; // text is produced in receive(); LIST is a "send now, read result next" pair like MQTT's INFO-ish commands
    } else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown DDS command:"); LOG_STRING(tokens[0]));
    }

    if (cmdKeyword == "LIST") {
        std::lock_guard<std::mutex> lock(m_activeTopicMutex);
        m_strActiveTopic = "\x01LIST"; // sentinel consumed by receive() below, never a legal topic name
    }

    result.status = ok ? ICommDriver::Status::SUCCESS : ICommDriver::Status::OPERATION_FAILED;
    result.bytes_written = ok ? dataSpan.size() : 0;
    return result;
}

ICommDriver::ReadResult DdsDriver::receive(uint32_t u32ReadTimeout, std::span<uint8_t> dataSpan,
                                            const ICommDriver::ReadOptions&, std::string_view xtra_params) const
{
    (void)xtra_params;
    ReadResult result;

    if (!is_open()) {
        result.status = ICommDriver::Status::PORT_ACCESS;
        return result;
    }

    std::string strActiveTopic;
    { std::lock_guard<std::mutex> lock(m_activeTopicMutex); strActiveTopic = m_strActiveTopic; }

    if (strActiveTopic == "\x01LIST") {
        const std::string text = m_BuildListText();
        const size_t len = std::min(dataSpan.size(), text.size());
        std::memcpy(dataSpan.data(), text.data(), len);
        result.status = ICommDriver::Status::SUCCESS;
        result.bytes_read = len;
        return result;
    }

    if (strActiveTopic.empty()) {
        // No preceding SUBSCRIBE on this participant — nothing sensible to
        // wait on (mirrors MqttDriver's standalone receive, but this
        // driver has no single implicit topic the way MQTT's session has
        // an implicit connection: DDS.CMD < always needs a prior SUBSCRIBE
        // on the same participant to know which topic to block on).
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("DDS.CMD < with no prior SUBSCRIBE on this participant"));
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }

    auto reader = m_EnsureLocalReader(strActiveTopic);
    std::unique_lock<std::mutex> qlock(reader->queueMutex);
    // 0 == infinite timeout: condition_variable::wait_for(0ms) would check
    // the predicate once and return immediately (the opposite of what we
    // want), so route a literal 0 through the unbounded wait() overload
    // instead of a zero-duration wait_for().
    bool got;
    if (u32ReadTimeout == 0) {
        reader->queueCv.wait(qlock, [&] { return !reader->queue.empty(); });
        got = true;
    } else {
        got = reader->queueCv.wait_for(qlock, std::chrono::milliseconds(u32ReadTimeout),
                                        [&] { return !reader->queue.empty(); });
    }
    if (!got) {
        result.status = ICommDriver::Status::READ_TIMEOUT;
        return result;
    }

    const std::string payload = std::move(reader->queue.front());
    reader->queue.pop_front();
    qlock.unlock();

    const size_t len = std::min(dataSpan.size(), payload.size());
    std::memcpy(dataSpan.data(), payload.data(), len);
    result.status = ICommDriver::Status::SUCCESS;
    result.bytes_read = len;

    if (gui_mode_active()) {
        gui_notify_comm_dump(m_config.strInstanceName, describeConnection(strActiveTopic), CommDir::Rx,
                              reinterpret_cast<const uint8_t*>(payload.data()), static_cast<uint32_t>(payload.size()));
    }
    return result;
}
