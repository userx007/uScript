#ifndef PROFIBUS_PLUGIN_HPP
#define PROFIBUS_PLUGIN_HPP

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

#include "profibus_driver.hpp"

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN NAME / VERSION                              //
/////////////////////////////////////////////////////////////////////////////////

#define PROFIBUS_PLUGIN_VERSION   "1.0.0.0"
#define PROFIBUS_PLUGIN_NAME      "PROFIBUS"


/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN COMMANDS                                    //
/////////////////////////////////////////////////////////////////////////////////

#define PROFIBUS_PLUGIN_COMMANDS_CONFIG_TABLE \
    PROFIBUS_PLUGIN_CMD_RECORD(INFO)          \
    PROFIBUS_PLUGIN_CMD_RECORD(CONFIG)        \
    PROFIBUS_PLUGIN_CMD_RECORD(CMD)           \
    PROFIBUS_PLUGIN_CMD_RECORD(SCRIPT)        \
    PROFIBUS_PLUGIN_CMD_RECORD(CYCLIC)

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN INTERFACE                                   //
/////////////////////////////////////////////////////////////////////////////////

/**
 * @brief PROFIBUS plugin — thin shell over `ProfibusDriver` (profibus_driver.hpp),
 * which holds every PROFIBUS-specific implementation detail (FDL telegram
 * framing, the SYN inter-telegram pause, the PROFIBUS.CMD intermediary
 * command parsing, and the GUI comm-dump reporting). This class's only
 * jobs are, following the same pattern as MqttPlugin (see mqtt_plugin.hpp
 * for the pattern this mirrors):
 *
 *   - **CONFIG storage** — the getters/setters below, and `m_PROFIBUS_CONFIG()`/
 *     `.ini` binding (`m_LocalSetParams()`) that fill them in.
 *   - **INFO** — a human-readable summary, no protocol involvement.
 *   - **Wiring** — `m_OpenDriver()` builds a `ProfibusDriver::Config` from
 *     the stored settings, constructs (or reuses) the one persistent
 *     `ProfibusDriver` for this plugin instance, and `m_PROFIBUS_CMD()`/
 *     `m_PROFIBUS_SCRIPT()`/`m_PROFIBUS_CYCLIC()` hand that driver to
 *     `ucmdexec::generic_cmd()`/`generic_script()`/`generic_send_cyclic()` — the same
 *     shared mechanism every other comm-
 *     driver plugin (UART, TCPIP, KVCAN, MQTT, ...) uses — supplying
 *     `ProfibusDriver::send()`/`receive()` directly as the `pfsend`/`pfrecv`
 *     override (see profibus_driver.hpp's class doc comment for why that's
 *     needed for accurate GUI comm-dump reporting). Note the lambdas below
 *     capture nothing from this plugin at all — everything they need comes
 *     through the driver parameter already.
 *
 * -------------------------------------------------------------------------
 * Session lifetime
 * -------------------------------------------------------------------------
 * Unlike a typical UART/TCPIP CMD (fresh connection per call), every
 * PROFIBUS.CMD/PROFIBUS.SCRIPT/PROFIBUS.CYCLIC call shares one persistent `ProfibusDriver`
 * (and the serial port + FCB security-sequence state it owns) per plugin
 * instance — opened by `m_OpenDriver()` the first time it's needed, and
 * kept alive for as long as the plugin is loaded (closed by doCleanup()).
 * This is what makes `PROFIBUS.CMD <` meaningful as a passive bus monitor:
 * it can be issued at any point, including repeatedly from a background
 * thread (`PROFIBUS.CMD < &`), to observe whatever is currently on the bus.
 *
 * See profibus_driver.hpp's class doc comment for the hardware/timing
 * limitations (reachable baud rates, best-effort SYN
 * pause) that apply regardless of what's configured here.
 */
class ProfibusPlugin : public PluginInterface
{
public:
    ProfibusPlugin()
        : m_strVersion(PROFIBUS_PLUGIN_VERSION)
        , m_strInstanceName(PROFIBUS_PLUGIN_NAME)
        , m_strResultData()
        , m_bRawResult(false)
        , m_bCyclicCached(true)
        , m_bIsInitialized(false)
        , m_bIsEnabled(false)
        , m_bIsFaultTolerant(false)
        , m_bIsPrivileged(false)
        , m_strDevice()
        , m_u32Baud(19200)
        , m_u8OwnAddress(2)
        , m_u32ResponseTimeout(200)
        , m_bDefaultHighPriority(false)
        , m_u32ReadBufferSize(256)
    {
        #define PROFIBUS_PLUGIN_CMD_RECORD(a) m_mapCmds.insert( std::make_pair( #a, \
            PluginCommandEntry<ProfibusPlugin>{&ProfibusPlugin::m_PROFIBUS_##a, false} ));
        PROFIBUS_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  PROFIBUS_PLUGIN_CMD_RECORD
    }

    ~ProfibusPlugin() = default;

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
        m_pDriver.reset(); // ~ProfibusDriver() closes the serial port
    }

    bool setParams(const PluginDataSet *psSetParams)
    {
        bool bRetVal = false;
        if (generic_setparams<ProfibusPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
            if (m_LocalSetParams(psSetParams)) {
                bRetVal = true;
            }
        }
        return bRetVal;
    }

    void getParams(PluginDataGet *psGetParams) const
    {
        generic_getparams<ProfibusPlugin>(this, psGetParams);
    }

