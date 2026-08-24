#ifndef DDS_SETUP_HPP
#define DDS_SETUP_HPP

#include "dds_plugin.hpp"
#include "uBoolEvaluator.hpp"
#include "uCommandExec.hpp"
#include "uPluginSettings.hpp"

#include <sstream>

#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LOG_HDR "DDS PLUGIN  |"

// INI Keys
#define K_DOMAIN        "DOMAIN"
#define K_PARTICIPANT_ID "PARTICIPANT_ID"
#define K_USE_IPV6      "USE_IPV6"
#define K_IFACE         "IFACE"
#define K_MCAST_IFACE   "MCAST_IFACE"
#define K_SPDP_MCAST_GROUP "SPDP_MULTICAST_GROUP"
#define K_NAME          "PARTICIPANT_NAME"
#define K_TTL           "TTL"
#define K_SPDP_PERIOD   "SPDP_PERIOD_MS"
#define K_LEASE         "LEASE_DURATION_SEC"
#define K_RELIABLE      "RELIABLE"
#define K_HB_PERIOD     "HEARTBEAT_PERIOD_MS"
#define K_HISTORY_DEPTH "HISTORY_DEPTH"
#define K_FRAG_THRESHOLD "FRAGMENT_THRESHOLD_BYTES"
#define K_ARTEFACTS     "ARTEFACTS_PATH"
#define K_READ_TIMEOUT  "READ_TIMEOUT"
#define K_READ_BUFSIZE  "READ_BUFFER_SIZE"

// Config Command Short Keys
#define SK_DOMAIN  "d"
#define SK_PID     "pid"
#define SK_IPV6    "v6"
#define SK_IFACE   "i"
#define SK_MCAST   "mi"
#define SK_MCGROUP "mg"
#define SK_NAME    "n"
#define SK_TTL     "t"
#define SK_SPDP    "sp"
#define SK_LEASE   "l"
#define SK_REL     "r"
#define SK_HBPER   "hb"
#define SK_HIST    "hd"
#define SK_FRAG    "fr"
#define SK_RTOUT   "rt"
#define SK_RBUF    "rb"

bool DdsPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    m_strInstanceName = psSetParams->strInstanceName.empty() ? DDS_PLUGIN_NAME : psSetParams->strInstanceName;

    if (psSetParams->mapSettings.empty()) return true;

    PluginSettingsBinder sSettings;
    sSettings.Bind(K_ARTEFACTS, m_strArtefactsPath);
    sSettings.Bind(K_DOMAIN,         [this](const std::string& v) { return setDomainId(v); });
    sSettings.Bind(K_PARTICIPANT_ID, [this](const std::string& v) { return setParticipantId(v); });
    sSettings.Bind(K_USE_IPV6,       [this](const std::string& v) { return setUseIpv6(v); });
    sSettings.Bind(K_IFACE,          m_strIface);
    sSettings.Bind(K_MCAST_IFACE,    m_strMcastIface);
    sSettings.Bind(K_SPDP_MCAST_GROUP, m_strSpdpMcastGroup);
    sSettings.Bind(K_NAME,           m_strParticipantName);
    sSettings.Bind(K_TTL,            [this](const std::string& v) { return setTtl(v); });
    sSettings.Bind(K_SPDP_PERIOD,    [this](const std::string& v) { return setSpdpPeriodMs(v); });
    sSettings.Bind(K_LEASE,          [this](const std::string& v) { return setLeaseDurationSec(v); });
    sSettings.Bind(K_RELIABLE,       [this](const std::string& v) { return setReliable(v); });
    sSettings.Bind(K_HB_PERIOD,      [this](const std::string& v) { return setHeartbeatPeriodMs(v); });
    sSettings.Bind(K_HISTORY_DEPTH,  [this](const std::string& v) { return setHistoryDepth(v); });
    sSettings.Bind(K_FRAG_THRESHOLD, [this](const std::string& v) { return setFragmentThresholdBytes(v); });
    sSettings.Bind(K_READ_TIMEOUT,   [this](const std::string& v) { return setReadTimeout(v); });
    sSettings.Bind(K_READ_BUFSIZE,   [this](const std::string& v) { return setReadBufferSize(v); });
    sSettings.Bind(ucmdexec::RAW_RESULT_INI_KEY, m_bRawResult);
    sSettings.Bind(ucmdexec::CYCLIC_CACHED_INI_KEY, m_bCyclicCached);

    sSettings.Apply(psSetParams->mapSettings, nullptr, /*bStopOnFirstError=*/false);

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Config updated. Domain:"); LOG_UINT32(m_u32DomainId));
    return true;
}

bool DdsPlugin::m_DDS_CONFIG(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();
    if (args.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing config args"));
        return false;
    }

    std::istringstream stream(args);
    std::string token;
    bool bRetVal = true;

    while (stream >> token) {
        auto eqPos = token.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = token.substr(0, eqPos);
        std::string val = token.substr(eqPos + 1);

        if (!val.empty() && val[0] == '$') {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Deferring '"); LOG_STRING(key);
                      LOG_STRING("=" ); LOG_STRING(val);
                      LOG_STRING("' - value is a macro, resolved at execution time"));
            continue;
        }

        if (key == SK_DOMAIN)      { if (!setDomainId(val)) bRetVal = false; }
        else if (key == SK_PID)    { if (!setParticipantId(val)) bRetVal = false; }
        else if (key == SK_IPV6)   { if (!setUseIpv6(val)) bRetVal = false; }
        else if (key == SK_IFACE)  setIface(val);
        else if (key == SK_MCAST)  setMcastIface(val);
        else if (key == SK_MCGROUP) setSpdpMcastGroup(val);
        else if (key == SK_NAME)   setParticipantName(val);
        else if (key == SK_TTL)    { if (!setTtl(val)) bRetVal = false; }
        else if (key == SK_SPDP)   { if (!setSpdpPeriodMs(val)) bRetVal = false; }
        else if (key == SK_LEASE)  { if (!setLeaseDurationSec(val)) bRetVal = false; }
        else if (key == SK_REL)    { if (!setReliable(val)) bRetVal = false; }
        else if (key == SK_HBPER)  { if (!setHeartbeatPeriodMs(val)) bRetVal = false; }
        else if (key == SK_HIST)   { if (!setHistoryDepth(val)) bRetVal = false; }
        else if (key == SK_FRAG)   { if (!setFragmentThresholdBytes(val)) bRetVal = false; }
        else if (key == SK_RTOUT)  { if (!setReadTimeout(val)) bRetVal = false; }
        else if (key == SK_RBUF)   { if (!setReadBufferSize(val)) bRetVal = false; }
        else if (key == ucmdexec::RAW_RESULT_CONFIG_KEY) { if (!setRawResult(val)) bRetVal = false; }
        else if (key == ucmdexec::CYCLIC_CACHED_CONFIG_KEY) { if (!setCyclicCached(val)) bRetVal = false; }
    }

    // A CONFIG changing DOMAIN/PARTICIPANT_ID/IFACE after the driver is
    // already open would silently leave stale sockets bound to the old
    // ports — force a fresh open() next use instead, same convention as
    // "config changed, re-open on next CMD" everywhere else in this codebase.
    if (bRetVal) {
        m_pDriver.reset();
    }
    return bRetVal;
}

#endif // DDS_SETUP_HPP
