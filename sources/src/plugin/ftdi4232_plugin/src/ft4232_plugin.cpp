/*
 * FT4232H Plugin – core lifecycle and top-level command handlers
 *
 * Responsibilities:
 *   - Plugin entry / exit (C ABI)
 *   - doInit / doCleanup
 *   - INFO command
 *   - Module dispatch routing (SPI / I2C / GPIO)
 *   - Map accessors (getModuleCmdsMap / getModuleSpeedsMap)
 *   - setModuleSpeed()  — re-opens active driver at new clock
 *   - INI parameter loading
 */

#include "ft4232_plugin.hpp"

#include "uNumeric.hpp"
#include "uLogger.hpp"

#include <cstring>

///////////////////////////////////////////////////////////////////
//                       LOG DEFINES                             //
///////////////////////////////////////////////////////////////////

#ifdef  LT_HDR
#undef  LT_HDR
#endif
#ifdef  LOG_HDR
#undef  LOG_HDR
#endif
#define LT_HDR   "FT4232     |"
#define LOG_HDR  LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                   INI KEY STRINGS                             //
///////////////////////////////////////////////////////////////////

#define ARTEFACTS_PATH  "ARTEFACTS_PATH"
#define DEVICE_INDEX    "DEVICE_INDEX"
#define SPI_CHANNEL     "SPI_CHANNEL"
#define I2C_CHANNEL     "I2C_CHANNEL"
#define GPIO_CHANNEL    "GPIO_CHANNEL"
#define UART_CHANNEL    "UART_CHANNEL"
#define SPI_CLOCK       "SPI_CLOCK"
#define I2C_CLOCK       "I2C_CLOCK"
#define I2C_ADDRESS     "I2C_ADDRESS"
#define UART_BAUD       "UART_BAUD"
#define READ_TIMEOUT    "READ_TIMEOUT"   // ms — used by script execution
#define SCRIPT_DELAY    "SCRIPT_DELAY"   // ms — inter-command delay for scripts

///////////////////////////////////////////////////////////////////
//                   PLUGIN ENTRY POINTS                         //
///////////////////////////////////////////////////////////////////

extern "C"
{
    EXPORTED FT4232Plugin* pluginEntry()
    {
        return new FT4232Plugin();
    }

    EXPORTED void pluginExit(FT4232Plugin* p)
    {
        delete p;
    }
}

///////////////////////////////////////////////////////////////////
//                   INI ACCESSOR (friend)                       //
///////////////////////////////////////////////////////////////////

const FT4232Plugin::IniValues* getAccessIniValues(const FT4232Plugin& obj)
{
    return &obj.m_sIniValues;
}

///////////////////////////////////////////////////////////////////
//                   INIT / CLEANUP                              //
///////////////////////////////////////////////////////////////////

bool FT4232Plugin::doInit(void* /*pvUserData*/)
{
    // Propagate INI defaults into pending config structs
    m_sSpiCfg.clockHz   = m_sIniValues.u32SpiClockHz;
    m_sSpiCfg.channel   = m_sIniValues.eSpiChannel;
    m_sI2cCfg.clockHz   = m_sIniValues.u32I2cClockHz;
    m_sI2cCfg.address   = m_sIniValues.u8I2cAddress;
    m_sI2cCfg.channel   = m_sIniValues.eI2cChannel;
    m_sGpioCfg.channel  = m_sIniValues.eGpioChannel;
    m_sUartCfg.baudRate = m_sIniValues.u32UartBaudRate;
    m_sUartCfg.channel  = m_sIniValues.eUartChannel;

    m_bIsInitialized = true;

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Initialized — device index:"); LOG_UINT32(m_sIniValues.u8DeviceIndex);
              LOG_STRING("SPI ch:"); LOG_UINT32(static_cast<uint8_t>(m_sIniValues.eSpiChannel));
              LOG_STRING("I2C ch:"); LOG_UINT32(static_cast<uint8_t>(m_sIniValues.eI2cChannel)));
    return true;
}

