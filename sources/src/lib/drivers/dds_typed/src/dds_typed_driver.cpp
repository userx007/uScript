#include "dds_typed_driver.hpp"
#include "uLogger.hpp"
#include "uGuiNotify.hpp"
#include "uString.hpp"

#include <dds/dds.h>
#include "DdsTypePluginAbi.h"

#include <dlfcn.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <sstream>

#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LOG_HDR "DDS_TDRV    |"

namespace
{
    constexpr const char* kPluginNameForDump = "DDS_TYPED";
    constexpr uint32_t kBuiltinReadBatch = 64;
    constexpr size_t kEncodeBufCap = 4096; // see DdsTypeEntry::encode()'s doc comment — generous, fixed, stack-resident

    std::string guidToHex(const dds_guid_t& g)
    {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (uint8_t b : g.v) oss << std::setw(2) << static_cast<int>(b);
        return oss.str();
    }

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

    bool looksLikeIpLiteral(const std::string& s)
    {
        return s.find(':') != std::string::npos || s.find('.') != std::string::npos;
    }

    inline const DdsTypeEntry* asTypeEntry(const void* p) { return static_cast<const DdsTypeEntry*>(p); }
} // namespace

// ---------------------------------------------------------------------------
DdsTypedDriver::DdsTypedDriver(Config config)
    : m_config(std::move(config))
{
    if (m_config.strInstanceName.empty()) {
        m_config.strInstanceName = kPluginNameForDump;
    }
}

DdsTypedDriver::~DdsTypedDriver()
{
    close();
    // dlclose() every loaded customer plugin only after every DDS entity
    // that could still call into it is gone — close() above already
    // dds_delete()d the participant (cascading every topic/writer/reader),
    // so nothing can still be executing plugin code by this point.
    for (void* h : m_loadedHandles) {
        if (h) dlclose(h);
    }
    m_loadedHandles.clear();
}

// ---------------------------------------------------------------------------
// Domain configuration — identical field mapping to DdsDriver's, see that
// class's Config doc comments for the rationale of each.
// ---------------------------------------------------------------------------
std::string DdsTypedDriver::m_BuildDomainConfigXml() const
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
bool DdsTypedDriver::open()
{
    if (is_open()) return true;

    const std::string xml = m_BuildDomainConfigXml();
    const DdsEntity domainRc = dds_create_domain(static_cast<dds_domainid_t>(m_config.domainId), xml.c_str());
    if (domainRc < 0 && -domainRc != DDS_RETCODE_PRECONDITION_NOT_MET) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Custom transport config for domain rejected ("); LOG_STRING(dds_strretcode(-domainRc));
                  LOG_STRING(") — continuing with whatever config this process already has for this domain id, if any"));
    }
    m_domain = (domainRc >= 0) ? domainRc : kInvalidEntity;

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

    m_biParticipantReader  = dds_create_reader(m_participant, DDS_BUILTIN_TOPIC_DCPSPARTICIPANT, nullptr, nullptr);
    m_biPublicationReader  = dds_create_reader(m_participant, DDS_BUILTIN_TOPIC_DCPSPUBLICATION, nullptr, nullptr);
    m_biSubscriptionReader = dds_create_reader(m_participant, DDS_BUILTIN_TOPIC_DCPSSUBSCRIPTION, nullptr, nullptr);
    if (m_biParticipantReader < 0 || m_biPublicationReader < 0 || m_biSubscriptionReader < 0) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("One or more builtin discovery readers failed — DDS_TYPED.CMD > LIST will be incomplete"));
    }

    m_strIdentityLabel = "DDS_TYPED domain=" + std::to_string(m_config.domainId) +
                          " participant_index=" + std::to_string(m_config.participantId) +
                          (m_config.useIpv6 ? " (IPv6)" : " (IPv4)") +
                          " guid=" + m_guidHex + " backend=CycloneDDS";
    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING(m_strIdentityLabel.c_str()));

    bool allPreloadsOk = true;
    for (const auto& path : m_config.preloadPluginPaths) {
        if (!m_LoadPlugin(path)) allPreloadsOk = false;
    }
    if (!allPreloadsOk) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("One or more PRELOAD_PLUGINS entries failed to load — driver is still open, "
                  "affected topics just won't be available until DDS_TYPED.CMD > LOAD succeeds for them"));
    }
    return true;
}

