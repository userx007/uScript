/*
 * FT2232 Plugin – core lifecycle and top-level command handlers
 *
 * Key difference from FT4232H plugin:
 *   Every driver open() now takes a Variant (FT2232H / FT2232D) which
 *   determines the USB PID, the MPSSE base clock, and which channels are
 *   legal. The Variant is stored in the pending config structs and
 *   forwarded on each open / reopen call.
 */

#include "ft2232_plugin.hpp"

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
#define LT_HDR   "FT2232     |"
#define LOG_HDR  LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                   INI KEY STRINGS                             //
///////////////////////////////////////////////////////////////////

#define ARTEFACTS_PATH   "ARTEFACTS_PATH"
#define DEVICE_INDEX     "DEVICE_INDEX"
#define DEFAULT_VARIANT  "VARIANT"        // "H" or "D"
#define SPI_CHANNEL      "SPI_CHANNEL"
#define I2C_CHANNEL      "I2C_CHANNEL"
#define GPIO_CHANNEL     "GPIO_CHANNEL"
#define SPI_CLOCK        "SPI_CLOCK"
#define I2C_CLOCK        "I2C_CLOCK"
#define I2C_ADDRESS      "I2C_ADDRESS"
#define READ_TIMEOUT     "READ_TIMEOUT"   // ms, used by script execution
#define SCRIPT_DELAY     "SCRIPT_DELAY"   // ms inter-command delay for scripts

///////////////////////////////////////////////////////////////////
//                   PLUGIN ENTRY POINTS                         //
///////////////////////////////////////////////////////////////////

extern "C"
{
    EXPORTED FT2232Plugin* pluginEntry()
    {
        return new FT2232Plugin();
    }

    EXPORTED void pluginExit(FT2232Plugin* p)
    {
        delete p;
    }
}

///////////////////////////////////////////////////////////////////
//                   INI ACCESSOR (friend)                       //
///////////////////////////////////////////////////////////////////

const FT2232Plugin::IniValues* getAccessIniValues(const FT2232Plugin& obj)
{
    return &obj.m_sIniValues;
}

///////////////////////////////////////////////////////////////////
//                   PARSE HELPERS                               //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::parseChannel(const std::string& s, FT2232Base::Channel& out)
{
    if (s == "A" || s == "a") { out = FT2232Base::Channel::A; return true; }
    if (s == "B" || s == "b") { out = FT2232Base::Channel::B; return true; }
    LOG_PRINT(LOG_ERROR, LOG_STRING("FT2232     |");
              LOG_STRING("Invalid channel (use A or B):"); LOG_STRING(s));
    return false;
}

bool FT2232Plugin::parseVariant(const std::string& s, FT2232Base::Variant& out)
{
    if (s == "H" || s == "h" || s == "FT2232H" || s == "2232H") {
        out = FT2232Base::Variant::FT2232H; return true;
    }
    if (s == "D" || s == "d" || s == "FT2232D" || s == "2232D") {
        out = FT2232Base::Variant::FT2232D; return true;
    }
    LOG_PRINT(LOG_ERROR, LOG_STRING("FT2232     |");
              LOG_STRING("Invalid variant (use H or D):"); LOG_STRING(s));
    return false;
}

bool FT2232Plugin::checkVariantSpeedLimit(FT2232Base::Variant v,
                                           const std::string& protocol,
                                           uint32_t hz)
{
    if (v != FT2232Base::Variant::FT2232D) return true;

    // FT2232D limits: SPI max 3 MHz, I2C max 400 kHz (practical; base 6 MHz / 2 / 7 ≈ 428 kHz)
    const uint32_t limit = (protocol == "SPI") ? 3000000u : 400000u;
    if (hz > limit) {
        LOG_PRINT(LOG_ERROR, LOG_STRING("FT2232     |");
                  LOG_STRING("FT2232D variant:"); LOG_STRING(protocol);
                  LOG_STRING("clock"); LOG_UINT32(hz);
                  LOG_STRING("Hz exceeds hardware limit of"); LOG_UINT32(limit); LOG_STRING("Hz"));
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////
//                   INIT / CLEANUP                              //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::doInit(void* /*pvUserData*/)
{
    // Propagate INI defaults into pending config structs
    m_sSpiCfg.clockHz   = m_sIniValues.u32SpiClockHz;
    m_sSpiCfg.channel   = m_sIniValues.eSpiChannel;
    m_sSpiCfg.variant   = m_sIniValues.eDefaultVariant;

    m_sI2cCfg.clockHz   = m_sIniValues.u32I2cClockHz;
    m_sI2cCfg.address   = m_sIniValues.u8I2cAddress;
    m_sI2cCfg.channel   = m_sIniValues.eI2cChannel;
    m_sI2cCfg.variant   = m_sIniValues.eDefaultVariant;

    m_sGpioCfg.channel  = m_sIniValues.eGpioChannel;
    m_sGpioCfg.variant  = m_sIniValues.eDefaultVariant;

    m_bIsInitialized = true;

    const char* varStr = (m_sIniValues.eDefaultVariant == FT2232Base::Variant::FT2232H)
                         ? "FT2232H (60 MHz)" : "FT2232D (6 MHz)";
    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Initialized — variant:"); LOG_STRING(varStr);
              LOG_STRING("device index:"); LOG_UINT32(m_sIniValues.u8DeviceIndex));
    return true;
}

void FT2232Plugin::doCleanup()
{
    if (m_pSPI)  { m_pSPI->close();  m_pSPI.reset();  }
    if (m_pI2C)  { m_pI2C->close();  m_pI2C.reset();  }
    if (m_pGPIO) { m_pGPIO->close(); m_pGPIO.reset(); }
    m_bIsInitialized = false;
    m_bIsEnabled     = false;
}

///////////////////////////////////////////////////////////////////
//              DRIVER INSTANCE ACCESSORS                        //
///////////////////////////////////////////////////////////////////

FT2232SPI* FT2232Plugin::m_spi() const
{
    if (!m_pSPI || !m_pSPI->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("SPI not open — call FT2232.SPI open [...]"));
        return nullptr;
    }
    return m_pSPI.get();
}

