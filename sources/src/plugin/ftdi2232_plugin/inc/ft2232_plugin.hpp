#pragma once

#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "ICommDriver.hpp"
#include "uLogger.hpp"

#include "ft2232_generic.hpp"

// FT2232 library
#include "uFT2232SPI.hpp"
#include "uFT2232I2C.hpp"
#include "uFT2232GPIO.hpp"

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

#define FT2232_PLUGIN_VERSION  "1.0.0.0"

///////////////////////////////////////////////////////////////////
//               TOP-LEVEL PLUGIN COMMANDS                       //
///////////////////////////////////////////////////////////////////

#define FT2232_PLUGIN_COMMANDS_CONFIG_TABLE  \
FT2_PLUGIN_CMD_RECORD( INFO )                \
FT2_PLUGIN_CMD_RECORD( SPI  )                \
FT2_PLUGIN_CMD_RECORD( I2C  )                \
FT2_PLUGIN_CMD_RECORD( GPIO )

///////////////////////////////////////////////////////////////////
//                      PLUGIN CLASS                             //
///////////////////////////////////////////////////////////////////

/**
 * @brief FT2232 plugin.
 *
 * Supports both the FT2232H (high-speed, dual MPSSE, 60 MHz) and the
 * FT2232D/C/L (full-speed, single MPSSE on channel A, 6 MHz).
 *
 * The variant is set once in the INI file (or per-command via variant=H|D).
 * It flows into every open() call and determines the clock base, which
 * PID is searched for on USB enumeration, and which channels are legal.
 *
 * Three independent modules — SPI, I2C, GPIO — each own their own USB
 * handle and can run simultaneously on different channels.
 *
 * Usage examples:
 *
 *   # FT2232H (default) — channels A and B available
 *   FT2232.SPI  open variant=H clock=10000000 mode=0 channel=A
 *   FT2232.SPI  xfer DEADBEEF
 *   FT2232.SPI  close
 *
 *   # FT2232D — channel A only, max 3 MHz SPI
 *   FT2232.SPI  open variant=D clock=1000000 channel=A
 *   FT2232.I2C  open variant=D addr=0x50 clock=100000
 *   FT2232.I2C  scan
 *
 *   FT2232.GPIO open variant=H channel=B lowdir=0xFF
 *   FT2232.GPIO set  low 0x01
 *   FT2232.GPIO read low
 *   FT2232.GPIO close
 */
class FT2232Plugin : public PluginInterface
{
public:

    FT2232Plugin()
        : m_strVersion(FT2232_PLUGIN_VERSION)
        , m_bIsInitialized(false)
        , m_bIsEnabled(false)
        , m_bIsFaultTolerant(false)
        , m_bIsPrivileged(false)
    {
        // ── Top-level command map ───────────────────────────────────────
        #define FT2_PLUGIN_CMD_RECORD(a) \
            m_mapCmds.insert({#a, &FT2232Plugin::m_FT2232_##a});
        FT2232_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef FT2_PLUGIN_CMD_RECORD

        // ── SPI ────────────────────────────────────────────────────────
        #define SPI_CMD_RECORD(a) \
            m_mapCmds_SPI.insert({#a, &FT2232Plugin::m_handle_spi_##a});
        SPI_COMMANDS_CONFIG_TABLE
        #undef SPI_CMD_RECORD

        #define SPI_SPEED_RECORD(a,b) m_mapSpeed_SPI.insert({a, static_cast<size_t>(b)});
        SPI_SPEED_CONFIG_TABLE
        #undef SPI_SPEED_RECORD

        // ── I2C ────────────────────────────────────────────────────────
        #define I2C_CMD_RECORD(a) \
            m_mapCmds_I2C.insert({#a, &FT2232Plugin::m_handle_i2c_##a});
        I2C_COMMANDS_CONFIG_TABLE
        #undef I2C_CMD_RECORD

        #define I2C_SPEED_RECORD(a,b) m_mapSpeed_I2C.insert({a, static_cast<size_t>(b)});
        I2C_SPEED_CONFIG_TABLE
        #undef I2C_SPEED_RECORD

        // ── GPIO ───────────────────────────────────────────────────────
        #define GPIO_CMD_RECORD(a) \
            m_mapCmds_GPIO.insert({#a, &FT2232Plugin::m_handle_gpio_##a});
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

    ~FT2232Plugin() = default;

    // ── PluginInterface ────────────────────────────────────────────────

    bool isInitialized()   const override { return m_bIsInitialized;   }
    bool isEnabled()       const override { return m_bIsEnabled;       }
    bool isFaultTolerant() const override { return m_bIsFaultTolerant; }
    bool isPrivileged()    const override { return false;               }

    bool setParams(const PluginDataSet* ps) {
        bool ok = generic_setparams<FT2232Plugin>(this, ps, &m_bIsFaultTolerant, &m_bIsPrivileged);
        return ok && m_LocalSetParams(ps);
    }

    void getParams(PluginDataGet* pg) const {
        generic_getparams<FT2232Plugin>(this, pg);
    }

    bool doDispatch(const std::string& cmd, const std::string& params) const {
        return generic_dispatch<FT2232Plugin>(this, cmd, params);
    }

    const PluginCommandsMap<FT2232Plugin>* getMap() const { return &m_mapCmds; }

    const std::string& getVersion() const { return m_strVersion; }
    const std::string& getData()    const { return m_strResultData; }
    void resetData()                const { m_strResultData.clear(); }

    bool doInit(void* pvUserData);
    void doEnable()  { m_bIsEnabled = true; }
    void doCleanup();
    void setFaultTolerant() { m_bIsFaultTolerant = true; }

    // ── Module-map accessors ────────────────────────────────────────────

    ModuleCommandsMap<FT2232Plugin>* getModuleCmdsMap(const std::string& m) const;
    ModuleSpeedMap*                  getModuleSpeedsMap(const std::string& m) const;

    /**
     * @brief Apply a speed (Hz) to an open module.
     *
     * Re-opens the driver at the new clock if currently open.
     * For FT2232D, speeds above the hardware limit return false.
     */
    bool setModuleSpeed(const std::string& module, size_t hz) const;

    // ── INI accessor ───────────────────────────────────────────────────

    struct IniValues {
        std::string          strArtefactsPath;
        uint8_t              u8DeviceIndex    {0};
        FT2232Base::Variant  eDefaultVariant  {FT2232Base::Variant::FT2232H};
        // Per-module defaults
        FT2232Base::Channel  eSpiChannel      {FT2232Base::Channel::A};
        FT2232Base::Channel  eI2cChannel      {FT2232Base::Channel::A};
        FT2232Base::Channel  eGpioChannel     {FT2232Base::Channel::B};
        uint32_t             u32SpiClockHz    {1000000u};
        uint32_t             u32I2cClockHz    {100000u};
        uint8_t              u8I2cAddress     {0x50u};
    };

    friend const IniValues* getAccessIniValues(const FT2232Plugin& obj);

private:

    // ── Pending configuration (updated by cfg, applied by open) ───────

    struct SpiPendingCfg {
        uint32_t               clockHz    {1000000u};
        FT2232SPI::SpiMode     mode       {FT2232SPI::SpiMode::Mode0};
        FT2232SPI::BitOrder    bitOrder   {FT2232SPI::BitOrder::MsbFirst};
        uint8_t                csPin      {0x08u};
        FT2232SPI::CsPolarity  csPolarity {FT2232SPI::CsPolarity::ActiveLow};
        FT2232Base::Variant    variant    {FT2232Base::Variant::FT2232H};
        FT2232Base::Channel    channel    {FT2232Base::Channel::A};
    };

    struct I2cPendingCfg {
        uint8_t              address  {0x50u};
        uint32_t             clockHz  {100000u};
        FT2232Base::Variant  variant  {FT2232Base::Variant::FT2232H};
        FT2232Base::Channel  channel  {FT2232Base::Channel::A};
    };

    struct GpioPendingCfg {
        FT2232Base::Variant  variant    {FT2232Base::Variant::FT2232H};
        FT2232Base::Channel  channel    {FT2232Base::Channel::B};
        uint8_t              lowDirMask {0x00u};
        uint8_t              lowValue   {0x00u};
        uint8_t              highDirMask{0x00u};
        uint8_t              highValue  {0x00u};
    };

    // ── Driver instance accessors ──────────────────────────────────────

    FT2232SPI*  m_spi()  const;
    FT2232I2C*  m_i2c()  const;
    FT2232GPIO* m_gpio() const;

    // ── WrRd callbacks ─────────────────────────────────────────────────

    bool m_spi_wrrd_cb(std::span<const uint8_t> req, size_t rdlen) const;
    bool m_i2c_wrrd_cb(std::span<const uint8_t> req, size_t rdlen) const;

    // ── Top-level command handlers ─────────────────────────────────────

    #define FT2_PLUGIN_CMD_RECORD(a) \
        bool m_FT2232_##a(const std::string& args) const;
    FT2232_PLUGIN_COMMANDS_CONFIG_TABLE
    #undef FT2_PLUGIN_CMD_RECORD

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

    mutable std::unique_ptr<FT2232SPI>  m_pSPI;
    mutable std::unique_ptr<FT2232I2C>  m_pI2C;
    mutable std::unique_ptr<FT2232GPIO> m_pGPIO;

    PluginCommandsMap<FT2232Plugin>   m_mapCmds;
    SpeedsMapsMap                     m_mapSpeedsMaps;
    CommandsMapsMap<FT2232Plugin>     m_mapCommandsMaps;

    ModuleCommandsMap<FT2232Plugin>   m_mapCmds_SPI;
    ModuleCommandsMap<FT2232Plugin>   m_mapCmds_I2C;
    ModuleCommandsMap<FT2232Plugin>   m_mapCmds_GPIO;

    ModuleSpeedMap                    m_mapSpeed_SPI;
    ModuleSpeedMap                    m_mapSpeed_I2C;

    bool m_LocalSetParams(const PluginDataSet* ps);

    // ── Parse helpers ──────────────────────────────────────────────────
    static bool parseChannel(const std::string& s, FT2232Base::Channel& out);
    static bool parseVariant (const std::string& s, FT2232Base::Variant& out);

    static bool parseSpiKV(const std::string& key,
                           const std::string& val,
                           SpiPendingCfg& cfg);

    static bool parseSpiParams(const std::string& args,
                               SpiPendingCfg& cfg,
                               uint8_t* pDeviceIndexOut = nullptr);

    /**
     * @brief Validate requested Hz against the FT2232D 3 MHz SPI / 400 kHz I2C cap.
     *
     * Logs an error and returns false if variant is FT2232D and hz exceeds limit.
     * @param protocol  "SPI" or "I2C" — determines which limit applies
     */
    static bool checkVariantSpeedLimit(FT2232Base::Variant v,
                                        const std::string& protocol,
                                        uint32_t hz);
};
