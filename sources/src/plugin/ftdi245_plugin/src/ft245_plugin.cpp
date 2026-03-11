/*
 * FT245 Plugin – core lifecycle and top-level command handlers
 *
 * The FT245 is a parallel FIFO USB bridge — unlike the MPSSE-based FT2232
 * family it has no serial protocol engine, no clock divisor, and no
 * channel selector.  The two modules are:
 *
 *   FIFO  — bulk byte-stream (async or sync FIFO mode)
 *   GPIO  — byte-wide bit-bang on D0–D7 (mutually exclusive with FIFO)
 */

#include "ft245_plugin.hpp"

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
#define LT_HDR   "FT245      |"
#define LOG_HDR  LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                   INI KEY STRINGS                             //
///////////////////////////////////////////////////////////////////

#define ARTEFACTS_PATH   "ARTEFACTS_PATH"
#define DEVICE_INDEX     "DEVICE_INDEX"
#define DEFAULT_VARIANT  "VARIANT"        // "BM" or "R"
#define DEFAULT_FIFO_MODE "FIFO_MODE"     // "async" or "sync"
#define READ_TIMEOUT     "READ_TIMEOUT"   // ms, used by script execution
#define SCRIPT_DELAY     "SCRIPT_DELAY"   // ms inter-command delay for scripts

///////////////////////////////////////////////////////////////////
//                   PLUGIN ENTRY POINTS                         //
///////////////////////////////////////////////////////////////////

extern "C"
{
    EXPORTED FT245Plugin* pluginEntry()
    {
        return new FT245Plugin();
    }

    EXPORTED void pluginExit(FT245Plugin* p)
    {
        delete p;
    }
}

///////////////////////////////////////////////////////////////////
//                   INI ACCESSOR (friend)                       //
///////////////////////////////////////////////////////////////////

const FT245Plugin::IniValues* getAccessIniValues(const FT245Plugin& obj)
{
    return &obj.m_sIniValues;
}

///////////////////////////////////////////////////////////////////
//                   PARSE HELPERS                               //
///////////////////////////////////////////////////////////////////

bool FT245Plugin::parseVariant(const std::string& s, FT245Base::Variant& out)
{
    if (s == "BM" || s == "bm" || s == "FT245BM" || s == "245BM" || s == "RL") {
        out = FT245Base::Variant::FT245BM; return true;
    }
    if (s == "R"  || s == "r"  || s == "FT245R"  || s == "245R") {
        out = FT245Base::Variant::FT245R;  return true;
    }
    LOG_PRINT(LOG_ERROR, LOG_STRING("FT245      |");
              LOG_STRING("Invalid variant (use BM or R):"); LOG_STRING(s));
    return false;
}

bool FT245Plugin::parseFifoMode(const std::string& s, FT245Base::FifoMode& out)
{
    if (s == "async" || s == "ASYNC" || s == "a") { out = FT245Base::FifoMode::Async; return true; }
    if (s == "sync"  || s == "SYNC"  || s == "s") { out = FT245Base::FifoMode::Sync;  return true; }
    LOG_PRINT(LOG_ERROR, LOG_STRING("FT245      |");
              LOG_STRING("Invalid FIFO mode (use async or sync):"); LOG_STRING(s));
    return false;
}

bool FT245Plugin::parseFifoParams(const std::string& args,
                                   FifoPendingCfg& cfg,
                                   uint8_t* pDeviceIndexOut)
{
    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);

    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() != 2) continue;

        bool ok = true;
        if (kv[0] == "variant") {
            ok = parseVariant(kv[1], cfg.variant);
        } else if (kv[0] == "mode") {
            ok = parseFifoMode(kv[1], cfg.fifoMode);
        } else if (kv[0] == "device" && pDeviceIndexOut) {
            ok = numeric::str2uint8(kv[1], *pDeviceIndexOut);
        } else {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Unknown key:"); LOG_STRING(kv[0]));
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

bool FT245Plugin::parseGpioParams(const std::string& args,
                                   GpioPendingCfg& cfg,
                                   uint8_t* pDeviceIndexOut)
{
    std::vector<std::string> pairs;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, pairs);

    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        ustring::tokenize(pair, '=', kv);
        if (kv.size() != 2) continue;

        bool ok = true;
        if (kv[0] == "variant") {
            ok = parseVariant(kv[1], cfg.variant);
        } else if (kv[0] == "dir") {
            ok = numeric::str2uint8(kv[1], cfg.dirMask);
        } else if (kv[0] == "val" || kv[0] == "value") {
            ok = numeric::str2uint8(kv[1], cfg.initValue);
        } else if (kv[0] == "device" && pDeviceIndexOut) {
            ok = numeric::str2uint8(kv[1], *pDeviceIndexOut);
        } else {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Unknown key:"); LOG_STRING(kv[0]));
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
//                   INIT / CLEANUP                              //
///////////////////////////////////////////////////////////////////

bool FT245Plugin::doInit(void* /*pvUserData*/)
{
    m_sFifoCfg.variant  = m_sIniValues.eDefaultVariant;
    m_sFifoCfg.fifoMode = m_sIniValues.eDefaultFifoMode;
    m_sGpioCfg.variant  = m_sIniValues.eDefaultVariant;

    m_bIsInitialized = true;

    const char* varStr  = (m_sIniValues.eDefaultVariant == FT245Base::Variant::FT245BM)
                          ? "FT245BM/RL" : "FT245R";
    const char* modeStr = (m_sIniValues.eDefaultFifoMode == FT245Base::FifoMode::Async)
                          ? "Async" : "Sync";

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("Initialized — variant:"); LOG_STRING(varStr);
              LOG_STRING("default FIFO mode:"); LOG_STRING(modeStr);
              LOG_STRING("device index:"); LOG_UINT32(m_sIniValues.u8DeviceIndex));
    return true;
}

void FT245Plugin::doCleanup()
{
    if (m_pFIFO) { m_pFIFO->close(); m_pFIFO.reset(); }
    if (m_pGPIO) { m_pGPIO->close(); m_pGPIO.reset(); }
    m_bIsInitialized = false;
    m_bIsEnabled     = false;
}

///////////////////////////////////////////////////////////////////
//              DRIVER INSTANCE ACCESSORS                        //
///////////////////////////////////////////////////////////////////

FT245Sync* FT245Plugin::m_fifo() const
{
    if (!m_pFIFO || !m_pFIFO->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("FIFO not open — call FT245.FIFO open [...]"));
        return nullptr;
    }
    return m_pFIFO.get();
}

