#pragma once

#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "ICommDriver.hpp"
#include "uLogger.hpp"

#include "ft232h_generic.hpp"

// FT232H driver classes
#include "uFT232HSPI.hpp"
#include "uFT232HI2C.hpp"
#include "uFT232HGPIO.hpp"

// X-macro config tables
#include "spi_config.hpp"
#include "i2c_config.hpp"
#include "gpio_config.hpp"

#include <memory>
#include <string>
#include <map>
#include <span>

///////////////////////////////////////////////////////////////////
//                      PLUGIN VERSION                           //
///////////////////////////////////////////////////////////////////

#define FT232H_PLUGIN_VERSION  "1.0.0.0"

///////////////////////////////////////////////////////////////////
//               TOP-LEVEL PLUGIN COMMANDS                       //
///////////////////////////////////////////////////////////////////

#define FT232H_PLUGIN_COMMANDS_CONFIG_TABLE   \
FT232H_PLUGIN_CMD_RECORD( INFO )              \
FT232H_PLUGIN_CMD_RECORD( SPI  )              \
FT232H_PLUGIN_CMD_RECORD( I2C  )              \
FT232H_PLUGIN_CMD_RECORD( GPIO )

///////////////////////////////////////////////////////////////////
//                      PLUGIN CLASS                             //
///////////////////////////////////////////////////////////////////

/**
 * @brief FT232H plugin.
 *
 * Exposes three independent FT232H driver modules — SPI, I2C, GPIO —
 * through the same string-command dispatch mechanism used by all other
 * FTDI plugins in this project.
 *
 * The FT232H has a single MPSSE channel (unlike FT2232H or FT4232H), so
 * there is no channel= parameter on any command.  The three modules each
 * own their own USB handle and can be opened simultaneously only if
 * multiple FT232H chips are present (use device=N to select).  On a
 * single chip, open at most one module at a time.
 *
 * Usage examples:
 *
 *   FT232H.SPI  open clock=10000000 mode=0
 *   FT232H.SPI  xfer DEADBEEF
 *   FT232H.SPI  close
 *
 *   FT232H.I2C  open addr=0x50 clock=400000
 *   FT232H.I2C  scan
 *   FT232H.I2C  wrrd 0000:2
 *   FT232H.I2C  close
 *
 *   FT232H.GPIO open lowdir=0xFF lowval=0x00
 *   FT232H.GPIO set  low 0x01
 *   FT232H.GPIO read low
 *   FT232H.GPIO close
 */
class FT232HPlugin : public PluginInterface
{
public:

    FT232HPlugin()
        : m_strVersion(FT232H_PLUGIN_VERSION)
        , m_bIsInitialized(false)
        , m_bIsEnabled(false)
        , m_bIsFaultTolerant(false)
        , m_bIsPrivileged(false)
    {
        // ── Top-level command map ───────────────────────────────────────
        #define FT232H_PLUGIN_CMD_RECORD(a) \
            m_mapCmds.insert({#a, &FT232HPlugin::m_FT232H_##a});
        FT232H_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef FT232H_PLUGIN_CMD_RECORD

        // ── SPI ────────────────────────────────────────────────────────
        #define SPI_CMD_RECORD(a) \
            m_mapCmds_SPI.insert({#a, &FT232HPlugin::m_handle_spi_##a});
        SPI_COMMANDS_CONFIG_TABLE
        #undef SPI_CMD_RECORD

        #define SPI_SPEED_RECORD(a,b) m_mapSpeed_SPI.insert({a, static_cast<size_t>(b)});
        SPI_SPEED_CONFIG_TABLE
        #undef SPI_SPEED_RECORD

        // ── I2C ────────────────────────────────────────────────────────
        #define I2C_CMD_RECORD(a) \
            m_mapCmds_I2C.insert({#a, &FT232HPlugin::m_handle_i2c_##a});
        I2C_COMMANDS_CONFIG_TABLE
        #undef I2C_CMD_RECORD

        #define I2C_SPEED_RECORD(a,b) m_mapSpeed_I2C.insert({a, static_cast<size_t>(b)});
        I2C_SPEED_CONFIG_TABLE
        #undef I2C_SPEED_RECORD

        // ── GPIO ───────────────────────────────────────────────────────
        #define GPIO_CMD_RECORD(a) \
            m_mapCmds_GPIO.insert({#a, &FT232HPlugin::m_handle_gpio_##a});
        GPIO_COMMANDS_CONFIG_TABLE
        #undef GPIO_CMD_RECORD

        // ── Meta maps ──────────────────────────────────────────────────
        m_mapSpeedsMaps.insert({"SPI",  &m_mapSpeed_SPI});
        m_mapSpeedsMaps.insert({"I2C",  &m_mapSpeed_I2C});
        m_mapSpeedsMaps.insert({"GPIO", nullptr});

        m_mapCommandsMaps.insert({"SPI",  &m_mapCmds_SPI});
        m_mapCommandsMaps.insert({"I2C",  &m_mapCmds_I2C});
        m_mapCommandsMaps.insert({"GPIO", &m_mapCmds_GPIO});
    }

    ~FT232HPlugin() = default;

    // ── PluginInterface ────────────────────────────────────────────────

    bool isInitialized()   const override { return m_bIsInitialized;   }
    bool isEnabled()       const override { return m_bIsEnabled;       }
    bool isFaultTolerant() const override { return m_bIsFaultTolerant; }
    bool isPrivileged()    const override { return false;               }

    bool setParams(const PluginDataSet* ps) {
        bool ok = generic_setparams<FT232HPlugin>(this, ps, &m_bIsFaultTolerant, &m_bIsPrivileged);
        return ok && m_LocalSetParams(ps);
    }

    void getParams(PluginDataGet* pg) const {
        generic_getparams<FT232HPlugin>(this, pg);
    }

    bool doDispatch(const std::string& cmd, const std::string& params) const {
        return generic_dispatch<FT232HPlugin>(this, cmd, params);
    }

    const PluginCommandsMap<FT232HPlugin>* getMap() const { return &m_mapCmds; }

    const std::string& getVersion() const { return m_strVersion; }
    const std::string& getData()    const { return m_strResultData; }
    void resetData()                const { m_strResultData.clear(); }

    bool doInit(void* pvUserData);
    void doEnable()  { m_bIsEnabled = true; }
    void doCleanup();
    void setFaultTolerant() { m_bIsFaultTolerant = true; }

    // ── Module-map accessors ────────────────────────────────────────────

    ModuleCommandsMap<FT232HPlugin>* getModuleCmdsMap(const std::string& m) const;
    ModuleSpeedMap*                  getModuleSpeedsMap(const std::string& m) const;

    /**
     * @brief Apply a speed (Hz) to an open module.
     *
     * Re-opens the driver at the new clock if currently open.
     */
    bool setModuleSpeed(const std::string& module, size_t hz) const;

    // ── INI accessor ───────────────────────────────────────────────────

    struct IniValues {
        std::string strArtefactsPath;
        uint8_t     u8DeviceIndex  {0};
        uint32_t    u32SpiClockHz  {1000000u};
        uint32_t    u32I2cClockHz  {100000u};
        uint8_t     u8I2cAddress   {0x50u};
    };

    friend const IniValues* getAccessIniValues(const FT232HPlugin& obj);

private:

    // ── Pending configuration ─────────────────────────────────────────

    struct SpiPendingCfg {
        uint32_t               clockHz    {1000000u};
        FT232HSPI::SpiMode     mode       {FT232HSPI::SpiMode::Mode0};
        FT232HSPI::BitOrder    bitOrder   {FT232HSPI::BitOrder::MsbFirst};
        uint8_t                csPin      {0x08u};
        FT232HSPI::CsPolarity  csPolarity {FT232HSPI::CsPolarity::ActiveLow};
    };

    struct I2cPendingCfg {
        uint8_t  address {0x50u};
        uint32_t clockHz {100000u};
    };

    struct GpioPendingCfg {
        uint8_t lowDirMask  {0x00u};
        uint8_t lowValue    {0x00u};
        uint8_t highDirMask {0x00u};
        uint8_t highValue   {0x00u};
    };

    // ── Driver instance accessors ──────────────────────────────────────

    FT232HSPI*  m_spi()  const;
    FT232HI2C*  m_i2c()  const;
    FT232HGPIO* m_gpio() const;

    // ── WrRd callbacks ─────────────────────────────────────────────────

    bool m_spi_wrrd_cb(std::span<const uint8_t> req, size_t rdlen) const;
    bool m_i2c_wrrd_cb(std::span<const uint8_t> req, size_t rdlen) const;

    // ── Top-level command handlers ─────────────────────────────────────

    #define FT232H_PLUGIN_CMD_RECORD(a) \
        bool m_FT232H_##a(const std::string& args) const;
    FT232H_PLUGIN_COMMANDS_CONFIG_TABLE
    #undef FT232H_PLUGIN_CMD_RECORD

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

    // ── Member data ───────────────────────────────────────────────────

    std::string m_strVersion;
    mutable std::string m_strResultData;

    bool m_bIsInitialized;
    bool m_bIsEnabled;
    bool m_bIsFaultTolerant;
    bool m_bIsPrivileged;

    IniValues m_sIniValues;

    mutable SpiPendingCfg  m_sSpiCfg;
    mutable I2cPendingCfg  m_sI2cCfg;
    mutable GpioPendingCfg m_sGpioCfg;

    mutable std::unique_ptr<FT232HSPI>  m_pSPI;
    mutable std::unique_ptr<FT232HI2C>  m_pI2C;
    mutable std::unique_ptr<FT232HGPIO> m_pGPIO;

    PluginCommandsMap<FT232HPlugin>   m_mapCmds;
    SpeedsMapsMap                     m_mapSpeedsMaps;
    CommandsMapsMap<FT232HPlugin>     m_mapCommandsMaps;

    ModuleCommandsMap<FT232HPlugin>   m_mapCmds_SPI;
    ModuleCommandsMap<FT232HPlugin>   m_mapCmds_I2C;
    ModuleCommandsMap<FT232HPlugin>   m_mapCmds_GPIO;

    ModuleSpeedMap                    m_mapSpeed_SPI;
    ModuleSpeedMap                    m_mapSpeed_I2C;

    bool m_LocalSetParams(const PluginDataSet* ps);

    // ── Parse helpers ──────────────────────────────────────────────────
    static bool parseSpiParams(const std::string& args,
                               SpiPendingCfg& cfg,
                               uint8_t* pDeviceIndexOut = nullptr);
};
