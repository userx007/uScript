#ifndef MQTT_PLUGIN_HPP
#define MQTT_PLUGIN_HPP

#include "uSharedConfig.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "ICommDriver.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "uLogger.hpp"
#include "uNumeric.hpp"
#include "uString.hpp"
#include "uFile.hpp"

#include <string>
#include <memory>
#include <unordered_map>

// Include the actual driver definition
#include "mqtt_driver.hpp"

#define MQTT_PLUGIN_VERSION   "1.0.0.0"
#define MQTT_PLUGIN_NAME      "MQTT"

// Command Macros
#ifndef MQTT_GET_BLOCKING
#define MQTT_GET_BLOCKING(name, blocking, ...) blocking
#endif

#define MQTT_PLUGIN_COMMANDS_CONFIG_TABLE \
    MQTT_PLUGIN_CMD_RECORD(INFO)          \
    MQTT_PLUGIN_CMD_RECORD(CONFIG)        \
    MQTT_PLUGIN_CMD_RECORD(CMD)           \
    MQTT_PLUGIN_CMD_RECORD(SCRIPT)        \
    MQTT_PLUGIN_CMD_RECORD(SUBSCRIBE)     \
    MQTT_PLUGIN_CMD_RECORD(RECEIVE)

class MqttPlugin : public PluginInterface
{
public:
    MqttPlugin()
        : m_strVersion(MQTT_PLUGIN_VERSION)
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
        , m_u32ReadBufferSize(256)
        , m_strClientId("mqtt_pub_plugin_")
        , m_bShareSession(false)
        , m_bReceiveIncludeTopic(false)
    {
        #define MQTT_PLUGIN_CMD_RECORD(a, ...) m_mapCmds.insert( std::make_pair( #a, \
            PluginCommandEntry<MqttPlugin>{&MqttPlugin::m_MQTT_##a, MQTT_GET_BLOCKING(a, ##__VA_ARGS__, false)} ));
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
    void setHost(const std::string& host) const;
    uint16_t getPort(void) const { return m_u16Port; }
    bool setPort(const std::string& portStr) const;
    bool isTlsEnabled(void) const { return m_bUseTls; }
    void setTlsEnabled(bool val) const;
    uint8_t getQos(void) const { return m_u16Qos; }
    bool setQos(const std::string& qosStr) const;
    bool getRetain(void) const { return m_bRetain; }
    void setRetain(bool val) const;
    const std::string& getTlsCertPath(void) const { return m_strTlsCertPath; }
    void setTlsCertPath(const std::string& path) const;
    const std::string& getTlsKeyPath(void) const { return m_strTlsKeyPath; }
    void setTlsKeyPath(const std::string& path) const;
    const std::string& getTlsCaPath(void) const { return m_strTlsCaPath; }
    void setTlsCaPath(const std::string& path) const;
    uint32_t getReadTimeout(void) const { return m_u32ReadTimeout; }
    bool setReadTimeout(const std::string& timeoutStr) const;
    uint32_t getReadBufferSize(void) const { return m_u32ReadBufferSize; }
    bool setReadBufferSize(const std::string& bufSizeStr) const;

    // See m_OpenDriver()/m_GetOrOpenPersistentDriver() (mqtt_plugin.cpp) for
    // what this actually changes: whether MQTT.CMD/SCRIPT's publish reuses
    // the same persistent connection MQTT.SUBSCRIBE/MQTT.RECEIVE keep open,
    // or (default) always opens its own fresh one.
    bool getShareSession(void) const { return m_bShareSession; }
    void setShareSession(bool val) const { m_bShareSession = val; }

    // Whether MQTT.RECEIVE stores "topic:payload" or just "payload" into
    // its destination macro — see m_MQTT_RECEIVE() (mqtt_plugin.cpp).
    bool getReceiveIncludeTopic(void) const { return m_bReceiveIncludeTopic; }
    void setReceiveIncludeTopic(bool val) const { m_bReceiveIncludeTopic = val; }

private:

    // Helpers
    std::shared_ptr<MqttDriver> m_OpenDriver(void) const;
    std::shared_ptr<MqttDriver> m_OpenFreshDriver(void) const;
    std::shared_ptr<MqttDriver> m_GetOrOpenPersistentDriver(void) const;
    bool m_LocalSetParams(const PluginDataSet *psSetParams);

    // Members
    PluginCommandsMap<MqttPlugin> m_mapCmds;
    std::string m_strVersion;
    mutable std::string m_strResultData;
    bool m_bIsInitialized;
    bool m_bIsEnabled;
    bool m_bIsFaultTolerant;
    bool m_bIsPrivileged;

    std::string m_strArtefactsPath;

    // MQTT Specific Config
    mutable std::string m_strHost;
    mutable uint16_t m_u16Port;
    mutable bool m_bUseTls;
    mutable uint8_t m_u16Qos; // 0, 1, or 2
    mutable bool m_bRetain;

    // Raw read timeout (ms) / receive buffer size (bytes) used by MQTT.CMD
    // and MQTT.SCRIPT (CommScriptCommandInterpreter<MqttDriver> /
    // CommScriptClient<MqttDriver>), same role as TCPIPPlugin's
    // m_u32ReadTimeout / m_u32TcpReadBufferSize.
    mutable uint32_t m_u32ReadTimeout;
    mutable uint32_t m_u32ReadBufferSize;

    // TLS Paths - marked mutable
    mutable std::string m_strTlsCaPath;
    mutable std::string m_strTlsCertPath;
    mutable std::string m_strTlsKeyPath;

    // Client ID
    std::string m_strClientId;

    // Config flags, both selectable via INI/CONFIG rather than hardcoded —
    // see getShareSession()/getReceiveIncludeTopic() above for what each
    // one changes.
    mutable bool m_bShareSession;
    mutable bool m_bReceiveIncludeTopic;

    // MQTT.SUBSCRIBE/MQTT.RECEIVE's persistent connection — opened by the
    // first MQTT.SUBSCRIBE call and kept alive across subsequent
    // MQTT.SUBSCRIBE/MQTT.RECEIVE calls (see m_GetOrOpenPersistentDriver()),
    // including ones made from a background thread via
    // "name ?= MQTT.RECEIVE &" (see src/script/core/README.md's "Threaded
    // variable macros" section). When m_bShareSession is true,
    // MQTT.CMD/SCRIPT's publish reuses this same connection instead of
    // opening its own fresh one — see m_OpenDriver().
    mutable std::shared_ptr<MqttDriver> m_pPersistentDriver;

    /**
      * \brief functions associated to the plugin commands
    */
    #define MQTT_PLUGIN_CMD_RECORD(a, ...)  bool m_MQTT_##a ( const std::string& args, std::stop_token st ) const;
    MQTT_PLUGIN_COMMANDS_CONFIG_TABLE
    #undef  MQTT_PLUGIN_CMD_RECORD
};

#endif // MQTT_PLUGIN_HPP