void DdsTypedDriver::close()
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

    if (m_participant >= 0) dds_delete(m_participant);
    m_participant = kInvalidEntity;
    m_biParticipantReader = m_biPublicationReader = m_biSubscriptionReader = kInvalidEntity;
    m_domain = kInvalidEntity; // see DdsDriver::close()'s identical rationale for not dds_delete()ing the domain

    m_typesByTopic.clear(); // the descriptors/function pointers would dangle once close() runs; re-LOAD after re-open()
    m_guidHex.clear();
    // Loaded .so handles are intentionally NOT dlclose()d here — see class
    // doc comment on UNLOAD and ~DdsTypedDriver()'s ordering, since close()
    // may be followed by another open() that expects preloaded types to
    // still be registered... actually they aren't (m_typesByTopic was just
    // cleared above), so a re-open() re-runs preloadPluginPaths from
    // scratch via m_LoadPlugin(), which is idempotent (dlopen() on an
    // already-loaded path returns the same handle, RTLD semantics).
}

bool DdsTypedDriver::is_open() const
{
    return m_participant >= 0;
}

CommDetails DdsTypedDriver::describeConnection(std::string_view xtra_params) const
{
    return commdump_details(CommFamily::NET, xtra_params.empty() ? m_strIdentityLabel : xtra_params);
}

ICommDriver::WriteResult DdsTypedDriver::tout_write(uint32_t, std::span<const uint8_t> buffer, std::string_view) const
{
    WriteResult r;
    r.status = is_open() ? ICommDriver::Status::OPERATION_FAILED : ICommDriver::Status::PORT_ACCESS;
    (void)buffer;
    return r;
}

ICommDriver::ReadResult DdsTypedDriver::tout_read(uint32_t, std::span<uint8_t>, const ICommDriver::ReadOptions&, std::string_view) const
{
    ReadResult r;
    r.status = ICommDriver::Status::OPERATION_FAILED;
    return r;
}

// ---------------------------------------------------------------------------
// Customer type plugin loading
// ---------------------------------------------------------------------------
bool DdsTypedDriver::m_LoadPlugin(const std::string& path) const
{
    void* handle = dlopen(path.c_str(), RTLD_NOW);
    if (!handle) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("dlopen failed for '"); LOG_STRING(path.c_str());
                  LOG_STRING("': "); LOG_STRING(dlerror()));
        return false;
    }

    using GetPluginFn = const DdsTypePlugin* (*)(void);
    auto getPlugin = reinterpret_cast<GetPluginFn>(dlsym(handle, "dds_type_plugin_get"));
    if (!getPlugin) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("dlsym('dds_type_plugin_get') failed for '"); LOG_STRING(path.c_str());
                  LOG_STRING("': "); LOG_STRING(dlerror()));
        dlclose(handle);
        return false;
    }

    const DdsTypePlugin* plugin = getPlugin();
    if (!plugin || plugin->abi_version != DDS_TYPE_PLUGIN_ABI_VERSION) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("ABI version mismatch loading '"); LOG_STRING(path.c_str());
                  LOG_STRING("' — expected"); LOG_UINT32(DDS_TYPE_PLUGIN_ABI_VERSION));
        dlclose(handle);
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    const size_t count = plugin->get_type_count();
    for (size_t i = 0; i < count; ++i) {
        const DdsTypeEntry* entry = plugin->get_type(i);
        if (!entry || !entry->topic_name) continue;
        auto existing = m_typesByTopic.find(entry->topic_name);
        if (existing != m_typesByTopic.end()) {
            LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Topic '"); LOG_STRING(entry->topic_name);
                      LOG_STRING("' already registered by a previously loaded plugin — overriding with '");
                      LOG_STRING(plugin->customer_name); LOG_STRING("'"));
        }
        m_typesByTopic[entry->topic_name] = entry;
    }
    m_loadedHandles.push_back(handle);

    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Loaded customer type plugin '"); LOG_STRING(plugin->customer_name);
              LOG_STRING("' from '"); LOG_STRING(path.c_str()); LOG_STRING("' —"); LOG_SIZET(count); LOG_STRING("topic(s)"));
    return true;
}

