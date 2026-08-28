#ifndef GRPC_PLUGIN_HPP
#define GRPC_PLUGIN_HPP

#include "uSharedConfig.hpp"
#include "uCommandExec.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "uLogger.hpp"
#include "uNumeric.hpp"
#include "uString.hpp"
#include "uFile.hpp"
#include "uBoolEvaluator.hpp"

#include <string>
#include <memory>

#include "grpc_driver.hpp"

#define GRPC_PLUGIN_VERSION   "1.0.0.0"
#define GRPC_PLUGIN_NAME      "GRPC"

#define GRPC_PLUGIN_COMMANDS_CONFIG_TABLE \
    GRPC_PLUGIN_CMD_RECORD(INFO)          \
    GRPC_PLUGIN_CMD_RECORD(CONFIG)        \
    GRPC_PLUGIN_CMD_RECORD(CMD)           \
    GRPC_PLUGIN_CMD_RECORD(SCRIPT)        \
    GRPC_PLUGIN_CMD_RECORD(CYCLIC)

/**
 * @brief gRPC/Protobuf plugin — thin shell over `GrpcDriver` (grpc_driver.hpp),
 * which holds every gRPC-specific implementation detail (the real
 * grpc::Channel, TLS, the CALL intermediary command parsing, and the GUI
 * comm-dump reporting), and `GrpcProtocol` (grpc_protocol.hpp), which holds
 * every protobuf-specific detail (descriptor loading, dynamic message
 * construction, JSON conversion). This class's only jobs are:
 *
 *   - **CONFIG storage** — the getters/setters below, and `m_LocalSetParams()`.
 *   - **INFO** — a human-readable summary, no protocol involvement.
 *   - **Wiring** — `m_OpenDriver()` builds a `GrpcDriver::Config` from the
 *     stored settings, constructs (or reuses) the one persistent
 *     `GrpcDriver` for this plugin instance, and `m_GRPC_CMD()` hands that
 *     driver to `ucmdexec::generic_cmd()` — the same shared mechanism
 *     every other comm-driver plugin (UART, TCPIP, MQTT, ...) uses —
 *     supplying `GrpcDriver::send()`/`receive()` directly as the
 *     `pfsend`/`pfrecv` override.
 *
 * `GRPC.CMD` covers unary, server-streaming, client-streaming and bidi
 * calls (see grpc_driver.hpp's class doc comment for the CALL/FINISH
 * grammar for each); `GRPC.CYCLIC` covers periodic unary polling (see
 * grpc_driver.hpp's "CYCLIC" section — note its one real limitation
 * there, and echoed in this plugin's own INFO text: a request body with
 * more than one JSON field breaks CYCLIC's entry parsing, which isn't
 * quote-aware around its comma-separated entry list). `GRPC.SCRIPT` is
 * intentionally not wired up (mirroring MQTT, which also only exposes
 * INFO/CONFIG/CMD/CYCLIC) — it would need its own multi-line grammar
 * decision (each script line is its own CALL? one CALL spanning several
 * lines of JSON?) that hasn't been made yet, rather than being a smaller
 * version of what CMD already does.
 *
 * -------------------------------------------------------------------------
 * Session lifetime
 * -------------------------------------------------------------------------
 * Like MqttPlugin, every GRPC.CMD call shares one persistent `GrpcDriver`
 * (and the grpc::Channel + loaded descriptor set it owns) per plugin
 * instance — opened by `m_OpenDriver()` the first time it's needed, and
 * kept alive for as long as the plugin is loaded (closed by doCleanup()).
 */
