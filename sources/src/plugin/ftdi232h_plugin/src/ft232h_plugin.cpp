/*
 * FT232H Plugin – core lifecycle and top-level command handlers
 *
 * The FT232H has a single MPSSE channel (unlike FT2232H / FT4232H).
 * Three modules — SPI, I2C, GPIO — each own their own USB handle and
 * can be used simultaneously only if multiple FT232H chips are present.
 * On a single chip, open at most one module at a time.
 */

#include "ft232h_plugin.hpp"

#include "uNumeric.hpp"
#include "uLogger.hpp"

///////////////////////////////////////////////////////////////////
//                       LOG DEFINES                             //
///////////////////////////////////////////////////////////////////

#ifdef  LT_HDR
#undef  LT_HDR
#endif
#ifdef  LOG_HDR
#undef  LOG_HDR
#endif
#define LT_HDR   "FT232H     |"
#define LOG_HDR  LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                   INI KEY STRINGS                             //
///////////////////////////////////////////////////////////////////

#define ARTEFACTS_PATH  "ARTEFACTS_PATH"
#define DEVICE_INDEX    "DEVICE_INDEX"
#define SPI_CLOCK       "SPI_CLOCK"
#define I2C_CLOCK       "I2C_CLOCK"
#define I2C_ADDRESS     "I2C_ADDRESS"
#define READ_TIMEOUT    "READ_TIMEOUT"   // ms, used by script execution
#define SCRIPT_DELAY    "SCRIPT_DELAY"   // ms inter-command delay for scripts
#define UART_BAUD       "UART_BAUD"       // default baud rate for UART module

///////////////////////////////////////////////////////////////////
//                   PLUGIN ENTRY POINTS                         //
///////////////////////////////////////////////////////////////////

extern "C"
{
    EXPORTED FT232HPlugin* pluginEntry()
    {
        return new FT232HPlugin();
    }

    EXPORTED void pluginExit(FT232HPlugin* p)
    {
        delete p;
    }
}

///////////////////////////////////////////////////////////////////
//                   INI ACCESSOR (friend)                       //
///////////////////////////////////////////////////////////////////

const FT232HPlugin::IniValues* getAccessIniValues(const FT232HPlugin& obj)
{
    return &obj.m_sIniValues;
}

///////////////////////////////////////////////////////////////////
//                   INIT / CLEANUP                              //
///////////////////////////////////////////////////////////////////

bool FT232HPlugin::doInit(void* /*pvUserData*/)
{
    m_sSpiCfg.clockHz = m_sIniValues.u32SpiClockHz;
    m_sI2cCfg.clockHz = m_sIniValues.u32I2cClockHz;
    m_sI2cCfg.address = m_sIniValues.u8I2cAddress;

    m_sUartCfg.baudRate = m_sIniValues.u32UartBaudRate;

    m_bIsInitialized = true;

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Initialized — FT232H (60 MHz, single MPSSE channel)");
              LOG_STRING("device index:"); LOG_UINT32(m_sIniValues.u8DeviceIndex));
    return true;
}

void FT232HPlugin::doCleanup()
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

FT232HSPI* FT232HPlugin::m_spi() const
{
    if (!m_pSPI || !m_pSPI->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("SPI not open — call FT232H.SPI open [...]"));
        return nullptr;
    }
    return m_pSPI.get();
}

FT232HI2C* FT232HPlugin::m_i2c() const
{
    if (!m_pI2C || !m_pI2C->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("I2C not open — call FT232H.I2C open [...]"));
        return nullptr;
    }
    return m_pI2C.get();
}

FT232HGPIO* FT232HPlugin::m_gpio() const
{
    if (!m_pGPIO || !m_pGPIO->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("GPIO not open — call FT232H.GPIO open [...]"));
        return nullptr;
    }
    return m_pGPIO.get();
}

FT232HUART* FT232HPlugin::m_uart() const
{
    if (!m_pUART || !m_pUART->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("UART not open — call FT232H.UART open [...]"));
        return nullptr;
    }
    return m_pUART.get();
}

///////////////////////////////////////////////////////////////////
//              MAP ACCESSORS                                     //
///////////////////////////////////////////////////////////////////

ModuleCommandsMap<FT232HPlugin>*
FT232HPlugin::getModuleCmdsMap(const std::string& m) const
{
    auto it = m_mapCommandsMaps.find(m);
    return (it != m_mapCommandsMaps.end()) ? it->second : nullptr;
}