FT2232I2C* FT2232Plugin::m_i2c() const
{
    if (!m_pI2C || !m_pI2C->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("I2C not open — call FT2232.I2C open [...]"));
        return nullptr;
    }
    return m_pI2C.get();
}

FT2232GPIO* FT2232Plugin::m_gpio() const
{
    if (!m_pGPIO || !m_pGPIO->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("GPIO not open — call FT2232.GPIO open [...]"));
        return nullptr;
    }
    return m_pGPIO.get();
}

///////////////////////////////////////////////////////////////////
//              MAP ACCESSORS                                     //
///////////////////////////////////////////////////////////////////

ModuleCommandsMap<FT2232Plugin>*
FT2232Plugin::getModuleCmdsMap(const std::string& m) const
{
    auto it = m_mapCommandsMaps.find(m);
    return (it != m_mapCommandsMaps.end()) ? it->second : nullptr;
}

ModuleSpeedMap*
FT2232Plugin::getModuleSpeedsMap(const std::string& m) const
{
    auto it = m_mapSpeedsMaps.find(m);
    if (it == m_mapSpeedsMaps.end()) return nullptr;
    return it->second;
}

///////////////////////////////////////////////////////////////////
//              setModuleSpeed                                    //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::setModuleSpeed(const std::string& module, size_t hz) const
{
    if (module == "SPI") {
        if (!checkVariantSpeedLimit(m_sSpiCfg.variant, "SPI",
                                    static_cast<uint32_t>(hz))) return false;
        m_sSpiCfg.clockHz = static_cast<uint32_t>(hz);

        if (m_pSPI && m_pSPI->is_open()) {
            m_pSPI->close();
            FT2232SPI::SpiConfig cfg;
            cfg.clockHz    = m_sSpiCfg.clockHz;
            cfg.mode       = m_sSpiCfg.mode;
            cfg.bitOrder   = m_sSpiCfg.bitOrder;
            cfg.csPin      = m_sSpiCfg.csPin;
            cfg.csPolarity = m_sSpiCfg.csPolarity;
            cfg.variant    = m_sSpiCfg.variant;
            cfg.channel    = m_sSpiCfg.channel;
            auto s = m_pSPI->open(cfg, m_sIniValues.u8DeviceIndex);
            if (s != FT2232SPI::Status::SUCCESS) {
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
        if (!checkVariantSpeedLimit(m_sI2cCfg.variant, "I2C",
                                    static_cast<uint32_t>(hz))) return false;
        m_sI2cCfg.clockHz = static_cast<uint32_t>(hz);

        if (m_pI2C && m_pI2C->is_open()) {
            m_pI2C->close();
            auto s = m_pI2C->open(m_sI2cCfg.address,
                                  m_sI2cCfg.clockHz,
                                  m_sI2cCfg.variant,
                                  m_sI2cCfg.channel,
                                  m_sIniValues.u8DeviceIndex);
            if (s != FT2232I2C::Status::SUCCESS) {
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

    LOG_PRINT(LOG_ERROR, LOG_HDR;
              LOG_STRING("setModuleSpeed: no speed support for module:"); LOG_STRING(module));
    return false;
}

///////////////////////////////////////////////////////////////////
//              TOP-LEVEL COMMAND HANDLERS                       //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::m_FT2232_INFO(const std::string& /*args*/) const
{
    if (!m_bIsEnabled) return true;

    const char* defVar = (m_sIniValues.eDefaultVariant == FT2232Base::Variant::FT2232H)
                         ? "FT2232H (60 MHz, channels A+B)"
                         : "FT2232D (6 MHz,  channel A only)";

    auto chStr = [](FT2232Base::Channel c) -> const char* {
        return (c == FT2232Base::Channel::A) ? "A" : "B";
    };
    auto varStr = [](FT2232Base::Variant v) -> const char* {
        return (v == FT2232Base::Variant::FT2232H) ? "H" : "D";
    };

    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Version  :"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Build    :"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Device   :"); LOG_STRING(defVar));
    LOG_PRINT(LOG_FIXED, LOG_HDR;
              LOG_STRING("DevIndex :"); LOG_UINT32(m_sIniValues.u8DeviceIndex));

    LOG_PRINT(LOG_FIXED, LOG_HDR;
              LOG_STRING("SPI      :"); LOG_STRING(m_pSPI && m_pSPI->is_open() ? "open" : "closed");
              LOG_STRING("variant="); LOG_STRING(varStr(m_sSpiCfg.variant));
              LOG_STRING("ch="); LOG_STRING(chStr(m_sSpiCfg.channel));
              LOG_STRING("clock="); LOG_UINT32(m_sSpiCfg.clockHz));

    LOG_PRINT(LOG_FIXED, LOG_HDR;
              LOG_STRING("I2C      :"); LOG_STRING(m_pI2C && m_pI2C->is_open() ? "open" : "closed");
              LOG_STRING("variant="); LOG_STRING(varStr(m_sI2cCfg.variant));
              LOG_STRING("ch="); LOG_STRING(chStr(m_sI2cCfg.channel));
              LOG_STRING("addr=0x"); LOG_HEX8(m_sI2cCfg.address);
              LOG_STRING("clock="); LOG_UINT32(m_sI2cCfg.clockHz));

    LOG_PRINT(LOG_FIXED, LOG_HDR;
              LOG_STRING("GPIO     :"); LOG_STRING(m_pGPIO && m_pGPIO->is_open() ? "open" : "closed");
              LOG_STRING("variant="); LOG_STRING(varStr(m_sGpioCfg.variant));
              LOG_STRING("ch="); LOG_STRING(chStr(m_sGpioCfg.channel)));

    LOG_PRINT(LOG_FIXED, LOG_HDR;
              LOG_STRING("Commands : INFO SPI I2C GPIO"));
    return true;
}

bool FT2232Plugin::m_FT2232_SPI(const std::string& args) const
{
    return generic_module_dispatch<FT2232Plugin>(this, "SPI", args);
}

bool FT2232Plugin::m_FT2232_I2C(const std::string& args) const
{
    return generic_module_dispatch<FT2232Plugin>(this, "I2C", args);
}

bool FT2232Plugin::m_FT2232_GPIO(const std::string& args) const
{
    return generic_module_dispatch<FT2232Plugin>(this, "GPIO", args);
}

///////////////////////////////////////////////////////////////////
//              INI PARAMETER LOADING                            //
///////////////////////////////////////////////////////////////////

bool FT2232Plugin::m_LocalSetParams(const PluginDataSet* ps)
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
    auto getCh = [&](const char* key, FT2232Base::Channel& dst) {
        if (m.count(key)) ok &= parseChannel(m.at(key), dst);
    };
    auto getVar = [&](const char* key, FT2232Base::Variant& dst) {
        if (m.count(key)) ok &= parseVariant(m.at(key), dst);
    };

    getString(ARTEFACTS_PATH,   m_sIniValues.strArtefactsPath);
    getU8   (DEVICE_INDEX,      m_sIniValues.u8DeviceIndex);
    getVar  (DEFAULT_VARIANT,   m_sIniValues.eDefaultVariant);
    getCh   (SPI_CHANNEL,       m_sIniValues.eSpiChannel);
    getCh   (I2C_CHANNEL,       m_sIniValues.eI2cChannel);
    getCh   (GPIO_CHANNEL,      m_sIniValues.eGpioChannel);
    getU32  (SPI_CLOCK,         m_sIniValues.u32SpiClockHz);
    getU32  (I2C_CLOCK,         m_sIniValues.u32I2cClockHz);
    getU8   (I2C_ADDRESS,       m_sIniValues.u8I2cAddress);
    getU32  (READ_TIMEOUT,      m_sIniValues.u32ReadTimeout);
    getU32  (SCRIPT_DELAY,      m_sIniValues.u32ScriptDelay);

    if (!ok)
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("One or more config values failed to parse"));

    return ok;
}
