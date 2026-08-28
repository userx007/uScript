#ifndef MODBUS_PLUGIN_HPP
#define MODBUS_PLUGIN_HPP

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

#include <string>
#include <memory>

#include "modbus_driver.hpp"

#define MODBUS_PLUGIN_VERSION   "1.0.0.0"
#define MODBUS_PLUGIN_NAME      "MODBUS"

#define MODBUS_PLUGIN_COMMANDS_CONFIG_TABLE \
    MODBUS_PLUGIN_CMD_RECORD(INFO)          \
    MODBUS_PLUGIN_CMD_RECORD(CONFIG)        \
    MODBUS_PLUGIN_CMD_RECORD(CMD)           \
    MODBUS_PLUGIN_CMD_RECORD(SCRIPT)        \
    MODBUS_PLUGIN_CMD_RECORD(CYCLIC)

/**
 * @brief Modbus TCP plugin — thin shell over `ModbusDriver`
 * (modbus_driver.hpp), which holds every Modbus-specific implementation
 * detail (ADU framing, the MODBUS.CMD intermediary command parsing, and
 * the GUI comm-dump reporting). Same shape as the MQTT plugin
 * (MqttProtocol/MqttDriver/MqttPlugin) — see modbus_driver.hpp's class doc
 * comment for the full three-way split. This class's only jobs are:
 *
 *   - **CONFIG storage** — the getters/setters below, and `m_MODBUS_CONFIG()`/
 *     `.ini` binding (`m_LocalSetParams()`) that fill them in.
 *   - **INFO** — a human-readable summary, no protocol involvement.
 *   - **Wiring** — `m_OpenDriver()` builds a `ModbusDriver::Config` from
 *     the stored settings, constructs (or reuses) the one persistent
 *     `ModbusDriver` for this plugin instance, and `m_MODBUS_CMD()`/
 *     `m_MODBUS_SCRIPT()`/`m_MODBUS_CYCLIC()` hand that driver to
 *     `ucmdexec::generic_cmd()`/`generic_script()`/`generic_send_cyclic()`
 *     — the same shared mechanism every other comm-
 *     driver plugin (UART, TCPIP, KVCAN, MQTT, ...) uses — supplying
 *     `ModbusDriver::send()`/`receive()` directly as the `pfsend`/`pfrecv`
 *     override. The lambdas below capture nothing from this plugin at
 *     all — everything they need comes through the driver parameter.
 *
 * -------------------------------------------------------------------------
 * Command surface
 * -------------------------------------------------------------------------
 * `MODBUS.CMD > <FUNCTION> <unit_id> <address> <args...> [| expected]` —
 * one Modbus request/response round trip per call, on the plugin's single
 * persistent TCP connection (opened lazily on first use, kept alive for as
 * long as the plugin is loaded — see "Session lifetime" below). Unlike
 * MQTT, there is no "<" receive mode: Modbus has no asynchronous,
 * broker-initiated messages, so a request's response is always waited for
 * as the receive half of the same command line.
 *
 * Functions: `READ_COILS`, `READ_DISCRETE_INPUTS`,
 * `READ_HOLDING_REGISTERS`, `READ_INPUT_REGISTERS` (all four:
 * `<unit_id> <start_addr> <quantity>`, response is comma-separated
 * "0"/"1" per bit for the two coil/input reads, comma-separated decimal
 * values for the two register reads); `WRITE_SINGLE_COIL`
 * (`<unit_id> <addr> <0|1>`), `WRITE_SINGLE_REGISTER`
 * (`<unit_id> <addr> <value 0-65535>`), `WRITE_MULTIPLE_COILS`
 * (`<unit_id> <start_addr> <v1> [v2] ...`, each 0/1), and
 * `WRITE_MULTIPLE_REGISTERS` (`<unit_id> <start_addr> <v1> [v2] ...`, each
 * 0-65535) — all four write functions confirm with `"OK"`. A Modbus
 * exception response fails the command with `"EXCEPTION:<code>"`.
 *
 * -------------------------------------------------------------------------
 * Session lifetime
 * -------------------------------------------------------------------------
 * Every MODBUS.CMD/MODBUS.SCRIPT/MODBUS.CYCLIC call shares one persistent TCP connection
 * per plugin instance — opened lazily by `m_OpenDriver()` the first time
 * it's needed, and kept alive for as long as the plugin is loaded (closed
 * by doCleanup()). Unlike MQTT there's no session handshake to redo, so
 * this is purely a "keep the socket open across calls" optimisation rather
 * than something any command's correctness depends on.
 */
