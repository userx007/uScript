/*
 * CP2112 Plugin – core lifecycle and top-level command handlers
 *
 * The CP2112 is a USB-HID-to-I2C/SMBus bridge with 8 GPIO pins.
 * Two modules are exposed: I2C and GPIO.
 * Neither module requires open before INFO/setParams — only before data ops.
 */

#include "cp2112_plugin.hpp"

#include "uNumeric.hpp"
#include "uLogger.hpp"

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "CP2112      |"
#define LOG_HDR    LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                   INI KEY STRINGS                             //
///////////////////////////////////////////////////////////////////

#define ARTEFACTS_PATH  "ARTEFACTS_PATH"
#define DEVICE_INDEX    "DEVICE_INDEX"
#define I2C_CLOCK       "I2C_CLOCK"
#define I2C_ADDRESS     "I2C_ADDRESS"
#define READ_TIMEOUT    "READ_TIMEOUT"   
#define SCRIPT_DELAY    "SCRIPT_DELAY"   

///////////////////////////////////////////////////////////////////
//                   PLUGIN ENTRY POINTS                         //
///////////////////////////////////////////////////////////////////

extern "C"
{
    EXPORTED CP2112Plugin* pluginEntry()
    {
        return new CP2112Plugin();
    }

    EXPORTED void pluginExit(CP2112Plugin* p)
    {
        delete p;
    }
}

///////////////////////////////////////////////////////////////////
//                   INI ACCESSOR (friend)                       //
///////////////////////////////////////////////////////////////////

const CP2112Plugin::IniValues* getAccessIniValues(const CP2112Plugin& obj)
{
    return &obj.m_sIniValues;
}

///////////////////////////////////////////////////////////////////
//                   INIT / CLEANUP                              //
///////////////////////////////////////////////////////////////////

bool CP2112Plugin::doInit(void* /*pvUserData*/)
{
    // Seed pending configs from INI values so that an open without
    // explicit parameters uses whatever was in the config file.
    m_sI2cCfg.clockHz = m_sIniValues.u32I2cClockHz;
    m_sI2cCfg.address = m_sIniValues.u8I2cAddress;

    m_bIsInitialized = true;

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("Initialized — CP2112 (VID 0x10C4 / PID 0xEA90)");
              LOG_STRING("device index:"); LOG_UINT32(m_sIniValues.u8DeviceIndex));
    return true;
}

void CP2112Plugin::doCleanup()
{
    if (m_pI2C)  { m_pI2C->close();  m_pI2C.reset();  }
    if (m_pGPIO) { m_pGPIO->close(); m_pGPIO.reset(); }
    m_bIsInitialized = false;
    m_bIsEnabled     = false;
}

///////////////////////////////////////////////////////////////////
//              DRIVER INSTANCE ACCESSORS                        //
///////////////////////////////////////////////////////////////////

CP2112* CP2112Plugin::m_i2c() const
{
    if (m_bIsEnabled) {
        if (!m_pI2C || !m_pI2C->is_open()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("I2C not open — call CP2112.I2C open [addr=0xNN] [clock=N]"));
            return nullptr;
        }
        return m_pI2C.get();
    }
    return nullptr;
}

CP2112Gpio* CP2112Plugin::m_gpio() const
{
    if (m_bIsEnabled) {    
        if (!m_pGPIO || !m_pGPIO->is_open()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("GPIO not open — call CP2112.GPIO open [device=N] [dir=0xNN] ..."));
            return nullptr;
        }
        return m_pGPIO.get();
    }
    return nullptr;    
}

///////////////////////////////////////////////////////////////////
//              MAP ACCESSORS                                     //
///////////////////////////////////////////////////////////////////

ModuleCommandsMap<CP2112Plugin>*
CP2112Plugin::getModuleCmdsMap(const std::string& m) const
{
    auto it = m_mapCommandsMaps.find(m);
    return (it != m_mapCommandsMaps.end()) ? it->second : nullptr;
}

ModuleSpeedMap*
CP2112Plugin::getModuleSpeedsMap(const std::string& m) const
{
    auto it = m_mapSpeedsMaps.find(m);
    if (it == m_mapSpeedsMaps.end()) return nullptr;
    return it->second;
}

