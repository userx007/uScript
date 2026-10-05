#include "dds_typed_driver.hpp"

#include "DdsTypePluginAbi.h"
#include "uGuiNotify.hpp"
#include "uLogger.hpp"
#include "uString.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <compare>
#include <cstdlib>
#include <cstring>
#include <dds/dds.h>
#include <dlfcn.h>
#include <filesystem>
#include <fnmatch.h>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <utility>

/////////////////////////////////////////////////////////////////////////////////
//                            LOG DEFINITIONS                                  //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
#undef LT_HDR
#endif
#ifdef LOG_HDR
#undef LOG_HDR
#endif

#define LT_HDR  "DDSTY_DRV   |"
#define LOG_HDR LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                            IMPLEMENTATION                                   //
/////////////////////////////////////////////////////////////////////////////////

namespace {
    constexpr const char *kPluginNameForDump = "DDS_TYPED";
    constexpr uint32_t kBuiltinReadBatch     = 64;
    constexpr size_t kEncodeBufCap           = 4096; // see DdsTypeEntry::encode()'s doc comment — generous, fixed, stack-resident

    std::string guidToHex(const dds_guid_t &g)
    {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (uint8_t b : g.v) {
            oss << std::setw(2) << static_cast<int>(b);
        }
        return oss.str();
    }

    std::string xmlEscape(const std::string &strIn)
    {
        std::string out;
        out.reserve(strIn.size());
        for (char c : strIn) {
            switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '\'':
                out += "&apos;";
                break;
            default:
                out += c;
                break;
            }
        }
        return out;
    }

    bool looksLikeIpLiteral(const std::string &strS)
    {
        return strS.find(':') != std::string::npos || strS.find('.') != std::string::npos;
    }

    inline const DdsTypeEntry *asTypeEntry(const void *p)
    {
        return static_cast<const DdsTypeEntry *>(p);
    }
} // namespace

/// Shared body of the "block on one specific reader's queue" wait — used by
/// receive()'s explicit-topic and single-subscription forms (previously
/// duplicated inline in both). Same stop_tok contract as the rest of this
/// file: a native predicate wait for the infinite (u32ReadTimeout == 0)
/// case, a 200ms-slice retry loop otherwise. Pops and returns the front
/// sample on success; returns std::nullopt on timeout/cancellation,
/// touching nothing. A private static member (not a free function) purely
/// because LocalReader is a private nested type.
std::optional<std::string> DdsTypedDriver::m_WaitPopOne(LocalReader &reader, uint32_t u32ReadTimeout,
                                                        std::stop_token stop_tok)
{
    std::unique_lock<std::mutex> qlock(reader.queueMutex);
    bool got;
    if (u32ReadTimeout == 0) {
        got = reader.queueCv.wait(qlock, stop_tok, [&] { return !reader.queue.empty(); });
    } else {
        constexpr auto kSliceMs = std::chrono::milliseconds(200);
        const auto tDeadline    = std::chrono::steady_clock::now() + std::chrono::milliseconds(u32ReadTimeout);
        got                     = false;
        while (true) {
            if (stop_tok.stop_requested()) {
                got = false;
                break;
            }
            const auto tNow = std::chrono::steady_clock::now();
            if (tNow >= tDeadline) {
                got = false;
                break;
            }
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(tDeadline - tNow);
            const auto sliceMs   = std::min(kSliceMs, remaining);
            got                  = reader.queueCv.wait_for(qlock, sliceMs, [&] { return !reader.queue.empty(); });
            if (got) {
                break;
            }
        }
    }
    if (!got) {
        return std::nullopt;
    }
    std::string text = std::move(reader.queue.front());
    reader.queue.pop_front();
    return text;
}

// ---------------------------------------------------------------------------
DdsTypedDriver::DdsTypedDriver(Config sConfig)
    : m_config(std::move(sConfig))
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
    for (void *h : m_loadedHandles) {
        if (h) {
            dlclose(h);
        }
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
    const bool listensOnAll    = (m_config.ifaceAddress == "0.0.0.0" || m_config.ifaceAddress == "::" ||
                               m_config.ifaceAddress.empty());
    const std::string ifaceSel = !listensOnAll ? m_config.ifaceAddress : m_config.multicastInterface;
    if (!ifaceSel.empty()) {
        const char *attr = looksLikeIpLiteral(ifaceSel) ? "address" : "name";
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

// Which configuration string each domain of THIS process was created with by a
// DdsTypedDriver. Cyclone's dds_create_domain() fails with PRECONDITION_NOT_MET
// for a second participant on the same domain id; that is only worth a warning
// if the running domain was configured differently from what this instance
// asked for. Two instances given the same config (the common case: several
// DDSTY:n.CONFIG lines with the same cf=) are simply sharing one domain.
namespace {
    std::mutex g_domainCfgMutex;
    std::map<uint32_t, std::string> g_domainCfgs;
} // namespace

// ---------------------------------------------------------------------------
// External Cyclone configuration file (CYCLONE_CONFIG_FILE / cf=)
// ---------------------------------------------------------------------------
bool DdsTypedDriver::m_ResolveExternalConfig(std::string &strConfigOut) const
{
    namespace fs = std::filesystem;
    std::error_code ec;

    const std::string &strPath = m_config.cycloneConfigFile;
    if (strPath.find(',') != std::string::npos) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CYCLONE_CONFIG_FILE '"); LOG_STRING(strPath.c_str());
                  LOG_STRING("' contains a ',' — Cyclone splits its config string on commas, rename/move the file"));
        return false;
    }

    const fs::path abs = fs::absolute(fs::path(strPath), ec);
    if (ec || !fs::is_regular_file(abs, ec)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CYCLONE_CONFIG_FILE '"); LOG_STRING(strPath.c_str());
                  LOG_STRING("' does not exist or is not a regular file"));
        return false;
    }

    std::ifstream in(abs, std::ios::binary);
    if (!in) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CYCLONE_CONFIG_FILE '"); LOG_STRING(abs.string().c_str());
                  LOG_STRING("' is not readable"));
        return false;
    }
    // Cheap sanity check only — the real parse/validation is Cyclone's own
    // (dds_create_domain() below rejects malformed content).
    const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (content.find("<CycloneDDS") == std::string::npos) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("CYCLONE_CONFIG_FILE '"); LOG_STRING(abs.string().c_str());
                  LOG_STRING("' has no <CycloneDDS> root element — is this really a Cyclone DDS config file "
                             "(and not, e.g., a DDS-XML QoS profile)?"));
    }

    strConfigOut = "file://" + abs.string();
    return true;
}

