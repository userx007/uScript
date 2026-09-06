#ifndef MQTT_SETUP_HPP
#define MQTT_SETUP_HPP

#include "mqtt_plugin.hpp"
#include "PluginSetup.hpp"
#include "uCommandExec.hpp"
#include "uPluginSettings.hpp"

#include <sstream>

/////////////////////////////////////////////////////////////////////////////////
//                            LOG DEFINITIONS                                  //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR "MQTT_PLUGIN |"
#define LOG_HDR  LOG_STRING(LT_HDR)


/////////////////////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                               //
/////////////////////////////////////////////////////////////////////////////////

#define K_ARTEFACTS       "ARTEFACTS_PATH"
#define K_HOST            "HOST"
#define K_PORT            "PORT"
#define K_TLS_ENABLED     "TLS_ENABLED"
#define K_QOS             "QOS"
#define K_RETAIN          "RETAIN"
#define K_TLS_CA          "TLS_CA_CERT"
#define K_TLS_CLIENT_CERT "TLS_CLIENT_CERT"
#define K_TLS_CLIENT_KEY  "TLS_CLIENT_KEY"
#define K_READ_TIMEOUT    "READ_TIMEOUT"
#define K_READ_BUFSIZE    "READ_BUFFER_SIZE"
#define K_RECEIVE_TOPIC   "RECEIVE_TOPIC"
#define K_CLIENT_ID       "CLIENT_ID"
#define K_USERNAME        "USERNAME"
#define K_PASSWORD        "PASSWORD"
#define K_WILL_TOPIC      "WILL_TOPIC"
#define K_WILL_PAYLOAD    "WILL_PAYLOAD"
#define K_WILL_QOS        "WILL_QOS"
#define K_WILL_RETAIN     "WILL_RETAIN"
#define K_CLEAN_SESSION   "CLEAN_SESSION"


/////////////////////////////////////////////////////////////////////////////////
//                  CONFIGURATION INTERFACES                                   //
/////////////////////////////////////////////////////////////////////////////////

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief processing of the plugin specific settings.
  *
  * Pulls the plugin-specific keys out of the ini-backed PluginDataSet and feeds them through the
  * same setter surface the CONFIG command uses (generic_mqtt_set_params() above), so an ini file
  * and a runtime CONFIG command are always interpreted identically - same convention as
  * TCPIPPlugin::m_LocalSetParams() (see tcpip_plugin.cpp).
*/
/*--------------------------------------------------------------------------------------------------------*/
bool MqttPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    // Runtime instance identity for the GUI comm-dump panel (e.g. "MQTT:1"); falls back
    // to the fixed plugin name if the interpreter didn't supply one.
    m_strInstanceName = psSetParams->strInstanceName.empty() ? MQTT_PLUGIN_NAME : psSetParams->strInstanceName;

    if (psSetParams->mapSettings.empty()) return true;

    PluginSettingsBinder sSettings;
    sSettings.Bind(K_ARTEFACTS,       m_strArtefactsPath);
    sSettings.Bind(K_HOST,            m_strHost);
    sSettings.Bind(K_PORT,            [this](const std::string& v) { return setPort(v); });
    sSettings.Bind(K_QOS,             [this](const std::string& v) { return setQos(v); });
    sSettings.Bind(K_RETAIN,          [this](const std::string& v) { return setRetain(v); });
    sSettings.Bind(K_TLS_ENABLED,     [this](const std::string& v) { return setTlsEnabled(v); });
    sSettings.Bind(K_TLS_CA,          m_strTlsCaPath);
    sSettings.Bind(K_TLS_CLIENT_CERT, m_strTlsCertPath);
    sSettings.Bind(K_TLS_CLIENT_KEY,  m_strTlsKeyPath);
    sSettings.Bind(K_READ_TIMEOUT,    [this](const std::string& v) { return setReadTimeout(v); });
    sSettings.Bind(K_READ_BUFSIZE,    [this](const std::string& v) { return setReadBufferSize(v); });
    sSettings.Bind(K_RECEIVE_TOPIC,   [this](const std::string& v) { return setReceiveIncludeTopic(v); });
    sSettings.Bind(K_CLIENT_ID,       m_strClientId);
    sSettings.Bind(K_USERNAME,        m_strUsername);
    sSettings.Bind(K_PASSWORD,        m_strPassword);
    sSettings.Bind(K_WILL_TOPIC,      m_strWillTopic);
    sSettings.Bind(K_WILL_PAYLOAD,    m_strWillPayload);
    sSettings.Bind(K_WILL_QOS,        [this](const std::string& v) { return setWillQos(v); });
    sSettings.Bind(K_WILL_RETAIN,     [this](const std::string& v) { return setWillRetain(v); });
    sSettings.Bind(K_CLEAN_SESSION,   [this](const std::string& v) { return setCleanSession(v); });
    sSettings.Bind(ucmdexec::RAW_RESULT_INI_KEY,    m_bRawResult);
    sSettings.Bind(ucmdexec::CYCLIC_CACHED_INI_KEY, m_bCyclicCached);

    sSettings.Apply(psSetParams->mapSettings, nullptr, /*bStopOnFirstError=*/false);

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Config updated. Host:") LOG_STRING(m_strHost)
              LOG_STRING(" TLS:") LOG_BOOL(m_bUseTls));
    return true;
}


bool MqttPlugin::m_MQTT_CONFIG(const std::string& args, std::stop_token st) const
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
        else if (key == SK_QOS)  { if (!setQos(val))  bRetVal = false; }
        else if (key == SK_TLS) {
            bool b = false;
            if (true == (bRetVal = beEvaluator.evaluate(val, b))) setTlsEnabled(b);
        }
        else if (key == SK_RET) {
            bool b = false;
            if (true == (bRetVal = beEvaluator.evaluate(val, b))) setRetain(b);
        }
        else if (key == SK_CA)  setTlsCaPath(val);
        else if (key == SK_CRT) setTlsCertPath(val);
        else if (key == SK_KEY) setTlsKeyPath(val);
        else if (key == SK_RTOUT) { if (!setReadTimeout(val)) bRetVal = false; }
        else if (key == SK_RBUF)  { if (!setReadBufferSize(val)) bRetVal = false; }
        else if (key == SK_RTOPIC) {
            bool b = false;
            if (true == (bRetVal = beEvaluator.evaluate(val, b))) setReceiveIncludeTopic(b);
        }
        else if (key == SK_CID)  setClientId(val);
        else if (key == SK_USER) setUsername(val);
        else if (key == SK_PASS) setPassword(val);
        else if (key == SK_WTOPIC) setWillTopic(val);
        else if (key == SK_WPAY)   setWillPayload(val);
        else if (key == SK_WQOS)   { if (!setWillQos(val)) bRetVal = false; }
        else if (key == SK_WRET) {
            bool b = false;
            if (true == (bRetVal = beEvaluator.evaluate(val, b))) setWillRetain(b);
        }
        else if (key == SK_CLEAN) {
            bool b = false;
            if (true == (bRetVal = beEvaluator.evaluate(val, b))) setCleanSession(b);
        }
        else if (key == ucmdexec::RAW_RESULT_CONFIG_KEY) { if (!setRawResult(val)) bRetVal = false; }
        else if (key == ucmdexec::CYCLIC_CACHED_CONFIG_KEY) { if (!setCyclicCached(val)) bRetVal = false; }
    }
    return bRetVal;
}


#endif // MQTT_SETUP_HPP
