/*
 * HydraBus Plugin – core lifecycle and top-level command handlers
 *
 * Responsibilities:
 *   - Plugin entry / exit (C ABI)
 *   - doInit / doCleanup
 *   - INFO command
 *   - MODE command  →  creates / destroys HydraHAL protocol instances
 *   - Module dispatch maps (getModuleCmdsMap / getModuleSpeedsMap)
 *   - setModuleSpeed()  – routes speed index to the active protocol
 *   - INI parameter loading
 */

#include "hydrabus_plugin.hpp"

#include "uNumeric.hpp"
#include "uLogger.hpp"
#include "uHexdump.hpp"

#include <iostream>
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
#define LT_HDR   "HYDRABUS   |"
#define LOG_HDR  LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                   INI KEY STRINGS                             //
///////////////////////////////////////////////////////////////////

#define ARTEFACTS_PATH   "ARTEFACTS_PATH"
#define UART_PORT        "UART_PORT"
#define BAUDRATE         "BAUDRATE"
#define READ_TIMEOUT     "READ_TIMEOUT"
#define WRITE_TIMEOUT    "WRITE_TIMEOUT"
#define READ_BUF_SIZE    "READ_BUF_SIZE"
#define SCRIPT_DELAY     "SCRIPT_DELAY"

///////////////////////////////////////////////////////////////////
//                   PLUGIN ENTRY POINTS                         //
///////////////////////////////////////////////////////////////////

extern "C"
{
    EXPORTED HydrabusPlugin* pluginEntry()
    {
        return new HydrabusPlugin();
    }

    EXPORTED void pluginExit(HydrabusPlugin* p)
    {
        delete p;
    }
}

///////////////////////////////////////////////////////////////////
//                   INI ACCESSOR (friend)                       //
///////////////////////////////////////////////////////////////////

const HydrabusPlugin::IniValues* getAccessIniValues(const HydrabusPlugin& obj)
{
    return &obj.m_sIniValues;
}

///////////////////////////////////////////////////////////////////
//                   INIT / CLEANUP                              //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::doInit(void* /*pvUserData*/)
{
    m_drvUart.open(m_sIniValues.strUartPort, m_sIniValues.u32UartBaudrate);

    if (!m_drvUart.is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Failed to open UART:"); LOG_STRING(m_sIniValues.strUartPort));
        return false;
    }

    // Wrap the UART driver in a shared_ptr for HydraHAL
    auto drvPtr = std::shared_ptr<const ICommDriver>(&m_drvUart, [](const ICommDriver*){});
    m_pHydrabus = std::make_shared<HydraHAL::Hydrabus>(drvPtr);

    m_bIsInitialized = true;
    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Initialized on port:"); LOG_STRING(m_sIniValues.strUartPort));
    return true;
}

void HydrabusPlugin::doCleanup()
{
    if (m_bIsInitialized) {
        m_exit_mode();
        m_drvUart.close();
    }
    m_pHydrabus.reset();
    m_bIsInitialized = false;
    m_bIsEnabled     = false;
}

///////////////////////////////////////////////////////////////////
//                   MODE MANAGEMENT                             //
///////////////////////////////////////////////////////////////////

void HydrabusPlugin::m_exit_mode() const
{
    // Destroy whichever protocol object is live.
    // HydraHAL destructors do NOT send an exit command, so we must
    // send 0x00 explicitly to return to BBIO before deleting.
    if (m_pHydrabus && m_eMode != Mode::None) {
        // Attempt a graceful BBIO reset (20 × 0x00).
        // Ignore any errors – we are tearing down regardless.
        try { m_pHydrabus->enter_bbio(); } catch (...) {}
    }

    m_pSPI.reset();
    m_pI2C.reset();
    m_pUART.reset();
    m_pOneWire.reset();
    m_pRawWire.reset();
    m_pSWD.reset();
    m_pSmartcard.reset();
    m_pNFC.reset();
    m_pMMC.reset();
    m_pSDIO.reset();
    m_eMode = Mode::None;
}

