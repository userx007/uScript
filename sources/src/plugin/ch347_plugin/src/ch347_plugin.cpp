/*
 * CH347 Plugin – core lifecycle and top-level command handlers
 *
 * The CH347 exposes SPI, I2C, GPIO and JTAG over a single USB device
 * file.  Each module opens the device independently via CH347OpenDevice()
 * so all four can run simultaneously.
 *
 * The device path is configured in the INI file (DEVICE_PATH key)
 * and defaults to "/dev/ch34xpis0" (Linux) or "0" (Windows).
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

bool CH347Plugin::m_CH347_INFO(const std::string& args) const
{
    if (!args.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("INFO expects no arguments"));
        return false;
    }

    if (!m_bIsEnabled) return true;

    LOG_PRINT(LOG_EMPTY, LOG_STRING(CH347_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: WCH CH347 Hi-Speed USB adapter (SPI/I2C/GPIO/JTAG)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Device:"); LOG_STRING(m_sIniValues.strDevicePath);
                         LOG_STRING("  (Linux default: /dev/ch34xpis0  Windows default: 0)"));

    // ── SPI ───────────────────────────────────────────────────────────────
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SPI : full-duplex SPI bus (must call open before any transfer)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  open : open SPI interface and apply initial configuration"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [clock=N] [mode=0-3] [order=msb|lsb] [cs=cs1|cs2|none] [device=PATH]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           clock range: 468750 – 60000000 Hz"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.SPI open clock=15000000 mode=0"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CH347.SPI open clock=1000000 mode=3 order=lsb cs=cs2"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CH347.SPI open device=/dev/ch34xpis1   - open alternate device"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  close : release SPI interface"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.SPI close"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : update SPI configuration without reopening"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [clock=N] [mode=0-3] [order=msb|lsb] [cs=cs1|cs2|none]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.SPI cfg clock=7500000 mode=1"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CH347.SPI cfg ?              - print current pending config"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : preset clock labels: 468kHz 937kHz 1.875MHz 3.75MHz 7.5MHz 15MHz 30MHz 60MHz"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cs : manually assert or deassert chip-select"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : en | dis  (CS is also asserted/deasserted automatically per transfer)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.SPI cs en"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CH347.SPI cs dis"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : transmit bytes (MOSI only, MISO discarded)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA   (hex bytes, no spaces)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.SPI write DEADBEEF      - send 4 bytes"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : receive N bytes (clocks 0x00 on MOSI, prints MISO)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N   (byte count)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.SPI read 4"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : MISO bytes printed as hex dump"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  xfer : full-duplex transfer (MOSI written, MISO printed)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA   (hex bytes)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.SPI xfer DEADBEEF       - write 4 bytes, print MISO"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : MISO bytes printed as hex dump"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  wrrd : write then read in one transaction"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA:rdlen"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.SPI wrrd 9F:3           - send cmd 0x9F, read 3 bytes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CH347.SPI wrrd DEADBEEF:4     - write 4 bytes, read 4 bytes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : read bytes printed as hex dump"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  wrrdf : write/read using binary files from ARTEFACTS_PATH"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : filename[:wrchunk][:rdchunk]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.SPI wrrdf flash_cmd.bin"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  script : run a command script from ARTEFACTS_PATH (SPI must be open)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : scriptname"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.SPI script read_flash.txt"));

    // ── I2C ───────────────────────────────────────────────────────────────
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("I2C : I2C bus master (must call open before any transfer)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  open : open I2C interface and apply initial configuration"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [speed=PRESET] [addr=0xNN] [device=PATH]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           speed presets: 20kHz | 50kHz | 100kHz | 200kHz | 400kHz | 750kHz | 1MHz"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           aliases: low | std50 | standard | std200 | fast | high | fast1m"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.I2C open speed=400kHz addr=0x50"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CH347.I2C open speed=100kHz device=/dev/ch34xpis1"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  close : release I2C interface"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.I2C close"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : update I2C configuration without reopening"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [speed=PRESET] [addr=0xNN]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.I2C cfg speed=100kHz addr=0x68"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CH347.I2C cfg ?              - print current pending config"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : write bytes (START + addr+W + data + STOP)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA   (buffer[0] = (devAddr<<1)|0, buffer[1..] = reg + payload)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.I2C write A050       - write device 0x50, register 0x00"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : ACK/NACK status"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : read N bytes from device configured by open/cfg"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N   (byte count)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.I2C read 2           - read 2 bytes from configured addr"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : received bytes printed as hex dump"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  wrrd : write then read (write phase first, then read rdlen bytes)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA:rdlen"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.I2C wrrd 5000:2      - write 0x50 0x00, read 2 bytes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : read bytes printed as hex dump"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  wrrdf : write/read using files from ARTEFACTS_PATH"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : filename[:wrchunk][:rdchunk]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.I2C wrrdf i2c_seq.bin"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  scan : probe I2C addresses 0x08..0x77 for ACK"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.I2C scan"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : list of responding device addresses (e.g. Found device at 0x48)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  eeprom : read or write a 24Cxx series EEPROM via I2C"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : read  TYPE ADDR N"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           write TYPE ADDR HEXDATA"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           TYPE: 0=24C01 1=24C02 2=24C04 3=24C08 4=24C16"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("                 5=24C32 6=24C64 7=24C128 8=24C256"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.I2C eeprom read 2 0 16    - read 16 bytes from 24C04 at addr 0"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CH347.I2C eeprom write 1 0 DEADBEEF  - write 4 bytes to 24C02 at addr 0"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : (read) bytes printed as hex dump"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  script : run a command script from ARTEFACTS_PATH (I2C must be open)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : scriptname"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.I2C script eeprom_test.txt"));

    // ── GPIO ──────────────────────────────────────────────────────────────
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("GPIO : 8-pin GPIO interface GPIO0-GPIO7 (must call open before use)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : all commands use bitmasks; bit N = pin N (bit 0 = GPIO0, bit 7 = GPIO7)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  open : open GPIO interface (all 8 pins default to inputs)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [device=PATH]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.GPIO open"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CH347.GPIO open device=/dev/ch34xpis1"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  close : release GPIO interface"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.GPIO close"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  dir : set pin directions (1=output, 0=input)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : output=0xNN   (bitmask; bits set to 1 become outputs)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.GPIO dir output=0x0F    - GPIO0-3 outputs, GPIO4-7 inputs"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CH347.GPIO dir output=0xFF    - all 8 pins as outputs"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CH347.GPIO dir output=0x00    - all 8 pins as inputs"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : drive output pins to specified levels"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : pins=0xNN levels=0xNN"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.GPIO write pins=0x0F levels=0x05  - GPIO0+2 high, GPIO1+3 low"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CH347.GPIO write 0xAA               - bare form: set all pins to 0xAA"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  set : drive masked pins HIGH"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : pins=0xNN  (or bare 0xNN)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.GPIO set pins=0x01     - drive GPIO0 high"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CH347.GPIO set 0x0F          - drive GPIO0-3 high"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  clear : drive masked pins LOW"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : pins=0xNN  (or bare 0xNN)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.GPIO clear pins=0x01   - drive GPIO0 low"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CH347.GPIO clear 0xF0        - drive GPIO4-7 low"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  toggle : invert masked output pins (read-modify-write on cached state)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : pins=0xNN  (or bare 0xNN)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.GPIO toggle pins=0x01  - invert GPIO0"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CH347.GPIO toggle 0xFF       - invert all 8 output pins"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : snapshot all 8 GPIO pins and print state"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.GPIO read"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : GPIO state: dir=0xNN  data=0xNN  [BBBBBBBB]"));

    // ── JTAG ──────────────────────────────────────────────────────────────
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("JTAG : JTAG TAP interface (must call open before any operation)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : clock rate 0=slowest, 5=fastest; IR/DR register arg is optional (remembers last used)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  open : open JTAG interface"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [rate=0-5] [device=PATH]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.JTAG open rate=2"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CH347.JTAG open rate=4 device=/dev/ch34xpis1"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  close : release JTAG interface"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.JTAG close"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : update JTAG clock rate without reopening"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : rate=0-5"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.JTAG cfg rate=3"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CH347.JTAG cfg ?             - print current config"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  reset : send TAP logic reset sequence"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : (none) | trst"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.JTAG reset            - TAP reset via TMS sequence"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CH347.JTAG reset trst       - assert TRST pin"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : shift bytes into IR or DR"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [ir|dr] HEXDATA"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.JTAG write ir FF      - load 0xFF into IR"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CH347.JTAG write dr DEADBEEF - shift 4 bytes into DR"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CH347.JTAG write DEADBEEF   - use last-used register (DR by default)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : shift N bytes out of IR or DR"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [ir|dr] N"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.JTAG read dr 4        - read 4 bytes from DR"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CH347.JTAG read ir 1        - read 1 byte from IR"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : received bytes printed as hex dump"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  wrrd : shift-in then shift-out in one operation"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [ir|dr] HEXDATA:rdlen"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.JTAG wrrd dr DEADBEEF:4  - write 4 bytes, read 4 bytes from DR"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CH347.JTAG wrrd ir FF:1         - write 0xFF, read 1 byte from IR"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : read bytes printed as hex dump"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  script : run a command script from ARTEFACTS_PATH (JTAG must be open)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : scriptname"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CH347.JTAG script jtag_prog.txt"));

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
