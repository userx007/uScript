#ifndef MQTT_PLUGIN_HPP
#define MQTT_PLUGIN_HPP

#include "uSharedConfig.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "uLogger.hpp"
#include "uNumeric.hpp"
#include "uString.hpp"
#include "uFile.hpp"

#include <string>
#include <memory>

#include "mqtt_driver.hpp"

#define MQTT_PLUGIN_VERSION   "5.0.0.0"
#define MQTT_PLUGIN_NAME      "MQTT"

#define MQTT_PLUGIN_COMMANDS_CONFIG_TABLE \
    MQTT_PLUGIN_CMD_RECORD(INFO)          \
    MQTT_PLUGIN_CMD_RECORD(CONFIG)        \
    MQTT_PLUGIN_CMD_RECORD(CMD)           \
    MQTT_PLUGIN_CMD_RECORD(SCRIPT)

/**
 * @brief MQTT plugin — thin shell over `MqttDriver` (mqtt_driver.hpp),
 * which holds every MQTT-specific implementation detail (protocol framing,
 * TLS, the CONNECT/CONNACK session handshake, the MQTT.CMD intermediary
 * command parsing, and the GUI comm-dump reporting). This class's only
 * jobs are:
 *
 *   - **CONFIG storage** — the getters/setters below, and `m_MQTT_CONFIG()`/
 *     `.ini` binding (`m_LocalSetParams()`) that fill them in.
 *   - **INFO** — a human-readable summary, no protocol involvement.
 *   - **Wiring** — `m_OpenDriver()` builds an `MqttDriver::Config` from the
 *     stored settings, constructs (or reuses) the one persistent
 *     `MqttDriver` for this plugin instance, and `m_MQTT_CMD()`/
 *     `m_MQTT_SCRIPT()` hand that driver to `ucmdexec::generic_cmd()`/
 *     `generic_script()` — the same shared mechanism every other comm-
 *     driver plugin (UART, TCPIP, KVCAN, ...) uses — supplying
 *     `MqttDriver::send()`/`receive()` directly as the `pfsend`/`pfrecv`
 *     override (see mqtt_driver.hpp's class doc comment for why that's
 *     needed for accurate GUI comm-dump reporting). Note the lambdas below
 *     capture nothing from this plugin at all — everything they need comes
 *     through the driver parameter already.
 *
 * -------------------------------------------------------------------------
 * Session lifetime
 * -------------------------------------------------------------------------
 * Unlike a typical UART/TCPIP CMD (fresh connection per call), every
 * MQTT.CMD/MQTT.SCRIPT call shares one persistent `MqttDriver` (and the
 * TCPIP connection + MQTT session it owns) per plugin instance — opened by
 * `m_OpenDriver()` the first time it's needed, and kept alive for as long
 * as the plugin is loaded (closed by doCleanup()). This is what makes
 * `MQTT.CMD <` meaningful: it waits on whatever `MQTT.CMD > SUBSCRIBE ...`
 * calls happened earlier on that same session, including from a background
 * thread (`MQTT.CMD < &`).
 */
