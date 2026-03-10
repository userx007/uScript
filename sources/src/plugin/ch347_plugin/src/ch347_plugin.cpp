/*
 * CH347 Plugin – core lifecycle and top-level command handlers
 *
 * The CH347 exposes SPI, I2C, GPIO and JTAG over a single USB device
 * file.  Each module opens the device independently via CH347OpenDevice()
 * so all four can run simultaneously.
 *
 * The device path is configured in the INI file (DEVICE_PATH key)
 * and defaults to "/dev/ch34xpis0".
 */

#include "ch347_plugin.hpp"

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
#define LT_HDR   "CH347      |"
#define LOG_HDR  LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                   INI KEY STRINGS                             //
///////////////////////////////////////////////////////////////////

#define ARTEFACTS_PATH   "ARTEFACTS_PATH"
#define DEVICE_PATH      "DEVICE_PATH"
#define SPI_CLOCK        "SPI_CLOCK"
#define I2C_SPEED        "I2C_SPEED"
#define I2C_ADDRESS      "I2C_ADDRESS"
#define JTAG_CLOCK_RATE  "JTAG_CLOCK_RATE"
#define READ_TIMEOUT     "READ_TIMEOUT"
#define SCRIPT_DELAY     "SCRIPT_DELAY"

///////////////////////////////////////////////////////////////////
//                   PLUGIN ENTRY POINTS                         //
///////////////////////////////////////////////////////////////////

extern "C"
{
    EXPORTED CH347Plugin* pluginEntry()
    {
        return new CH347Plugin();
    }

    EXPORTED void pluginExit(CH347Plugin* p)
    {
        delete p;
    }
}

///////////////////////////////////////////////////////////////////
//                   INI ACCESSOR (friend)                       //
///////////////////////////////////////////////////////////////////

const CH347Plugin::IniValues* getAccessIniValues(const CH347Plugin& obj)
{
    return &obj.m_sIniValues;
}

///////////////////////////////////////////////////////////////////
//                   PARSE HELPERS                               //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::parseI2cSpeed(const std::string& s, I2cSpeed& out)
{
    if (s == "20kHz"  || s == "low"     ) { out = I2cSpeed::Low;      return true; }
    if (s == "100kHz" || s == "standard") { out = I2cSpeed::Standard; return true; }
    if (s == "400kHz" || s == "fast"    ) { out = I2cSpeed::Fast;     return true; }
    if (s == "750kHz" || s == "high"    ) { out = I2cSpeed::High;     return true; }
    if (s == "50kHz"  || s == "std50"   ) { out = I2cSpeed::Std50;    return true; }
    if (s == "200kHz" || s == "std200"  ) { out = I2cSpeed::Std200;   return true; }
    if (s == "1MHz"   || s == "fast1m"  ) { out = I2cSpeed::Fast1M;   return true; }

    // also accept the raw enum integer
    uint8_t v = 0;
    if (numeric::str2uint8(s, v) && v <= 6) { out = static_cast<I2cSpeed>(v); return true; }

    LOG_PRINT(LOG_ERROR, LOG_HDR;
              LOG_STRING("Invalid I2C speed (use 20kHz/100kHz/400kHz/750kHz/50kHz/200kHz/1MHz):"); LOG_STRING(s));
    return false;
}

bool CH347Plugin::parseSpiParams(const std::string& args,
                                  SpiPendingCfg& cfg,
                                  std::string* pDevPathOut)
{
    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);

    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() != 2) continue;

        const auto& k = kv[0];
        const auto& v = kv[1];
        bool ok = true;

        if (k == "clock") {
            uint32_t hz = 0;
            ok = numeric::str2uint32(v, hz);
            if (ok) cfg.cfg.iClock = spiHzToClockIndex(hz);
        } else if (k == "mode") {
            uint8_t m = 0;
            ok = numeric::str2uint8(v, m);
            if (ok && m <= 3) cfg.cfg.iMode = m;
            else ok = false;
        } else if (k == "order") {
            if      (v == "msb" || v == "MSB") cfg.cfg.iByteOrder = 1;
            else if (v == "lsb" || v == "LSB") cfg.cfg.iByteOrder = 0;
            else ok = false;
        } else if (k == "cs") {
            if      (v == "cs1") cfg.xferOpts.chipSelect = SpiCS::CS1;
            else if (v == "cs2") cfg.xferOpts.chipSelect = SpiCS::CS2;
            else if (v == "none") cfg.xferOpts.ignoreCS  = true;
            else ok = false;
        } else if (k == "device" && pDevPathOut) {
            *pDevPathOut = v;
        } else {
            LOG_PRINT(LOG_ERROR, LOG_STRING("CH347_SPI  |");
                      LOG_STRING("Unknown key:"); LOG_STRING(k));
            return false;
        }

        if (!ok) {
            LOG_PRINT(LOG_ERROR, LOG_STRING("CH347_SPI  |");
                      LOG_STRING("Invalid value for:"); LOG_STRING(k));
            return false;
        }
    }
    cfg.cfgDirty = true;
    return true;
}