FT245GPIO* FT245Plugin::m_gpio() const
{
    if (!m_pGPIO || !m_pGPIO->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("GPIO not open — call FT245.GPIO open [...]"));
        return nullptr;
    }
    return m_pGPIO.get();
}

///////////////////////////////////////////////////////////////////
//              MAP ACCESSORS                                     //
///////////////////////////////////////////////////////////////////

ModuleCommandsMap<FT245Plugin>*
FT245Plugin::getModuleCmdsMap(const std::string& m) const
{
    auto it = m_mapCommandsMaps.find(m);
    return (it != m_mapCommandsMaps.end()) ? it->second : nullptr;
}

ModuleSpeedMap*
FT245Plugin::getModuleSpeedsMap(const std::string& m) const
{
    auto it = m_mapSpeedsMaps.find(m);
    if (it == m_mapSpeedsMaps.end()) return nullptr;
    return it->second;
}

///////////////////////////////////////////////////////////////////
//              setModuleSpeed                                    //
///////////////////////////////////////////////////////////////////

bool FT245Plugin::setModuleSpeed(const std::string& module, size_t /*hz*/) const
{
    // The FT245 has no configurable clock divisor — transfer rate is
    // entirely governed by the USB bulk transfer engine.  Speed presets
    // are not applicable.
    LOG_PRINT(LOG_WARNING, LOG_HDR;
              LOG_STRING("setModuleSpeed: FT245 has no configurable clock;");
              LOG_STRING("module:"); LOG_STRING(module);
              LOG_STRING("— speed setting ignored"));
    return false;
}

///////////////////////////////////////////////////////////////////
//              TOP-LEVEL COMMAND HANDLERS                       //
///////////////////////////////////////////////////////////////////

bool FT245Plugin::m_FT245_FIFO(const std::string& args) const
{
    return generic_module_dispatch<FT245Plugin>(this, "FIFO", args);
}

bool FT245Plugin::m_FT245_GPIO(const std::string& args) const
{
    return generic_module_dispatch<FT245Plugin>(this, "GPIO", args);
}