bool HydrabusPlugin::m_enter_mode(const std::string& modeName)
{
    if (!m_pHydrabus) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Plugin not initialized"));
        return false;
    }

    // Tear down any existing session first
    m_exit_mode();

    // Enter BBIO
    if (!m_pHydrabus->enter_bbio()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to enter BBIO"));
        return false;
    }

    try {
        if (modeName == "spi") {
            m_pSPI   = std::make_unique<HydraHAL::SPI>(m_pHydrabus);
            m_eMode  = Mode::SPI;
        } else if (modeName == "i2c") {
            m_pI2C   = std::make_unique<HydraHAL::I2C>(m_pHydrabus);
            m_eMode  = Mode::I2C;
        } else if (modeName == "uart") {
            m_pUART  = std::make_unique<HydraHAL::UART>(m_pHydrabus);
            m_eMode  = Mode::UART;
        } else if (modeName == "onewire") {
            m_pOneWire  = std::make_unique<HydraHAL::OneWire>(m_pHydrabus);
            m_eMode     = Mode::OneWire;
        } else if (modeName == "rawwire") {
            m_pRawWire  = std::make_unique<HydraHAL::RawWire>(m_pHydrabus);
            m_eMode     = Mode::RawWire;
        } else if (modeName == "swd") {
            m_pSWD   = std::make_unique<HydraHAL::SWD>(m_pHydrabus);
            m_eMode  = Mode::SWD;
        } else if (modeName == "smartcard") {
            m_pSmartcard = std::make_unique<HydraHAL::Smartcard>(m_pHydrabus);
            m_eMode      = Mode::Smartcard;
        } else if (modeName == "nfc") {
            m_pNFC   = std::make_unique<HydraHAL::NFC>(m_pHydrabus);
            m_eMode  = Mode::NFC;
        } else if (modeName == "mmc") {
            m_pMMC   = std::make_unique<HydraHAL::MMC>(m_pHydrabus);
            m_eMode  = Mode::MMC;
        } else if (modeName == "sdio") {
            m_pSDIO  = std::make_unique<HydraHAL::SDIO>(m_pHydrabus);
            m_eMode  = Mode::SDIO;
        } else if (modeName == "bbio") {
            m_eMode = Mode::None;   // BBIO entry already done above
            LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Returned to BBIO"));
            return true;
        } else {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown mode:"); LOG_STRING(modeName));
            return false;
        }
    } catch (const std::exception& e) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Failed to enter mode"); LOG_STRING(modeName);
                  LOG_STRING(":"); LOG_STRING(e.what()));
        m_eMode = Mode::None;
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Mode active:"); LOG_STRING(modeName));
    return true;
}

///////////////////////////////////////////////////////////////////
//              PROTOCOL INSTANCE ACCESSORS                      //
///////////////////////////////////////////////////////////////////

#define PROTO_GETTER(Name, Type, field, modeval)                            \
HydraHAL::Type* HydrabusPlugin::m_##field() const {                        \
    if (m_eMode != Mode::modeval || !m_p##Name) {                          \
        LOG_PRINT(LOG_ERROR, LOG_HDR;                                       \
                  LOG_STRING("Not in " #Type " mode – call MODE " #field)); \
        return nullptr;                                                     \
    }                                                                       \
    return m_p##Name.get();                                                 \
}

PROTO_GETTER(SPI,       SPI,       spi,       SPI)
PROTO_GETTER(I2C,       I2C,       i2c,       I2C)
PROTO_GETTER(UART,      UART,      uart,      UART)
PROTO_GETTER(OneWire,   OneWire,   onewire,   OneWire)
PROTO_GETTER(RawWire,   RawWire,   rawwire,   RawWire)
PROTO_GETTER(SWD,       SWD,       swd,       SWD)
PROTO_GETTER(Smartcard, Smartcard, smartcard, Smartcard)
PROTO_GETTER(NFC,       NFC,       nfc,       NFC)
PROTO_GETTER(MMC,       MMC,       mmc,       MMC)
PROTO_GETTER(SDIO,      SDIO,      sdio,      SDIO)

#undef PROTO_GETTER

///////////////////////////////////////////////////////////////////
//              MAP ACCESSORS                                     //
///////////////////////////////////////////////////////////////////

ModuleCommandsMap<HydrabusPlugin>*
HydrabusPlugin::getModuleCmdsMap(const std::string& m) const
{
    auto it = m_mapCommandsMaps.find(m);
    return (it != m_mapCommandsMaps.end()) ? it->second : nullptr;
}

ModuleSpeedMap*
HydrabusPlugin::getModuleSpeedsMap(const std::string& m) const
{
    auto it = m_mapSpeedsMaps.find(m);
    return (it != m_mapSpeedsMaps.end()) ? it->second : nullptr;
}

///////////////////////////////////////////////////////////////////
//              setModuleSpeed                                    //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::setModuleSpeed(const std::string& module, size_t index) const
{
    if (module == "SPI") {
        auto* p = m_spi();
        if (!p) return false;
        return p->set_speed(static_cast<HydraHAL::SPI::Speed>(index));
    }
    if (module == "I2C") {
        auto* p = m_i2c();
        if (!p) return false;
        return p->set_speed(static_cast<HydraHAL::I2C::Speed>(index));
    }
    if (module == "RAWWIRE") {
        auto* p = m_rawwire();
        if (!p) return false;
        // Map index 0-3 to Hz values matching HydraHAL::RawWire::set_speed()
        static constexpr uint32_t hz[] = {5000, 50000, 100000, 1000000};
        if (index >= 4) return false;
        return p->set_speed(hz[index]);
    }
    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("No speed map for module:"); LOG_STRING(module));
    return false;
}