bool CH347Plugin::parseI2cParams(const std::string& args,
                                  I2cPendingCfg& cfg,
                                  std::string* pDevPathOut)
{
    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);

    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() != 2) continue;

        const auto& k = kv[0];
        const auto& v = kv[1];
        bool ok = true;

        if (k == "speed") {
            ok = parseI2cSpeed(v, cfg.speed);
        } else if (k == "addr" || k == "address") {
            ok = numeric::str2uint8(v, cfg.address);
        } else if (k == "device" && pDevPathOut) {
            *pDevPathOut = v;
        } else {
            LOG_PRINT(LOG_ERROR, LOG_STRING("CH347_I2C  |");
                      LOG_STRING("Unknown key:"); LOG_STRING(k));
            return false;
        }

        if (!ok) {
            LOG_PRINT(LOG_ERROR, LOG_STRING("CH347_I2C  |");
                      LOG_STRING("Invalid value for:"); LOG_STRING(k));
            return false;
        }
    }
    return true;
}

///////////////////////////////////////////////////////////////////
//                   INIT / CLEANUP                              //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::doInit(void* /*pvUserData*/)
{
    // Propagate INI defaults into pending config structs
    m_sI2cCfg.speed   = m_sIniValues.eI2cSpeed;
    m_sI2cCfg.address = m_sIniValues.u8I2cAddress;
    m_sJtagCfg.clockRate = m_sIniValues.u8JtagClockRate;

    // Default SPI: mode 0, MSB-first, CS1, 1 MHz
    m_sSpiCfg.cfg.iMode      = 0;
    m_sSpiCfg.cfg.iByteOrder = 1;
    m_sSpiCfg.cfg.iClock     = spiHzToClockIndex(m_sIniValues.u32SpiClockHz);

    m_bIsInitialized = true;
    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Initialized — device:"); LOG_STRING(m_sIniValues.strDevicePath));
    return true;
}

void CH347Plugin::doCleanup()
{
    if (m_pSPI)  { m_pSPI->close();  m_pSPI.reset();  }
    if (m_pI2C)  { m_pI2C->close();  m_pI2C.reset();  }
    if (m_pGPIO) { m_pGPIO->close(); m_pGPIO.reset(); }
    if (m_pJTAG) { m_pJTAG->close(); m_pJTAG.reset(); }
    m_bIsInitialized = false;
    m_bIsEnabled     = false;
    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Cleanup done"));
}

///////////////////////////////////////////////////////////////////
//                   LOCAL SET PARAMS                            //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_LocalSetParams(const PluginDataSet* ps)
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
    auto getSpd = [&](const char* key, I2cSpeed& dst) {
        if (m.count(key)) ok &= parseI2cSpeed(m.at(key), dst);
    };

    getString(ARTEFACTS_PATH,  m_sIniValues.strArtefactsPath);
    getString(DEVICE_PATH,     m_sIniValues.strDevicePath);
    getU32  (SPI_CLOCK,        m_sIniValues.u32SpiClockHz);
    getSpd  (I2C_SPEED,        m_sIniValues.eI2cSpeed);
    getU8   (I2C_ADDRESS,      m_sIniValues.u8I2cAddress);
    getU8   (JTAG_CLOCK_RATE,  m_sIniValues.u8JtagClockRate);
    getU32  (READ_TIMEOUT,     m_sIniValues.u32ReadTimeout);
    getU32  (SCRIPT_DELAY,     m_sIniValues.u32ScriptDelay);

    if (!ok)
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("One or more config values failed to parse"));

    return ok;
}