bool FT245Plugin::m_FT245_INFO(const std::string& args) const
{
    if (!args.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("INFO expects no arguments"));
        return false;
    }

    if (!m_bIsEnabled) return true;

    LOG_PRINT(LOG_EMPTY, LOG_STRING(FT245_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: FTDI FT245 USB parallel FIFO interface"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Variants: FT245BM/RL (async+sync FIFO, up to 1 MB/s sync)  |  FT245R (async FIFO only, integrated oscillator)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  DeviceIndex:"); LOG_UINT32(m_sIniValues.u8DeviceIndex);
                         LOG_STRING("  Default variant:");
                         LOG_STRING(m_sIniValues.eDefaultVariant == FT245Base::Variant::FT245BM ? "BM (FT245BM/RL)" : "R (FT245R)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note: FIFO and GPIO modes are mutually exclusive — close one before opening the other"));

    // ── FIFO ──────────────────────────────────────────────────────────────
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("FIFO : bulk byte-stream via FT245 parallel FIFO (must call open before any transfer)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : async mode works on both FT245BM and FT245R"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : sync mode (up to 1 MB/s) is FT245BM/RL only — rejected for FT245R"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : no clock divisor; USB bulk transfer rate is hardware-governed"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  open : open FIFO interface and configure transfer mode"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [variant=BM|R] [mode=async|sync] [device=N]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           variant : BM=FT245BM/RL (default) | R=FT245R"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           mode    : async (default, both variants) | sync (FT245BM only)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           device  : zero-based FT245 device index (default 0)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT245.FIFO open variant=BM mode=async"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT245.FIFO open variant=BM mode=sync"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT245.FIFO open variant=R"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  close : release FIFO interface"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT245.FIFO close"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : update FIFO configuration (takes effect on next open)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [variant=BM|R] [mode=async|sync]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT245.FIFO cfg mode=sync"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT245.FIFO cfg ?        - print current pending config"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : transmit bytes into TX FIFO"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA   (hex bytes, no spaces, up to 65536)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT245.FIFO write DEADBEEF   - write 4 bytes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : receive N bytes from RX FIFO"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N   (byte count)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT245.FIFO read 4"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : bytes printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  wrrd : write then read in sequence"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA:rdlen  (e.g. 9F:3 or :4 or DEADBEEF)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT245.FIFO wrrd 9F00:4     - write 2 bytes, read 4"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : read bytes printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  wrrdf : write/read using binary files from ARTEFACTS_PATH"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : filename[:wrchunk][:rdchunk]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT245.FIFO wrrdf payload.bin"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT245.FIFO wrrdf payload.bin:4096:4096"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  flush : purge RX and TX FIFOs without closing"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT245.FIFO flush"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  script : run a CommScript from ARTEFACTS_PATH (FIFO must be open)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : scriptname"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT245.FIFO script comm_test.txt"));

    // ── GPIO ──────────────────────────────────────────────────────────────
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("GPIO : byte-wide bit-bang GPIO on D0–D7 via BITMODE_BITBANG (must call open before use)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : bit-bang and FIFO modes are mutually exclusive on one device"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : direction mask 1=output, 0=input (per D0–D7 pin)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : read samples all 8 pins; output pins echo the last written value"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  open : open GPIO bit-bang mode"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [variant=BM|R] [dir=0xNN] [val=0xNN] [device=N]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           variant : BM=FT245BM/RL (default) | R=FT245R"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           dir     : direction mask (1=output; default 0x00 = all inputs)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           val     : initial output levels (default 0x00)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           device  : zero-based device index (default 0)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT245.GPIO open variant=BM dir=0xFF val=0x00"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT245.GPIO open dir=0xF0   - upper nibble outputs, lower inputs"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT245.GPIO open variant=R  - FT245R, all inputs"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  close : release GPIO interface (resets all pins to inputs)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT245.GPIO close"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : update pending GPIO configuration (takes effect on next open)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [variant=BM|R] [dir=0xNN] [val=0xNN]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT245.GPIO cfg dir=0xFF val=0xAA"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT245.GPIO cfg ?           - print current pending config"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  dir : set pin direction at runtime"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : MASK [INITVAL]   (hex bytes; 1=output, 0=input per D0–D7)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT245.GPIO dir 0xFF 0x00  - all outputs, initially low"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT245.GPIO dir 0x0F       - D0–D3 outputs, D4–D7 inputs"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : write a byte to all output pins"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : VALUE  (hex byte)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT245.GPIO write 0xAA     - drive D0–D7 to 0xAA"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  set : drive masked pins HIGH (leaves unmasked pins unchanged)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : MASK  (hex byte)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT245.GPIO set 0x01       - D0 high"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT245.GPIO set 0x80       - D7 high"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  clear : drive masked pins LOW (leaves unmasked pins unchanged)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : MASK  (hex byte)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT245.GPIO clear 0x01     - D0 low"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT245.GPIO clear 0xF0     - D4–D7 all low"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  toggle : invert masked output pins"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : MASK  (hex byte)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT245.GPIO toggle 0x01    - invert D0"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT245.GPIO toggle 0xFF    - invert all outputs"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : sample all 8 pins; prints hex + binary"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT245.GPIO read"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : D0–D7: 0xNN  [BBBBBBBB]  (hex + binary, D7 MSB)"));

    return true;
}

///////////////////////////////////////////////////////////////////
//              INI PARAMETER LOADING                            //
///////////////////////////////////////////////////////////////////

bool FT245Plugin::m_LocalSetParams(const PluginDataSet* ps)
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
    auto getVar = [&](const char* key, FT245Base::Variant& dst) {
        if (m.count(key)) ok &= parseVariant(m.at(key), dst);
    };
    auto getMode = [&](const char* key, FT245Base::FifoMode& dst) {
        if (m.count(key)) ok &= parseFifoMode(m.at(key), dst);
    };

    getString(ARTEFACTS_PATH,    m_sIniValues.strArtefactsPath);
    getU8   (DEVICE_INDEX,       m_sIniValues.u8DeviceIndex);
    getVar  (DEFAULT_VARIANT,    m_sIniValues.eDefaultVariant);
    getMode (DEFAULT_FIFO_MODE,  m_sIniValues.eDefaultFifoMode);
    getU32  (READ_TIMEOUT,       m_sIniValues.u32ReadTimeout);
    getU32  (SCRIPT_DELAY,       m_sIniValues.u32ScriptDelay);

    if (!ok)
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("One or more config values failed to parse"));

    return ok;
}
