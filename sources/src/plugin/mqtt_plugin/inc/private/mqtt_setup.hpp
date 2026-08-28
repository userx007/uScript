#ifndef MQTT_SETUP_HPP
#define MQTT_SETUP_HPP

#include "mqtt_plugin.hpp"
#include "PluginSetup.hpp"
#include "uCommandExec.hpp"
#include "uPluginSettings.hpp"

#include <sstream>

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR "MQTT_PLUGIN |"
#define LOG_HDR  LOG_STRING(LT_HDR)


///////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                 //
///////////////////////////////////////////////////////////////////

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

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of MQTT parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (h=host  p=port  t=tls_enabled  q=qos  r=retain  ca=tls_ca_cert
 *                     crt=tls_client_cert  key=tls_client_key  rt=read_tout  rb=recv_bufsize
 *                     it=receive_include_topic  id=client_id  u=username  pw=password
 *                     wt=will_topic  wp=will_payload  wq=will_qos  wr=will_retain  cs=clean_session)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_mqtt_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "h",      .voidSetter = &T::setHost                 },
        { .key = "p",      .boolSetter = &T::setPort                 },
        { .key = "t",      .boolSetter = &T::setTlsEnabled           },
        { .key = "q",      .boolSetter = &T::setQos                  },
        { .key = "r",      .boolSetter = &T::setRetain               },
        { .key = "ca",     .voidSetter = &T::setTlsCaPath            },
        { .key = "crt",    .voidSetter = &T::setTlsCertPath          },
        { .key = "key",    .voidSetter = &T::setTlsKeyPath           },
        { .key = "rt",     .boolSetter = &T::setReadTimeout          },
        { .key = "rb",     .boolSetter = &T::setReadBufferSize       },
        { .key = "it",     .boolSetter = &T::setReceiveIncludeTopic  },
        { .key = "id",     .voidSetter = &T::setClientId             },
        { .key = "u",      .voidSetter = &T::setUsername             },
        { .key = "pw",     .voidSetter = &T::setPassword             },
        { .key = "wt",     .voidSetter = &T::setWillTopic            },
        { .key = "wp",     .voidSetter = &T::setWillPayload          },
        { .key = "wq",     .boolSetter = &T::setWillQos              },
        { .key = "wr",     .boolSetter = &T::setWillRetain           },
        { .key = "cs",     .boolSetter = &T::setCleanSession         },
        { .key = "raw",    .boolSetter = &T::setRawResult            },
        { .key = "cached", .boolSetter = &T::setCyclicCached         },
    };

    return generic_setup_params(pOwner, args, table, LT_HDR);
}

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

} /* m_LocalSetParams() */

bool MqttPlugin::setPort(const std::string& portStr) const
{
    uint32_t port = 0;
    if (!numeric::str2uint32(portStr, port)) return false;
    if (port > 65535) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid port:"); LOG_UINT32(port));
        return false;
    }
    m_u16Port = static_cast<uint16_t>(port);
    return true;
}

bool MqttPlugin::setQos(const std::string& qosStr) const
{
    return numeric::str2uint8(qosStr, m_u8Qos);
}

bool MqttPlugin::setReadTimeout(const std::string& timeoutStr) const
{
    return numeric::str2uint32(timeoutStr, m_u32ReadTimeout);
}

bool MqttPlugin::setReadBufferSize(const std::string& bufSizeStr) const
{
    uint32_t sz = 0;
    if (!numeric::str2uint32(bufSizeStr, sz)) return false;
    if (sz == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid read buffer size:"); LOG_UINT32(sz));
        return false;
    }
    m_u32ReadBufferSize = sz;
    return true;
}

bool MqttPlugin::setWillQos(const std::string& qosStr) const
{
    return numeric::str2uint8(qosStr, m_u8WillQos);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CONFIG command: apply host/port/TLS/session settings at runtime, through the same
  *        setters used by the ini-file loader in m_LocalSetParams() (see generic_mqtt_set_params()
  *        above).
*/
/*--------------------------------------------------------------------------------------------------------*/
bool MqttPlugin::m_MQTT_CONFIG(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    return generic_mqtt_set_params(this, args);

} /* m_MQTT_CONFIG() */

#endif // MQTT_SETUP_HPP