///////////////////////////////////////////////////////////////////
//                   MODULE MAP ACCESSORS                        //
///////////////////////////////////////////////////////////////////

ModuleCommandsMap<CH347Plugin>* CH347Plugin::getModuleCmdsMap(const std::string& m) const
{
    auto it = m_mapCommandsMaps.find(m);
    return (it != m_mapCommandsMaps.end()) ? it->second : nullptr;
}

ModuleSpeedMap* CH347Plugin::getModuleSpeedsMap(const std::string& m) const
{
    auto it = m_mapSpeedsMaps.find(m);
    return (it != m_mapSpeedsMaps.end()) ? it->second : nullptr;
}

bool CH347Plugin::setModuleSpeed(const std::string& module, size_t hz) const
{
    if (module == "SPI") {
        m_sSpiCfg.cfg.iClock = spiHzToClockIndex(static_cast<uint32_t>(hz));
        m_sSpiCfg.cfgDirty   = true;
        if (m_pSPI && m_pSPI->is_open()) {
            return m_pSPI->set_frequency(static_cast<uint32_t>(hz)) == CH347SPI::Status::SUCCESS;
        }
        return true;
    }
    if (module == "I2C") {
        I2cSpeed spd = I2cSpeed::Fast;
        // Map Hz to the nearest preset
        if      (hz <= 20000 ) spd = I2cSpeed::Low;
        else if (hz <= 50000 ) spd = I2cSpeed::Std50;
        else if (hz <= 100000) spd = I2cSpeed::Standard;
        else if (hz <= 200000) spd = I2cSpeed::Std200;
        else if (hz <= 400000) spd = I2cSpeed::Fast;
        else if (hz <= 750000) spd = I2cSpeed::High;
        else                   spd = I2cSpeed::Fast1M;

        m_sI2cCfg.speed = spd;
        if (m_pI2C && m_pI2C->is_open()) {
            return m_pI2C->set_speed(spd) == CH347I2C::Status::SUCCESS;
        }
        return true;
    }
    LOG_PRINT(LOG_ERROR, LOG_HDR;
              LOG_STRING("setModuleSpeed: unsupported module:"); LOG_STRING(module));
    return false;
}

///////////////////////////////////////////////////////////////////
//                   DRIVER ACCESSORS                            //
///////////////////////////////////////////////////////////////////

CH347SPI*  CH347Plugin::m_spi()  const { return m_pSPI.get();  }
CH347I2C*  CH347Plugin::m_i2c()  const { return m_pI2C.get();  }
CH347GPIO* CH347Plugin::m_gpio() const { return m_pGPIO.get(); }
CH347JTAG* CH347Plugin::m_jtag() const { return m_pJTAG.get(); }

///////////////////////////////////////////////////////////////////
//               TOP-LEVEL COMMAND HANDLERS                      //
///////////////////////////////////////////////////////////////////

bool CH347Plugin::m_CH347_INFO(const std::string&) const
{
    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("CH347 Plugin v"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Device path : "); LOG_STRING(m_sIniValues.strDevicePath));
    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("SPI open    : "); LOG_STRING(m_pSPI  && m_pSPI->is_open()  ? "yes" : "no"));
    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("I2C open    : "); LOG_STRING(m_pI2C  && m_pI2C->is_open()  ? "yes" : "no"));
    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("GPIO open   : "); LOG_STRING(m_pGPIO && m_pGPIO->is_open() ? "yes" : "no"));
    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("JTAG open   : "); LOG_STRING(m_pJTAG && m_pJTAG->is_open() ? "yes" : "no"));
    return true;
}

bool CH347Plugin::m_CH347_SPI(const std::string& args) const
{
    return generic_module_dispatch<CH347Plugin>(this, "SPI", args);
}

bool CH347Plugin::m_CH347_I2C(const std::string& args) const
{
    return generic_module_dispatch<CH347Plugin>(this, "I2C", args);
}

bool CH347Plugin::m_CH347_GPIO(const std::string& args) const
{
    return generic_module_dispatch<CH347Plugin>(this, "GPIO", args);
}

bool CH347Plugin::m_CH347_JTAG(const std::string& args) const
{
    return generic_module_dispatch<CH347Plugin>(this, "JTAG", args);
}
