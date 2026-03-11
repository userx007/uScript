#pragma once

#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "ICommDriver.hpp"
#include "uLogger.hpp"

#include "ft4232_generic.hpp"

// FT4232H library
#include "uFT4232SPI.hpp"
#include "uFT4232I2C.hpp"
#include "uFT4232GPIO.hpp"
#include "uFT4232UART.hpp"   // class FT4232UART : public ICommDriver (channels C/D async UART)

// X-macro config tables
#include "spi_config.hpp"
#include "i2c_config.hpp"
#include "gpio_config.hpp"
#include "uart_config.hpp"

#include <memory>
#include <string>
#include <map>
#include <span>

///////////////////////////////////////////////////////////////////
//                      PLUGIN VERSION                           //
///////////////////////////////////////////////////////////////////

#define FT4232_PLUGIN_VERSION  "1.0.0.0"
#define FT4232_PLUGIN_NAME     "FT4232"


///////////////////////////////////////////////////////////////////
//               TOP-LEVEL PLUGIN COMMANDS                       //
///////////////////////////////////////////////////////////////////

//  INFO is a standalone command.
//  SPI / I2C / GPIO / UART each route into their own sub-command map.
#define FT4232_PLUGIN_COMMANDS_CONFIG_TABLE  \
FT_PLUGIN_CMD_RECORD( INFO )                 \
FT_PLUGIN_CMD_RECORD( SPI  )                 \
FT_PLUGIN_CMD_RECORD( I2C  )                 \
FT_PLUGIN_CMD_RECORD( GPIO )                 \
FT_PLUGIN_CMD_RECORD( UART )

///////////////////////////////////////////////////////////////////
//                      PLUGIN CLASS                             //
///////////////////////////////////////////////////////////////////

/**
 * @brief FT4232H plugin.
 *
 * Exposes four independent FT4232H driver modules — SPI, I2C, GPIO, UART —
 * through the same string-command dispatch mechanism used by the
 * HydraBus plugin.
 *
 * SPI/I2C/GPIO use MPSSE channels A or B.
 * UART uses async channels C or D.
 *
 * Unlike the HydraBus plugin there is no global "mode" switch: each
 * module manages its own USB handle and can be open simultaneously.
 *
 * Typical usage:
 *   FT4232.SPI  open clock=10000000 mode=0 channel=A
 *   FT4232.SPI  cfg  cspin=8 cspol=low
 *   FT4232.SPI  cs   en
 *   FT4232.SPI  wrrd 9F:3
 *   FT4232.SPI  script my_test.csc
 *   FT4232.SPI  close
 *
 *   FT4232.I2C  open addr=0x50 clock=400000 channel=B
 *   FT4232.I2C  scan
 *   FT4232.I2C  wrrd 0000:2
 *   FT4232.I2C  script eeprom_read.csc
 *   FT4232.I2C  close
 *
 *   FT4232.GPIO open channel=B lowdir=0xFF lowval=0x00
 *   FT4232.GPIO write low 0xAB
 *   FT4232.GPIO read  low
 *   FT4232.GPIO close
 *
 *   FT4232.UART open baud=115200 channel=C
 *   FT4232.UART script comms_sequence.csc
 *   FT4232.UART close
 */
class FT4232Plugin : public PluginInterface
{
public:

    FT4232Plugin()
        : m_strVersion(FT4232_PLUGIN_VERSION)
        , m_bIsInitialized(false)
        , m_bIsEnabled(false)
        , m_bIsFaultTolerant(false)
        , m_bIsPrivileged(false)
    {
        // ── Top-level command map ───────────────────────────────────────
        #define FT_PLUGIN_CMD_RECORD(a) \
            m_mapCmds.insert({#a, &FT4232Plugin::m_FT4232_##a});
        FT4232_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef FT_PLUGIN_CMD_RECORD

        // ── SPI subcommand map ──────────────────────────────────────────
        #define SPI_CMD_RECORD(a) \
            m_mapCmds_SPI.insert({#a, &FT4232Plugin::m_handle_spi_##a});
        SPI_COMMANDS_CONFIG_TABLE
        #undef SPI_CMD_RECORD

        #define SPI_SPEED_RECORD(a,b) m_mapSpeed_SPI.insert({a, static_cast<size_t>(b)});
        SPI_SPEED_CONFIG_TABLE
        #undef SPI_SPEED_RECORD

        // ── I2C subcommand map ──────────────────────────────────────────
        #define I2C_CMD_RECORD(a) \
            m_mapCmds_I2C.insert({#a, &FT4232Plugin::m_handle_i2c_##a});
        I2C_COMMANDS_CONFIG_TABLE
        #undef I2C_CMD_RECORD

        #define I2C_SPEED_RECORD(a,b) m_mapSpeed_I2C.insert({a, static_cast<size_t>(b)});
        I2C_SPEED_CONFIG_TABLE
        #undef I2C_SPEED_RECORD

        // ── GPIO subcommand map ─────────────────────────────────────────
        #define GPIO_CMD_RECORD(a) \
            m_mapCmds_GPIO.insert({#a, &FT4232Plugin::m_handle_gpio_##a});
        GPIO_COMMANDS_CONFIG_TABLE
        #undef GPIO_CMD_RECORD

        // ── UART subcommand map ─────────────────────────────────────────
        #define UART_CMD_RECORD(a) \
            m_mapCmds_UART.insert({#a, &FT4232Plugin::m_handle_uart_##a});
        UART_COMMANDS_CONFIG_TABLE
        #undef UART_CMD_RECORD

        #define UART_SPEED_RECORD(a,b) m_mapSpeed_UART.insert({a, static_cast<size_t>(b)});
        UART_SPEED_CONFIG_TABLE
        #undef UART_SPEED_RECORD

        // ── Meta maps (keyed by module name string) ─────────────────────
        m_mapSpeedsMaps.insert({"SPI",  &m_mapSpeed_SPI});
        m_mapSpeedsMaps.insert({"I2C",  &m_mapSpeed_I2C});
        m_mapSpeedsMaps.insert({"GPIO", nullptr});           // no preset speeds
        m_mapSpeedsMaps.insert({"UART", &m_mapSpeed_UART});

        m_mapCommandsMaps.insert({"SPI",  &m_mapCmds_SPI});
        m_mapCommandsMaps.insert({"I2C",  &m_mapCmds_I2C});
        m_mapCommandsMaps.insert({"GPIO", &m_mapCmds_GPIO});
        m_mapCommandsMaps.insert({"UART", &m_mapCmds_UART});
    }

    ~FT4232Plugin() = default;

    // ── PluginInterface ────────────────────────────────────────────────

    bool isInitialized()   const override { return m_bIsInitialized;   }
    bool isEnabled()       const override { return m_bIsEnabled;       }
    bool isFaultTolerant() const override { return m_bIsFaultTolerant; }
    bool isPrivileged()    const override { return false;               }

    bool setParams(const PluginDataSet* ps) {
        bool ok = generic_setparams<FT4232Plugin>(this, ps, &m_bIsFaultTolerant, &m_bIsPrivileged);
        return ok && m_LocalSetParams(ps);
    }

    void getParams(PluginDataGet* pg) const {
        generic_getparams<FT4232Plugin>(this, pg);
    }

    bool doDispatch(const std::string& cmd, const std::string& params) const {
        return generic_dispatch<FT4232Plugin>(this, cmd, params);
    }

    const PluginCommandsMap<FT4232Plugin>* getMap() const {
        return &m_mapCmds;
    }

    const std::string& getVersion() const { return m_strVersion; }
    const std::string& getData()    const { return m_strResultData; }
    void resetData()                const { m_strResultData.clear(); }

    bool doInit(void* pvUserData);
    void doEnable()  { m_bIsEnabled = true; }
    void doCleanup();
    void setFaultTolerant() { m_bIsFaultTolerant = true; }

    // ── Module-map accessors (used by generic helpers) ─────────────────

    ModuleCommandsMap<FT4232Plugin>* getModuleCmdsMap(const std::string& m) const;
    ModuleSpeedMap*                  getModuleSpeedsMap(const std::string& m) const;

    /**
     * @brief Apply a speed (Hz) to an open module.
     *
     * Called by generic_module_set_speed() when a preset or raw-Hz value
     * is parsed.  For SPI and I2C the driver must be re-opened with the
     * new clock; for GPIO there is no clock concept; for UART it sets
     * the baud rate.
     */
    bool setModuleSpeed(const std::string& module, size_t hz) const;

    // ── INI accessor (friend for generic_execute_script) ──────────────

    struct IniValues {
        std::string strArtefactsPath;
        uint8_t     u8DeviceIndex    {0};
        // Per-module defaults (overridable via open command)
        FT4232Base::Channel  eSpiChannel  {FT4232Base::Channel::A};
        FT4232Base::Channel  eI2cChannel  {FT4232Base::Channel::A};
        FT4232Base::Channel  eGpioChannel {FT4232Base::Channel::B};
        FT4232Base::Channel  eUartChannel {FT4232Base::Channel::C};
        uint32_t    u32SpiClockHz    {1000000u};
        uint32_t    u32I2cClockHz    {100000u};
        uint8_t     u8I2cAddress     {0x50u};
        uint32_t    u32UartBaudRate  {115200u};
        uint32_t    u32ReadTimeout   {1000u};   ///< ms — used by script execution
        uint32_t    u32ScriptDelay   {0u};      ///< ms — inter-command delay for scripts
    };

    friend const IniValues* getAccessIniValues(const FT4232Plugin& obj);

private:

    // ── Pending open-configuration (set by cfg, applied by open) ──────

    struct SpiPendingCfg {
        uint32_t             clockHz    {1000000u};
        FT4232SPI::SpiMode   mode       {FT4232SPI::SpiMode::Mode0};
        FT4232SPI::BitOrder  bitOrder   {FT4232SPI::BitOrder::MsbFirst};
        uint8_t              csPin      {0x08u};  // ADBUS3
        FT4232SPI::CsPolarity csPolarity{FT4232SPI::CsPolarity::ActiveLow};
        FT4232Base::Channel  channel    {FT4232Base::Channel::A};
    };

    struct I2cPendingCfg {
        uint8_t             address  {0x50u};
        uint32_t            clockHz  {100000u};
        FT4232Base::Channel channel  {FT4232Base::Channel::A};
    };

    struct GpioPendingCfg {
        FT4232Base::Channel  channel    {FT4232Base::Channel::B};
        uint8_t              lowDirMask {0x00u};
        uint8_t              lowValue   {0x00u};
        uint8_t              highDirMask{0x00u};
        uint8_t              highValue  {0x00u};
    };

    // ── UART pending config — use the library's own config struct directly ──
    using UartPendingCfg = FT4232UART::UartConfig;

    // ── Driver instance accessors (guard + log on missing) ────────────

    FT4232SPI*  m_spi()  const;
    FT4232I2C*  m_i2c()  const;
    FT4232GPIO* m_gpio() const;
    FT4232UART* m_uart() const;

    // ── WrRd callbacks ─────────────────────────────────────────────────

    bool m_spi_wrrd_cb(std::span<const uint8_t> req, size_t rdlen) const;
    bool m_i2c_wrrd_cb(std::span<const uint8_t> req, size_t rdlen) const;

    // ── Top-level command handlers ─────────────────────────────────────

    #define FT_PLUGIN_CMD_RECORD(a) \
        bool m_FT4232_##a(const std::string& args) const;
    FT4232_PLUGIN_COMMANDS_CONFIG_TABLE
    #undef FT_PLUGIN_CMD_RECORD

    // Module-level top-level commands (SPI/I2C/GPIO/UART) route generically:
    // The declarations above already cover INFO.

    // ── Per-module subcommand declarations ────────────────────────────

    #define SPI_CMD_RECORD(a)  bool m_handle_spi_##a (const std::string&) const;
    SPI_COMMANDS_CONFIG_TABLE
    #undef SPI_CMD_RECORD

    #define I2C_CMD_RECORD(a)  bool m_handle_i2c_##a (const std::string&) const;
    I2C_COMMANDS_CONFIG_TABLE
    #undef I2C_CMD_RECORD

    #define GPIO_CMD_RECORD(a) bool m_handle_gpio_##a(const std::string&) const;
    GPIO_COMMANDS_CONFIG_TABLE
    #undef GPIO_CMD_RECORD

    #define UART_CMD_RECORD(a) bool m_handle_uart_##a(const std::string&) const;
    UART_COMMANDS_CONFIG_TABLE
    #undef UART_CMD_RECORD

    // ── Member data ───────────────────────────────────────────────────

    std::string m_strVersion;
    mutable std::string m_strResultData;

    bool m_bIsInitialized;
    bool m_bIsEnabled;
    bool m_bIsFaultTolerant;
    bool m_bIsPrivileged;

    IniValues m_sIniValues;

    // Pending configuration (updated by cfg before open)
    mutable SpiPendingCfg  m_sSpiCfg;
    mutable I2cPendingCfg  m_sI2cCfg;
    mutable GpioPendingCfg m_sGpioCfg;
    mutable UartPendingCfg m_sUartCfg;

    // Active driver instances — each manages its own USB handle
    mutable std::unique_ptr<FT4232SPI>  m_pSPI;
    mutable std::unique_ptr<FT4232I2C>  m_pI2C;
    mutable std::unique_ptr<FT4232GPIO> m_pGPIO;
    mutable std::unique_ptr<FT4232UART> m_pUART;

    // Dispatch maps
    PluginCommandsMap<FT4232Plugin>   m_mapCmds;
    SpeedsMapsMap                     m_mapSpeedsMaps;
    CommandsMapsMap<FT4232Plugin>     m_mapCommandsMaps;

    ModuleCommandsMap<FT4232Plugin>   m_mapCmds_SPI;
    ModuleCommandsMap<FT4232Plugin>   m_mapCmds_I2C;
    ModuleCommandsMap<FT4232Plugin>   m_mapCmds_GPIO;
    ModuleCommandsMap<FT4232Plugin>   m_mapCmds_UART;

    ModuleSpeedMap                    m_mapSpeed_SPI;
    ModuleSpeedMap                    m_mapSpeed_I2C;
    ModuleSpeedMap                    m_mapSpeed_UART;

    bool m_LocalSetParams(const PluginDataSet* ps);

    // ── Helpers ────────────────────────────────────────────────────────
    static bool parseChannel(const std::string& s, FT4232Base::Channel& out);
    static bool parseUartParams(const std::string& args, UartPendingCfg& cfg,
                                uint8_t* pDeviceIndexOut = nullptr);
};