// ---------------------------------------------------------------------------
// open()/close()
// ---------------------------------------------------------------------------
bool DdsTypedDriver::open()
{
    if (is_open()) {
        return true;
    }

    const bool bExternalCfg = !m_config.cycloneConfigFile.empty();
    std::string strDomainCfg;
    if (bExternalCfg) {
        if (!m_ResolveExternalConfig(strDomainCfg)) {
            return false;
        }
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("Using external Cyclone config: "); LOG_STRING(strDomainCfg.c_str());
                  LOG_STRING(" (IFACE/MCAST_IFACE/TTL/SPDP_*/LEASE/FRAGMENT/PARTICIPANT_ID/USE_IPV6 are not applied)"));
    } else {
        strDomainCfg = m_BuildDomainConfigXml();
    }

    // Resolve QoS profiles before any DDS entity exists, so a bad file/profile
    // name leaves nothing to clean up.
    if (!m_ResolveQosProfiles()) {
        return false;
    }

    const DdsEntity domainRc = dds_create_domain(static_cast<dds_domainid_t>(m_config.domainId), strDomainCfg.c_str());
    // Cyclone's DDS_RETCODE_* macros are negative in current releases (-4 for
    // PRECONDITION_NOT_MET, which is exactly what the API returns) but were
    // positive in older ones, so compare magnitudes. "Not met" here means the
    // domain already exists in this process — not an error.
    const bool bDomainExists = domainRc < 0 && std::abs(static_cast<int>(domainRc)) == std::abs(static_cast<int>(DDS_RETCODE_PRECONDITION_NOT_MET));
    if (bDomainExists) {
        std::string strExisting;
        bool bKnown = false;
        {
            std::lock_guard<std::mutex> lk(g_domainCfgMutex);
            if (auto it = g_domainCfgs.find(m_config.domainId); it != g_domainCfgs.end()) {
                bKnown      = true;
                strExisting = it->second;
            }
        }
        if (bKnown && strExisting == strDomainCfg) {
            LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("Domain "); LOG_UINT32(m_config.domainId);
                      LOG_STRING(" already exists in this process with the same configuration — sharing it"));
        } else if (bKnown) {
            LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Domain "); LOG_UINT32(m_config.domainId);
                      LOG_STRING(" already exists in this process, created with a DIFFERENT configuration ('");
                      LOG_STRING(strExisting.c_str()); LOG_STRING("') — the requested one is NOT applied, the running domain keeps its own"));
        } else if (bExternalCfg) {
            LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Domain "); LOG_UINT32(m_config.domainId);
                      LOG_STRING(" already exists in this process (created outside this driver, e.g. by another plugin) — "
                                 "CYCLONE_CONFIG_FILE is NOT applied, the running domain keeps its current configuration"));
        }
    } else if (domainRc < 0) {
        if (bExternalCfg) {
            // An explicitly requested config file must not be silently dropped.
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Cyclone rejected CYCLONE_CONFIG_FILE (");
                      LOG_STRING(dds_strretcode(-domainRc));
                      LOG_STRING(") — check the file's content and the Cyclone error output"));
            return false;
        }
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Custom transport config for domain rejected (");
                  LOG_STRING(dds_strretcode(-domainRc));
                  LOG_STRING(") — continuing with whatever config this process already has for this domain id, if any"));
    }
    if (domainRc >= 0) {
        std::lock_guard<std::mutex> lk(g_domainCfgMutex);
        g_domainCfgs[m_config.domainId] = strDomainCfg;
    }
    m_domain        = (domainRc >= 0) ? domainRc : kInvalidEntity;

    dds_qos_t *pqos = dds_create_qos();
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
    m_guidHex              = guidToHex(guid);

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
    LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING(m_strIdentityLabel.c_str()));

    bool allPreloadsOk = true;
    for (const auto &path : m_config.preloadPluginPaths) {
        if (!m_LoadPlugin(path)) {
            allPreloadsOk = false;
        }
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

    for (auto &[topic, w] : m_localWriters) {
        if (w.writer >= 0) {
            dds_delete(w.writer);
        }
        if (w.topic >= 0) {
            dds_delete(w.topic);
        }
    }
    m_localWriters.clear();

    for (auto &[topic, r] : m_localReaders) {
        if (r->reader >= 0) {
            dds_delete(r->reader);
        }
        if (r->topic >= 0) {
            dds_delete(r->topic);
        }
    }
    m_localReaders.clear();

    if (m_participant >= 0) {
        dds_delete(m_participant);
    }
    m_participant         = kInvalidEntity;
    m_biParticipantReader = m_biPublicationReader = m_biSubscriptionReader = kInvalidEntity;
    // Unlike DdsDriver::close(): if THIS driver created the domain (m_domain
    // is only valid in that case) and no other participant is attached to it
    // any more, delete it too. Otherwise the domain — and with it the
    // configuration it was created with — would live on for the whole process,
    // and a later DDS_TYPED.CONFIG (e.g. a different cf= file, or another
    // IFACE) followed by a re-open would be silently ignored
    // (dds_create_domain() => PRECONDITION_NOT_MET). If another participant
    // still uses it, we leave it alone, exactly as before.
    if (m_domain >= 0 && dds_get_children(m_domain, nullptr, 0) == 0) {
        dds_delete(m_domain);
        std::lock_guard<std::mutex> lk(g_domainCfgMutex);
        g_domainCfgs.erase(m_config.domainId);
    }
    m_domain = kInvalidEntity;

    m_qosRules.clear();
    m_defaultQos.reset();
    m_strDefaultQosName.clear();

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

ICommDriver::WriteResult DdsTypedDriver::tout_write(uint32_t, std::span<const uint8_t> buffer, std::string_view,
                                                    std::stop_token /*stop_tok*/) const
{
    WriteResult r;
    r.status = is_open() ? ICommDriver::Status::OPERATION_FAILED : ICommDriver::Status::PORT_ACCESS;
    (void)buffer;
    return r;
}

ICommDriver::ReadResult DdsTypedDriver::tout_read(uint32_t, std::span<uint8_t>, const ICommDriver::ReadOptions &,
                                                  std::string_view, std::stop_token /*stop_tok*/) const
{
    ReadResult r;
    r.status = ICommDriver::Status::OPERATION_FAILED;
    return r;
}

// ---------------------------------------------------------------------------
// QoS profiles (QOS_PROFILE_FILE / qf=) — OMG DDS-XML via Cyclone's QoS Provider
// ---------------------------------------------------------------------------

/// Independent copies of one profile's reader/writer/topic QoS. Copies (not
/// the provider's own pointers) so the provider can be deleted right after
/// loading and these stay valid for the driver's lifetime. Any of the three
/// may be nullptr: the profile simply has no entry of that kind (an empty
/// profile such as "Default" has none) => DDS defaults for that entity.
struct DdsTypedDriver::ResolvedQos {
        std::string key;
        dds_qos_t *reader                           = nullptr;
        dds_qos_t *writer                           = nullptr;
        dds_qos_t *topic                            = nullptr;

        ResolvedQos()                               = default;
        ResolvedQos(const ResolvedQos &)            = delete;
        ResolvedQos &operator=(const ResolvedQos &) = delete;

        ~ResolvedQos()
        {
            for (dds_qos_t *q : {reader, writer, topic}) {
                if (q) {
                    dds_delete_qos(q);
                }
            }
        }
};

namespace {
    struct RawQosRule {
            std::string glob;
            std::string profile;
    };

    // "topicA=ProfA;topicB*=Lib::ProfB,other=ProfC" -> rules. ';' and ',' both
    // separate (DDS topic names can't contain either). Returns false + message on
    // a malformed entry.
    bool parseQosTopicRules(const std::string &strIn, std::vector<RawQosRule> &out, std::string &strErr)
    {
        size_t start = 0;
        while (start <= strIn.size()) {
            const size_t sep = strIn.find_first_of(";,", start);
            const std::string token =
                ustring::trim(strIn.substr(start, sep == std::string::npos ? std::string::npos : sep - start));
            if (!token.empty()) {
                const size_t eq = token.find('=');
                if (eq == std::string::npos) {
                    strErr = "'" + token + "' is not <topic>=<profile>";
                    return false;
                }
                RawQosRule r{ustring::trim(token.substr(0, eq)), ustring::trim(token.substr(eq + 1))};
                if (r.glob.empty() || r.profile.empty()) {
                    strErr = "'" + token + "' has an empty topic or profile";
                    return false;
                }
                out.push_back(std::move(r));
            }
            if (sep == std::string::npos) {
                break;
            }
            start = sep + 1;
        }
        return true;
    }

    bool absoluteRegularFile(const std::string &strPath, std::string &strAbsOut)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path abs = fs::absolute(fs::path(strPath), ec);
        if (ec || !fs::is_regular_file(abs, ec)) {
            return false;
        }
        strAbsOut = abs.string();
        return true;
    }
} // namespace

// ---------------------------------------------------------------------------
// Built-in DDS-XML QoS reader.
//
// Used when the CycloneDDS this is built against has no QoS Provider
// (DDS_HAS_QOS_PROVIDER undefined: Cyclone < 11.0, or built with
// ENABLE_QOS_PROVIDER=OFF), or when DDS_TYPED_BUILTIN_QOS_PARSER is defined.
// Where the provider exists it is preferred: it is Cyclone's own reader.
//
// A deliberately small XML subset (elements, attributes, text, comments, CDATA,
// declarations) plus the QoS policies of the OMG DDS-XML <datareader_qos> /
// <datawriter_qos> / <topic_qos> sections that map onto dds_qset_*(). Only the
// profiles that are actually selected are validated, so a file with an
// unfinished template profile elsewhere in it (e.g. one with an empty
// <deadline><period/>) can still be used for the profiles that are complete.
// Policies that can't be applied (unsupported, or not applicable to that kind
// of entity) are reported as warnings and skipped; invalid values are errors.
// ---------------------------------------------------------------------------
namespace qosxml {
    struct Node {
            std::string name;
            std::map<std::string, std::string> attrs;
            std::string text; // concatenated character data (untrimmed)
            std::vector<Node> children;

            const Node *child(const char *n) const
            {
                for (const auto &c : children) {
                    if (c.name == n) {
                        return &c;
                    }
                }
                return nullptr;
            }

            std::string trimmedText() const
            {
                return ustring::trim(text);
            }

            std::string attr(const char *k) const
            {
                auto it = attrs.find(k);
                return it == attrs.end() ? std::string() : it->second;
            }
    };

    struct Document {
            Node root;
            std::map<std::string, const Node *> profiles; // "Library::Profile" -> <qos_profile>
    };

    enum class Kind { Reader,
                      Writer,
                      Topic };

    inline std::string decodeEntities(const std::string &in)
    {
        std::string out;
        out.reserve(in.size());
        for (size_t i = 0; i < in.size(); ++i) {
            if (in[i] == '&') {
                const size_t semi = in.find(';', i);
                if (semi != std::string::npos && semi - i <= 8) {
                    const std::string ent = in.substr(i + 1, semi - i - 1);
                    char rep              = 0;
                    if (ent == "lt") {
                        rep = '<';
                    } else if (ent == "gt") {
                        rep = '>';
                    } else if (ent == "amp") {
                        rep = '&';
                    } else if (ent == "quot") {
                        rep = '"';
                    } else if (ent == "apos") {
                        rep = '\'';
                    }
                    if (rep) {
                        out += rep;
                        i = semi;
                        continue;
                    }
                }
            }
            out += in[i];
        }
        return out;
    }

    inline std::string stripPrefix(const std::string &n)
    {
        const size_t c = n.rfind(':');
        return c == std::string::npos ? n : n.substr(c + 1);
    }