// ---------------------------------------------------------------------------
// Per-topic writer/reader lifecycle
// ---------------------------------------------------------------------------
DdsTypedDriver::DdsEntity DdsTypedDriver::m_EnsureLocalWriter(const std::string& topic) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_localWriters.find(topic);
    if (it != m_localWriters.end()) return it->second.writer;

    auto typeIt = m_typesByTopic.find(topic);
    if (typeIt == m_typesByTopic.end()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("No loaded type plugin publishes topic '"); LOG_STRING(topic.c_str());
                  LOG_STRING("' — LOAD its customer .so first"));
        return kInvalidEntity;
    }
    const DdsTypeEntry* entry = asTypeEntry(typeIt->second);

    const DdsEntity topicEnt = dds_create_topic(m_participant, entry->descriptor, topic.c_str(), nullptr, nullptr);
    if (topicEnt < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("dds_create_topic failed for '"); LOG_STRING(topic.c_str());
                  LOG_STRING("': "); LOG_STRING(dds_strretcode(-topicEnt)));
        return kInvalidEntity;
    }
    dds_qos_t* qos = dds_create_qos();
    dds_qset_reliability(qos, m_config.reliable ? DDS_RELIABILITY_RELIABLE : DDS_RELIABILITY_BEST_EFFORT,
                         m_config.reliable ? DDS_SECS(10) : 0);
    dds_qset_history(qos, DDS_HISTORY_KEEP_LAST, static_cast<int32_t>(std::max<uint32_t>(1, m_config.historyDepth)));
    const DdsEntity writerEnt = dds_create_writer(m_participant, topicEnt, qos, nullptr);
    dds_delete_qos(qos);
    if (writerEnt < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("dds_create_writer failed for '"); LOG_STRING(topic.c_str());
                  LOG_STRING("': "); LOG_STRING(dds_strretcode(-writerEnt)));
        dds_delete(topicEnt);
        return kInvalidEntity;
    }

    m_localWriters.emplace(topic, LocalWriter{topicEnt, writerEnt, entry});
    return writerEnt;
}

void DdsTypedDriver::m_OnReaderDataAvailable(DdsEntity reader, void* arg)
{
    auto* localReader = static_cast<LocalReader*>(arg);
    const DdsTypeEntry* entry = asTypeEntry(localReader->typeEntry);

    void* samples[kBuiltinReadBatch] = {};
    dds_sample_info_t infos[kBuiltinReadBatch];
    for (auto& s : samples) s = entry->alloc_sample();

    dds_return_t n;
    while ((n = dds_take(reader, samples, infos, kBuiltinReadBatch, kBuiltinReadBatch)) > 0) {
        {
            std::lock_guard<std::mutex> lock(localReader->queueMutex);
            for (dds_return_t i = 0; i < n; ++i) {
                if (!infos[i].valid_data) continue;
                char buf[kEncodeBufCap];
                if (entry->encode(samples[i], buf, sizeof(buf))) {
                    localReader->queue.emplace_back(buf);
                }
            }
        }
        localReader->queueCv.notify_all();
        if (n < static_cast<dds_return_t>(kBuiltinReadBatch)) break;
    }

    for (auto& s : samples) entry->free_sample(s, DDS_FREE_ALL);
}

