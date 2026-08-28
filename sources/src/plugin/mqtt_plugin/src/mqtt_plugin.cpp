#include "mqtt_plugin.hpp"
#include "private/mqtt_setup.hpp"
#include "uCommandExec.hpp"

#include <sstream>
#include <chrono>

/////////////////////////////////////////////////////////////////////////////////
//                  GLOBAL DEFINITIONS                                         //
/////////////////////////////////////////////////////////////////////////////////

static constexpr uint16_t kKeepAliveSeconds = 60;

/////////////////////////////////////////////////////////////////////////////////
//                  PLUGIN ENTRY POINTS                                        //
/////////////////////////////////////////////////////////////////////////////////

extern "C"
{
    EXPORTED MqttPlugin* pluginEntry()
    {
        return new MqttPlugin();
    }

    EXPORTED void pluginExit(MqttPlugin *ptrPlugin)
    {
        if(nullptr != ptrPlugin)
        {
            delete ptrPlugin;
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////
// Driver factory
/////////////////////////////////////////////////////////////////////////////////

std::shared_ptr<MqttDriver> MqttPlugin::m_OpenDriver(void) const
{
    if (m_pDriver && m_pDriver->is_open()) {
        return m_pDriver;
    }

    if (m_strHost.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Host not configured — MQTT.CONFIG h=<host> first"));
        return nullptr;
    }

    MqttDriver::Config cfg;
    cfg.host             = m_strHost;
    cfg.port             = m_u16Port;
    cfg.connectTimeoutMs = 5000;
    cfg.useTls           = m_bUseTls;
    cfg.caCertPath       = m_strTlsCaPath;
    cfg.clientCertPath   = m_strTlsCertPath;
    cfg.clientKeyPath    = m_strTlsKeyPath;
    cfg.clientId = m_strClientId.empty()
        ? ("mqtt_plugin_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))
        : m_strClientId;
    cfg.username           = m_strUsername;
    cfg.password           = m_strPassword;
    cfg.willTopic          = m_strWillTopic;
    cfg.willPayload        = m_strWillPayload;
    cfg.willQos            = m_u8WillQos;
    cfg.willRetain         = m_bWillRetain;
    cfg.cleanSession       = m_bCleanSession;
    cfg.keepAlive          = kKeepAliveSeconds;
    cfg.qos                = m_u8Qos;
    cfg.retain              = m_bRetain;
    cfg.receiveIncludeTopic = m_bReceiveIncludeTopic;
    cfg.strInstanceName     = m_strInstanceName;

    auto driver = std::make_shared<MqttDriver>(cfg);
    if (!driver->open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("MqttDriver open failed"));
        return nullptr;
    }

    m_pDriver = driver;
    return m_pDriver;
}

/////////////////////////////////////////////////////////////////////////////////
//                 PLUGIN TOP LEVEL COMMANDS                                   //
/////////////////////////////////////////////////////////////////////////////////

bool MqttPlugin::m_MQTT_INFO(const std::string& args, std::stop_token st) const
{
    (void)args; (void)st;
    resetData();
    std::ostringstream oss;
    oss << MQTT_PLUGIN_NAME " v" << m_strVersion
        << " host=" << m_strHost
        << " port=" << m_u16Port
        << " tls=" << (m_bUseTls ? "true" : "false")
        << " qos=" << (int)m_u8Qos
        << " cleanSession=" << (m_bCleanSession ? "true" : "false")
        << " auth=" << (m_strUsername.empty() ? "none" : "username/password")
        << " will=" << (m_strWillTopic.empty() ? "none" : m_strWillTopic);
    m_strResultData = oss.str();

    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING(MQTT_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: publish/subscribe against an MQTT v3.1.1 broker (e.g. Mosquitto)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Architecture: MqttProtocol (protocol) / TCPIP (real driver, undecorated) / MqttDriver (protocol+driver glue, ICommDriver) / this plugin (CONFIG + wiring only)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the broker host, port, TLS, auth, Will and transfer parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [h=host] [p=port] [q=qos] [t=tls] [r=retain] [ca=capath] [crt=certpath] [key=keypath]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [rt=read_tout] [rb=read_bufsize] [id=clientid] [u=username] [pw=password] [cs=cleansession]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [wt=will_topic] [wp=will_payload] [wq=will_qos] [wr=will_retain] [it=include_topic]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : MQTT.CONFIG h=broker.local p=1883 q=1"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : one MQTT operation, on the plugin's single persistent session (opened on first use)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : > <SUBSCRIBE|UNSUBSCRIBE|PING|PUBLISH> ... [| expected]   |   <"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : MQTT.CMD > SUBSCRIBE sensors/temp 1"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         MQTT.CMD > UNSUBSCRIBE sensors/temp"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         MQTT.CMD > PING"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         MQTT.CMD > PUBLISH OPEN actuators/valve3/cmd | PUBACK"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         MQTT.CMD <                 // one blocking receive; requires an active SUBSCRIBE"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         reading ?= MQTT.CMD < &    // background thread; $reading tracks the latest message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : PUBLISH's QoS/retain come from CONFIG (q=/r=), not from the CMD line."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         PUBLISH's payload may contain spaces (the topic is always the LAST token); no quoting is available here."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         Always pair a QoS>0 PUBLISH/SUBSCRIBE/UNSUBSCRIBE/PING with its '| expected' — an omitted ack is read (and mismatched) by the next MQTT.CMD <."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         The GUI comm-dump panel shows the real bytes exchanged with the broker (one row per complete MQTT packet)."));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : run several MQTT.CMD-style lines from a file over the same session"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : scriptpathname [|delay]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : MQTT.SCRIPT script.txt"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC : periodic publish/ping, same driver session as CMD"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : \"time1:val1, time2:val2, ...\" — each val is a full MQTT.CMD-style '> ...' argument"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : MQTT.CYCLIC 1000:> PUBLISH sensors/temp 21.5"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         MQTT.CYCLIC 1000:> PUBLISH sensors/temp 21.5, 5000:> PING &   // repeats until stopped"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : a val containing a literal ',' (e.g. a multi-field payload) must stay wrapped in"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         '...' — CYCLIC's entry-list split is quote-aware around '...', same as SCRIPT/CMD lines."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         PUBLISH's QoS/retain still come from CONFIG (q=/r=), not from the CYCLIC entry."));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("INI file parameters (copy/paste into your ini file):"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("[MQTT]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("ARTEFACTS_PATH   =            # directory used by SCRIPT/CMD/wrrdf for reading/writing artefact files"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("HOST             = 127.0.0.1  # MQTT broker host to connect to"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("PORT             = 1883       # MQTT broker port to connect to"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TLS_ENABLED      = false      # use TLS for the connection when true"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("QOS              = 0          # default publish/subscribe QoS level (0, 1 or 2)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RETAIN           = false      # set the MQTT retain flag on published messages by default"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TLS_CA_CERT      =            # path to the CA certificate used to verify the broker (TLS)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TLS_CLIENT_CERT  =            # path to the client certificate (mutual TLS)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TLS_CLIENT_KEY   =            # path to the client private key (mutual TLS)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_TIMEOUT     = 2000       # read timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_BUFFER_SIZE = 1024       # size in bytes of the local read buffer"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RECEIVE_TOPIC    = false      # prefix received payloads with their topic when true"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CLIENT_ID        =            # MQTT client identifier (empty = auto-generated)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("USERNAME         =            # username for broker authentication"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("PASSWORD         =            # password for broker authentication"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("WILL_TOPIC       =            # Last Will and Testament topic (empty = no LWT)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("WILL_PAYLOAD     =            # Last Will and Testament payload"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("WILL_QOS         = 0          # Last Will and Testament QoS level"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("WILL_RETAIN      = false      # set the retain flag on the Last Will and Testament message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CLEAN_SESSION    = true       # request a clean session on connect"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RAW_RESULT       = false      # CMD returns raw bytes instead of a hexlified string when true"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC_CACHED    = true       # true=validate/parse each CYCLIC entry once per session; false=re-resolve every tick (needed for volatile ?= macros)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note: the CONFIG command above can override a subset of these at runtime;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("      any key not accepted by CONFIG must be set via the ini file."));


    return true;
}

// -----------------------------------------------------------------------
// MQTT.CONFIG command: apply host/port/TLS/session settings at runtime, through the same
//      setters used by the ini-file loader in m_LocalSetParams(), see generic_mqtt_set_params()
// -----------------------------------------------------------------------

bool MqttPlugin::m_MQTT_CONFIG(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    return generic_mqtt_set_params(this, args);

} /* m_MQTT_CONFIG() */

// -----------------------------------------------------------------------
// MQTT.CMD — see class doc comment (mqtt_plugin.hpp)
// -----------------------------------------------------------------------

bool MqttPlugin::m_MQTT_CMD(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    return ucmdexec::generic_cmd(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<MqttDriver> { return m_OpenDriver(); },
        m_strInstanceName,
        m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, &m_strResultData, m_bRawResult,
        // Non-capturing: MqttDriver::send()/receive() are handed everything
        // they need through the driver parameter itself — see
        // mqtt_driver.hpp's class doc comment.
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const MqttDriver> drv, std::string_view x) {
            return drv->send(t, d, x);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const MqttDriver> drv, std::string_view x) {
            return drv->receive(t, b, o, x);
        });
}

// -----------------------------------------------------------------------
// MQTT.SCRIPT — see class doc comment (mqtt_plugin.hpp)
// -----------------------------------------------------------------------

bool MqttPlugin::m_MQTT_SCRIPT(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    return ucmdexec::generic_script(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<MqttDriver> { return m_OpenDriver(); },
        m_strInstanceName,
        m_strArtefactsPath, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR,
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const MqttDriver> drv, std::string_view x) {
            return drv->send(t, d, x);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const MqttDriver> drv, std::string_view x) {
            return drv->receive(t, b, o, x);
        });
}

// -----------------------------------------------------------------------
// MQTT.CYCLIC — see class doc comment (mqtt_plugin.hpp)
// -----------------------------------------------------------------------

bool MqttPlugin::m_MQTT_CYCLIC(const std::string& args, std::stop_token st) const
{
    resetData();

    return ucmdexec::generic_send_cyclic(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<MqttDriver> { return m_OpenDriver(); },
        m_strInstanceName, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, st, m_bCyclicCached,
        // Non-capturing: MqttDriver::send()/receive() are handed everything
        // they need through the driver parameter itself — see
        // mqtt_driver.hpp's class doc comment.
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const MqttDriver> drv, std::string_view x) {
            return drv->send(t, d, x);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const MqttDriver> drv, std::string_view x) {
            return drv->receive(t, b, o, x);
        });
}
