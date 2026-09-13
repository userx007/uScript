#ifndef MQTT_SETUP_HPP
#define MQTT_SETUP_HPP
#include "mqtt_plugin.hpp"
#include "PluginSetup.hpp"
#include "uCommandExec.hpp"
#include "uPluginSettings.hpp"

#include <string>

/////////////////////////////////////////////////////////////////////////////////
//                            LOG DEFINITIONS                                  //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR "MQTT_P      |"
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
#define K_KEEPALIVE_TOUT  "KEEP_ALIVE_TOUT"

/////////////////////////////////////////////////////////////////////////////////
//                  CONFIG COMMAND SHORT KEYS                                  //
/////////////////////////////////////////////////////////////////////////////////
// Short tokens accepted by the CONFIG command (m_MQTT_CONFIG() below) — see
// the usage string in m_MQTT_INFO() and docs/mqtt_plugin_tutorial.md
// section 5 for the documented key table these must match.

#define SK_HOST         "h"
#define SK_PORT         "p"
#define SK_QOS          "q"
#define SK_TLS          "t"
#define SK_RET          "r"
#define SK_CA           "ca"
#define SK_CRT          "crt"
#define SK_KEY          "key"
#define SK_RTOUT        "rt"
#define SK_RBUF         "rb"
#define SK_RTOPIC       "it"
#define SK_CID          "id"
#define SK_USER         "u"
#define SK_PASS         "pw"
#define SK_WTOPIC       "wt"
#define SK_WPAY         "wp"
#define SK_WQOS         "wq"
#define SK_WRET         "wr"
#define SK_CLEAN        "cs"
#define SK_KAT          "kat"


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
    sSettings.Bind(K_KEEPALIVE_TOUT,  [this](const std::string& v) { return setKeepAliveSeconds(v); });
    sSettings.Bind(ucmdexec::RAW_RESULT_INI_KEY,    m_bRawResult);
    sSettings.Bind(ucmdexec::CYCLIC_CACHED_INI_KEY, m_bCyclicCached);

    sSettings.Apply(psSetParams->mapSettings, nullptr, /*bStopOnFirstError=*/false);

    LOG_PRINT(LOG_WERBOSE, LOG_HDR; LOG_STRING("Config updated. Host:") LOG_STRING(m_strHost)
              LOG_STRING(" TLS:") LOG_BOOL(m_bUseTls));
    return true;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of MQTT parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs, see the SK_* keys above for the recognised set
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_mqtt_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = SK_HOST,   .voidSetter = &T::setHost                },
        { .key = SK_PORT,   .boolSetter = &T::setPort                },
        { .key = SK_QOS,    .boolSetter = &T::setQos                 },
        { .key = SK_TLS,    .boolSetter = &T::setTlsEnabled          },
        { .key = SK_RET,    .boolSetter = &T::setRetain              },
        { .key = SK_CA,     .voidSetter = &T::setTlsCaPath           },
        { .key = SK_CRT,    .voidSetter = &T::setTlsCertPath         },
        { .key = SK_KEY,    .voidSetter = &T::setTlsKeyPath          },
        { .key = SK_RTOUT,  .boolSetter = &T::setReadTimeout         },
        { .key = SK_RBUF,   .boolSetter = &T::setReadBufferSize      },
        { .key = SK_RTOPIC, .boolSetter = &T::setReceiveIncludeTopic },
        { .key = SK_CID,    .voidSetter = &T::setClientId            },
        { .key = SK_USER,   .voidSetter = &T::setUsername            },
        { .key = SK_PASS,   .voidSetter = &T::setPassword            },
        { .key = SK_WTOPIC, .voidSetter = &T::setWillTopic           },
        { .key = SK_WPAY,   .voidSetter = &T::setWillPayload         },
        { .key = SK_WQOS,   .boolSetter = &T::setWillQos             },
        { .key = SK_WRET,   .boolSetter = &T::setWillRetain          },
        { .key = SK_CLEAN,  .boolSetter = &T::setCleanSession        },
        { .key = SK_KAT,    .boolSetter = &T::setKeepAliveSeconds    },        
        { .key = "raw",     .boolSetter = &T::setRawResult           },
        { .key = "cached",  .boolSetter = &T::setCyclicCached        },
    };

    return generic_setup_params(pOwner, args, table, LT_HDR);
}

#endif // MQTT_SETUP_HPP