std::shared_ptr<DdsTypedDriver::LocalReader> DdsTypedDriver::m_EnsureLocalReader(const std::string& topic) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_localReaders.find(topic);
    if (it != m_localReaders.end()) return it->second;

    auto typeIt = m_typesByTopic.find(topic);
    if (typeIt == m_typesByTopic.end()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("No loaded type plugin subscribes topic '"); LOG_STRING(topic.c_str());
                  LOG_STRING("' — LOAD its customer .so first"));
        return nullptr;
    }
    const DdsTypeEntry* entry = asTypeEntry(typeIt->second);

    auto localReader = std::make_shared<LocalReader>();
    localReader->typeEntry = entry;

    const DdsEntity topicEnt = dds_create_topic(m_participant, entry->descriptor, topic.c_str(), nullptr, nullptr);
    if (topicEnt < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("dds_create_topic failed for '"); LOG_STRING(topic.c_str());
                  LOG_STRING("': "); LOG_STRING(dds_strretcode(-topicEnt)));
        return localReader; // still registered, empty queue forever — same convention as DdsDriver
    }
    dds_qos_t* qos = dds_create_qos();
    dds_qset_reliability(qos, m_config.reliable ? DDS_RELIABILITY_RELIABLE : DDS_RELIABILITY_BEST_EFFORT,
                         m_config.reliable ? DDS_SECS(10) : 0);
    dds_qset_history(qos, DDS_HISTORY_KEEP_LAST, static_cast<int32_t>(std::max<uint32_t>(1, m_config.historyDepth)));

    dds_listener_t* listener = dds_create_listener(localReader.get());
    dds_lset_data_available(listener, &DdsTypedDriver::m_OnReaderDataAvailable);
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

bool DdsTypedDriver::m_Publish(const std::string& topic, const std::string& text) const
{
    const DdsEntity writer = m_EnsureLocalWriter(topic);
    if (writer < 0) return false;

    const DdsTypeEntry* entry = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        entry = asTypeEntry(m_localWriters.at(topic).typeEntry);
    }

    void* sample = entry->alloc_sample();
    if (!entry->decode(text.c_str(), sample)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("decode() rejected PUBLISH payload for '"); LOG_STRING(topic.c_str()); LOG_STRING("'"));
        entry->free_sample(sample, DDS_FREE_ALL);
        return false;
    }

    const dds_return_t rc = dds_write(writer, sample);
    entry->free_sample(sample, DDS_FREE_ALL);
    if (rc != DDS_RETCODE_OK) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("dds_write failed for '"); LOG_STRING(topic.c_str());
                  LOG_STRING("': "); LOG_STRING(dds_strretcode(-rc)));
        return false;
    }
    return true;
}

bool DdsTypedDriver::m_Subscribe(const std::string& topic) const
{
    return m_EnsureLocalReader(topic) != nullptr;
}