ModuleSpeedMap*
FT232HPlugin::getModuleSpeedsMap(const std::string& m) const
{
    auto it = m_mapSpeedsMaps.find(m);
    if (it == m_mapSpeedsMaps.end()) return nullptr;
    return it->second;
}

///////////////////////////////////////////////////////////////////
//              setModuleSpeed                                    //
///////////////////////////////////////////////////////////////////

bool FT232HPlugin::setModuleSpeed(const std::string& module, size_t hz) const
{
    if (module == "SPI") {
        m_sSpiCfg.clockHz = static_cast<uint32_t>(hz);

        if (m_pSPI && m_pSPI->is_open()) {
            m_pSPI->close();
            FT232HSPI::SpiConfig cfg;
            cfg.clockHz    = m_sSpiCfg.clockHz;
            cfg.mode       = m_sSpiCfg.mode;
            cfg.bitOrder   = m_sSpiCfg.bitOrder;
            cfg.csPin      = m_sSpiCfg.csPin;
            cfg.csPolarity = m_sSpiCfg.csPolarity;
            auto s = m_pSPI->open(cfg, m_sIniValues.u8DeviceIndex);
            if (s != FT232HSPI::Status::SUCCESS) {
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
                                  m_sIniValues.u8DeviceIndex);
            if (s != FT232HI2C::Status::SUCCESS) {
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
            if (s != FT232HUART::Status::SUCCESS) {
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
//              TOP-LEVEL COMMAND HANDLERS                       //
///////////////////////////////////////////////////////////////////

bool FT232HPlugin::m_FT232H_INFO(const std::string& /*args*/) const
{
    if (!m_bIsEnabled) return true;

    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Version  :"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Build    :"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Device   : FT232H (60 MHz, single MPSSE channel, PID 0x6014)"));
    LOG_PRINT(LOG_FIXED, LOG_HDR;
              LOG_STRING("DevIndex :"); LOG_UINT32(m_sIniValues.u8DeviceIndex));

    LOG_PRINT(LOG_FIXED, LOG_HDR;
              LOG_STRING("SPI      :"); LOG_STRING(m_pSPI && m_pSPI->is_open() ? "open" : "closed");
              LOG_STRING("clock="); LOG_UINT32(m_sSpiCfg.clockHz));

    LOG_PRINT(LOG_FIXED, LOG_HDR;
              LOG_STRING("I2C      :"); LOG_STRING(m_pI2C && m_pI2C->is_open() ? "open" : "closed");
              LOG_STRING("addr=0x"); LOG_HEX8(m_sI2cCfg.address);
              LOG_STRING("clock="); LOG_UINT32(m_sI2cCfg.clockHz));

    LOG_PRINT(LOG_FIXED, LOG_HDR;
              LOG_STRING("GPIO     :"); LOG_STRING(m_pGPIO && m_pGPIO->is_open() ? "open" : "closed"));

    LOG_PRINT(LOG_FIXED, LOG_HDR;
              LOG_STRING("UART     :"); LOG_STRING(m_pUART && m_pUART->is_open() ? "open" : "closed");
              LOG_STRING("baud="); LOG_UINT32(m_sUartCfg.baudRate));

    LOG_PRINT(LOG_FIXED, LOG_HDR;
              LOG_STRING("Commands : INFO SPI I2C GPIO UART"));
    return true;
}

bool FT232HPlugin::m_FT232H_SPI(const std::string& args) const
{
    return generic_module_dispatch<FT232HPlugin>(this, "SPI", args);
}

bool FT232HPlugin::m_FT232H_I2C(const std::string& args) const
{
    return generic_module_dispatch<FT232HPlugin>(this, "I2C", args);
}

bool FT232HPlugin::m_FT232H_GPIO(const std::string& args) const
{
    return generic_module_dispatch<FT232HPlugin>(this, "GPIO", args);
}

bool FT232HPlugin::m_FT232H_UART(const std::string& args) const
{
    return generic_module_dispatch<FT232HPlugin>(this, "UART", args);
}

///////////////////////////////////////////////////////////////////
//              INI PARAMETER LOADING                            //
///////////////////////////////////////////////////////////////////

bool FT232HPlugin::m_LocalSetParams(const PluginDataSet* ps)
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

    getString(ARTEFACTS_PATH, m_sIniValues.strArtefactsPath);
    getU8   (DEVICE_INDEX,    m_sIniValues.u8DeviceIndex);
    getU32  (SPI_CLOCK,       m_sIniValues.u32SpiClockHz);
    getU32  (I2C_CLOCK,       m_sIniValues.u32I2cClockHz);
    getU8   (I2C_ADDRESS,     m_sIniValues.u8I2cAddress);
    getU32  (READ_TIMEOUT,    m_sIniValues.u32ReadTimeout);
    getU32  (SCRIPT_DELAY,    m_sIniValues.u32ScriptDelay);
    getU32  (UART_BAUD,       m_sIniValues.u32UartBaudRate);

    if (!ok)
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("One or more config values failed to parse"));

    return ok;
}

///////////////////////////////////////////////////////////////////
//              SPI params parse helper                          //
///////////////////////////////////////////////////////////////////

bool FT232HPlugin::parseSpiParams(const std::string& args,
                                   SpiPendingCfg& cfg,
                                   uint8_t* pDeviceIndexOut)
{
    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);

    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() != 2) continue;

        bool ok = true;
        if (kv[0] == "clock") {
            ok = numeric::str2uint32(kv[1], cfg.clockHz);
        } else if (kv[0] == "mode") {
            uint8_t v = 0;
            ok = numeric::str2uint8(kv[1], v);
            if (ok && v <= 3) cfg.mode = static_cast<FT232HSPI::SpiMode>(v);
            else ok = false;
        } else if (kv[0] == "bitorder") {
            if      (kv[1] == "msb") cfg.bitOrder = FT232HSPI::BitOrder::MsbFirst;
            else if (kv[1] == "lsb") cfg.bitOrder = FT232HSPI::BitOrder::LsbFirst;
            else ok = false;
        } else if (kv[0] == "cspin") {
            ok = numeric::str2uint8(kv[1], cfg.csPin);
        } else if (kv[0] == "cspol") {
            if      (kv[1] == "low")  cfg.csPolarity = FT232HSPI::CsPolarity::ActiveLow;
            else if (kv[1] == "high") cfg.csPolarity = FT232HSPI::CsPolarity::ActiveHigh;
            else ok = false;
        } else if (kv[0] == "device" && pDeviceIndexOut) {
            ok = numeric::str2uint8(kv[1], *pDeviceIndexOut);
        } else {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown key:"); LOG_STRING(kv[0]));
            return false;
        }

        if (!ok) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Invalid value for:"); LOG_STRING(kv[0]));
            return false;
        }
    }
    return true;
}