    inline bool parseDocument(const std::string &s, Node &root, std::string &err)
    {
        Node holder; // its only child becomes the root
        std::vector<Node *> stack{&holder};
        const auto lineOf = [&](size_t pos) {
            return 1 + std::count(s.begin(), s.begin() + static_cast<std::ptrdiff_t>(std::min(pos, s.size())), '\n');
        };
        const auto fail = [&](size_t pos, const std::string &m) {
            err = "line " + std::to_string(lineOf(pos)) + ": " + m;
            return false;
        };
        size_t i = 0;
        while (i < s.size()) {
            if (s[i] != '<') {
                const size_t j = std::min(s.find('<', i), s.size());
                stack.back()->text += decodeEntities(s.substr(i, j - i));
                i = j;
                continue;
            }
            if (s.compare(i, 4, "<!--") == 0) {
                const size_t e = s.find("-->", i + 4);
                if (e == std::string::npos) {
                    return fail(i, "unterminated comment");
                }
                i = e + 3;
            } else if (s.compare(i, 9, "<![CDATA[") == 0) {
                const size_t e = s.find("]]>", i + 9);
                if (e == std::string::npos) {
                    return fail(i, "unterminated CDATA");
                }
                stack.back()->text += s.substr(i + 9, e - i - 9);
                i = e + 3;
            } else if (s.compare(i, 2, "<?") == 0) {
                const size_t e = s.find("?>", i + 2);
                if (e == std::string::npos) {
                    return fail(i, "unterminated processing instruction");
                }
                i = e + 2;
            } else if (s.compare(i, 2, "<!") == 0) { // DOCTYPE etc.
                const size_t e = s.find('>', i);
                if (e == std::string::npos) {
                    return fail(i, "unterminated declaration");
                }
                i = e + 1;
            } else if (s.compare(i, 2, "</") == 0) {
                const size_t e = s.find('>', i);
                if (e == std::string::npos) {
                    return fail(i, "unterminated closing tag");
                }
                const std::string nm = stripPrefix(ustring::trim(s.substr(i + 2, e - i - 2)));
                if (stack.size() <= 1 || stack.back()->name != nm) {
                    return fail(i, "unexpected closing tag </" + nm + ">");
                }
                stack.pop_back();
                i = e + 1;
            } else { // start tag
                size_t k = i + 1;
                while (k < s.size() && !std::isspace(static_cast<unsigned char>(s[k])) && s[k] != '>' && s[k] != '/') {
                    ++k;
                }
                Node n;
                n.name = stripPrefix(s.substr(i + 1, k - i - 1));
                if (n.name.empty()) {
                    return fail(i, "empty tag name");
                }
                bool selfClose = false;
                for (;;) {
                    while (k < s.size() && std::isspace(static_cast<unsigned char>(s[k]))) {
                        ++k;
                    }
                    if (k >= s.size()) {
                        return fail(i, "unterminated tag <" + n.name + ">");
                    }
                    if (s[k] == '>') {
                        ++k;
                        break;
                    }
                    if (s[k] == '/' && k + 1 < s.size() && s[k + 1] == '>') {
                        selfClose = true;
                        k += 2;
                        break;
                    }
                    const size_t a0 = k;
                    while (k < s.size() && s[k] != '=' && s[k] != '>' && s[k] != '/' && !std::isspace(static_cast<unsigned char>(s[k]))) {
                        ++k;
                    }
                    const std::string an = s.substr(a0, k - a0);
                    while (k < s.size() && std::isspace(static_cast<unsigned char>(s[k]))) {
                        ++k;
                    }
                    if (k >= s.size() || s[k] != '=' || an.empty()) {
                        return fail(i, "malformed attribute in <" + n.name + ">");
                    }
                    ++k;
                    while (k < s.size() && std::isspace(static_cast<unsigned char>(s[k]))) {
                        ++k;
                    }
                    if (k >= s.size() || (s[k] != '"' && s[k] != '\'')) {
                        return fail(i, "attribute '" + an + "' is not quoted");
                    }
                    const char q    = s[k++];
                    const size_t v1 = s.find(q, k);
                    if (v1 == std::string::npos) {
                        return fail(i, "unterminated attribute value");
                    }
                    n.attrs[stripPrefix(an)] = decodeEntities(s.substr(k, v1 - k));
                    k                        = v1 + 1;
                }
                stack.back()->children.push_back(std::move(n));
                if (!selfClose) {
                    stack.push_back(&stack.back()->children.back());
                }
                i = k;
            }
        }
        if (stack.size() != 1) {
            return fail(s.size(), "unclosed element <" + stack.back()->name + ">");
        }
        if (holder.children.size() != 1) {
            return fail(0, "expected exactly one root element");
        }
        root = std::move(holder.children.front());
        return true;
    }

    // <dds><qos_library name=..><qos_profile name=..> (or a bare <qos_library> root)
    inline bool indexProfiles(Document &doc, std::string &err)
    {
        std::vector<const Node *> libs;
        if (doc.root.name == "dds") {
            for (const auto &c : doc.root.children) {
                if (c.name == "qos_library") {
                    libs.push_back(&c);
                }
            }
        } else if (doc.root.name == "qos_library") {
            libs.push_back(&doc.root);
        } else {
            err = "root element is <" + doc.root.name + ">, expected <dds> (or <qos_library>)";
            return false;
        }
        for (const Node *lib : libs) {
            const std::string ln = lib->attr("name");
            if (ln.empty()) {
                err = "a <qos_library> has no name attribute";
                return false;
            }
            for (const auto &pr : lib->children) {
                if (pr.name != "qos_profile") {
                    continue;
                }
                const std::string pn = pr.attr("name");
                if (pn.empty()) {
                    err = "a <qos_profile> in library '" + ln + "' has no name attribute";
                    return false;
                }
                if (!doc.profiles.emplace(ln + "::" + pn, &pr).second) {
                    err = "profile '" + ln + "::" + pn + "' is defined twice";
                    return false;
                }
            }
        }
        return true;
    }

    inline bool loadFile(const std::string &strPath, Document &doc, std::string &err)
    {
        std::ifstream in(strPath, std::ios::binary);
        if (!in) {
            err = "cannot open file";
            return false;
        }
        const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (content.size() > (16u << 20)) {
            err = "file is larger than 16 MiB";
            return false;
        }
        if (!parseDocument(content, doc.root, err)) {
            err = "XML syntax error, " + err;
            return false;
        }
        return indexProfiles(doc, err);
    }

    // ---- value helpers --------------------------------------------------
    struct EnumItem {
            const char *name;
            int value;
    };

    inline bool toEnum(const Node *n, const EnumItem *items, size_t count, int def, const char *what, int &out, std::string &err)
    {
        const std::string t = n ? n->trimmedText() : std::string();
        if (t.empty()) {
            out = def;
            return true;
        }
        for (size_t i = 0; i < count; ++i) {
            if (t == items[i].name) {
                out = items[i].value;
                return true;
            }
        }
        err = std::string("invalid ") + what + " '" + t + "'";
        return false;
    }

    // <x><sec>..</sec><nanosec>..</nanosec></x>; an empty element leaves `isSet` false.
    inline bool toDuration(const Node &e, dds_duration_t &out, bool &isSet, std::string &err)
    {
        isSet            = false;
        const Node *sec  = e.child("sec");
        const Node *nsec = e.child("nanosec");
        if (!sec && !nsec) {
            if (e.trimmedText().empty()) {
                return true; // empty: left to the DDS default (caller warns)
            }
            err = "<" + e.name + "> needs <sec>/<nanosec>";
            return false;
        }
        const std::string ts = sec ? sec->trimmedText() : std::string("0");
        const std::string tn = nsec ? nsec->trimmedText() : std::string("0");
        if (ts == "DURATION_INFINITY" || ts == "DURATION_INFINITE_SEC" || tn == "DURATION_INFINITE_NSEC") {
            out   = DDS_INFINITY;
            isSet = true;
            return true;
        }
        char *e1 = nullptr, *e2 = nullptr;
        const long long sv = std::strtoll(ts.c_str(), &e1, 10);
        const long long nv = (tn == "DURATION_ZERO_NSEC") ? 0 : std::strtoll(tn.c_str(), &e2, 10);
        if (ts.empty() || *e1 != '\0' || (tn != "DURATION_ZERO_NSEC" && (tn.empty() || *e2 != '\0')) || sv < 0 || nv < 0 || nv > 999999999) {
            err = "invalid duration in <" + e.name + "> (sec='" + ts + "', nanosec='" + tn + "')";
            return false;
        }
        out   = static_cast<dds_duration_t>(sv) * 1000000000LL + static_cast<dds_duration_t>(nv);
        isSet = true;
        return true;
    }

    inline bool toInt(const Node *n, int32_t def, int32_t &out, std::string &err)
    {
        const std::string t = n ? n->trimmedText() : std::string();
        if (t.empty()) {
            out = def;
            return true;
        }
        if (t == "LENGTH_UNLIMITED") {
            out = -1;
            return true;
        }
        char *e           = nullptr;
        const long long v = std::strtoll(t.c_str(), &e, 10);
        if (*e != '\0' || v < INT32_MIN || v > INT32_MAX) {
            err = "invalid integer '" + t + "'";
            return false;
        }
        out = static_cast<int32_t>(v);
        return true;
    }

    inline bool toBool(const Node *n, bool def, bool &out, std::string &err)
    {
        const std::string t = n ? n->trimmedText() : std::string();
        if (t.empty()) {
            out = def;
            return true;
        }
        if (t == "true" || t == "TRUE" || t == "1") {
            out = true;
            return true;
        }
        if (t == "false" || t == "FALSE" || t == "0") {
            out = false;
            return true;
        }
        err = "invalid boolean '" + t + "'";
        return false;
    }