bool DdsTypedDriver::m_Unsubscribe(const std::string& topic) const
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
// DDS_TYPED.CMD > LIST
// ---------------------------------------------------------------------------
std::vector<DdsTypedDriver::DiscoveredParticipantView> DdsTypedDriver::listParticipants() const
{
    std::vector<DiscoveredParticipantView> out;
    if (!is_open() || m_biParticipantReader < 0) return out;

    void* samples[kBuiltinReadBatch] = {};
    dds_sample_info_t infos[kBuiltinReadBatch];
    for (auto& s : samples) s = dds_alloc(sizeof(dds_builtintopic_participant_t));

    const dds_return_t n = dds_read(m_biParticipantReader, samples, infos, kBuiltinReadBatch, kBuiltinReadBatch);
    const auto nowNs = dds_time();
    for (dds_return_t i = 0; i < n; ++i) {
        if (!infos[i].valid_data || infos[i].instance_state != DDS_ALIVE_INSTANCE_STATE) continue;
        auto* p = static_cast<dds_builtintopic_participant_t*>(samples[i]);
        const std::string guidHex = guidToHex(p->key);
        if (guidHex == m_guidHex) continue;

        DiscoveredParticipantView v;
        v.guidHex = guidHex;
        if (p->qos) {
            void* ud = nullptr; size_t udLen = 0;
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

std::vector<DdsTypedDriver::DiscoveredEndpointView> DdsTypedDriver::listEndpoints() const
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

std::string DdsTypedDriver::m_BuildListText() const
{
    std::ostringstream oss;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        oss << "loaded_types=" << m_typesByTopic.size();
        for (const auto& [topic, entryOpaque] : m_typesByTopic) {
            const DdsTypeEntry* e = asTypeEntry(entryOpaque);
            oss << " " << topic << "[" << e->descriptor->m_typename << "]";
        }
    }

    const auto participants = listParticipants();
    oss << " ; participants=" << participants.size();
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
// Intermediary layer: DDS_TYPED.CMD argument decomposition
// ---------------------------------------------------------------------------
namespace
{
    void tokenize(std::span<const uint8_t> dataSpan, std::vector<std::string>& outTokens)
    {
        outTokens.clear();
        size_t len = dataSpan.size();
        while (len > 0 && dataSpan[len - 1] == 0) --len;
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

ICommDriver::WriteResult DdsTypedDriver::send(uint32_t, std::span<const uint8_t> dataSpan, std::string_view xtra_params) const
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
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("DDS_TYPED.CMD > requires a command: LOAD, PUBLISH, SUBSCRIBE, UNSUBSCRIBE or LIST"));
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }

    std::string cmdKeyword = tokens[0];
    std::transform(cmdKeyword.begin(), cmdKeyword.end(), cmdKeyword.begin(),
                    [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    bool ok = false;
    if (cmdKeyword == "LOAD") {
        if (tokens.size() != 2) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("LOAD requires exactly: <path-to-customer.so>"));
        } else {
            ok = m_LoadPlugin(tokens[1]);
        }
    } else if (cmdKeyword == "PUBLISH") {
        if (tokens.size() < 2) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("PUBLISH requires: <topic> [payload words...]"));
        } else {
            const std::string topic = tokens[1];
            std::string text;
            for (size_t i = 2; i < tokens.size(); ++i) {
                if (i > 2) text += ' ';
                text += tokens[i];
            }
            ok = m_Publish(topic, text);
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
        ok = true;
    } else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown DDS_TYPED command:"); LOG_STRING(tokens[0]));
    }

    if (cmdKeyword == "LIST") {
        std::lock_guard<std::mutex> lock(m_activeTopicMutex);
        m_strActiveTopic = "\x01LIST";
    }

    result.status = ok ? ICommDriver::Status::SUCCESS : ICommDriver::Status::OPERATION_FAILED;
    result.bytes_written = ok ? dataSpan.size() : 0;
    return result;
}

ICommDriver::ReadResult DdsTypedDriver::receive(uint32_t u32ReadTimeout, std::span<uint8_t> dataSpan,
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
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("DDS_TYPED.CMD < with no prior SUBSCRIBE on this participant"));
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }

    auto reader = m_EnsureLocalReader(strActiveTopic);
    if (!reader) {
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }
    std::unique_lock<std::mutex> qlock(reader->queueMutex);
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

    const std::string text = std::move(reader->queue.front());
    reader->queue.pop_front();
    qlock.unlock();

    const size_t len = std::min(dataSpan.size(), text.size());
    std::memcpy(dataSpan.data(), text.data(), len);
    result.status = ICommDriver::Status::SUCCESS;
    result.bytes_read = len;

    if (gui_mode_active()) {
        gui_notify_comm_dump(m_config.strInstanceName, describeConnection(strActiveTopic), CommDir::Rx,
                              reinterpret_cast<const uint8_t*>(text.data()), static_cast<uint32_t>(text.size()));
    }
    return result;
}