class ModbusPlugin : public PluginInterface
{
public:
    ModbusPlugin()
        : m_strVersion(MODBUS_PLUGIN_VERSION)
        , m_strInstanceName(MODBUS_PLUGIN_NAME)
        , m_bIsInitialized(false)
        , m_bIsEnabled(false)
        , m_bIsFaultTolerant(false)
        , m_bIsPrivileged(false)
        , m_strResultData()
        , m_bRawResult(false)
        , m_bCyclicCached(true)
        , m_strHost("localhost")
        , m_u16Port(502)
        , m_u32ReadTimeout(3000)
        , m_u32ReadBufferSize(4096)
    {
        #define MODBUS_PLUGIN_CMD_RECORD(a) m_mapCmds.insert( std::make_pair( #a, \
            PluginCommandEntry<ModbusPlugin>{&ModbusPlugin::m_MODBUS_##a, false} ));
        MODBUS_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  MODBUS_PLUGIN_CMD_RECORD
    }

    ~ModbusPlugin() = default;

    bool isInitialized(void) const { return m_bIsInitialized; }
    bool isEnabled(void) const { return m_bIsEnabled; }

    bool setParams(const PluginDataSet *psSetParams);
    void getParams(PluginDataGet *psGetParams) const;
    bool doDispatch(const std::string& strCmd, const std::string& strParams, std::stop_token st = {}) const;
    const PluginCommandsMap<ModbusPlugin>* getMap(void) const { return &m_mapCmds; }
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
    uint32_t getReadTimeout(void) const { return m_u32ReadTimeout; }
    uint32_t getReadBufferSize(void) const { return m_u32ReadBufferSize; }

    /**
      * \brief CONFIG-command setter for the raw-result flag (see m_bRawResult)
    */
    bool setRawResult (const std::string& strValue) const
    {
        return ucmdexec::parseRawResultFlag(strValue, m_bRawResult);
    }

    /**
      * \brief CONFIG-command setter for the CYCLIC caching mode (see m_bCyclicCached)
    */
    bool setCyclicCached (const std::string& strValue) const
    {
        return ucmdexec::parseCyclicCachedFlag(strValue, m_bCyclicCached);
    }

    bool setPort(const std::string& portStr) const
    {
         return numeric::str2uint16(portStr, m_u16Port);
    }

    bool setReadTimeout(const std::string& timeoutStr) const
    {
        return numeric::str2uint32(timeoutStr, m_u32ReadTimeout);
    }

    bool setReadBufferSize(const std::string& bufSizeStr) const
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

private:

    // Factory used by both m_MODBUS_CMD() and m_MODBUS_SCRIPT() (passed as
    // ucmdexec::generic_cmd/generic_script's openFn): builds a
    // ModbusDriver::Config from the stored settings and returns the one
    // persistent ModbusDriver for this plugin instance, constructing (and
    // open()-ing) it on first use. Reused as-is on every later call as
    // long as it's still open; see class doc comment's "Session lifetime".
    std::shared_ptr<ModbusDriver> m_OpenDriver(void) const;

    bool m_LocalSetParams(const PluginDataSet *psSetParams);

    // Members
    PluginCommandsMap<ModbusPlugin> m_mapCmds;
    std::string m_strVersion;

    // Runtime instance identity for the GUI comm-dump panel (e.g. "MODBUS"
    // or "MODBUS:1" -- see PluginDataSet::strInstanceName). Falls back to
    // MODBUS_PLUGIN_NAME when unset.
    std::string m_strInstanceName;
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
    mutable uint32_t m_u32ReadTimeout;
    mutable uint32_t m_u32ReadBufferSize;

    // The persistent driver — see class doc comment's "Session lifetime"
    // and m_OpenDriver().
    mutable std::shared_ptr<ModbusDriver> m_pDriver;

    /**
      * \brief functions associated to the plugin commands
    */
    #define MODBUS_PLUGIN_CMD_RECORD(a)  bool m_MODBUS_##a ( const std::string& args, std::stop_token st ) const;
    MODBUS_PLUGIN_COMMANDS_CONFIG_TABLE
    #undef  MODBUS_PLUGIN_CMD_RECORD
};

#endif // MODBUS_PLUGIN_HPP