    inline const EnumItem kDurability[]  = {{"VOLATILE_DURABILITY_QOS", DDS_DURABILITY_VOLATILE},
                                            {"TRANSIENT_LOCAL_DURABILITY_QOS", DDS_DURABILITY_TRANSIENT_LOCAL},
                                            {"TRANSIENT_DURABILITY_QOS", DDS_DURABILITY_TRANSIENT},
                                            {"PERSISTENT_DURABILITY_QOS", DDS_DURABILITY_PERSISTENT}};
    inline const EnumItem kReliability[] = {{"BEST_EFFORT_RELIABILITY_QOS", DDS_RELIABILITY_BEST_EFFORT},
                                            {"RELIABLE_RELIABILITY_QOS", DDS_RELIABILITY_RELIABLE}};
    inline const EnumItem kHistory[]     = {{"KEEP_LAST_HISTORY_QOS", DDS_HISTORY_KEEP_LAST}, {"KEEP_ALL_HISTORY_QOS", DDS_HISTORY_KEEP_ALL}};
    inline const EnumItem kDestOrder[]   = {{"BY_RECEPTION_TIMESTAMP_DESTINATIONORDER_QOS", DDS_DESTINATIONORDER_BY_RECEPTION_TIMESTAMP},
                                            {"BY_SOURCE_TIMESTAMP_DESTINATIONORDER_QOS", DDS_DESTINATIONORDER_BY_SOURCE_TIMESTAMP}};
    inline const EnumItem kLiveliness[]  = {{"AUTOMATIC_LIVELINESS_QOS", DDS_LIVELINESS_AUTOMATIC},
                                            {"MANUAL_BY_PARTICIPANT_LIVELINESS_QOS", DDS_LIVELINESS_MANUAL_BY_PARTICIPANT},
                                            {"MANUAL_BY_TOPIC_LIVELINESS_QOS", DDS_LIVELINESS_MANUAL_BY_TOPIC}};
    inline const EnumItem kOwnership[]   = {{"SHARED_OWNERSHIP_QOS", DDS_OWNERSHIP_SHARED}, {"EXCLUSIVE_OWNERSHIP_QOS", DDS_OWNERSHIP_EXCLUSIVE}};

    // Which policies the DDS spec allows on which entity.
    inline bool applicable(Kind k, const std::string &p)
    {
        static const char *const common[] = {"durability", "deadline", "latency_budget", "liveliness", "reliability",
                                             "destination_order", "history", "resource_limits", "ownership"};
        for (const char *c : common) {
            if (p == c) {
                return true;
            }
        }
        switch (k) {
        case Kind::Reader:
            return p == "time_based_filter" || p == "reader_data_lifecycle";
        case Kind::Writer:
            return p == "transport_priority" || p == "lifespan" || p == "ownership_strength" || p == "writer_data_lifecycle";
        case Kind::Topic:
            return p == "transport_priority" || p == "lifespan";
        }
        return false;
    }

    // Builds the dds_qos_t for one <datareader_qos>/<datawriter_qos>/<topic_qos>
    // section. nullptr + err on an invalid value; `warnings` collects the rest.
    inline dds_qos_t *buildQos(const Node &sec, Kind kind, std::vector<std::string> &warnings, std::string &err)
    {
        dds_qos_t *q   = dds_create_qos();
        const auto bad = [&](const std::string &pol, const std::string &m) -> dds_qos_t * {
            err = "<" + sec.name + "><" + pol + ">: " + m;
            dds_delete_qos(q);
            return nullptr;
        };
        for (const Node &e : sec.children) {
            const std::string &pol = e.name;
            if (!applicable(kind, pol)) {
                warnings.push_back("<" + sec.name + "><" + pol + "> is not applicable to / not supported for this entity — ignored");
                continue;
            }
            std::string er;
            if (pol == "durability") {
                int k;
                if (!toEnum(e.child("kind"), kDurability, 4, DDS_DURABILITY_VOLATILE, "durability kind", k, er)) {
                    return bad(pol, er);
                }
                dds_qset_durability(q, static_cast<dds_durability_kind_t>(k));
            } else if (pol == "reliability") {
                int k;
                const int defK = (kind == Kind::Writer) ? DDS_RELIABILITY_RELIABLE : DDS_RELIABILITY_BEST_EFFORT;
                if (!toEnum(e.child("kind"), kReliability, 2, defK, "reliability kind", k, er)) {
                    return bad(pol, er);
                }
                dds_duration_t mbt = DDS_MSECS(100); // DDS-spec default
                bool set           = false;
                if (const Node *m = e.child("max_blocking_time")) {
                    dds_duration_t v;
                    if (!toDuration(*m, v, set, er)) {
                        return bad(pol, er);
                    }
                    if (set) {
                        mbt = v;
                    }
                }
                dds_qset_reliability(q, static_cast<dds_reliability_kind_t>(k), mbt);
            } else if (pol == "history") {
                int k;
                int32_t depth;
                if (!toEnum(e.child("kind"), kHistory, 2, DDS_HISTORY_KEEP_LAST, "history kind", k, er)) {
                    return bad(pol, er);
                }
                // Same defaulting as Cyclone's QoS Provider: 1 for KEEP_LAST, 0 (unused) for KEEP_ALL.
                if (!toInt(e.child("depth"), k == DDS_HISTORY_KEEP_ALL ? 0 : 1, depth, er)) {
                    return bad(pol, er);
                }
                if (k == DDS_HISTORY_KEEP_LAST && depth < 1) {
                    return bad(pol, "KEEP_LAST needs depth >= 1");
                }
                dds_qset_history(q, static_cast<dds_history_kind_t>(k), depth);
            } else if (pol == "destination_order") {
                int k;
                if (!toEnum(e.child("kind"), kDestOrder, 2, DDS_DESTINATIONORDER_BY_RECEPTION_TIMESTAMP, "destination_order kind", k, er)) {
                    return bad(pol, er);
                }
                dds_qset_destination_order(q, static_cast<dds_destination_order_kind_t>(k));
            } else if (pol == "deadline" || pol == "latency_budget" || pol == "lifespan" || pol == "time_based_filter") {
                const char *inner = pol == "deadline" ? "period" : pol == "latency_budget" ? "duration"
                                                               : pol == "lifespan"         ? "duration"
                                                                                           : "minimum_separation";
                const Node *d     = e.child(inner);
                dds_duration_t v  = 0;
                bool set          = false;
                if (!d) {
                    return bad(pol, std::string("missing <") + inner + ">");
                }
                if (!toDuration(*d, v, set, er)) {
                    return bad(pol, er);
                }
                if (!set) {
                    return bad(pol, std::string("<") + inner + "> is empty — a value must be specified");
                }
                if (pol == "deadline") {
                    dds_qset_deadline(q, v);
                } else if (pol == "latency_budget") {
                    dds_qset_latency_budget(q, v);
                } else if (pol == "lifespan") {
                    dds_qset_lifespan(q, v);
                } else {
                    dds_qset_time_based_filter(q, v);
                }
            } else if (pol == "liveliness") {
                int k;
                dds_duration_t lease = DDS_INFINITY;
                bool set             = false;
                if (!toEnum(e.child("kind"), kLiveliness, 3, DDS_LIVELINESS_AUTOMATIC, "liveliness kind", k, er)) {
                    return bad(pol, er);
                }
                if (const Node *l = e.child("lease_duration")) {
                    dds_duration_t v;
                    if (!toDuration(*l, v, set, er)) {
                        return bad(pol, er);
                    }
                    if (set) {
                        lease = v;
                    }
                }
                dds_qset_liveliness(q, static_cast<dds_liveliness_kind_t>(k), lease);
            } else if (pol == "ownership") {
                int k;
                if (!toEnum(e.child("kind"), kOwnership, 2, DDS_OWNERSHIP_SHARED, "ownership kind", k, er)) {
                    return bad(pol, er);
                }
                dds_qset_ownership(q, static_cast<dds_ownership_kind_t>(k));
            } else if (pol == "ownership_strength" || pol == "transport_priority") {
                int32_t v;
                if (!toInt(e.child("value"), 0, v, er)) {
                    return bad(pol, er);
                }
                if (pol == "ownership_strength") {
                    dds_qset_ownership_strength(q, v);
                } else {
                    dds_qset_transport_priority(q, v);
                }
            } else if (pol == "resource_limits") {
                int32_t ms, mi, mspi;
                if (!toInt(e.child("max_samples"), -1, ms, er) || !toInt(e.child("max_instances"), -1, mi, er) ||
                    !toInt(e.child("max_samples_per_instance"), -1, mspi, er)) {
                    return bad(pol, er);
                }
                dds_qset_resource_limits(q, ms, mi, mspi);
            } else if (pol == "writer_data_lifecycle") {
                bool v;
                if (!toBool(e.child("autodispose_unregistered_instances"), true, v, er)) {
                    return bad(pol, er);
                }
                dds_qset_writer_data_lifecycle(q, v);
            } else if (pol == "reader_data_lifecycle") {
                dds_duration_t nw = DDS_INFINITY, dp = DDS_INFINITY;
                bool set = false;
                if (const Node *a = e.child("autopurge_nowriter_samples_delay")) {
                    dds_duration_t v;
                    if (!toDuration(*a, v, set, er)) {
                        return bad(pol, er);
                    }
                    if (set) {
                        nw = v;
                    }
                }
                if (const Node *b = e.child("autopurge_disposed_samples_delay")) {
                    dds_duration_t v;
                    if (!toDuration(*b, v, set, er)) {
                        return bad(pol, er);
                    }
                    if (set) {
                        dp = v;
                    }
                }
                dds_qset_reader_data_lifecycle(q, nw, dp);
            }
        }
        return q;
    }

    // Builds the three optional QoS objects of one <qos_profile>. false + err on an invalid value.
    inline bool buildProfile(const Node &profile, std::vector<std::string> &warnings, dds_qos_t *&reader, dds_qos_t *&writer, dds_qos_t *&topic, std::string &err)
    {
        reader = writer = topic = nullptr;
        if (!profile.attr("base_name").empty()) {
            warnings.push_back("base_name inheritance is not supported by the built-in QoS reader — ignored");
        }
        for (const Node &c : profile.children) {
            dds_qos_t **dst = nullptr;
            Kind k          = Kind::Reader;
            if (c.name == "datareader_qos") {
                dst = &reader;
                k   = Kind::Reader;
            } else if (c.name == "datawriter_qos") {
                dst = &writer;
                k   = Kind::Writer;
            } else if (c.name == "topic_qos") {
                dst = &topic;
                k   = Kind::Topic;
            } else {
                warnings.push_back("<" + c.name + "> in a profile is not applied (only datareader_qos / datawriter_qos / topic_qos are)");
                continue;
            }
            if (*dst) {
                warnings.push_back("a second <" + c.name + "> in the same profile is ignored");
                continue;
            }
            *dst = buildQos(c, k, warnings, err);
            if (!*dst) {
                for (dds_qos_t *x : {reader, writer, topic}) {
                    if (x) {
                        dds_delete_qos(x);
                    }
                }
                reader = writer = topic = nullptr;
                return false;
            }
        }
        return true;
    }
} // namespace qosxml