class GrpcPlugin : public PluginInterface
{
public:
    GrpcPlugin()
        : m_strVersion(GRPC_PLUGIN_VERSION)
        , m_bIsInitialized(false)
        , m_bIsEnabled(false)
        , m_bIsFaultTolerant(false)
        , m_bIsPrivileged(false)
        , m_strResultData()
        , m_bRawResult(false)
        , m_bCyclicCached(true)
        , m_strHost()
        , m_u16Port(50051)
        , m_bUseTls(false)
        , m_u32CallTimeout(5000)
        , m_u32ConnectTimeout(5000)
        , m_u32ReadTimeout(5000)
        , m_u32ReadBufferSize(65536)
    {
        #define GRPC_PLUGIN_CMD_RECORD(a) m_mapCmds.insert( std::make_pair( #a, \
            PluginCommandEntry<GrpcPlugin>{&GrpcPlugin::m_GRPC_##a, false} ));
        GRPC_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  GRPC_PLUGIN_CMD_RECORD
    }

    ~GrpcPlugin() = default;

    bool isInitialized(void) const { return m_bIsInitialized; }
    bool isEnabled(void) const { return m_bIsEnabled; }
    bool isFaultTolerant(void) const { return m_bIsFaultTolerant; }
    bool isPrivileged(void) const { return m_bIsPrivileged; }

    bool doInit(void *pvUserData)
    {
        (void)pvUserData;
        m_bIsInitialized = true;
        return true;
    }

    void doCleanup(void)
    {
        m_bIsInitialized = false;
        m_bIsEnabled = false;
        m_strResultData.clear();
        m_pDriver.reset();
    }

    bool setParams(const PluginDataSet *psSetParams)
    {
        bool bRetVal = false;
        if (generic_setparams<GrpcPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
            if (m_LocalSetParams(psSetParams)) {
                bRetVal = true;
            }
        }
        return bRetVal;
    }

    void getParams(PluginDataGet *psGetParams) const
    {
        generic_getparams<GrpcPlugin>(this, psGetParams);
    }

    bool doDispatch(const std::string& strCmd, const std::string& strParams, std::stop_token st) const
    {
        return generic_dispatch<GrpcPlugin>(this, strCmd, strParams, st);
    }

    bool setRawResult (const std::string& strValue) const
    {
        return ucmdexec::parseRawResultFlag(strValue, m_bRawResult);
    }

    bool setPort(const std::string& portStr) const
    {
        return numeric::str2uint16(portStr, m_u16Port);
    }

    bool setCallTimeout(const std::string& timeoutStr) const
    {
        return numeric::str2uint32(timeoutStr, m_u32CallTimeout);
    }

    bool setConnectTimeout(const std::string& timeoutStr) const
    {
        return numeric::str2uint32(timeoutStr, m_u32ConnectTimeout);
    }

    bool setReadTimeout(const std::string& timeoutStr) const
    {
        return numeric::str2uint32(timeoutStr, m_u32ReadTimeout);
    }

    bool setReadBufferSize(const std::string& v) const {
        uint32_t sz = 0;
        if (!numeric::str2uint32(v, sz) || sz == 0) return false;
        m_u32ReadBufferSize = sz;
        return true;
    }

    bool setCyclicCached (const std::string& strValue) const
    {
       return ucmdexec::parseCyclicCachedFlag(strValue, m_bCyclicCached);
    }

    bool doEnable(void) { m_bIsEnabled = true; return true; }

    const PluginCommandsMap<GrpcPlugin>* getMap(void) const { return &m_mapCmds; }
    const std::string& getVersion(void) const { return m_strVersion; }
    const std::string& getData(void) const { return m_strResultData; }
    void resetData(void) const { m_strResultData.clear(); }

    // Getters/Setters
    const std::string& getHost(void) const { return m_strHost; }
    void setHost(const std::string& host) const { m_strHost = host; }
    uint16_t getPort(void) const { return m_u16Port; }
    bool isTlsEnabled(void) const { return m_bUseTls; }
    bool setTlsEnabled(const std::string& strValue) const { BoolExprEvaluator e; return e.evaluate(strValue, m_bUseTls); }
    const std::string& getTlsCaPath(void) const { return m_strTlsCaPath; }
    void setTlsCaPath(const std::string& path) const { m_strTlsCaPath = path; }
    const std::string& getTlsCertPath(void) const { return m_strTlsCertPath; }
    void setTlsCertPath(const std::string& path) const { m_strTlsCertPath = path; }
    const std::string& getTlsKeyPath(void) const { return m_strTlsKeyPath; }
    void setTlsKeyPath(const std::string& path) const { m_strTlsKeyPath = path; }
    const std::string& getDescriptorSetPath(void) const { return m_strDescriptorSetPath; }
    void setDescriptorSetPath(const std::string& path) const { m_strDescriptorSetPath = path; }
    const std::string& getAuthToken(void) const { return m_strAuthToken; }
    void setAuthToken(const std::string& val) const { m_strAuthToken = val; }
    uint32_t getCallTimeout(void) const { return m_u32CallTimeout; }
    uint32_t getConnectTimeout(void) const { return m_u32ConnectTimeout; }
    // Forwarded to ucmdexec::generic_cmd()/CommScriptCommandInterpreter for interface
    // symmetry with every other plugin; GrpcDriver::receive() itself never blocks on
    // the network (see grpc_driver.hpp) so u32ReadTimeout has no effect on GRPC.CMD <
    // — the RPC's own deadline is m_u32CallTimeout, applied inside send().
    uint32_t getReadTimeout(void) const { return m_u32ReadTimeout; }
    // Bounds the largest response JSON text a `GRPC.CMD <` can deliver.
    uint32_t getReadBufferSize(void) const { return m_u32ReadBufferSize; }


private:

    // Factory used by m_GRPC_CMD() (passed as ucmdexec::generic_cmd's
    // openFn): builds a GrpcDriver::Config from the stored settings and
    // returns the one persistent GrpcDriver for this plugin instance,
    // constructing (and open()-ing) it on first use. Reused as-is on every
    // later call as long as it's still open; see class doc comment's
    // "Session lifetime".
    std::shared_ptr<GrpcDriver> m_OpenDriver(void) const;

    bool m_LocalSetParams(const PluginDataSet *psSetParams);

    // Members
    PluginCommandsMap<GrpcPlugin> m_mapCmds;
    std::string m_strVersion;
    mutable std::string m_strResultData;

    /**
      * \brief when true, CMD returns the raw received bytes as-is instead of
      *        hexlifying them (see ucmdexec::generic_cmd()'s bRawResult parameter);
      *        settable via the ini file's RAW_RESULT key or the CONFIG command's
      *        raw= token (see ucmdexec::RAW_RESULT_INI_KEY / RAW_RESULT_CONFIG_KEY)
    */
    mutable bool m_bRawResult;

    /**
      * \brief CYCLIC caching mode: true (default) validates/parses each CYCLIC entry's
      *        command exactly once for the whole session; false re-resolves and re-validates
      *        every due entry on every tick, needed to track a volatile ("?=") macro used as
      *        one entry's val/id - settable via the ini file's CYCLIC_CACHED key or the CONFIG
      *        command's cached= token (see ucmdexec::CYCLIC_CACHED_INI_KEY / CYCLIC_CACHED_CONFIG_KEY
      *        and ucmdexec::generic_send_cyclic()'s bCached parameter)
    */
    mutable bool m_bCyclicCached;
    bool m_bIsInitialized;
    bool m_bIsEnabled;
    bool m_bIsFaultTolerant;
    bool m_bIsPrivileged;

    std::string m_strArtefactsPath;

    mutable std::string m_strHost;
    mutable uint16_t m_u16Port;
    mutable bool m_bUseTls;

    mutable std::string m_strTlsCaPath;
    mutable std::string m_strTlsCertPath;
    mutable std::string m_strTlsKeyPath;

    mutable std::string m_strDescriptorSetPath;
    mutable std::string m_strAuthToken;

    mutable uint32_t m_u32CallTimeout;
    mutable uint32_t m_u32ConnectTimeout;
    mutable uint32_t m_u32ReadTimeout;
    mutable uint32_t m_u32ReadBufferSize;

    // The persistent driver — see class doc comment's "Session lifetime"
    // and m_OpenDriver().
    mutable std::shared_ptr<GrpcDriver> m_pDriver;

    /**
      * \brief functions associated to the plugin commands
    */
    #define GRPC_PLUGIN_CMD_RECORD(a)  bool m_GRPC_##a ( const std::string& args, std::stop_token st ) const;
    GRPC_PLUGIN_COMMANDS_CONFIG_TABLE
    #undef  GRPC_PLUGIN_CMD_RECORD
};

#endif // GRPC_PLUGIN_HPP
