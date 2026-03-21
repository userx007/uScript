#ifndef CH374_PLUGIN_HPP
#define CH374_PLUGIN_HPP

#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "ICommDriver.hpp"
#include "uLogger.hpp"

#include "ch347_generic.hpp"

// CH347 library
#include "uCH347Spi.hpp"
#include "uCH347I2c.hpp"
#include "uCH347Gpio.hpp"
#include "uCH347Jtag.hpp"

// X-macro config tables
#include "spi_config.hpp"
#include "i2c_config.hpp"
#include "gpio_config.hpp"
#include "jtag_config.hpp"

#include <memory>
#include <string>
#include <map>
#include <span>

///////////////////////////////////////////////////////////////////
//                      PLUGIN VERSION                           //
///////////////////////////////////////////////////////////////////

#define CH347_PLUGIN_VERSION  "1.0.0.0"
#define CH347_PLUGIN_NAME     "CH347"


///////////////////////////////////////////////////////////////////
//               TOP-LEVEL PLUGIN COMMANDS                       //
///////////////////////////////////////////////////////////////////

#define CH347_PLUGIN_COMMANDS_CONFIG_TABLE  \
CH347_PLUGIN_CMD_RECORD( INFO )             \
CH347_PLUGIN_CMD_RECORD( SPI  )             \
CH347_PLUGIN_CMD_RECORD( I2C  )             \
CH347_PLUGIN_CMD_RECORD( GPIO )             \
CH347_PLUGIN_CMD_RECORD( JTAG )

///////////////////////////////////////////////////////////////////
//                      PLUGIN CLASS                             //
///////////////////////////////////////////////////////////////////

/**
 * @brief CH347 plugin.
 *
 * The WCH CH347 is a Hi-Speed USB adapter exposing SPI, I2C, GPIO,
 * and JTAG interfaces over a single USB device file descriptor.
 *
 * Each module (SPI, I2C, GPIO, JTAG) opens the device independently
 * via CH347OpenDevice() and can be used simultaneously.
 *
 * The device path/index is configured via the INI file (DEVICE_PATH key).
 *
 * Platform defaults
 * =================
 * Linux  : "/dev/ch34xpis0"  (first WCH VCP interface)
 * Windows: "0"               (decimal device index passed to CH347OpenDevice)
 *
 * On Windows, override via DEVICE_PATH=1 (etc.) in the INI file for
 * devices beyond the first.
 *
 * Usage examples (device path shown for Linux; substitute "0" on Windows):
 *
 *   CH347.SPI  open clock=15000000 mode=0
 *   CH347.SPI  xfer DEADBEEF
 *   CH347.SPI  close
 *
 *   CH347.I2C  open speed=400kHz
 *   CH347.I2C  scan
 *   CH347.I2C  wrrd 5000:2           # write 0x50, read 2 bytes
 *   CH347.I2C  close
 *
 *   CH347.GPIO open
 *   CH347.GPIO dir  output=0x0F      # pins 0-3 as outputs
 *   CH347.GPIO set  pins=0x01        # set pin 0 high
 *   CH347.GPIO read
 *   CH347.GPIO close
 *
 *   CH347.JTAG open rate=2
 *   CH347.JTAG reset
 *   CH347.JTAG write dr DEADBEEF
 *   CH347.JTAG close
 */

class CH347Plugin : public PluginInterface
{

public:

    CH347Plugin()
        : m_strVersion(CH347_PLUGIN_VERSION)
        , m_bIsInitialized(false)
        , m_bIsEnabled(false)
        , m_bIsFaultTolerant(false)
        , m_bIsPrivileged(false)
    {
        // Top-level command map 
        #define CH347_PLUGIN_CMD_RECORD(a) \
            m_mapCmds.insert({#a, &CH347Plugin::m_CH347_##a});
        CH347_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef CH347_PLUGIN_CMD_RECORD

        // SPI 
        #define SPI_CMD_RECORD(a) \
            m_mapCmds_SPI.insert({#a, &CH347Plugin::m_handle_spi_##a});
        SPI_COMMANDS_CONFIG_TABLE
        #undef SPI_CMD_RECORD

        #define SPI_SPEED_RECORD(a,b) m_mapSpeed_SPI.insert({a, static_cast<size_t>(b)});
        SPI_SPEED_CONFIG_TABLE
        #undef SPI_SPEED_RECORD

        // I2C 
        #define I2C_CMD_RECORD(a) \
            m_mapCmds_I2C.insert({#a, &CH347Plugin::m_handle_i2c_##a});
        I2C_COMMANDS_CONFIG_TABLE
        #undef I2C_CMD_RECORD

        #define I2C_SPEED_RECORD(a,b) m_mapSpeed_I2C.insert({a, static_cast<size_t>(b)});
        I2C_SPEED_CONFIG_TABLE
        #undef I2C_SPEED_RECORD

        // GPIO 
        #define GPIO_CMD_RECORD(a) \
            m_mapCmds_GPIO.insert({#a, &CH347Plugin::m_handle_gpio_##a});
        GPIO_COMMANDS_CONFIG_TABLE
        #undef GPIO_CMD_RECORD

        // JTAG 
        #define JTAG_CMD_RECORD(a) \
            m_mapCmds_JTAG.insert({#a, &CH347Plugin::m_handle_jtag_##a});
        JTAG_COMMANDS_CONFIG_TABLE
        #undef JTAG_CMD_RECORD

        // Meta maps 
        m_mapSpeedsMaps.insert({"SPI",  &m_mapSpeed_SPI});
        m_mapSpeedsMaps.insert({"I2C",  &m_mapSpeed_I2C});
        m_mapSpeedsMaps.insert({"GPIO", nullptr});
        m_mapSpeedsMaps.insert({"JTAG", nullptr});

        m_mapCommandsMaps.insert({"SPI",  &m_mapCmds_SPI});
        m_mapCommandsMaps.insert({"I2C",  &m_mapCmds_I2C});
        m_mapCommandsMaps.insert({"GPIO", &m_mapCmds_GPIO});
        m_mapCommandsMaps.insert({"JTAG", &m_mapCmds_JTAG});
    }

    ~CH347Plugin() = default;

    // PluginInterface 

    bool isInitialized()   const override { return m_bIsInitialized;   }
    bool isEnabled()       const override { return m_bIsEnabled;       }
    bool isFaultTolerant() const override { return m_bIsFaultTolerant; }
    bool isPrivileged()    const override { return false;              }

    bool setParams(const PluginDataSet* ps) {
        bool ok = generic_setparams<CH347Plugin>(this, ps, &m_bIsFaultTolerant, &m_bIsPrivileged);
        return ok && m_LocalSetParams(ps);
    }

    void getParams(PluginDataGet* pg) const {
        generic_getparams<CH347Plugin>(this, pg);
    }

    bool doDispatch(const std::string& cmd, const std::string& params) const {
        return generic_dispatch<CH347Plugin>(this, cmd, params);
    }

    const PluginCommandsMap<CH347Plugin>* getMap() const { return &m_mapCmds; }

    const std::string& getVersion() const { return m_strVersion; }
    const std::string& getData()    const { return m_strResultData; }
    void resetData()                const { m_strResultData.clear(); }

    bool doInit(void* pvUserData);
    void doEnable()  { m_bIsEnabled = true; }
    void doCleanup();
    void setFaultTolerant() { m_bIsFaultTolerant = true; }

    // Module-map accessors 

    ModuleCommandsMap<CH347Plugin>* getModuleCmdsMap(const std::string& m) const;
    ModuleSpeedMap*                 getModuleSpeedsMap(const std::string& m) const;

    bool setModuleSpeed(const std::string& module, size_t hz) const;

    // INI accessor 

    struct IniValues {
        std::string  strArtefactsPath;
        std::string  strDevicePath     {
#ifdef _WIN32
            "0"                  ///< Windows: decimal device index for CH347OpenDevice
#else
            "/dev/ch34xpis0"     ///< Linux: VCP device node
#endif
        };
        uint32_t     u32SpiClockHz     {1000000u};
        I2cSpeed     eI2cSpeed         {I2cSpeed::Fast};
        uint8_t      u8I2cAddress      {0x50u};
        uint8_t      u8JtagClockRate   {2u};
        uint32_t     u32ReadTimeout    {5000u};
        uint32_t     u32ScriptDelay    {0u};
    };

    friend const IniValues* getAccessIniValues(const CH347Plugin& obj);

private:

    // Pending configuration structs 

    struct SpiPendingCfg {
        mSpiCfgS     cfg            {};
        SpiXferOptions xferOpts     {};
        bool         cfgDirty       {true};
    };

    struct I2cPendingCfg {
        I2cSpeed     speed          {I2cSpeed::Fast};
        uint8_t      address        {0x50u};
    };

    struct GpioPendingCfg {
        uint8_t      enableMask     {0xFFu};
        uint8_t      dirMask        {0x00u};  // default all inputs
        uint8_t      dataValue      {0x00u};
    };

    struct JtagPendingCfg {
        uint8_t      clockRate      {2u};
        JtagRegister lastReg        {JtagRegister::DR};
    };

    // Driver instance accessors 

    CH347SPI*  m_spi()  const;
    CH347I2C*  m_i2c()  const;
    CH347GPIO* m_gpio() const;
    CH347JTAG* m_jtag() const;

    // WrRd callbacks 

    bool m_spi_wrrd_cb(std::span<const uint8_t> req, size_t rdlen) const;
    bool m_i2c_wrrd_cb(std::span<const uint8_t> req, size_t rdlen) const;

    // Top-level command handlers 

    #define CH347_PLUGIN_CMD_RECORD(a) \
        bool m_CH347_##a(const std::string& args) const;
    CH347_PLUGIN_COMMANDS_CONFIG_TABLE
    #undef CH347_PLUGIN_CMD_RECORD

    // Per-module subcommand declarations 

    #define SPI_CMD_RECORD(a)  bool m_handle_spi_##a (const std::string&) const;
    SPI_COMMANDS_CONFIG_TABLE
    #undef SPI_CMD_RECORD

    #define I2C_CMD_RECORD(a)  bool m_handle_i2c_##a (const std::string&) const;
    I2C_COMMANDS_CONFIG_TABLE
    #undef I2C_CMD_RECORD

    #define GPIO_CMD_RECORD(a) bool m_handle_gpio_##a(const std::string&) const;
    GPIO_COMMANDS_CONFIG_TABLE
    #undef GPIO_CMD_RECORD

    #define JTAG_CMD_RECORD(a) bool m_handle_jtag_##a(const std::string&) const;
    JTAG_COMMANDS_CONFIG_TABLE
    #undef JTAG_CMD_RECORD

    // Member data 

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
    mutable JtagPendingCfg m_sJtagCfg;

    mutable std::unique_ptr<CH347SPI>  m_pSPI;
    mutable std::unique_ptr<CH347I2C>  m_pI2C;
    mutable std::unique_ptr<CH347GPIO> m_pGPIO;
    mutable std::unique_ptr<CH347JTAG> m_pJTAG;

    PluginCommandsMap<CH347Plugin>   m_mapCmds;
    SpeedsMapsMap                    m_mapSpeedsMaps;
    CommandsMapsMap<CH347Plugin>     m_mapCommandsMaps;

    ModuleCommandsMap<CH347Plugin>   m_mapCmds_SPI;
    ModuleCommandsMap<CH347Plugin>   m_mapCmds_I2C;
    ModuleCommandsMap<CH347Plugin>   m_mapCmds_GPIO;
    ModuleCommandsMap<CH347Plugin>   m_mapCmds_JTAG;

    ModuleSpeedMap                   m_mapSpeed_SPI;
    ModuleSpeedMap                   m_mapSpeed_I2C;

    bool m_LocalSetParams(const PluginDataSet* ps);

    //  Parse helpers 
    static bool parseI2cSpeed(const std::string& s, I2cSpeed& out);
    static bool parseSpiParams(const std::string& args,
                               SpiPendingCfg& cfg,
                               std::string* pDevPathOut = nullptr);
    static bool parseI2cParams(const std::string& args,
                               I2cPendingCfg& cfg,
                               std::string* pDevPathOut = nullptr);
};

#endif // CH374_PLUGIN_HPP