void FT4232Plugin::doCleanup()
{
    if (m_pSPI)  { m_pSPI->close();  m_pSPI.reset();  }
    if (m_pI2C)  { m_pI2C->close();  m_pI2C.reset();  }
    if (m_pGPIO) { m_pGPIO->close(); m_pGPIO.reset(); }
    if (m_pUART) { m_pUART->close(); m_pUART.reset(); }
    m_bIsInitialized = false;
    m_bIsEnabled     = false;
}

///////////////////////////////////////////////////////////////////
//              DRIVER INSTANCE ACCESSORS                        //
///////////////////////////////////////////////////////////////////

FT4232SPI* FT4232Plugin::m_spi() const
{
    if (!m_pSPI || !m_pSPI->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("SPI not open — call FT4232.SPI open [...]"));
        return nullptr;
    }
    return m_pSPI.get();
}

FT4232I2C* FT4232Plugin::m_i2c() const
{
    if (!m_pI2C || !m_pI2C->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("I2C not open — call FT4232.I2C open [...]"));
        return nullptr;
    }
    return m_pI2C.get();
}

FT4232GPIO* FT4232Plugin::m_gpio() const
{
    if (!m_pGPIO || !m_pGPIO->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("GPIO not open — call FT4232.GPIO open [...]"));
        return nullptr;
    }
    return m_pGPIO.get();
}

FT4232UART* FT4232Plugin::m_uart() const
{
    if (!m_pUART || !m_pUART->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("UART not open — call FT4232.UART open [baud=N] [channel=C|D]"));
        return nullptr;
    }
    return m_pUART.get();
}

///////////////////////////////////////////////////////////////////
//              MAP ACCESSORS                                     //
///////////////////////////////////////////////////////////////////

ModuleCommandsMap<FT4232Plugin>*
FT4232Plugin::getModuleCmdsMap(const std::string& m) const
{
    auto it = m_mapCommandsMaps.find(m);
    return (it != m_mapCommandsMaps.end()) ? it->second : nullptr;
}

ModuleSpeedMap*
FT4232Plugin::getModuleSpeedsMap(const std::string& m) const
{
    auto it = m_mapSpeedsMaps.find(m);
    if (it == m_mapSpeedsMaps.end()) return nullptr;
    return it->second;  // may be nullptr for GPIO (intentional)
}

///////////////////////////////////////////////////////////////////
//              setModuleSpeed                                    //
//                                                               //
//  For FT4232H, "speed" = clock Hz.  Applying a new clock       //
//  requires closing and reopening the driver on the same        //
//  channel/device with updated config.                          //
///////////////////////////////////////////////////////////////////