///////////////////////////////////////////////////////////////////
//              setModuleSpeed                                    //
///////////////////////////////////////////////////////////////////

bool CP2112Plugin::setModuleSpeed(const std::string& module, size_t hz) const
{
    if (module == "I2C") {
        m_sI2cCfg.clockHz = static_cast<uint32_t>(hz);

        if (m_pI2C && m_pI2C->is_open()) {
            m_pI2C->close();
            auto s = m_pI2C->open(m_sI2cCfg.address,
                                  m_sI2cCfg.clockHz,
                                  m_sIniValues.u8DeviceIndex);
            if (s != CP2112::Status::SUCCESS) {
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

    if (module == "GPIO") {
        // CP2112 GPIO has no numeric data-rate; the only "clock" is the
        // optional clock-output on GPIO.6, which is set via clockDivider in
        // the GpioConfig.  Direct frequency manipulation is not supported here.
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("GPIO has no speed setting (use cfg clkdiv=N for clock output)"));
        return false;
    }

    LOG_PRINT(LOG_ERROR, LOG_HDR;
              LOG_STRING("setModuleSpeed: unknown module:"); LOG_STRING(module));
    return false;
}

///////////////////////////////////////////////////////////////////
//              TOP-LEVEL COMMAND HANDLERS                       //
///////////////////////////////////////////////////////////////////

bool CP2112Plugin::m_CP2112_INFO(const std::string& args) const
{
    if (!args.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("INFO expects no arguments"));
        return false;
    }

    if (!m_bIsEnabled)
    {
        return true;
    }

    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING(CP2112_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: Silicon Labs CP2112 USB-HID to I2C/SMBus bridge with 8 GPIO pins"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  VID 0x10C4 / PID 0xEA90  DeviceIndex:"); LOG_UINT32(m_sIniValues.u8DeviceIndex));

    // ── I2C ───────────────────────────────────────────────────────────────
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("I2C : I2C/SMBus master port (must call open before any transfer)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : each open targets one 7-bit slave address; max payload 512 bytes, auto-chunked at 61-byte HID boundaries"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  open : open I2C interface and set slave address / clock"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [addr=0xNN] [clock=N] [device=N]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           addr   : 7-bit slave address (default 0x50)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           clock  : I2C clock in Hz (default 100000; presets: 10kHz 100kHz 400kHz)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           device : zero-based index when multiple CP2112 are attached (default 0)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CP2112.I2C open addr=0x50 clock=400000"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CP2112.I2C open addr=0x68 clock=100000 device=1"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  close : release I2C interface"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CP2112.I2C close"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : update pending I2C address / clock without reopening"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [addr=0xNN] [clock=N]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CP2112.I2C cfg addr=0x68 clock=400000"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CP2112.I2C cfg ?           - print current pending config"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : cfg changes take effect on the next open, not immediately"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : write bytes (I2C START + addr+W + data + STOP)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA   (hex bytes, no spaces; max 512 bytes)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CP2112.I2C write 0000        - write register 0x00, value 0x00"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CP2112.I2C write A550FF      - write 3 bytes to open slave"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : ACK/NACK status"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : read N bytes from the current slave address"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N   (byte count, 1..512)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CP2112.I2C read 2            - read 2 bytes from configured slave"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : received bytes printed as hex dump"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  wrrd : write then read in one sequence"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA:rdlen"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CP2112.I2C wrrd 0000:2       - write 0x00 0x00, then read 2 bytes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CP2112.I2C wrrd 50:1         - write 0x50, read 1 byte"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : read bytes printed as hex dump"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  wrrdf : write/read using binary files from ARTEFACTS_PATH"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : filename[:wrchunk][:rdchunk]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CP2112.I2C wrrdf i2c_seq.bin"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  scan : probe all I2C addresses 0x08..0x77 for ACK"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CP2112.I2C scan              - no open required; uses current clock and device index"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : list of responding device addresses (e.g. Found device at 0x48)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  script : run a command script from ARTEFACTS_PATH (I2C must be open)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : scriptname"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CP2112.I2C script eeprom_test.txt"));

    // ── GPIO ──────────────────────────────────────────────────────────────
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("GPIO : 8-pin GPIO port GPIO.0-GPIO.7 (must call open before use)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : all masks are 8-bit; bit N = GPIO.N  (bit 0 = GPIO.0, bit 7 = GPIO.7)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : special-function pins — GPIO.0=TX_LED  GPIO.1=IRQ_OUT  GPIO.6=CLK_OUT  GPIO.7=RX_LED"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  open : open GPIO interface and apply initial pin configuration"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [device=N] [dir=0xNN] [pp=0xNN] [special=0xNN] [clkdiv=N]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           device  : CP2112 device index (default 0)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           dir     : direction mask — 1=output, 0=input (default 0x00 = all inputs)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           pp      : drive-mode mask — 1=push-pull, 0=open-drain (default 0x00)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           special : special-function enable mask (default 0x00 = all GPIO)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           clkdiv  : clock divider for GPIO.6 clock-output (only active when special bit 6 = 1)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CP2112.GPIO open dir=0xFF pp=0xFF         - all outputs, push-pull"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CP2112.GPIO open device=0 dir=0x0F pp=0x0F  - GPIO.0-3 outputs, GPIO.4-7 inputs"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CP2112.GPIO open special=0x42 clkdiv=4    - GPIO.1=IRQ_OUT, GPIO.6=CLK_OUT"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  close : release GPIO interface"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CP2112.GPIO close"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : update pin configuration (applied immediately if open, else stored for next open)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [dir=0xNN] [pp=0xNN] [special=0xNN] [clkdiv=N]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CP2112.GPIO cfg dir=0x0F pp=0x0F    - lower nibble as push-pull outputs"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CP2112.GPIO cfg special=0x00         - disable all special functions"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CP2112.GPIO cfg ?                    - print current pending config"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : drive output pins with selective mask"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : VALUE MASK"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           VALUE : desired pin levels 0x00..0xFF (1=high, 0=low)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           MASK  : which pins to update (1=update, 0=leave unchanged)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CP2112.GPIO write 0x01 0x01  - set GPIO.0 high, leave others unchanged"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CP2112.GPIO write 0xAA 0xFF  - apply 0xAA pattern to all 8 pins"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CP2112.GPIO write 0x00 0x0F  - clear GPIO.0-3, leave GPIO.4-7 unchanged"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  set : drive masked pins HIGH (leaves unmasked pins unchanged)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : MASK   (0x00..0xFF bitmask)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CP2112.GPIO set 0x01         - GPIO.0 high"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CP2112.GPIO set 0x0F         - GPIO.0-3 all high"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  clear : drive masked pins LOW (leaves unmasked pins unchanged)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : MASK   (0x00..0xFF bitmask)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CP2112.GPIO clear 0x01       - GPIO.0 low"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           CP2112.GPIO clear 0xF0       - GPIO.4-7 all low"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : read current logic levels of all 8 GPIO pins"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: CP2112.GPIO read"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : GPIO: 0xNN  [BBBBBBBB]  (hex value + binary bit pattern, MSB first)"));
    LOG_SEP();

    return true;
}

bool CP2112Plugin::m_CP2112_I2C(const std::string& args) const
{
    return generic_module_dispatch<CP2112Plugin>(this, "I2C", args);
}

bool CP2112Plugin::m_CP2112_GPIO(const std::string& args) const
{
    return generic_module_dispatch<CP2112Plugin>(this, "GPIO", args);
}

///////////////////////////////////////////////////////////////////
//              INI PARAMETER LOADING                            //
///////////////////////////////////////////////////////////////////

bool CP2112Plugin::m_LocalSetParams(const PluginDataSet* ps)
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
    getU32  (I2C_CLOCK,       m_sIniValues.u32I2cClockHz);
    getU8   (I2C_ADDRESS,     m_sIniValues.u8I2cAddress);
    getU32  (READ_TIMEOUT,    m_sIniValues.u32ReadTimeout);
    getU32  (SCRIPT_DELAY,    m_sIniValues.u32ScriptDelay);

    if (!ok) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("One or more config values failed to parse"));
    }

    return ok;
}
