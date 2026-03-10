#pragma once

#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "ICommDriver.hpp"
#include "uLogger.hpp"

#include "cp2112_generic.hpp"

// CP2112 driver classes
#include "uCP2112.hpp"
#include "uCP2112Gpio.hpp"

// X-macro config tables
#include "i2c_config.hpp"
#include "gpio_config.hpp"

#include <memory>
#include <string>
#include <map>
#include <span>

///////////////////////////////////////////////////////////////////
//                      PLUGIN VERSION                           //
///////////////////////////////////////////////////////////////////

#define CP2112_PLUGIN_VERSION  "1.0.0.0"

///////////////////////////////////////////////////////////////////
//               TOP-LEVEL PLUGIN COMMANDS                       //
///////////////////////////////////////////////////////////////////

#define CP2112_PLUGIN_COMMANDS_CONFIG_TABLE   \
CP2112_PLUGIN_CMD_RECORD( INFO )              \
CP2112_PLUGIN_CMD_RECORD( I2C  )              \
CP2112_PLUGIN_CMD_RECORD( GPIO )

///////////////////////////////////////////////////////////////////
//                      PLUGIN CLASS                             //
///////////////////////////////////////////////////////////////////

/**
 * @brief CP2112 plugin.
 *
 * Exposes the Silicon Labs CP2112 USB-HID-to-I²C/SMBus bridge as two
 * independent modules — I2C and GPIO — through the same string-command
 * dispatch mechanism used by all other plugins in this project.
 *
 * The CP2112 is a single USB device (VID 0x10C4 / PID 0xEA90) with:
 *   - One I²C/SMBus master port (up to 400 kHz standard; hardware supports
 *     higher but 400 kHz is the well-tested ceiling for most targets).
 *   - Eight general-purpose GPIO pins with configurable direction,
 *     push-pull/open-drain drive, and optional special functions
 *     (TX/RX LEDs, interrupt output, clock output on GPIO.6).
 *
 * Each module opens its own HID handle — they can be used independently.
 * On the same physical chip, both can be open simultaneously.
 *
 * Usage examples:
 *
 *   CP2112.I2C  open addr=0x50 clock=400000
 *   CP2112.I2C  scan
 *   CP2112.I2C  write 0000
 *   CP2112.I2C  wrrd 0000:2
 *   CP2112.I2C  close
 *
 *   CP2112.GPIO open device=0 dir=0xFF pp=0xFF
 *   CP2112.GPIO set  0x01
 *   CP2112.GPIO clear 0x01
 *   CP2112.GPIO read
 *   CP2112.GPIO close
 */
class CP2112Plugin : public PluginInterface
{
public:

    CP2112Plugin()
        : m_strVersion(CP2112_PLUGIN_VERSION)
        , m_bIsInitialized(false)
        , m_bIsEnabled(false)
        , m_bIsFaultTolerant(false)
        , m_bIsPrivileged(false)
    {
        // ── Top-level command map ───────────────────────────────────────
        #define CP2112_PLUGIN_CMD_RECORD(a) \
            m_mapCmds.insert({#a, &CP2112Plugin::m_CP2112_##a});
        CP2112_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef CP2112_PLUGIN_CMD_RECORD

        // ── I2C ────────────────────────────────────────────────────────
        #define I2C_CMD_RECORD(a) \
            m_mapCmds_I2C.insert({#a, &CP2112Plugin::m_handle_i2c_##a});
        I2C_COMMANDS_CONFIG_TABLE
        #undef I2C_CMD_RECORD

        #define I2C_SPEED_RECORD(a,b) m_mapSpeed_I2C.insert({a, static_cast<size_t>(b)});
        I2C_SPEED_CONFIG_TABLE
        #undef I2C_SPEED_RECORD

        // ── GPIO ───────────────────────────────────────────────────────
        #define GPIO_CMD_RECORD(a) \
            m_mapCmds_GPIO.insert({#a, &CP2112Plugin::m_handle_gpio_##a});
        GPIO_COMMANDS_CONFIG_TABLE
        #undef GPIO_CMD_RECORD

        // ── Meta maps ──────────────────────────────────────────────────
        m_mapSpeedsMaps.insert({"I2C",  &m_mapSpeed_I2C});
        m_mapSpeedsMaps.insert({"GPIO", nullptr});   // no speed map for GPIO

        m_mapCommandsMaps.insert({"I2C",  &m_mapCmds_I2C});
        m_mapCommandsMaps.insert({"GPIO", &m_mapCmds_GPIO});
    }

    ~CP2112Plugin() = default;

    // ── PluginInterface ────────────────────────────────────────────────

    bool isInitialized()   const override { return m_bIsInitialized;   }
    bool isEnabled()       const override { return m_bIsEnabled;       }
    bool isFaultTolerant() const override { return m_bIsFaultTolerant; }
    bool isPrivileged()    const override { return false;               }

    bool setParams(const PluginDataSet* ps) {
        bool ok = generic_setparams<CP2112Plugin>(this, ps, &m_bIsFaultTolerant, &m_bIsPrivileged);
        return ok && m_LocalSetParams(ps);
    }

    void getParams(PluginDataGet* pg) const {
        generic_getparams<CP2112Plugin>(this, pg);
    }

    bool doDispatch(const std::string& cmd, const std::string& params) const {
        return generic_dispatch<CP2112Plugin>(this, cmd, params);
    }

    const PluginCommandsMap<CP2112Plugin>* getMap() const { return &m_mapCmds; }

    const std::string& getVersion() const { return m_strVersion; }
    const std::string& getData()    const { return m_strResultData; }
    void resetData()                const { m_strResultData.clear(); }

    bool doInit(void* pvUserData);
    void doEnable()  { m_bIsEnabled = true; }
    void doCleanup();
    void setFaultTolerant() { m_bIsFaultTolerant = true; }

    // ── Module-map accessors ────────────────────────────────────────────

    ModuleCommandsMap<CP2112Plugin>* getModuleCmdsMap(const std::string& m) const;
    ModuleSpeedMap*                  getModuleSpeedsMap(const std::string& m) const;

    /**
     * @brief Re-open I²C at a new clock frequency while keeping the same address.
     *
     * GPIO has no numeric speed concept; passing "GPIO" logs a warning and
     * returns false.
     */
    bool setModuleSpeed(const std::string& module, size_t hz) const;

    // ── INI accessor ───────────────────────────────────────────────────

    struct IniValues {
        std::string strArtefactsPath;
        uint8_t     u8DeviceIndex  {0};
        uint32_t    u32I2cClockHz  {100000u};
        uint8_t     u8I2cAddress   {0x50u};
        uint32_t    u32ReadTimeout {1000u};    ///< Default read timeout (ms) for script execution
        uint32_t    u32ScriptDelay {0u};       ///< Inter-command delay (ms) for script execution
    };

    friend const IniValues* getAccessIniValues(const CP2112Plugin& obj);

private:

    // ── Pending configuration ─────────────────────────────────────────

    struct I2cPendingCfg {
        uint8_t  address {0x50u};
        uint32_t clockHz {100000u};
    };

    struct GpioPendingCfg {
        uint8_t directionMask   {0x00u}; ///< 1 = output, 0 = input (default all inputs)
        uint8_t pushPullMask    {0x00u}; ///< 1 = push-pull, 0 = open-drain
        uint8_t specialFuncMask {0x00u}; ///< Special function enables (TX LED, irq, clk, RX LED)
        uint8_t clockDivider    {0x00u}; ///< Clock divider (used when GPIO.6 = clock output)
    };

    // ── Driver instance accessors ──────────────────────────────────────

    CP2112*     m_i2c()  const;
    CP2112Gpio* m_gpio() const;

    // ── WrRd callback ──────────────────────────────────────────────────

    bool m_i2c_wrrd_cb(std::span<const uint8_t> req, size_t rdlen) const;

    // ── Top-level command handlers ─────────────────────────────────────

    #define CP2112_PLUGIN_CMD_RECORD(a) \
        bool m_CP2112_##a(const std::string& args) const;
    CP2112_PLUGIN_COMMANDS_CONFIG_TABLE
    #undef CP2112_PLUGIN_CMD_RECORD

    // ── Per-module subcommand declarations ────────────────────────────

    #define I2C_CMD_RECORD(a)  bool m_handle_i2c_##a (const std::string&) const;
    I2C_COMMANDS_CONFIG_TABLE
    #undef I2C_CMD_RECORD

    #define GPIO_CMD_RECORD(a) bool m_handle_gpio_##a(const std::string&) const;
    GPIO_COMMANDS_CONFIG_TABLE
    #undef GPIO_CMD_RECORD

    // ── Parse helpers ──────────────────────────────────────────────────
    static bool parseGpioKv(const std::string& key,
                            const std::string& val,
                            GpioPendingCfg& cfg);

    // ── Member data ───────────────────────────────────────────────────

    std::string m_strVersion;
    mutable std::string m_strResultData;

    bool m_bIsInitialized;
    bool m_bIsEnabled;
    bool m_bIsFaultTolerant;
    bool m_bIsPrivileged;

    IniValues       m_sIniValues;
    mutable I2cPendingCfg  m_sI2cCfg;
    mutable GpioPendingCfg m_sGpioCfg;

    mutable std::unique_ptr<CP2112>     m_pI2C;
    mutable std::unique_ptr<CP2112Gpio> m_pGPIO;

    PluginCommandsMap<CP2112Plugin>   m_mapCmds;
    SpeedsMapsMap                     m_mapSpeedsMaps;
    CommandsMapsMap<CP2112Plugin>     m_mapCommandsMaps;

    ModuleCommandsMap<CP2112Plugin>   m_mapCmds_I2C;
    ModuleCommandsMap<CP2112Plugin>   m_mapCmds_GPIO;

    ModuleSpeedMap                    m_mapSpeed_I2C;

    bool m_LocalSetParams(const PluginDataSet* ps);
};