///////////////////////////////////////////////////////////////////
//              AUX HELPER (shared)                              //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_handle_aux_common(const std::string& args,
                                          HydraHAL::Protocol* proto) const
{
    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Use: aux [0-3] [in|out|pp] [0|1]"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("  e.g.  aux 0 out 1   – set AUX0 high"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("        aux 1 in      – set AUX1 as input"));
        return true;
    }
    if (!proto) return false;

    // Parse: "N [in|out|pp] [0|1]"
    std::vector<std::string> parts;
    ustring::tokenize(args, CHAR_SEPARATOR_SPACE, parts);
    if (parts.empty()) return false;

    size_t idx = 0;
    if (!numeric::str2sizet(parts[0], idx) || idx > 3) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("AUX index must be 0-3"));
        return false;
    }

    auto& pin = proto->aux(idx);

    if (parts.size() >= 2) {
        if      (parts[1] == "in")  { pin.set_direction(0); }
        else if (parts[1] == "out") { pin.set_direction(1); }
        else if (parts[1] == "pp")  { pin.set_pullup(true);  }
        else {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown direction:"); LOG_STRING(parts[1]));
            return false;
        }
    }
    if (parts.size() >= 3) {
        uint8_t v = 0;
        if (!numeric::str2uint8(parts[2], v)) return false;
        pin.set_value(v);
    }

    LOG_PRINT(LOG_INFO, LOG_HDR;
              LOG_STRING("AUX"); LOG_SIZET(idx);
              LOG_STRING("dir="); LOG_UINT8(static_cast<uint8_t>(pin.get_direction()));
              LOG_STRING("val="); LOG_UINT8(static_cast<uint8_t>(pin.get_value())));
    return true;
}

///////////////////////////////////////////////////////////////////
//              TOP-LEVEL COMMAND HANDLERS                       //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_Hydrabus_INFO(const std::string& args) const
{
    if (!args.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("INFO expects no arguments"));
        return false;
    }
    if (!m_bIsEnabled) return true;

    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Version :"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Build   :"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Device  : HydraBus"));
    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Port    :"); LOG_STRING(m_sIniValues.strUartPort));
    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Baud    :"); LOG_UINT32(m_sIniValues.u32UartBaudrate));

    static const char* modeNames[] = {
        "None","SPI","I2C","UART","OneWire","RawWire","SWD","Smartcard","NFC","MMC","SDIO"
    };
    LOG_PRINT(LOG_FIXED, LOG_HDR;
              LOG_STRING("Mode    :"); LOG_STRING(modeNames[static_cast<int>(m_eMode)]));

    LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Commands: INFO MODE SPI I2C UART ONEWIRE RAWWIRE SWD SMARTCARD NFC MMC SDIO"));
    return true;
}

bool HydrabusPlugin::m_Hydrabus_MODE(const std::string& args) const
{
    if (args.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("MODE requires an argument"));
        return false;
    }
    if (!m_bIsEnabled) return true;

    if (args == "help") {
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Available modes:"));
        for (const auto& m : m_mapModes) {
            LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("  -"); LOG_STRING(m.first));
        }
        return true;
    }

    return const_cast<HydrabusPlugin*>(this)->m_enter_mode(args);
}

///////////////////////////////////////////////////////////////////
//              INI PARAMETER LOADING                            //
///////////////////////////////////////////////////////////////////

bool HydrabusPlugin::m_LocalSetParams(const PluginDataSet* ps)
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

    getString(ARTEFACTS_PATH, m_sIniValues.strArtefactsPath);
    getString(UART_PORT,      m_sIniValues.strUartPort);
    getU32(BAUDRATE,          m_sIniValues.u32UartBaudrate);
    getU32(READ_TIMEOUT,      m_sIniValues.u32ReadTimeout);
    getU32(WRITE_TIMEOUT,     m_sIniValues.u32WriteTimeout);
    getU32(READ_BUF_SIZE,     m_sIniValues.u32UartReadBufferSize);
    getU32(SCRIPT_DELAY,      m_sIniValues.u32ScriptDelay);

    if (!ok)
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("One or more config values failed to parse"));

    return ok;
}