bool DdsTypedDriver::m_ResolveQosProfiles()
{
    m_defaultQos.reset();
    m_strDefaultQosName.clear();
    m_qosRules.clear();

    if (m_config.qosProfileFile.empty()) {
        if (!m_config.qosDefaultProfile.empty() || !m_config.qosTopicProfiles.empty()) {
            LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("QOS_DEFAULT_PROFILE / QOS_TOPIC_PROFILES are set but QOS_PROFILE_FILE is empty — ignored"));
        }
        return true;
    }

    // Cyclone's QoS Provider (>= 11.0, ENABLE_QOS_PROVIDER) when there is one,
    // otherwise the built-in reader above — same profiles, same semantics.
#if defined(DDS_HAS_QOS_PROVIDER) && !defined(DDS_TYPED_BUILTIN_QOS_PARSER)
#define DDS_TYPED_USE_QOS_PROVIDER 1
#endif
    std::string strFile;
    if (!absoluteRegularFile(m_config.qosProfileFile, strFile)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("QOS_PROFILE_FILE '"); LOG_STRING(m_config.qosProfileFile.c_str());
                  LOG_STRING("' does not exist or is not a regular file"));
        return false;
    }

    std::vector<RawQosRule> rawRules;
    std::string strErr;
    if (!parseQosTopicRules(m_config.qosTopicProfiles, rawRules, strErr)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("QOS_TOPIC_PROFILES: "); LOG_STRING(strErr.c_str());
                  LOG_STRING(" (expected <topic-or-glob>=<profile>[;...])"));
        return false;
    }
    if (m_config.qosDefaultProfile.empty() && rawRules.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("QOS_PROFILE_FILE is set but neither QOS_DEFAULT_PROFILE nor QOS_TOPIC_PROFILES names a profile — the file has no effect"));
    }

    // Whole-file check first, so "the file itself is unusable" and "this profile
    // name doesn't exist" get different messages.
#ifdef DDS_TYPED_USE_QOS_PROVIDER
    {
        dds_qos_provider_t *whole = nullptr;
        const dds_return_t rc     = dds_create_qos_provider(strFile.c_str(), &whole);
        if (rc != DDS_RETCODE_OK || !whole) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Cyclone's QoS Provider rejected '"); LOG_STRING(strFile.c_str());
                      LOG_STRING("' — it needs a DDS-XML file with a <dds> root (see Cyclone's own message above; "
                                 "typical causes: missing <dds> wrapper, empty <deadline>/<period/> elements, unknown tags)"));
            return false;
        }
        dds_delete_qos_provider(whole);
    }
#else
    qosxml::Document doc; // stays in place: doc.profiles points into doc.root
    {
        std::string strXmlErr;
        if (!qosxml::loadFile(strFile, doc, strXmlErr)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("QOS_PROFILE_FILE '"); LOG_STRING(strFile.c_str()); LOG_STRING("': ");
                      LOG_STRING(strXmlErr.c_str()));
            return false;
        }
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("QoS profiles: using the built-in DDS-XML reader ("); LOG_SIZET(doc.profiles.size());
                  LOG_STRING("profile(s) in the file)"));
    }
#endif

    const auto qualify = [this](const std::string &strName) {
        return strName.find("::") != std::string::npos ? strName : m_config.qosLibrary + "::" + strName;
    };

    std::map<std::string, std::shared_ptr<const ResolvedQos>> cache;
    const auto load = [&](const std::string &strKey) -> std::shared_ptr<const ResolvedQos> {
        if (auto it = cache.find(strKey); it != cache.end()) {
            return it->second;
        }
#ifdef DDS_TYPED_USE_QOS_PROVIDER
        dds_qos_provider_t *prov = nullptr;
        if (dds_create_qos_provider_scope(strFile.c_str(), &prov, strKey.c_str()) != DDS_RETCODE_OK || !prov) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("QoS profile '"); LOG_STRING(strKey.c_str()); LOG_STRING("' not found in '");
                      LOG_STRING(strFile.c_str()); LOG_STRING("' (names are case-sensitive; form is <library>::<profile>, "
                                                              "a bare name is looked up in QOS_LIBRARY='");
                      LOG_STRING(m_config.qosLibrary.c_str()); LOG_STRING("')"));
            return nullptr;
        }
        auto rq          = std::make_shared<ResolvedQos>();
        rq->key          = strKey;
        const auto fetch = [&](dds_qos_kind_t kind, dds_qos_t *&dst) {
            const dds_qos_t *q = nullptr;
            if (dds_qos_provider_get_qos(prov, kind, strKey.c_str(), &q) == DDS_RETCODE_OK && q) {
                dst = dds_create_qos();
                if (dds_copy_qos(dst, q) != DDS_RETCODE_OK) {
                    dds_delete_qos(dst);
                    dst = nullptr;
                }
            }
        };
        fetch(DDS_READER_QOS, rq->reader);
        fetch(DDS_WRITER_QOS, rq->writer);
        fetch(DDS_TOPIC_QOS, rq->topic);
        dds_delete_qos_provider(prov);
#else
        const auto pit = doc.profiles.find(strKey);
        if (pit == doc.profiles.end()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("QoS profile '"); LOG_STRING(strKey.c_str()); LOG_STRING("' not found in '");
                      LOG_STRING(strFile.c_str()); LOG_STRING("' (names are case-sensitive; form is <library>::<profile>, "
                                                              "a bare name is looked up in QOS_LIBRARY='");
                      LOG_STRING(m_config.qosLibrary.c_str()); LOG_STRING("')"));
            return nullptr;
        }
        auto rq = std::make_shared<ResolvedQos>();
        rq->key = strKey;
        {
            std::vector<std::string> warnings;
            std::string strBuildErr;
            const bool ok = qosxml::buildProfile(*pit->second, warnings, rq->reader, rq->writer, rq->topic, strBuildErr);
            for (const auto &w : warnings) {
                LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("QoS profile '"); LOG_STRING(strKey.c_str()); LOG_STRING("': "); LOG_STRING(w.c_str()));
            }
            if (!ok) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("QoS profile '"); LOG_STRING(strKey.c_str()); LOG_STRING("': "); LOG_STRING(strBuildErr.c_str()));
                return nullptr;
            }
        }
#endif

        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("QoS profile '"); LOG_STRING(strKey.c_str()); LOG_STRING("': reader=");
                  LOG_STRING(rq->reader ? "yes" : "no"); LOG_STRING("writer="); LOG_STRING(rq->writer ? "yes" : "no");
                  LOG_STRING("topic="); LOG_STRING(rq->topic ? "yes" : "no"));
        if (!rq->reader || !rq->writer || !rq->topic) {
            LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("  (a kind the profile doesn't define gets DDS defaults; if Cyclone printed a "
                                                     "'Failed to get qos with name' line for it, that is expected)"));
        }
        cache.emplace(strKey, rq);
        return rq;
    };

    if (!m_config.qosDefaultProfile.empty()) {
        m_strDefaultQosName = qualify(m_config.qosDefaultProfile);
        m_defaultQos        = load(m_strDefaultQosName);
        if (!m_defaultQos) {
            return false;
        }
    }
    for (const auto &r : rawRules) {
        QosRule rule;
        rule.topicGlob   = r.glob;
        rule.profileName = qualify(r.profile);
        rule.qos         = load(rule.profileName);
        if (!rule.qos) {
            return false;
        }
        m_qosRules.push_back(std::move(rule));
    }
    LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("QoS profiles active from '"); LOG_STRING(strFile.c_str()); LOG_STRING("':");
              LOG_SIZET(m_qosRules.size()); LOG_STRING("topic rule(s), default="); LOG_STRING(m_strDefaultQosName.empty() ? "(none)" : m_strDefaultQosName.c_str()));
    return true;
}

std::shared_ptr<const DdsTypedDriver::ResolvedQos> DdsTypedDriver::m_QosForTopic(const std::string &strTopic, std::string *pstrProfileName) const
{
    for (const auto &rule : m_qosRules) {
        if (fnmatch(rule.topicGlob.c_str(), strTopic.c_str(), 0) == 0) {
            if (pstrProfileName) {
                *pstrProfileName = rule.profileName;
            }
            return rule.qos;
        }
    }
    if (m_defaultQos && pstrProfileName) {
        *pstrProfileName = m_strDefaultQosName;
    }
    return m_defaultQos;
}

