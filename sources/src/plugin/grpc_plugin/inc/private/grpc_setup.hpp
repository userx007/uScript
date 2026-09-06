#ifndef GRPC_SETUP_HPP
#define GRPC_SETUP_HPP

#include "grpc_plugin.hpp"
#include "PluginSetup.hpp"
#include "uCommandExec.hpp"
#include "uPluginSettings.hpp"

/////////////////////////////////////////////////////////////////////////////////
//                            LOG DEFINITIONS                                  //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR "GRPC_P      |"
#define LOG_HDR  LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                               //
/////////////////////////////////////////////////////////////////////////////////

// INI Keys
#define K_HOST              "HOST"
#define K_PORT              "PORT"
#define K_TLS_ENABLED       "TLS_ENABLED"
#define K_TLS_CA            "TLS_CA_CERT"
#define K_TLS_CLIENT_CERT   "TLS_CLIENT_CERT"
#define K_TLS_CLIENT_KEY    "TLS_CLIENT_KEY"
#define K_ARTEFACTS         "ARTEFACTS_PATH"
#define K_DESCRIPTOR_SET    "DESCRIPTOR_SET"
#define K_AUTH_TOKEN        "AUTH_TOKEN"
#define K_CALL_TIMEOUT      "CALL_TIMEOUT"
#define K_CONNECT_TIMEOUT   "CONNECT_TIMEOUT"
#define K_READ_TIMEOUT      "READ_TIMEOUT"
#define K_READ_BUFSIZE      "READ_BUFFER_SIZE"


/////////////////////////////////////////////////////////////////////////////////
//                  CONFIGURATION INTERFACES                                   //
/////////////////////////////////////////////////////////////////////////////////

bool GrpcPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    if (psSetParams->mapSettings.empty()) return true;

    PluginSettingsBinder sSettings;
    sSettings.Bind(K_ARTEFACTS,      m_strArtefactsPath);
    sSettings.Bind(K_HOST,           m_strHost);
    sSettings.Bind(K_PORT,           [this](const std::string& v) { return setPort(v); });
    sSettings.Bind(K_TLS_ENABLED,    [this](const std::string& v) { return setTlsEnabled(v); });
    sSettings.Bind(K_TLS_CA,          m_strTlsCaPath);
    sSettings.Bind(K_TLS_CLIENT_CERT, m_strTlsCertPath);
    sSettings.Bind(K_TLS_CLIENT_KEY,  m_strTlsKeyPath);
    sSettings.Bind(K_DESCRIPTOR_SET,  m_strDescriptorSetPath);
    sSettings.Bind(K_AUTH_TOKEN,      m_strAuthToken);
    sSettings.Bind(K_CALL_TIMEOUT,    [this](const std::string& v) { return setCallTimeout(v); });
    sSettings.Bind(K_CONNECT_TIMEOUT, [this](const std::string& v) { return setConnectTimeout(v); });
    sSettings.Bind(K_READ_TIMEOUT,    [this](const std::string& v) { return setReadTimeout(v); });
    sSettings.Bind(K_READ_BUFSIZE,    [this](const std::string& v) { return setReadBufferSize(v); });
    sSettings.Bind(ucmdexec::RAW_RESULT_INI_KEY, m_bRawResult);
    sSettings.Bind(ucmdexec::CYCLIC_CACHED_INI_KEY, m_bCyclicCached);

    sSettings.Apply(psSetParams->mapSettings, nullptr, /*bStopOnFirstError=*/false);

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Config updated. Host:") LOG_STRING(m_strHost));

    return true;
}


bool GrpcPlugin::m_GRPC_CONFIG(const std::string& args, std::stop_token st) const
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
    BoolExprEvaluator beEvaluator;

    while (stream >> token) {
        auto eqPos = token.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = token.substr(0, eqPos);
        std::string val = token.substr(eqPos + 1);

        if (!val.empty() && val[0] == '$') {
            // Unexpanded macro reference during script VALIDATION (dry run) —
            // real execution always resolves $macros before the plugin sees
            // the string; defer the actual value check to then.
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Deferring '"); LOG_STRING(key);
                      LOG_STRING("=" ); LOG_STRING(val);
                      LOG_STRING("' - value is a macro, resolved at execution time"));
            continue;
        }

        if (key == SK_HOST) setHost(val);
        else if (key == SK_PORT) { if (!setPort(val)) bRetVal = false; }
        else if (key == SK_TLS) {
            bool b = false;
            if (true == (bRetVal = beEvaluator.evaluate(val, b))) setTlsEnabled(b);
        }
        else if (key == SK_CA)   setTlsCaPath(val);
        else if (key == SK_CRT)  setTlsCertPath(val);
        else if (key == SK_KEY)  setTlsKeyPath(val);
        else if (key == SK_DESC) setDescriptorSetPath(val);
        else if (key == SK_AUTH) setAuthToken(val);
        else if (key == SK_CTOUT) { if (!setCallTimeout(val))    bRetVal = false; }
        else if (key == SK_XTOUT) { if (!setConnectTimeout(val)) bRetVal = false; }
        else if (key == SK_RTOUT) { if (!setReadTimeout(val))    bRetVal = false; }
        else if (key == SK_RBUF)  { if (!setReadBufferSize(val)) bRetVal = false; }
        else if (key == ucmdexec::RAW_RESULT_CONFIG_KEY) { if (!setRawResult(val)) bRetVal = false; }
        else if (key == ucmdexec::CYCLIC_CACHED_CONFIG_KEY) { if (!setCyclicCached(val)) bRetVal = false; }
    }
    return bRetVal;
}

#endif // GRPC_SETUP_HPP