///////////////////////////////////////////////////////////////////
//              UART params parse helper                         //
///////////////////////////////////////////////////////////////////

static bool parseParity_ft232h(const std::string& s, uint8_t& out)
{
    if (s=="none"||s=="NONE") { out=0; return true; }
    if (s=="odd" ||s=="ODD" ) { out=1; return true; }
    if (s=="even"||s=="EVEN") { out=2; return true; }
    if (s=="mark"||s=="MARK") { out=3; return true; }
    if (s=="space"||s=="SPACE"){ out=4; return true; }
    return false;
}

bool FT232HPlugin::parseUartParams(const std::string& args, UartPendingCfg& cfg,
                                    uint8_t* pDeviceIndexOut)
{
    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);
    bool ok = true;
    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() != 2) continue;
        const auto& k = kv[0];
        const auto& v = kv[1];
        if      (k == "baud"   ) ok &= numeric::str2uint32(v, cfg.baudRate);
        else if (k == "data"   ) ok &= numeric::str2uint8 (v, cfg.dataBits);
        else if (k == "stop"   ) ok &= numeric::str2uint8 (v, cfg.stopBits);
        else if (k == "parity" ) ok &= parseParity_ft232h (v, cfg.parity);
        else if (k == "flow"   ) cfg.hwFlowCtrl = (v=="hw"||v=="HW"||v=="rtscts");
        else if (k == "device" && pDeviceIndexOut) ok &= numeric::str2uint8(v, *pDeviceIndexOut);
        if (!ok) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Invalid value for:"); LOG_STRING(k);
                      LOG_STRING("="); LOG_STRING(v));
            return false;
        }
    }
    return ok;
}