// ---------------------------------------------------------------------------
// Customer type plugin loading
// ---------------------------------------------------------------------------
bool DdsTypedDriver::m_LoadPlugin(const std::string &strPath) const
{
    void *handle = dlopen(strPath.c_str(), RTLD_NOW);
    if (!handle) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("dlopen failed for '"); LOG_STRING(strPath.c_str());
                  LOG_STRING("': "); LOG_STRING(dlerror()));
        return false;
    }

    using GetPluginFn = const DdsTypePlugin *(*)(void);
    auto getPlugin    = reinterpret_cast<GetPluginFn>(dlsym(handle, "dds_type_plugin_get"));
    if (!getPlugin) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("dlsym('dds_type_plugin_get') failed for '"); LOG_STRING(strPath.c_str());
                  LOG_STRING("': "); LOG_STRING(dlerror()));
        dlclose(handle);
        return false;
    }

    const DdsTypePlugin *plugin = getPlugin();
    if (!plugin || plugin->abi_version != DDS_TYPE_PLUGIN_ABI_VERSION) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("ABI version mismatch loading '"); LOG_STRING(strPath.c_str());
                  LOG_STRING("' — expected"); LOG_UINT32(DDS_TYPE_PLUGIN_ABI_VERSION));
        dlclose(handle);
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    const size_t count = plugin->get_type_count();
    for (size_t i = 0; i < count; ++i) {
        const DdsTypeEntry *entry = plugin->get_type(i);
        if (!entry || !entry->topic_name) {
            continue;
        }
        auto existing = m_typesByTopic.find(entry->topic_name);
        if (existing != m_typesByTopic.end()) {
            LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Topic '"); LOG_STRING(entry->topic_name);
                      LOG_STRING("' already registered by a previously loaded plugin — overriding with '");
                      LOG_STRING(plugin->customer_name); LOG_STRING("'"));
        }
        m_typesByTopic[entry->topic_name] = entry;
    }
    m_loadedHandles.push_back(handle);

    LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("Loaded customer type plugin '"); LOG_STRING(plugin->customer_name);
              LOG_STRING("' from '"); LOG_STRING(strPath.c_str()); LOG_STRING("' —"); LOG_SIZET(count); LOG_STRING("topic(s)"));
    return true;
}

// ---------------------------------------------------------------------------
// Per-topic writer/reader lifecycle
// ---------------------------------------------------------------------------
DdsTypedDriver::DdsEntity DdsTypedDriver::m_EnsureLocalWriter(const std::string &strTopic) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_localWriters.find(strTopic);
    if (it != m_localWriters.end()) {
        return it->second.writer;
    }

    auto typeIt = m_typesByTopic.find(strTopic);
    if (typeIt == m_typesByTopic.end()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("No loaded type plugin publishes topic '"); LOG_STRING(strTopic.c_str());
                  LOG_STRING("' — LOAD its customer .so first"));
        return kInvalidEntity;
    }
    const DdsTypeEntry *entry = asTypeEntry(typeIt->second);

    std::string strProfile;
    const auto profile       = m_QosForTopic(strTopic, &strProfile);

    const DdsEntity topicEnt = dds_create_topic(m_participant, entry->descriptor, strTopic.c_str(), profile ? profile->topic : nullptr, nullptr);
    if (topicEnt < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("dds_create_topic failed for '"); LOG_STRING(strTopic.c_str());
                  LOG_STRING("': "); LOG_STRING(dds_strretcode(-topicEnt)));
        return kInvalidEntity;
    }
    // Profile selected for this topic: its <datawriter_qos> is used as-is (nullptr =
    // the profile has none = DDS defaults). No profile: the built-in QoS.
    dds_qos_t *ownQos    = nullptr;
    const dds_qos_t *qos = nullptr;
    if (profile) {
        qos = profile->writer;
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("Writer for '"); LOG_STRING(strTopic.c_str()); LOG_STRING("' uses QoS profile '");
                  LOG_STRING(strProfile.c_str()); LOG_STRING("'"));
    } else {
        ownQos = dds_create_qos();
        dds_qset_reliability(ownQos, m_config.reliable ? DDS_RELIABILITY_RELIABLE : DDS_RELIABILITY_BEST_EFFORT,
                             m_config.reliable ? DDS_SECS(10) : 0);
        dds_qset_history(ownQos, DDS_HISTORY_KEEP_LAST, static_cast<int32_t>(std::max<uint32_t>(1, m_config.historyDepth)));
        qos = ownQos;
    }
    const DdsEntity writerEnt = dds_create_writer(m_participant, topicEnt, qos, nullptr);
    if (ownQos) {
        dds_delete_qos(ownQos);
    }
    if (writerEnt < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("dds_create_writer failed for '"); LOG_STRING(strTopic.c_str());
                  LOG_STRING("': "); LOG_STRING(dds_strretcode(-writerEnt)));
        dds_delete(topicEnt);
        return kInvalidEntity;
    }

    m_localWriters.emplace(strTopic, LocalWriter{topicEnt, writerEnt, entry});
    return writerEnt;
}

void DdsTypedDriver::m_OnReaderDataAvailable(DdsEntity reader, void *pvArg)
{
    auto *localReader                = static_cast<LocalReader *>(pvArg);
    const DdsTypeEntry *entry        = asTypeEntry(localReader->typeEntry);

    void *samples[kBuiltinReadBatch] = {};
    dds_sample_info_t infos[kBuiltinReadBatch];
    for (auto &s : samples) {
        s = entry->alloc_sample();
    }

    dds_return_t n;
    while ((n = dds_take(reader, samples, infos, kBuiltinReadBatch, kBuiltinReadBatch)) > 0) {
        {
            std::lock_guard<std::mutex> lock(localReader->queueMutex);
            for (dds_return_t i = 0; i < n; ++i) {
                if (!infos[i].valid_data) {
                    continue;
                }
                char buf[kEncodeBufCap];
                if (entry->encode(samples[i], buf, sizeof(buf))) {
                    localReader->queue.emplace_back(buf);
                }
            }
        }
        localReader->queueCv.notify_all();
        if (localReader->owner) {
            // Wakes m_MultiplexedReceive()'s slice-loop promptly instead of
            // making it wait out its next poll slice — see that function's
            // doc comment and m_anyDataGeneration's. No lock needed to bump
            // a std::atomic or notify_all() a condition_variable.
            localReader->owner->m_anyDataGeneration.fetch_add(1, std::memory_order_relaxed);
            localReader->owner->m_anyDataCv.notify_all();
        }
        if (n < static_cast<dds_return_t>(kBuiltinReadBatch)) {
            break;
        }
    }

    for (auto &s : samples) {
        entry->free_sample(s, DDS_FREE_ALL);
    }
}

std::shared_ptr<DdsTypedDriver::LocalReader> DdsTypedDriver::m_EnsureLocalReader(const std::string &topic) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_localReaders.find(topic);
    if (it != m_localReaders.end()) {
        return it->second;
    }

    if (m_config.maxSubscriptions > 0 && m_localReaders.size() >= m_config.maxSubscriptions) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SUBSCRIBE would exceed the configured cap of");
                  LOG_UINT32(m_config.maxSubscriptions);
                  LOG_STRING("concurrent topics ('ms=' CONFIG / MAX_SUBSCRIPTIONS ini key) — UNSUBSCRIBE something first or raise the cap"));
        return nullptr;
    }

    auto typeIt = m_typesByTopic.find(topic);
    if (typeIt == m_typesByTopic.end()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("No loaded type plugin subscribes topic '"); LOG_STRING(topic.c_str());
                  LOG_STRING("' — LOAD its customer .so first"));
        return nullptr;
    }
    const DdsTypeEntry *entry = asTypeEntry(typeIt->second);

    auto localReader          = std::make_shared<LocalReader>();
    localReader->typeEntry    = entry;
    // const_cast is safe/intentional here, same rationale as every other
    // mutable member this (const) method already writes through — owner is
    // stored as a plain pointer (not `mutable`) because it lives inside
    // LocalReader, not directly on DdsTypedDriver, but it's used exactly
    // like one: only ever to reach m_anyDataCv/m_anyDataGeneration, which
    // are themselves mutable.
    localReader->owner        = const_cast<DdsTypedDriver *>(this);

    std::string strProfile;
    const auto profile       = m_QosForTopic(topic, &strProfile);

    const DdsEntity topicEnt = dds_create_topic(m_participant, entry->descriptor, topic.c_str(), profile ? profile->topic : nullptr, nullptr);
    if (topicEnt < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("dds_create_topic failed for '"); LOG_STRING(topic.c_str());
                  LOG_STRING("': "); LOG_STRING(dds_strretcode(-topicEnt)));
        return localReader; // still registered, empty queue forever — same convention as DdsDriver
    }
    // See m_EnsureLocalWriter(): profile's <datareader_qos> as-is, else the built-in QoS.
    dds_qos_t *ownQos    = nullptr;
    const dds_qos_t *qos = nullptr;
    if (profile) {
        qos = profile->reader;
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("Reader for '"); LOG_STRING(topic.c_str()); LOG_STRING("' uses QoS profile '");
                  LOG_STRING(strProfile.c_str()); LOG_STRING("'"));
    } else {
        ownQos = dds_create_qos();
        dds_qset_reliability(ownQos, m_config.reliable ? DDS_RELIABILITY_RELIABLE : DDS_RELIABILITY_BEST_EFFORT,
                             m_config.reliable ? DDS_SECS(10) : 0);
        dds_qset_history(ownQos, DDS_HISTORY_KEEP_LAST, static_cast<int32_t>(std::max<uint32_t>(1, m_config.historyDepth)));
        qos = ownQos;
    }

    dds_listener_t *listener = dds_create_listener(localReader.get());
    dds_lset_data_available(listener, &DdsTypedDriver::m_OnReaderDataAvailable);
    const DdsEntity readerEnt = dds_create_reader(m_participant, topicEnt, qos, listener);
    dds_delete_listener(listener);
    if (ownQos) {
        dds_delete_qos(ownQos);
    }
    if (readerEnt < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("dds_create_reader failed for '"); LOG_STRING(topic.c_str());
                  LOG_STRING("': "); LOG_STRING(dds_strretcode(-readerEnt)));
        dds_delete(topicEnt);
    } else {
        localReader->topic  = topicEnt;
        localReader->reader = readerEnt;
    }

    m_localReaders.emplace(topic, localReader);
    return localReader;
}

