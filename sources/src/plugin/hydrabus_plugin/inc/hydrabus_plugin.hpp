#ifndef HYDRABUS_PLUGIN_HPP
#define HYDRABUS_PLUGIN_HPP

#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "ICommDriver.hpp"
#include "uUart.hpp"
#include "uLogger.hpp"

#include "hydrabus_generic.hpp"

// HydraHAL
#include "HydraHAL.hpp"

// X-macro config tables
#include "mode_config.hpp"
#include "spi_config.hpp"
#include "i2c_config.hpp"
#include "protocol_configs.hpp"

#include <memory>
#include <string>
#include <map>
#include <span>
#include <optional>
#include <variant>

///////////////////////////////////////////////////////////////////
//                      PLUGIN VERSION                           //
///////////////////////////////////////////////////////////////////

#define HYDRABUS_PLUGIN_VERSION  "1.0.0.0"
#define HYDRABUS_PLUGIN_NAME     "HYDRABUS"

///////////////////////////////////////////////////////////////////
//               TOP-LEVEL PLUGIN COMMANDS                       //
///////////////////////////////////////////////////////////////////

#define HYDRABUS_PLUGIN_COMMANDS_CONFIG_TABLE_STD  \
HB_PLUGIN_CMD_RECORD( INFO )                       \
HB_PLUGIN_CMD_RECORD( MODE )

//  One entry per protocol module — the dispatcher routes to the
//  per-module command map automatically.
#define HYDRABUS_PLUGIN_COMMANDS_CONFIG_TABLE_CMDS \
HB_PLUGIN_CMD_RECORD( SPI       )                  \
HB_PLUGIN_CMD_RECORD( I2C       )                  \
HB_PLUGIN_CMD_RECORD( UART      )                  \
HB_PLUGIN_CMD_RECORD( ONEWIRE   )                  \
HB_PLUGIN_CMD_RECORD( RAWWIRE   )                  \
HB_PLUGIN_CMD_RECORD( SWD       )                  \
HB_PLUGIN_CMD_RECORD( SMARTCARD )                  \
HB_PLUGIN_CMD_RECORD( NFC       )                  \
HB_PLUGIN_CMD_RECORD( MMC       )                  \
HB_PLUGIN_CMD_RECORD( SDIO      )

///////////////////////////////////////////////////////////////////
//                      PLUGIN CLASS                             //
///////////////////////////////////////////////////////////////////

/**
 * @brief HydraBus plugin.
 *
 * Implements the PluginInterface and exposes every HydraHAL protocol
 * through a string-command dispatch mechanism that mirrors the
 * BusPirate plugin architecture.
 *
 * Usage (example sequence):
 *   HYDRABUS.MODE  spi
 *   HYDRABUS.SPI   speed 10MHz
 *   HYDRABUS.SPI   cfg   polarity=0 phase=1
 *   HYDRABUS.SPI   cs    en
 *   HYDRABUS.SPI   wrrd  DEADBEEF:4
 *   HYDRABUS.SPI   cs    dis
 *   HYDRABUS.MODE  bbio
 */
class HydrabusPlugin : public PluginInterface
{
public:

