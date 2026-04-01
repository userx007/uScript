#ifndef FT245_PLUGIN_HPP
#define FT245_PLUGIN_HPP

#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "ICommDriver.hpp"
#include "uLogger.hpp"

#include "ft245_generic.hpp"

// FT245 library
#include "uFT245Sync.hpp"
#include "uFT245GPIO.hpp"

// X-macro config tables
#include "fifo_config.hpp"
#include "gpio_config.hpp"

#include <memory>
#include <string>
#include <map>
#include <span>

///////////////////////////////////////////////////////////////////
//                      PLUGIN VERSION                           //
///////////////////////////////////////////////////////////////////

#define FT245_PLUGIN_VERSION  "1.0.0.0"
#define FT245_PLUGIN_NAME     "FT245"

///////////////////////////////////////////////////////////////////
//               TOP-LEVEL PLUGIN COMMANDS                       //
///////////////////////////////////////////////////////////////////

#define FT245_PLUGIN_COMMANDS_CONFIG_TABLE  \
FT245_PLUGIN_CMD_RECORD( INFO )             \
FT245_PLUGIN_CMD_RECORD( FIFO )             \
FT245_PLUGIN_CMD_RECORD( GPIO )

///////////////////////////////////////////////////////////////////
//                      PLUGIN CLASS                             //
///////////////////////////////////////////////////////////////////

/**
 * @brief FT245 plugin.
 *
 * Supports both the FT245BM/RL (async FIFO and sync FIFO) and the
 * FT245R (async FIFO only, integrated oscillator) variants.
 *
 * The variant is set once in the INI file (or per-command via variant=BM|R).
 * It flows into every open() call and determines which PID is searched for
 * on USB enumeration and which FIFO modes are legal.
 *
 * Two independent modules are exposed:
 *
 *   FIFO  — bulk byte-stream transfers via the FT245 parallel FIFO.
 *            Supports both async (default) and sync FIFO modes.
 *            Implements the full ICommDriver interface including
 *            Exact / UntilDelimiter / UntilToken read modes and
 *            CommScriptClient script execution.
 *
 *   GPIO  — byte-wide bit-bang GPIO on all 8 data pins (D0–D7)
 *            via BITMODE_BITBANG.  FIFO and GPIO modes are mutually
 *            exclusive on a single device; close one before opening
 *            the other.
 *
 * Usage examples:
 *
 *   # FT245BM async FIFO (default)
 *   FT245.FIFO open variant=BM mode=async
 *   FT245.FIFO write DEADBEEF
 *   FT245.FIFO read  4
 *   FT245.FIFO close
 *
 *   # FT245BM synchronous FIFO
 *   FT245.FIFO open variant=BM mode=sync
 *   FT245.FIFO wrrdf large_payload.bin
 *   FT245.FIFO close
 *
 *   # FT245R (async only)
 *   FT245.FIFO open variant=R
 *   FT245.FIFO script comm_test.txt
 *
 *   # GPIO bit-bang
 *   FT245.GPIO open variant=BM dir=0xFF val=0x00
 *   FT245.GPIO set  0x01
 *   FT245.GPIO read
 *   FT245.GPIO close
 */

class FT245Plugin : public PluginInterface
{

public:

    FT245Plugin()
        : m_strVersion(FT245_PLUGIN_VERSION)
        , m_bIsInitialized(false)
        , m_bIsEnabled(false)
        , m_bIsFaultTolerant(false)
        , m_bIsPrivileged(false)
    {
        // Top-level command map 
        #define FT245_PLUGIN_CMD_RECORD(a) \
            m_mapCmds.insert({#a, &FT245Plugin::m_FT245_##a});
        FT245_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef FT245_PLUGIN_CMD_RECORD

        // FIFO 
        #define FIFO_CMD_RECORD(a) \
            m_mapCmds_FIFO.insert({#a, &FT245Plugin::m_handle_fifo_##a});
        FIFO_COMMANDS_CONFIG_TABLE
        #undef FIFO_CMD_RECORD

        // GPIO 
        #define GPIO_CMD_RECORD(a) \
            m_mapCmds_GPIO.insert({#a, &FT245Plugin::m_handle_gpio_##a});
        GPIO_COMMANDS_CONFIG_TABLE
        #undef GPIO_CMD_RECORD

        // Meta maps 
        // FT245 has no speed/clock presets (FIFO rate is USB-governed)
        m_mapSpeedsMaps.insert({"FIFO", nullptr});
        m_mapSpeedsMaps.insert({"GPIO", nullptr});

        m_mapCommandsMaps.insert({"FIFO", &m_mapCmds_FIFO});
        m_mapCommandsMaps.insert({"GPIO", &m_mapCmds_GPIO});
    }

    ~FT245Plugin() = default;

    // PluginInterface 

    bool isInitialized()   const override { return m_bIsInitialized;   }
    bool isEnabled()       const override { return m_bIsEnabled;       }
    bool isFaultTolerant() const override { return m_bIsFaultTolerant; }
    bool isPrivileged()    const override { return false;              }

    bool setParams(const PluginDataSet* ps) {
        bool ok = generic_setparams<FT245Plugin>(this, ps, &m_bIsFaultTolerant, &m_bIsPrivileged);
        return ok && m_LocalSetParams(ps);
    }

    void getParams(PluginDataGet* pg) const {
        generic_getparams<FT245Plugin>(this, pg);
    }

    bool doDispatch(const std::string& cmd, const std::string& params) const {
        return generic_dispatch<FT245Plugin>(this, cmd, params);
    }

    const PluginCommandsMap<FT245Plugin>* getMap() const { return &m_mapCmds; }

    const std::string& getVersion() const { return m_strVersion; }
    const std::string& getData()    const { return m_strResultData; }
    void resetData()                const { m_strResultData.clear(); }

    bool doInit(void* pvUserData);
    bool doEnable()  { m_bIsEnabled = true; return true; }
    void doCleanup();
    void setFaultTolerant() { m_bIsFaultTolerant = true; }

    // Module-map accessors 

    ModuleCommandsMap<FT245Plugin>* getModuleCmdsMap(const std::string& m) const;
    ModuleSpeedMap*                 getModuleSpeedsMap(const std::string& m) const;

    /**
     * @brief Not applicable for FT245 (no configurable clock divisor).
     *        Returns false for all modules; included for interface parity.
     */
    bool setModuleSpeed(const std::string& module, size_t hz) const;

    // INI accessor 

    struct IniValues {
        std::string        strArtefactsPath;
        uint8_t            u8DeviceIndex   {0};
        FT245Base::Variant eDefaultVariant {FT245Base::Variant::FT245BM};
        FT245Base::FifoMode eDefaultFifoMode {FT245Base::FifoMode::Async};
        uint32_t           u32ReadTimeout  {1000u};  ///< ms, for script execution
        uint32_t           u32ScriptDelay  {0u};     ///< ms inter-command delay for scripts
    };

    friend const IniValues* getAccessIniValues(const FT245Plugin& obj);

private:

    // Pending configuration (updated by cfg, applied by open) 

    struct FifoPendingCfg {
        FT245Base::Variant  variant  {FT245Base::Variant::FT245BM};
        FT245Base::FifoMode fifoMode {FT245Base::FifoMode::Async};
    };

    struct GpioPendingCfg {
        FT245Base::Variant variant    {FT245Base::Variant::FT245BM};
        uint8_t            dirMask   {0x00u};
        uint8_t            initValue {0x00u};
    };

    // Driver instance accessors 

    FT245Sync*  m_fifo() const;
    FT245GPIO*  m_gpio() const;

    // WrRd callback 

    bool m_fifo_wrrd_cb(std::span<const uint8_t> req, size_t rdlen) const;

    // Top-level command handlers 

    #define FT245_PLUGIN_CMD_RECORD(a) \
        bool m_FT245_##a(const std::string& args) const;
    FT245_PLUGIN_COMMANDS_CONFIG_TABLE
    #undef FT245_PLUGIN_CMD_RECORD

    // Per-module subcommand declarations 

    #define FIFO_CMD_RECORD(a)  bool m_handle_fifo_##a(const std::string&) const;
    FIFO_COMMANDS_CONFIG_TABLE
    #undef FIFO_CMD_RECORD

    #define GPIO_CMD_RECORD(a)  bool m_handle_gpio_##a(const std::string&) const;
    GPIO_COMMANDS_CONFIG_TABLE
    #undef GPIO_CMD_RECORD

    // Member data 

    std::string m_strVersion;
    mutable std::string m_strResultData;

    bool m_bIsInitialized;
    bool m_bIsEnabled;
    bool m_bIsFaultTolerant;
    bool m_bIsPrivileged;

    IniValues m_sIniValues;

    mutable FifoPendingCfg m_sFifoCfg;
    mutable GpioPendingCfg m_sGpioCfg;

    mutable std::unique_ptr<FT245Sync>  m_pFIFO;
    mutable std::unique_ptr<FT245GPIO>  m_pGPIO;

    PluginCommandsMap<FT245Plugin>   m_mapCmds;
    SpeedsMapsMap                    m_mapSpeedsMaps;
    CommandsMapsMap<FT245Plugin>     m_mapCommandsMaps;

    ModuleCommandsMap<FT245Plugin>   m_mapCmds_FIFO;
    ModuleCommandsMap<FT245Plugin>   m_mapCmds_GPIO;

    bool m_LocalSetParams(const PluginDataSet* ps);

    // Parse helpers 
    static bool parseVariant (const std::string& s, FT245Base::Variant&  out);
    static bool parseFifoMode(const std::string& s, FT245Base::FifoMode& out);

    static bool parseFifoParams(const std::string& args,
                                FifoPendingCfg& cfg,
                                uint8_t* pDeviceIndexOut = nullptr);

    static bool parseGpioParams(const std::string& args,
                                GpioPendingCfg& cfg,
                                uint8_t* pDeviceIndexOut = nullptr);
};

#endif // FT245_PLUGIN_HPP