bool DdsTypedDriver::m_Publish(const std::string &strTopic, const std::string &strText) const
{
    const DdsEntity writer = m_EnsureLocalWriter(strTopic);
    if (writer < 0) {
        return false;
    }

    const DdsTypeEntry *entry = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        entry = asTypeEntry(m_localWriters.at(strTopic).typeEntry);
    }

    void *sample = entry->alloc_sample();
    if (!entry->decode(strText.c_str(), sample)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("decode() rejected PUBLISH payload for '"); LOG_STRING(strTopic.c_str()); LOG_STRING("'"));
        entry->free_sample(sample, DDS_FREE_ALL);
        return false;
    }

    const dds_return_t rc = dds_write(writer, sample);
    entry->free_sample(sample, DDS_FREE_ALL);
    if (rc != DDS_RETCODE_OK) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("dds_write failed for '"); LOG_STRING(strTopic.c_str());
                  LOG_STRING("': "); LOG_STRING(dds_strretcode(-rc)));
        return false;
    }

    if (gui_mode_active()) {
        // Dumps the PUBLISH text as given to decode() (see DdsTypeEntry's
        // doc comment) — same "whatever crossed the DDS_TYPED.CMD text
        // boundary" convention as receive()'s Rx dump below, which shows
        // whatever encode() produced.
        gui_notify_comm_dump(m_config.strInstanceName, describeConnection(strTopic), CommDir::Tx,
                             reinterpret_cast<const uint8_t *>(strText.data()), static_cast<uint32_t>(strText.size()));
    }
    return true;
}

bool DdsTypedDriver::m_Subscribe(const std::string &strTopic) const
{
    return m_EnsureLocalReader(strTopic) != nullptr;
}

bool DdsTypedDriver::m_Unsubscribe(const std::string &strTopic) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_localReaders.find(strTopic);
    if (it == m_localReaders.end()) {
        return false;
    }
    if (it->second->reader >= 0) {
        dds_delete(it->second->reader);
    }
    if (it->second->topic >= 0) {
        dds_delete(it->second->topic);
    }
    m_localReaders.erase(it);
    return true;
}

// ---------------------------------------------------------------------------
// DDS_TYPED.CMD > LIST
// ---------------------------------------------------------------------------
std::vector<DdsTypedDriver::DiscoveredParticipantView> DdsTypedDriver::listParticipants() const
{
    std::vector<DiscoveredParticipantView> out;
    if (!is_open() || m_biParticipantReader < 0) {
        return out;
    }

    void *samples[kBuiltinReadBatch] = {};
    dds_sample_info_t infos[kBuiltinReadBatch];
    for (auto &s : samples) {
        s = dds_alloc(sizeof(dds_builtintopic_participant_t));
    }

    const dds_return_t n = dds_read(m_biParticipantReader, samples, infos, kBuiltinReadBatch, kBuiltinReadBatch);
    const auto nowNs     = dds_time();
    for (dds_return_t i = 0; i < n; ++i) {
        if (!infos[i].valid_data || infos[i].instance_state != DDS_ALIVE_INSTANCE_STATE) {
            continue;
        }
        auto *p                   = static_cast<dds_builtintopic_participant_t *>(samples[i]);
        const std::string guidHex = guidToHex(p->key);
        if (guidHex == m_guidHex) {
            continue;
        }

        DiscoveredParticipantView v;
        v.guidHex = guidHex;
        if (p->qos) {
            void *ud     = nullptr;
            size_t udLen = 0;
            if (dds_qget_userdata(p->qos, &ud, &udLen) && ud != nullptr) {
                v.name.assign(static_cast<const char *>(ud), udLen);
                dds_free(ud);
            }
        }
        v.ageSec = static_cast<double>(nowNs - infos[i].source_timestamp) / 1e9;
        out.push_back(std::move(v));
    }
    for (auto &s : samples) {
        dds_free(s);
    }
    return out;
}

std::vector<DdsTypedDriver::DiscoveredEndpointView> DdsTypedDriver::listEndpoints() const
{
    std::vector<DiscoveredEndpointView> out;
    if (!is_open()) {
        return out;
    }

    const struct
    {
            DdsEntity reader;
            bool isWriter;
    } kBuiltinReaders[] = {
        {m_biPublicationReader, true},
        {m_biSubscriptionReader, false},
    };

    for (const auto &br : kBuiltinReaders) {
        if (br.reader < 0) {
            continue;
        }
        void *samples[kBuiltinReadBatch] = {};
        dds_sample_info_t infos[kBuiltinReadBatch];
        for (auto &s : samples) {
            s = dds_alloc(sizeof(dds_builtintopic_endpoint_t));
        }

        const dds_return_t n = dds_read(br.reader, samples, infos, kBuiltinReadBatch, kBuiltinReadBatch);
        for (dds_return_t i = 0; i < n; ++i) {
            if (!infos[i].valid_data || infos[i].instance_state != DDS_ALIVE_INSTANCE_STATE) {
                continue;
            }
            auto *e = static_cast<dds_builtintopic_endpoint_t *>(samples[i]);
            if (guidToHex(e->participant_key) == m_guidHex) {
                continue;
            }

            DiscoveredEndpointView v;
            v.guidHex  = guidToHex(e->key);
            v.topic    = e->topic_name ? e->topic_name : "";
            v.typeName = e->type_name ? e->type_name : "";
            v.isWriter = br.isWriter;
            if (e->qos) {
                dds_reliability_kind_t kind = DDS_RELIABILITY_BEST_EFFORT;
                dds_duration_t maxBlock     = 0;
                if (dds_qget_reliability(e->qos, &kind, &maxBlock)) {
                    v.reliable = (kind == DDS_RELIABILITY_RELIABLE);
                }
            }
            out.push_back(std::move(v));
        }
        for (auto &s : samples) {
            dds_free(s);
        }
    }
    return out;
}

std::string DdsTypedDriver::m_BuildListText() const
{
    std::ostringstream oss;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        oss << "loaded_types=" << m_typesByTopic.size();
        for (const auto &[topic, entryOpaque] : m_typesByTopic) {
            const DdsTypeEntry *e = asTypeEntry(entryOpaque);
            oss << " " << topic << "[" << e->descriptor->m_typename << "]";
        }
    }

    const auto participants = listParticipants();
    oss << " ; participants=" << participants.size();
    for (const auto &p : participants) {
        oss << " | " << p.guidHex << " '" << p.name << "' age=" << p.ageSec << "s";
    }

    const auto endpoints = listEndpoints();
    size_t remoteWriters = 0, remoteReaders = 0;
    for (const auto &e : endpoints) {
        (e.isWriter ? remoteWriters : remoteReaders)++;
    }
    oss << " ; remote_writers=" << remoteWriters << " remote_readers=" << remoteReaders;
    for (const auto &e : endpoints) {
        oss << " " << e.topic << "[" << e.typeName << "]" << (e.isWriter ? "(W," : "(R,")
            << (e.reliable ? "reliable)" : "best_effort)");
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    oss << " ; local_writers=" << m_localWriters.size();
    for (const auto &[topic, w] : m_localWriters) {
        (void)w;
        std::string strProfile;
        m_QosForTopic(topic, &strProfile);
        oss << " " << topic;
        if (!strProfile.empty()) {
            oss << "{qos=" << strProfile << "}";
        }
    }
    oss << " ; local_readers=" << m_localReaders.size();
    for (const auto &[topic, r] : m_localReaders) {
        (void)r;
        std::string strProfile;
        m_QosForTopic(topic, &strProfile);
        oss << " " << topic;
        if (!strProfile.empty()) {
            oss << "{qos=" << strProfile << "}";
        }
    }

    return oss.str();
}

// ---------------------------------------------------------------------------
// Intermediary layer: DDS_TYPED.CMD argument decomposition
// ---------------------------------------------------------------------------
namespace {
    void tokenize(std::span<const uint8_t> dataSpan, std::vector<std::string> &vOutTokens)
    {
        vOutTokens.clear();
        size_t len = dataSpan.size();
        while (len > 0 && dataSpan[len - 1] == 0) {
            --len;
        }
        std::string text(reinterpret_cast<const char *>(dataSpan.data()), len);
        text           = ustring::trim(text);

        size_t i       = 0;
        const size_t n = text.size();
        while (i < n) {
            while (i < n && std::isspace(static_cast<unsigned char>(text[i]))) {
                ++i;
            }
            if (i >= n) {
                break;
            }
            const size_t start = i;
            while (i < n && !std::isspace(static_cast<unsigned char>(text[i]))) {
                ++i;
            }
            vOutTokens.push_back(text.substr(start, i - start));
        }
    }
} // namespace