bool FT4232Plugin::setModuleSpeed(const std::string& module, size_t hz) const
{
    if (module == "SPI") {
        m_sSpiCfg.clockHz = static_cast<uint32_t>(hz);

        if (m_pSPI && m_pSPI->is_open()) {
            // Reopen with new clock
            m_pSPI->close();
            FT4232SPI::SpiConfig cfg;
            cfg.clockHz    = m_sSpiCfg.clockHz;
            cfg.mode       = m_sSpiCfg.mode;
            cfg.bitOrder   = m_sSpiCfg.bitOrder;
            cfg.csPin      = m_sSpiCfg.csPin;
            cfg.csPolarity = m_sSpiCfg.csPolarity;
            cfg.channel    = m_sSpiCfg.channel;
            auto s = m_pSPI->open(cfg, m_sIniValues.u8DeviceIndex);
            if (s != FT4232SPI::Status::SUCCESS) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("SPI reopen at new clock failed, hz="); LOG_UINT32(hz));
                m_pSPI.reset();
                return false;
            }
            LOG_PRINT(LOG_INFO, LOG_HDR;
                      LOG_STRING("SPI clock updated to"); LOG_UINT32(hz); LOG_STRING("Hz"));
        } else {
            LOG_PRINT(LOG_INFO, LOG_HDR;
                      LOG_STRING("SPI pending clock stored:"); LOG_UINT32(hz); LOG_STRING("Hz"));
        }
        return true;
    }

    if (module == "I2C") {
        m_sI2cCfg.clockHz = static_cast<uint32_t>(hz);

        if (m_pI2C && m_pI2C->is_open()) {
            m_pI2C->close();
            auto s = m_pI2C->open(m_sI2cCfg.address,
                                  m_sI2cCfg.clockHz,
                                  m_sI2cCfg.channel,
                                  m_sIniValues.u8DeviceIndex);
            if (s != FT4232I2C::Status::SUCCESS) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("I2C reopen at new clock failed, hz="); LOG_UINT32(hz));
                m_pI2C.reset();
                return false;
            }
            LOG_PRINT(LOG_INFO, LOG_HDR;
                      LOG_STRING("I2C clock updated to"); LOG_UINT32(hz); LOG_STRING("Hz"));
        } else {
            LOG_PRINT(LOG_INFO, LOG_HDR;
                      LOG_STRING("I2C pending clock stored:"); LOG_UINT32(hz); LOG_STRING("Hz"));
        }
        return true;
    }

    if (module == "UART") {
        m_sUartCfg.baudRate = static_cast<uint32_t>(hz);
        if (m_pUART && m_pUART->is_open()) {
            auto s = m_pUART->set_baud(static_cast<uint32_t>(hz));
            if (s != FT4232UART::Status::SUCCESS) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("UART baud update failed, baud="); LOG_UINT32(hz));
                return false;
            }
        }
        LOG_PRINT(LOG_INFO, LOG_HDR;
                  LOG_STRING("UART baud set to"); LOG_UINT32(hz));
        return true;
    }

    LOG_PRINT(LOG_ERROR, LOG_HDR;
              LOG_STRING("setModuleSpeed: no speed support for module:"); LOG_STRING(module));
    return false;
}

///////////////////////////////////////////////////////////////////
//              CHANNEL PARSE HELPERS                            //
///////////////////////////////////////////////////////////////////

bool FT4232Plugin::parseChannel(const std::string& s, FT4232Base::Channel& out)
{
    if (s == "A" || s == "a") { out = FT4232Base::Channel::A; return true; }
    if (s == "B" || s == "b") { out = FT4232Base::Channel::B; return true; }
    if (s == "C" || s == "c") { out = FT4232Base::Channel::C; return true; }
    if (s == "D" || s == "d") { out = FT4232Base::Channel::D; return true; }
    LOG_PRINT(LOG_ERROR, LOG_STRING("FT4232     |");
              LOG_STRING("Invalid channel (use A, B, C or D):"); LOG_STRING(s));
    return false;
}

///////////////////////////////////////////////////////////////////
//              TOP-LEVEL COMMAND HANDLERS                       //
///////////////////////////////////////////////////////////////////

bool FT4232Plugin::m_FT4232_INFO(const std::string& /*args*/) const
{
    if (!m_bIsEnabled) return true;

    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Version  :"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Build    :"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Device   : FT4232H"));
    LOG_PRINT(LOG_FIXED, LOG_HDR;
              LOG_STRING("DevIndex :"); LOG_UINT32(m_sIniValues.u8DeviceIndex));

    auto chStr = [](FT4232Base::Channel c) -> const char* {
        return (c == FT4232Base::Channel::A) ? "A" : "B";
    };

    LOG_PRINT(LOG_FIXED, LOG_HDR;
              LOG_STRING("SPI      :"); LOG_STRING(m_pSPI && m_pSPI->is_open() ? "open" : "closed");
              LOG_STRING("ch="); LOG_STRING(chStr(m_sSpiCfg.channel));
              LOG_STRING("clock="); LOG_UINT32(m_sSpiCfg.clockHz));

    LOG_PRINT(LOG_FIXED, LOG_HDR;
              LOG_STRING("I2C      :"); LOG_STRING(m_pI2C && m_pI2C->is_open() ? "open" : "closed");
              LOG_STRING("ch="); LOG_STRING(chStr(m_sI2cCfg.channel));
              LOG_STRING("addr=0x"); LOG_HEX8(m_sI2cCfg.address);
              LOG_STRING("clock="); LOG_UINT32(m_sI2cCfg.clockHz));

    LOG_PRINT(LOG_FIXED, LOG_HDR;
              LOG_STRING("GPIO     :"); LOG_STRING(m_pGPIO && m_pGPIO->is_open() ? "open" : "closed");
              LOG_STRING("ch="); LOG_STRING(chStr(m_sGpioCfg.channel)));

    LOG_PRINT(LOG_FIXED, LOG_HDR;
              LOG_STRING("UART     :"); LOG_STRING(m_pUART && m_pUART->is_open() ? "open" : "closed");
              LOG_STRING("ch="); LOG_UINT8(static_cast<uint8_t>(m_sUartCfg.channel));
              LOG_STRING("baud="); LOG_UINT32(m_sUartCfg.baudRate));

    LOG_PRINT(LOG_FIXED, LOG_HDR;
              LOG_STRING("Commands : INFO SPI I2C GPIO UART"));
    return true;
}