    HydrabusPlugin()
        : m_strVersion(HYDRABUS_PLUGIN_VERSION)
        , m_bIsInitialized(false)
        , m_bIsEnabled(false)
        , m_bIsFaultTolerant(false)
        , m_bIsPrivileged(false)
        , m_eMode(Mode::None)
    {
        // Top-level commands 
        #define HB_PLUGIN_CMD_RECORD(a) \
            m_mapCmds.insert({#a, &HydrabusPlugin::m_Hydrabus_##a});
        HYDRABUS_PLUGIN_COMMANDS_CONFIG_TABLE_STD
        #undef HB_PLUGIN_CMD_RECORD

        #define HB_PLUGIN_CMD_RECORD(a) \
            m_mapCmds.insert({#a, &HydrabusPlugin::m_Hydrabus_##a});
        HYDRABUS_PLUGIN_COMMANDS_CONFIG_TABLE_CMDS
        #undef HB_PLUGIN_CMD_RECORD

        // Mode table 
        #define MODE_CMD_RECORD(a,b,c,d) { \
            mode_s s{b,c,std::string(#d)}; \
            m_mapModes.insert({#a, s}); }
        MODE_COMMANDS_CONFIG_TABLE
        #undef MODE_CMD_RECORD

        // SPI 
        #define SPI_CMD_RECORD(a) \
            m_mapCmds_SPI.insert({#a, &HydrabusPlugin::m_handle_spi_##a});
        SPI_COMMANDS_CONFIG_TABLE
        #undef SPI_CMD_RECORD

        #define SPI_SPEED_RECORD(a,b) m_mapSpeed_SPI.insert({a, b});
        SPI_SPEED_CONFIG_TABLE
        #undef SPI_SPEED_RECORD

        // I2C 
        #define I2C_CMD_RECORD(a) \
            m_mapCmds_I2C.insert({#a, &HydrabusPlugin::m_handle_i2c_##a});
        I2C_COMMANDS_CONFIG_TABLE
        #undef I2C_CMD_RECORD

        #define I2C_SPEED_RECORD(a,b) m_mapSpeed_I2C.insert({a, b});
        I2C_SPEED_CONFIG_TABLE
        #undef I2C_SPEED_RECORD

        // UART 
        #define UART_CMD_RECORD(a) \
            m_mapCmds_UART.insert({#a, &HydrabusPlugin::m_handle_uart_##a});
        UART_COMMANDS_CONFIG_TABLE
        #undef UART_CMD_RECORD

        // OneWire 
        #define ONEWIRE_CMD_RECORD(a) \
            m_mapCmds_ONEWIRE.insert({#a, &HydrabusPlugin::m_handle_onewire_##a});
        ONEWIRE_COMMANDS_CONFIG_TABLE
        #undef ONEWIRE_CMD_RECORD

        // RawWire 
        #define RAWWIRE_CMD_RECORD(a) \
            m_mapCmds_RAWWIRE.insert({#a, &HydrabusPlugin::m_handle_rawwire_##a});
        RAWWIRE_COMMANDS_CONFIG_TABLE
        #undef RAWWIRE_CMD_RECORD

        #define RAWWIRE_SPEED_RECORD(a,b) m_mapSpeed_RAWWIRE.insert({a, b});
        RAWWIRE_SPEED_CONFIG_TABLE
        #undef RAWWIRE_SPEED_RECORD

        // SWD 
        #define SWD_CMD_RECORD(a) \
            m_mapCmds_SWD.insert({#a, &HydrabusPlugin::m_handle_swd_##a});
        SWD_COMMANDS_CONFIG_TABLE
        #undef SWD_CMD_RECORD

        // Smartcard 
        #define SMARTCARD_CMD_RECORD(a) \
            m_mapCmds_SMARTCARD.insert({#a, &HydrabusPlugin::m_handle_smartcard_##a});
        SMARTCARD_COMMANDS_CONFIG_TABLE
        #undef SMARTCARD_CMD_RECORD

        // NFC 
        #define NFC_CMD_RECORD(a) \
            m_mapCmds_NFC.insert({#a, &HydrabusPlugin::m_handle_nfc_##a});
        NFC_COMMANDS_CONFIG_TABLE
        #undef NFC_CMD_RECORD

        // MMC 
        #define MMC_CMD_RECORD(a) \
            m_mapCmds_MMC.insert({#a, &HydrabusPlugin::m_handle_mmc_##a});
        MMC_COMMANDS_CONFIG_TABLE
        #undef MMC_CMD_RECORD

        // SDIO 
        #define SDIO_CMD_RECORD(a) \
            m_mapCmds_SDIO.insert({#a, &HydrabusPlugin::m_handle_sdio_##a});
        SDIO_COMMANDS_CONFIG_TABLE
        #undef SDIO_CMD_RECORD

        // Meta maps (speeds / commands keyed by module name) 
        #define HB_PLUGIN_CMD_RECORD(a) \
            m_mapSpeedsMaps.insert({#a, &m_mapSpeed_##a});
        HYDRABUS_PLUGIN_COMMANDS_CONFIG_TABLE_CMDS
        #undef HB_PLUGIN_CMD_RECORD

        #define HB_PLUGIN_CMD_RECORD(a) \
            m_mapCommandsMaps.insert({#a, &m_mapCmds_##a});
        HYDRABUS_PLUGIN_COMMANDS_CONFIG_TABLE_CMDS
        #undef HB_PLUGIN_CMD_RECORD
    }

    ~HydrabusPlugin() = default;

    // PluginInterface 

    bool isInitialized()   const override { return m_bIsInitialized;   }
    bool isEnabled()       const override { return m_bIsEnabled;       }
    bool isFaultTolerant() const override { return m_bIsFaultTolerant; }
    bool isPrivileged()    const override { return false;               }

    bool setParams(const PluginDataSet* ps) {
        bool ok = generic_setparams<HydrabusPlugin>(this, ps, &m_bIsFaultTolerant, &m_bIsPrivileged);
        return ok && m_LocalSetParams(ps);
    }

    void getParams(PluginDataGet* pg) const {
        generic_getparams<HydrabusPlugin>(this, pg);
    }

    bool doDispatch(const std::string& cmd, const std::string& params) const {
        return generic_dispatch<HydrabusPlugin>(this, cmd, params);
    }

    const PluginCommandsMap<HydrabusPlugin>* getMap() const {
        return &m_mapCmds;
    }

    const std::string& getVersion() const { return m_strVersion; }
    const std::string& getData()    const { return m_strResultData; }
    void resetData()                const { m_strResultData.clear(); }

    bool doInit(void* pvUserData);
    void doEnable()  { m_bIsEnabled = true; }
    void doCleanup();
    void setFaultTolerant() { m_bIsFaultTolerant = true; }

    // Module-map accessors (used by generic helpers) 

    ModuleCommandsMap<HydrabusPlugin>* getModuleCmdsMap(const std::string& m) const;
    ModuleSpeedMap*                    getModuleSpeedsMap(const std::string& m) const;

    /**
     * @brief Called by generic_module_set_speed to apply a speed index.
     *        Each protocol interprets the index according to its own enum.
     */
    bool setModuleSpeed(const std::string& module, size_t index) const;

    // INI accessor (friend for generic_execute_script) 

    struct IniValues {
        std::string strArtefactsPath;
        std::string strUartPort;
        uint32_t    u32UartBaudrate      {0};
        uint32_t    u32ReadTimeout       {0};
        uint32_t    u32WriteTimeout      {0};
        uint32_t    u32UartReadBufferSize{0};
        uint32_t    u32ScriptDelay       {0};
    };

    friend const IniValues* getAccessIniValues(const HydrabusPlugin& obj);
    friend bool getEnabledStatus(const HydrabusPlugin& obj);

    // ── UART driver — public so generic_execute_script can alias it ──
    mutable UART drvUart;

private:

    // Mode tracking 

    enum class Mode {
        None, SPI, I2C, UART, OneWire, RawWire, SWD, Smartcard, NFC, MMC, SDIO
    };

    struct mode_s {
        uint8_t     iRequest;
        uint8_t     iRepetition;
        std::string strAnswer;
    };

    using ModesMap = std::map<const std::string, mode_s>;

    /**
     * @brief Enter a new protocol mode.
     *        Destroys any existing protocol instance, resets BBIO, and
     *        creates the requested HydraHAL object.
     */
    bool m_enter_mode(const std::string& modeName);

    /**
     * @brief Tear down active protocol, reset to BBIO.
     */
    void m_exit_mode() const;

    // Protocol instance helpers (const because called from const handlers) 

    HydraHAL::SPI*       m_spi()       const;
    HydraHAL::I2C*       m_i2c()       const;
    HydraHAL::UART*      m_uart()      const;
    HydraHAL::OneWire*   m_onewire()   const;
    HydraHAL::RawWire*   m_rawwire()   const;
    HydraHAL::SWD*       m_swd()       const;
    HydraHAL::Smartcard* m_smartcard() const;
    HydraHAL::NFC*       m_nfc()       const;
    HydraHAL::MMC*       m_mmc()       const;
    HydraHAL::SDIO*      m_sdio()      const;

    // WrRd callbacks (for generic_write_read_data / _file) 

    bool m_spi_wrrd_cb (std::span<const uint8_t> req, size_t rdlen) const;
    bool m_i2c_wrrd_cb (std::span<const uint8_t> req, size_t rdlen) const;

    // AUX helper (shared across all modes) 

    bool m_handle_aux_common(const std::string& args, HydraHAL::Protocol* proto) const;

    // Top-level command handlers (INFO, MODE) 

    bool m_Buspirate_INFO(const std::string& args) const; // kept name pattern for macro
    bool m_Buspirate_MODE(const std::string& args) const;

    // Protocols dispatch through the generic macro-generated inline

    #define HB_PLUGIN_CMD_RECORD(a) \
        bool m_Hydrabus_##a(const std::string& args) const;
    HYDRABUS_PLUGIN_COMMANDS_CONFIG_TABLE_STD
    #undef HB_PLUGIN_CMD_RECORD

    #define HB_PLUGIN_CMD_RECORD(a) \
        bool m_Hydrabus_##a(const std::string& args) const { \
            return generic_module_dispatch<HydrabusPlugin>(this, #a, args); }
    HYDRABUS_PLUGIN_COMMANDS_CONFIG_TABLE_CMDS
    #undef HB_PLUGIN_CMD_RECORD

    // Per-protocol subcommand declarations 

    #define SPI_CMD_RECORD(a)       bool m_handle_spi_##a      (const std::string&) const;
    SPI_COMMANDS_CONFIG_TABLE
    #undef SPI_CMD_RECORD

    #define I2C_CMD_RECORD(a)       bool m_handle_i2c_##a      (const std::string&) const;
    I2C_COMMANDS_CONFIG_TABLE
    #undef I2C_CMD_RECORD

    #define UART_CMD_RECORD(a)      bool m_handle_uart_##a     (const std::string&) const;
    UART_COMMANDS_CONFIG_TABLE
    #undef UART_CMD_RECORD

    #define ONEWIRE_CMD_RECORD(a)   bool m_handle_onewire_##a  (const std::string&) const;
    ONEWIRE_COMMANDS_CONFIG_TABLE
    #undef ONEWIRE_CMD_RECORD

    #define RAWWIRE_CMD_RECORD(a)   bool m_handle_rawwire_##a  (const std::string&) const;
    RAWWIRE_COMMANDS_CONFIG_TABLE
    #undef RAWWIRE_CMD_RECORD

    #define SWD_CMD_RECORD(a)       bool m_handle_swd_##a      (const std::string&) const;
    SWD_COMMANDS_CONFIG_TABLE
    #undef SWD_CMD_RECORD

    #define SMARTCARD_CMD_RECORD(a) bool m_handle_smartcard_##a(const std::string&) const;
    SMARTCARD_COMMANDS_CONFIG_TABLE
    #undef SMARTCARD_CMD_RECORD

    #define NFC_CMD_RECORD(a)       bool m_handle_nfc_##a      (const std::string&) const;
    NFC_COMMANDS_CONFIG_TABLE
    #undef NFC_CMD_RECORD

    #define MMC_CMD_RECORD(a)       bool m_handle_mmc_##a      (const std::string&) const;
    MMC_COMMANDS_CONFIG_TABLE
    #undef MMC_CMD_RECORD

    #define SDIO_CMD_RECORD(a)      bool m_handle_sdio_##a     (const std::string&) const;
    SDIO_COMMANDS_CONFIG_TABLE
    #undef SDIO_CMD_RECORD

    // Member data 

    std::string m_strVersion;
    mutable std::string m_strResultData;

    bool m_bIsInitialized;
    bool m_bIsEnabled;
    bool m_bIsFaultTolerant;
    bool m_bIsPrivileged;

    IniValues m_sIniValues;

    // Driver + Hydrabus core (created in doInit)
    std::shared_ptr<HydraHAL::Hydrabus> m_pHydrabus;

    // Active protocol instance — at most one exists at a time
    mutable Mode                                  m_eMode;
    mutable std::unique_ptr<HydraHAL::SPI>        m_pSPI;
    mutable std::unique_ptr<HydraHAL::I2C>        m_pI2C;
    mutable std::unique_ptr<HydraHAL::UART>       m_pUART;
    mutable std::unique_ptr<HydraHAL::OneWire>    m_pOneWire;
    mutable std::unique_ptr<HydraHAL::RawWire>    m_pRawWire;
    mutable std::unique_ptr<HydraHAL::SWD>        m_pSWD;
    mutable std::unique_ptr<HydraHAL::Smartcard>  m_pSmartcard;
    mutable std::unique_ptr<HydraHAL::NFC>        m_pNFC;
    mutable std::unique_ptr<HydraHAL::MMC>        m_pMMC;
    mutable std::unique_ptr<HydraHAL::SDIO>       m_pSDIO;

    // Dispatch maps
    PluginCommandsMap<HydrabusPlugin>             m_mapCmds;
    ModesMap                                      m_mapModes;
    SpeedsMapsMap                                 m_mapSpeedsMaps;
    CommandsMapsMap<HydrabusPlugin>               m_mapCommandsMaps;
        
    ModuleCommandsMap<HydrabusPlugin>             m_mapCmds_SPI;
    ModuleCommandsMap<HydrabusPlugin>             m_mapCmds_I2C;
    ModuleCommandsMap<HydrabusPlugin>             m_mapCmds_UART;
    ModuleCommandsMap<HydrabusPlugin>             m_mapCmds_ONEWIRE;
    ModuleCommandsMap<HydrabusPlugin>             m_mapCmds_RAWWIRE;
    ModuleCommandsMap<HydrabusPlugin>             m_mapCmds_SWD;
    ModuleCommandsMap<HydrabusPlugin>             m_mapCmds_SMARTCARD;
    ModuleCommandsMap<HydrabusPlugin>             m_mapCmds_NFC;
    ModuleCommandsMap<HydrabusPlugin>             m_mapCmds_MMC;
    ModuleCommandsMap<HydrabusPlugin>             m_mapCmds_SDIO;
        
    ModuleSpeedMap                                m_mapSpeed_SPI;
    ModuleSpeedMap                                m_mapSpeed_I2C;
    ModuleSpeedMap                                m_mapSpeed_RAWWIRE;
    
    // Stubs — needed by the meta-map loop but unused (no preset speeds)
    ModuleSpeedMap                                m_mapSpeed_UART;
    ModuleSpeedMap                                m_mapSpeed_ONEWIRE;
    ModuleSpeedMap                                m_mapSpeed_SWD;
    ModuleSpeedMap                                m_mapSpeed_SMARTCARD;
    ModuleSpeedMap                                m_mapSpeed_NFC;
    ModuleSpeedMap                                m_mapSpeed_MMC;
    ModuleSpeedMap                                m_mapSpeed_SDIO;

    bool m_LocalSetParams(const PluginDataSet* ps);
};

#endif // HYDRABUS_PLUGIN_HPP