ICommDriver::WriteResult DdsTypedDriver::send(uint32_t, std::span<const uint8_t> dataSpan, std::string_view xtra_params,
                                              std::stop_token /*stop_tok*/) const
{
    // send() never blocks — same rationale as DdsDriver::send(); stop_tok is
    // accepted only for signature consistency.
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
                if (i > 2) {
                    text += ' ';
                }
                text += tokens[i];
            }
            ok = m_Publish(topic, text);
        }
    } else if (cmdKeyword == "SUBSCRIBE") {
        if (tokens.size() < 2) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SUBSCRIBE requires: <topic>[,<topic>...] [<topic>[,<topic>...] ...]"));
        } else {
            // Each whitespace token may itself be a comma-separated list, so
            // "SUBSCRIBE a,b c" and "SUBSCRIBE a b c" (and any mix) all name
            // the same three topics — see class doc comment. Every named
            // topic gets its own parallel Cyclone reader (m_EnsureLocalReader
            // is a no-op for one already SUBSCRIBEd, so re-listing an
            // existing topic is harmless).
            std::vector<std::string> topics;
            for (size_t i = 1; i < tokens.size(); ++i) {
                for (auto &t : ustring::tokenize(tokens[i], ',')) {
                    if (!t.empty()) {
                        topics.push_back(std::move(t));
                    }
                }
            }
            if (topics.empty()) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SUBSCRIBE requires at least one non-empty topic name"));
            } else {
                // Best-effort across the list — one bad/typo'd topic (e.g. no
                // loaded type owns it yet) shouldn't stop the rest from being
                // SUBSCRIBEd, same "continue past individual failures, log
                // each one" convention as PRELOAD_PLUGINS in open() above.
                // ok reflects whether *every* requested topic succeeded.
                ok = true;
                for (const auto &topic : topics) {
                    if (!m_Subscribe(topic)) {
                        ok = false;
                    }
                }
            }
            // The `\x01LIST` sentinel only ever applies until the next
            // SUBSCRIBE/UNSUBSCRIBE — see receive()'s doc comment.
            std::lock_guard<std::mutex> lock(m_activeTopicMutex);
            m_strActiveTopic.clear();
        }
    } else if (cmdKeyword == "UNSUBSCRIBE") {
        if (tokens.size() != 2) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("UNSUBSCRIBE requires exactly: <topic>"));
        } else {
            ok = m_Unsubscribe(tokens[1]);
            std::lock_guard<std::mutex> lock(m_activeTopicMutex);
            m_strActiveTopic.clear();
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

    result.status        = ok ? ICommDriver::Status::SUCCESS : ICommDriver::Status::OPERATION_FAILED;
    result.bytes_written = ok ? dataSpan.size() : 0;
    return result;
}

ICommDriver::ReadResult DdsTypedDriver::receive(uint32_t u32ReadTimeout, std::span<uint8_t> dataSpan,
                                                const ICommDriver::ReadOptions &, std::string_view xtra_params,
                                                std::stop_token stop_tok) const
{
    ReadResult result;

    if (!is_open()) {
        result.status = ICommDriver::Status::PORT_ACCESS;
        return result;
    }

    {
        std::lock_guard<std::mutex> lock(m_activeTopicMutex);
        if (m_strActiveTopic == "\x01LIST") {
            const std::string text = m_BuildListText();
            const size_t len       = std::min(dataSpan.size(), text.size());
            std::memcpy(dataSpan.data(), text.data(), len);
            result.status     = ICommDriver::Status::SUCCESS;
            result.bytes_read = len;
            return result;
        }
    }

    // `DDS_TYPED.CMD < ~ <topic>` — explicit topic, always raw text, must
    // already be SUBSCRIBEd (a receive never silently creates a new reader —
    // only LOAD/SUBSCRIBE create DDS entities, see class doc comment).
    std::string strTopic(xtra_params);
    if (!strTopic.empty()) {
        std::shared_ptr<LocalReader> reader;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_localReaders.find(strTopic);
            if (it != m_localReaders.end()) {
                reader = it->second;
            }
        }
        if (!reader) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("DDS_TYPED.CMD < ~ '"); LOG_STRING(strTopic.c_str());
                      LOG_STRING("' — not currently SUBSCRIBEd, SUBSCRIBE it first"));
            result.status = ICommDriver::Status::INVALID_PARAM;
            return result;
        }

        auto text = m_WaitPopOne(*reader, u32ReadTimeout, stop_tok);
        if (!text) {
            result.status = ICommDriver::Status::READ_TIMEOUT;
            return result;
        }

        const size_t len = std::min(dataSpan.size(), text->size());
        std::memcpy(dataSpan.data(), text->data(), len);
        result.status     = ICommDriver::Status::SUCCESS;
        result.bytes_read = len;

        if (gui_mode_active()) {
            gui_notify_comm_dump(m_config.strInstanceName, describeConnection(strTopic), CommDir::Rx,
                                 reinterpret_cast<const uint8_t *>(text->data()), static_cast<uint32_t>(text->size()));
        }
        return result;
    }

    // Bare `DDS_TYPED.CMD <` — no explicit topic. Snapshot which topics are
    // currently SUBSCRIBEd right now (a concurrent SUBSCRIBE/UNSUBSCRIBE
    // from another thread mid-wait just means this particular call didn't
    // see it — the next call will).
    std::vector<std::pair<std::string, std::shared_ptr<LocalReader>>> readers;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        readers.reserve(m_localReaders.size());
        for (const auto &[topic, r] : m_localReaders) {
            readers.emplace_back(topic, r);
        }
    }

    if (readers.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("DDS_TYPED.CMD < with no prior SUBSCRIBE on this participant"));
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }

    if (readers.size() > 1) {
        // 2+ topics SUBSCRIBEd in parallel — multiplex across all of them.
        return m_MultiplexedReceive(u32ReadTimeout, dataSpan, stop_tok);
    }

    // Exactly one topic SUBSCRIBEd: identical to every prior release —
    // single reader, raw (unprefixed) text.
    const auto &[strTopic1, reader] = readers.front();
    auto text                       = m_WaitPopOne(*reader, u32ReadTimeout, stop_tok);
    if (!text) {
        result.status = ICommDriver::Status::READ_TIMEOUT;
        return result;
    }

    const size_t len = std::min(dataSpan.size(), text->size());
    std::memcpy(dataSpan.data(), text->data(), len);
    result.status     = ICommDriver::Status::SUCCESS;
    result.bytes_read = len;

    if (gui_mode_active()) {
        gui_notify_comm_dump(m_config.strInstanceName, describeConnection(strTopic1), CommDir::Rx,
                             reinterpret_cast<const uint8_t *>(text->data()), static_cast<uint32_t>(text->size()));
    }
    return result;
}

// ---------------------------------------------------------------------------
// Multiplexed receive — bare "DDS_TYPED.CMD <" with 2+ topics SUBSCRIBEd
// ---------------------------------------------------------------------------
/// Blocks until any currently-SUBSCRIBEd topic's reader has a queued sample,
/// then returns `<topic>: <text>` for whichever one it picks.
///
/// Ordering caveat: this is *not* a strict global-arrival-order FIFO across
/// topics — each topic's own queue is FIFO, but when two topics both have
/// data at the moment this wakes, the earlier one in topic-name order (the
/// scan below walks m_localReaders, a std::map) is drained first, not
/// necessarily whichever sample physically arrived first on the wire. For
/// the ~200ms slice this can differ within, that's an acceptable trade-off
/// for a scripting/test tool; a caller that needs strict cross-topic
/// ordering should read each topic individually via `< ~ <topic>` instead.
ICommDriver::ReadResult DdsTypedDriver::m_MultiplexedReceive(uint32_t u32ReadTimeout, std::span<uint8_t> dataSpan,
                                                             std::stop_token stop_tok) const
{
    ReadResult result;
    constexpr auto kSliceMs = std::chrono::milliseconds(200);
    const bool bInfinite    = (u32ReadTimeout == 0);
    const auto tDeadline    = std::chrono::steady_clock::now() + std::chrono::milliseconds(u32ReadTimeout);

    while (true) {
        if (stop_tok.stop_requested()) {
            result.status = ICommDriver::Status::READ_TIMEOUT;
            return result;
        }
        if (!bInfinite && std::chrono::steady_clock::now() >= tDeadline) {
            result.status = ICommDriver::Status::READ_TIMEOUT;
            return result;
        }

        // Re-snapshot every slice — a SUBSCRIBE/UNSUBSCRIBE that happens
        // while this call is blocked takes effect on the very next slice.
        std::vector<std::pair<std::string, std::shared_ptr<LocalReader>>> readers;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            readers.reserve(m_localReaders.size());
            for (const auto &[topic, r] : m_localReaders) {
                readers.emplace_back(topic, r);
            }
        }

        for (const auto &[topic, reader] : readers) {
            std::string text;
            {
                std::lock_guard<std::mutex> qlock(reader->queueMutex);
                if (reader->queue.empty()) {
                    continue;
                }
                text = std::move(reader->queue.front());
                reader->queue.pop_front();
            }

            const std::string line = topic + ": " + text;
            const size_t len       = std::min(dataSpan.size(), line.size());
            std::memcpy(dataSpan.data(), line.data(), len);
            result.status     = ICommDriver::Status::SUCCESS;
            result.bytes_read = len;

            if (gui_mode_active()) {
                gui_notify_comm_dump(m_config.strInstanceName, describeConnection(topic), CommDir::Rx,
                                     reinterpret_cast<const uint8_t *>(text.data()), static_cast<uint32_t>(text.size()));
            }
            return result;
        }

        // Nothing ready on any topic yet — wait for the next arrival (on any
        // reader) or the end of this slice, whichever comes first, then loop
        // to rescan. m_anyDataMutex only ever guards this wait; the actual
        // per-topic data lives under each reader's own queueMutex above.
        // The predicate compares m_anyDataGeneration against the value
        // captured just before waiting, so a genuine notify_all() (bumped
        // generation) returns immediately instead of spinning out the whole
        // slice — see m_anyDataGeneration's doc comment.
        const uint64_t genBefore = m_anyDataGeneration.load(std::memory_order_relaxed);
        auto pred                = [this, genBefore] {
            return m_anyDataGeneration.load(std::memory_order_relaxed) != genBefore;
        };
        std::unique_lock<std::mutex> anyLock(m_anyDataMutex);
        if (bInfinite) {
            m_anyDataCv.wait_for(anyLock, stop_tok, kSliceMs, pred);
        } else {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                tDeadline - std::chrono::steady_clock::now());
            m_anyDataCv.wait_for(anyLock, stop_tok, std::clamp(remaining, std::chrono::milliseconds(0), kSliceMs), pred);
        }
    }
}