// SPI / I2C / GPIO / UART each route straight into their module dispatch map

bool FT4232Plugin::m_FT4232_SPI(const std::string& args) const
{
    return generic_module_dispatch<FT4232Plugin>(this, "SPI", args);
}

bool FT4232Plugin::m_FT4232_I2C(const std::string& args) const
{
    return generic_module_dispatch<FT4232Plugin>(this, "I2C", args);
}

bool FT4232Plugin::m_FT4232_GPIO(const std::string& args) const
{
    return generic_module_dispatch<FT4232Plugin>(this, "GPIO", args);
}

bool FT4232Plugin::m_FT4232_UART(const std::string& args) const
{
    return generic_module_dispatch<FT4232Plugin>(this, "UART", args);
}

///////////////////////////////////////////////////////////////////
//              INI PARAMETER LOADING                            //
///////////////////////////////////////////////////////////////////

bool FT4232Plugin::m_LocalSetParams(const PluginDataSet* ps)
{
    if (!ps || ps->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("No settings in config"));
        return true;
    }

    const auto& m = ps->mapSettings;
    bool ok = true;

    auto getString = [&](const char* key, std::string& dst) {
        if (m.count(key)) dst = m.at(key);
    };
    auto getU32 = [&](const char* key, uint32_t& dst) {
        if (m.count(key)) ok &= numeric::str2uint32(m.at(key), dst);
    };
    auto getU8 = [&](const char* key, uint8_t& dst) {
        if (m.count(key)) ok &= numeric::str2uint8(m.at(key), dst);
    };
    auto getCh = [&](const char* key, FT4232Base::Channel& dst) {
        if (m.count(key)) ok &= parseChannel(m.at(key), dst);
    };

    getString  (ARTEFACTS_PATH, m_sIniValues.strArtefactsPath);
    getU8      (DEVICE_INDEX,   m_sIniValues.u8DeviceIndex);
    getCh      (SPI_CHANNEL,    m_sIniValues.eSpiChannel);
    getCh      (I2C_CHANNEL,    m_sIniValues.eI2cChannel);
    getCh      (GPIO_CHANNEL,   m_sIniValues.eGpioChannel);
    getCh      (UART_CHANNEL,   m_sIniValues.eUartChannel);
    getU32     (SPI_CLOCK,      m_sIniValues.u32SpiClockHz);
    getU32     (I2C_CLOCK,      m_sIniValues.u32I2cClockHz);
    getU8      (I2C_ADDRESS,    m_sIniValues.u8I2cAddress);
    getU32     (UART_BAUD,      m_sIniValues.u32UartBaudRate);
    getU32     (READ_TIMEOUT,   m_sIniValues.u32ReadTimeout);
    getU32     (SCRIPT_DELAY,   m_sIniValues.u32ScriptDelay);

    if (!ok)
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("One or more config values failed to parse"));

    return ok;
}