    bool doDispatch(const std::string& strCmd, const std::string& strParams, std::stop_token st) const
    {
        return generic_dispatch<ProfibusPlugin>(this, strCmd, strParams, st);
    }

    const PluginCommandsMap<ProfibusPlugin>* getMap(void) const { return &m_mapCmds; }
    const std::string& getVersion(void) const { return m_strVersion; }
    const std::string& getData(void) const { return m_strResultData; }
    void resetData(void) const { m_strResultData.clear(); }
    
    bool setRawResult (const std::string& strValue) const
    {
        return ucmdexec::parseRawResultFlag(strValue, m_bRawResult);
    }

    bool setCyclicCached (const std::string& strValue) const
    {
        return ucmdexec::parseCyclicCachedFlag(strValue, m_bCyclicCached);
    }

    bool doEnable(void) { m_bIsEnabled = true; return true; }

    // Getters/Setters
    const std::string& getDevice(void) const { return m_strDevice; }
    void setDevice(const std::string& device) const { m_strDevice = device; }

    uint32_t getBaud(void) const { return m_u32Baud; }
    uint8_t getOwnAddress(void) const { return m_u8OwnAddress; }
    uint32_t getResponseTimeout(void) const { return m_u32ResponseTimeout; }
    bool setResponseTimeout(const std::string& timeoutStr) const { return numeric::str2uint32(timeoutStr, m_u32ResponseTimeout); }

    bool getDefaultHighPriority(void) const { return m_bDefaultHighPriority; }
    bool setDefaultHighPriority(const std::string& strValue) const { BoolExprEvaluator e; return e.evaluate(strValue, m_bDefaultHighPriority); }

    uint32_t getReadBufferSize(void) const { return m_u32ReadBufferSize; }
    bool setBaud(const std::string& baudStr) const
    {
        uint32_t baud = 0;
        if (!numeric::str2uint32(baudStr, baud)) return false;

        // Only rates ProfibusDriver can actually reach through UART::open() —
        // see profibus_driver.hpp's "Known hardware/timing limitations".
        // 1500000/3000000 are platform-dependent there (present only when the
        // build's <termios.h> defines B1500000/B3000000); accepted here too
        // since rejecting them outright would be wrong on the platforms where
        // they do work, and UART::open() itself will fall back to 9600 with a
        // clear log warning on the platforms where they don't.
        switch (baud) {
            case 9600: case 19200: case 500000: case 1500000: case 3000000:
                m_u32Baud = baud;
                return true;
            default:
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("Unreachable baud rate (see profibus_driver.hpp for why):"); LOG_UINT32(baud));
                return false;
        }
    }

    // Valid FDL station addresses are 0-125; 126 is reserved for
    // commissioning and 127 is the broadcast address — neither is a valid
    // address for this master's own identity.
    bool setOwnAddress(const std::string& addrStr) const
    {
        uint32_t addr = 0;
        if (!numeric::str2uint32(addrStr, addr)) return false;
        if (addr > 125) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Own address must be 0-125 (126=commissioning, 127=broadcast):"); LOG_UINT32(addr));
            return false;
        }
        m_u8OwnAddress = static_cast<uint8_t>(addr);
        return true;
    }

    bool setReadBufferSize(const std::string& bufSizeStr) const
    {
        uint32_t sz = 0;
        if (!numeric::str2uint32(bufSizeStr, sz)) return false;
        if (sz == 0) return false;
        m_u32ReadBufferSize = sz;
        return true;
    }

private:

    // Factory used by both m_PROFIBUS_CMD() and m_PROFIBUS_SCRIPT() (passed
    // as ucmdexec::generic_cmd/generic_script's openFn): builds a
    // ProfibusDriver::Config from the stored settings and returns the one
    // persistent ProfibusDriver for this plugin instance, constructing (and
    // open()-ing) it on first use. Reused as-is on every later call as long
    // as it's still open; see class doc comment's "Session lifetime".
    std::shared_ptr<ProfibusDriver> m_OpenDriver(void) const;

    bool m_LocalSetParams(const PluginDataSet *psSetParams);

    // Members
    PluginCommandsMap<ProfibusPlugin> m_mapCmds;
    std::string m_strVersion;

    // Runtime instance identity for the GUI comm-dump panel (e.g. "PROFIBUS"
    // or "PROFIBUS:1" -- see PluginDataSet::strInstanceName). Falls back to
    // PROFIBUS_PLUGIN_NAME when unset.
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

    mutable std::string m_strDevice;
    mutable uint32_t m_u32Baud;
    mutable uint8_t m_u8OwnAddress;
    mutable uint32_t m_u32ResponseTimeout;
    mutable bool m_bDefaultHighPriority;

    mutable uint32_t m_u32ReadBufferSize;

    // The persistent driver — see class doc comment's "Session lifetime"
    // and m_OpenDriver().
    mutable std::shared_ptr<ProfibusDriver> m_pDriver;

    /**
      * \brief functions associated to the plugin commands
    */
    #define PROFIBUS_PLUGIN_CMD_RECORD(a)  bool m_PROFIBUS_##a ( const std::string& args, std::stop_token st ) const;
    PROFIBUS_PLUGIN_COMMANDS_CONFIG_TABLE
    #undef  PROFIBUS_PLUGIN_CMD_RECORD
};

#endif // PROFIBUS_PLUGIN_HPP