class MqttPlugin : public PluginInterface
{
public:
    MqttPlugin()
        : m_strVersion(MQTT_PLUGIN_VERSION)
        , m_strInstanceName(MQTT_PLUGIN_NAME)
        , m_bIsInitialized(false)
        , m_bIsEnabled(false)
        , m_bIsFaultTolerant(false)
        , m_bIsPrivileged(false)
        , m_strResultData()
        , m_strHost("localhost")
        , m_u16Port(1883)
        , m_bUseTls(false)
        , m_u16Qos(0)
        , m_bRetain(false)
        , m_u32ReadTimeout(5000)
        , m_u32ReadBufferSize(4096)
        , m_strClientId()
        , m_bReceiveIncludeTopic(false)
        , m_u8WillQos(0)
        , m_bWillRetain(false)
        , m_bCleanSession(true)
    {
        #define MQTT_PLUGIN_CMD_RECORD(a) m_mapCmds.insert( std::make_pair( #a, \
            PluginCommandEntry<MqttPlugin>{&MqttPlugin::m_MQTT_##a, false} ));
        MQTT_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  MQTT_PLUGIN_CMD_RECORD
    }

    ~MqttPlugin() = default;

    bool isInitialized(void) const { return m_bIsInitialized; }
    bool isEnabled(void) const { return m_bIsEnabled; }

    bool setParams(const PluginDataSet *psSetParams);
    void getParams(PluginDataGet *psGetParams) const;
    bool doDispatch(const std::string& strCmd, const std::string& strParams, std::stop_token st = {}) const;
    const PluginCommandsMap<MqttPlugin>* getMap(void) const { return &m_mapCmds; }
    const std::string& getVersion(void) const { return m_strVersion; }
    const std::string& getData(void) const { return m_strResultData; }
    void resetData(void) const { m_strResultData.clear(); }
    bool doInit(void *pvUserData);
    bool doEnable(void) { m_bIsEnabled = true; return true; }
    void doCleanup(void);
    bool isFaultTolerant(void) const { return m_bIsFaultTolerant; }
    bool isPrivileged(void) const { return m_bIsPrivileged; }

    // Getters/Setters
    const std::string& getHost(void) const { return m_strHost; }
    void setHost(const std::string& host) const { m_strHost = host; }
    uint16_t getPort(void) const { return m_u16Port; }
    bool setPort(const std::string& portStr) const;
    bool isTlsEnabled(void) const { return m_bUseTls; }
    void setTlsEnabled(bool val) const { m_bUseTls = val; }
    uint8_t getQos(void) const { return m_u16Qos; }
    bool setQos(const std::string& qosStr) const;
    bool getRetain(void) const { return m_bRetain; }
    void setRetain(bool val) const { m_bRetain = val; }
    const std::string& getTlsCertPath(void) const { return m_strTlsCertPath; }
    void setTlsCertPath(const std::string& path) const { m_strTlsCertPath = path; }
    const std::string& getTlsKeyPath(void) const { return m_strTlsKeyPath; }
    void setTlsKeyPath(const std::string& path) const { m_strTlsKeyPath = path; }
    const std::string& getTlsCaPath(void) const { return m_strTlsCaPath; }
    void setTlsCaPath(const std::string& path) const { m_strTlsCaPath = path; }
    uint32_t getReadTimeout(void) const { return m_u32ReadTimeout; }
    bool setReadTimeout(const std::string& timeoutStr) const;
    uint32_t getReadBufferSize(void) const { return m_u32ReadBufferSize; }
    bool setReadBufferSize(const std::string& bufSizeStr) const;

    bool getReceiveIncludeTopic(void) const { return m_bReceiveIncludeTopic; }
    void setReceiveIncludeTopic(bool val) const { m_bReceiveIncludeTopic = val; }

    const std::string& getUsername(void) const { return m_strUsername; }
    void setUsername(const std::string& val) const { m_strUsername = val; }
    const std::string& getPassword(void) const { return m_strPassword; }
    void setPassword(const std::string& val) const { m_strPassword = val; }

    const std::string& getWillTopic(void) const { return m_strWillTopic; }
    void setWillTopic(const std::string& val) const { m_strWillTopic = val; }
    const std::string& getWillPayload(void) const { return m_strWillPayload; }
    void setWillPayload(const std::string& val) const { m_strWillPayload = val; }
    uint8_t getWillQos(void) const { return m_u8WillQos; }
    bool setWillQos(const std::string& qosStr) const;
    bool getWillRetain(void) const { return m_bWillRetain; }
    void setWillRetain(bool val) const { m_bWillRetain = val; }

    bool getCleanSession(void) const { return m_bCleanSession; }
    void setCleanSession(bool val) const { m_bCleanSession = val; }

    const std::string& getClientId(void) const { return m_strClientId; }
    void setClientId(const std::string& val) const { m_strClientId = val; }

private:

    // Factory used by both m_MQTT_CMD() and m_MQTT_SCRIPT() (passed as
    // ucmdexec::generic_cmd/generic_script's openFn): builds an
    // MqttDriver::Config from the stored settings and returns the one
    // persistent MqttDriver for this plugin instance, constructing (and
    // open()-ing) it on first use. Reused as-is on every later call as
    // long as it's still open; see class doc comment's "Session lifetime".
    std::shared_ptr<MqttDriver> m_OpenDriver(void) const;

    bool m_LocalSetParams(const PluginDataSet *psSetParams);

    // Members
    PluginCommandsMap<MqttPlugin> m_mapCmds;
    std::string m_strVersion;

    // Runtime instance identity for the GUI comm-dump panel (e.g. "MQTT"
    // or "MQTT:1" -- see PluginDataSet::strInstanceName). Falls back to
    // MQTT_PLUGIN_NAME when unset.
    std::string m_strInstanceName;
    mutable std::string m_strResultData;
    bool m_bIsInitialized;
    bool m_bIsEnabled;
    bool m_bIsFaultTolerant;
    bool m_bIsPrivileged;

    std::string m_strArtefactsPath;

    mutable std::string m_strHost;
    mutable uint16_t m_u16Port;
    mutable bool m_bUseTls;
    mutable uint8_t m_u16Qos;
    mutable bool m_bRetain;

    mutable uint32_t m_u32ReadTimeout;
    mutable uint32_t m_u32ReadBufferSize;

    mutable std::string m_strTlsCaPath;
    mutable std::string m_strTlsCertPath;
    mutable std::string m_strTlsKeyPath;

    mutable std::string m_strClientId;
    mutable bool m_bReceiveIncludeTopic;

    mutable std::string m_strUsername;
    mutable std::string m_strPassword;

    mutable std::string m_strWillTopic;
    mutable std::string m_strWillPayload;
    mutable uint8_t m_u8WillQos;
    mutable bool m_bWillRetain;

    mutable bool m_bCleanSession;

    // The persistent driver — see class doc comment's "Session lifetime"
    // and m_OpenDriver().
    mutable std::shared_ptr<MqttDriver> m_pDriver;

    /**
      * \brief functions associated to the plugin commands
    */
    #define MQTT_PLUGIN_CMD_RECORD(a)  bool m_MQTT_##a ( const std::string& args, std::stop_token st ) const;
    MQTT_PLUGIN_COMMANDS_CONFIG_TABLE
    #undef  MQTT_PLUGIN_CMD_RECORD
};

#endif // MQTT_PLUGIN_HPP
