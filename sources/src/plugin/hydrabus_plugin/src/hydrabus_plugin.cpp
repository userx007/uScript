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
#include <stdexcept>
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
    drvUart.open(m_sIniValues.strUartPort, m_sIniValues.u32UartBaudrate);

    if (!drvUart.is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Failed to open UART:"); LOG_STRING(m_sIniValues.strUartPort));
        return false;
    }

    // Wrap the UART driver in a shared_ptr for HydraHAL
    auto drvPtr = std::shared_ptr<const ICommDriver>(&drvUart, [](const ICommDriver*){});
    try {
        m_pHydrabus = std::make_shared<HydraHAL::Hydrabus>(drvPtr);
    } catch (const std::invalid_argument& e) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Failed to create Hydrabus instance:"); LOG_STRING(e.what()));
        drvUart.close();
        return false;
    }

    m_bIsInitialized = true;
    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Initialized on port:"); LOG_STRING(m_sIniValues.strUartPort));
    return true;
}

void HydrabusPlugin::doCleanup()
{
    if (m_bIsInitialized) {
        m_exit_mode();
        drvUart.close();
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
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Use: aux [0-3] [in|out|pp] [0|1]"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("  e.g.  aux 0 out 1   – set AUX0 high"));
        LOG_PRINT(LOG_EMPTY, LOG_STRING("        aux 1 in      – set AUX1 as input"));
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

    try {
        auto& pin = proto->aux(idx);

        if (parts.size() >= 2) {
            if      (parts[1] == "in")  { pin.set_direction(HydraHAL::AUXPin::Direction::Input);  }
            else if (parts[1] == "out") { pin.set_direction(HydraHAL::AUXPin::Direction::Output); }
            else if (parts[1] == "pp")  { pin.set_pullup(1); }
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
    } catch (const std::out_of_range& e) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("AUX pin error:"); LOG_STRING(e.what()));
        return false;
    }
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

    LOG_PRINT(LOG_EMPTY, LOG_STRING(HYDRABUS_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: HydraBus multi-protocol interface (SPI/I2C/UART/1-Wire/RawWire/SWD/Smartcard/NFC/MMC/SDIO)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Port:"); LOG_STRING(m_sIniValues.strUartPort);
                         LOG_STRING("  Baud:"); LOG_UINT32(m_sIniValues.u32UartBaudrate));

    // ── MODE ──────────────────────────────────────────────────────────────
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("MODE : activate a protocol module (must be called before any protocol command)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Args : spi | i2c | uart | onewire | rawwire | swd | smartcard | nfc | mmc | sdio | bbio"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Usage: HYDRABUS.MODE spi          - enter SPI mode"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         HYDRABUS.MODE bbio         - exit current mode, return to BBIO idle"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : only one protocol is active at a time; switching mode tears down the previous one"));

    // ── SPI ───────────────────────────────────────────────────────────────
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SPI : full-duplex SPI bus (prerequisite: MODE spi)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : set SPI bus parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : polarity=[0|1]  phase=[0|1]  device=[0|1]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SPI cfg polarity=0 phase=0 device=0   - CPOL=0, CPHA=0 (mode 0)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.SPI cfg polarity=1 phase=1            - change polarity and phase only"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.SPI cfg ?                             - print current cfg"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  speed : set SPI clock frequency"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : 320kHz | 650kHz | 1MHz | 2MHz | 5MHz | 10MHz | 21MHz | 42MHz"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SPI speed 10MHz"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cs : drive chip-select pin"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : en | dis   (en = assert CS low, dis = deassert CS high)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SPI cs en"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.SPI cs dis"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : transmit bytes (full-duplex; MISO bytes are printed)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA   (1-16 bytes as hex string, no spaces)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SPI write DEADBEEF         - send 4 bytes, print MISO"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : MISO bytes printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : receive bytes (clocks 0xFF on MOSI)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N   (byte count)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SPI read 4                 - read 4 bytes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : received bytes printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  wrrd : write then read in a single CS-asserted transaction"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA:rdlen   (write hex bytes, then read rdlen bytes)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SPI wrrd 9F:3               - send cmd 0x9F, read 3 bytes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.SPI wrrd DEADBEEF:4         - write 4 bytes, read 4 bytes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : read bytes printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  wrrdf : write/read using binary files from ARTEFACTS_PATH"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : filename[:wrchunk][:rdchunk]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SPI wrrdf flash_cmd.bin     - send file content, capture response"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.SPI wrrdf payload.bin:256:256"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  script : run a command script from ARTEFACTS_PATH"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : scriptname"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SPI script read_flash.txt"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  aux : control AUX GPIO pins (shared across all protocols)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : pin_idx [in|out|pp] [0|1]   (pin_idx = 0-3)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SPI aux 0 out 1    - set AUX0 as output, drive high"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.SPI aux 1 in       - set AUX1 as input"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.SPI aux 2 pp       - enable pull-up on AUX2"));

    // ── I2C ───────────────────────────────────────────────────────────────
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("I2C : I2C bus master (prerequisite: MODE i2c)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : set I2C bus parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : pullup=[0|1]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.I2C cfg pullup=1   - enable internal pull-ups"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.I2C cfg ?          - print current cfg"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  speed : set I2C clock speed"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : 50kHz | 100kHz | 400kHz | 1MHz"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.I2C speed 400kHz"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  bit : send a single bus-control sequence"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : start | stop | ack | nack"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.I2C bit start      - send START condition"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.I2C bit stop       - send STOP condition"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.I2C bit ack        - clock out ACK bit"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.I2C bit nack       - clock out NACK bit"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : use for manual framing when wrrd is not sufficient"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : write bytes to bus (ACK/NACK status printed per byte)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA   (1-16 bytes)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.I2C write A050     - write address 0xA0 + reg 0x50"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : ACK/NACK status per byte"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : read N bytes (ACKs all bytes except the last)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N   (byte count)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.I2C read 2         - read 2 bytes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : received bytes printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  wrrd : write then repeated-START read (standard I2C register access)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA:rdlen"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.I2C wrrd A050:2    - write 0xA0 0x50, read 2 bytes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : read bytes printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  wrrdf : write/read using files from ARTEFACTS_PATH"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : filename[:wrchunk][:rdchunk]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.I2C wrrdf i2c_seq.bin"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  scan : probe all 7-bit addresses and report responding devices"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.I2C scan"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : list of detected I2C addresses (e.g. Found device at 0x48)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  stretch : set clock-stretch timeout in cycles (0 = disabled)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N   (cycle count)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.I2C stretch 1000   - allow up to 1000 stretch cycles"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.I2C stretch 0      - disable clock stretching"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  script : run a command script from ARTEFACTS_PATH"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.I2C script eeprom_test.txt"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  aux : control AUX GPIO pins (see SPI aux for full usage)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.I2C aux 0 out 0    - drive AUX0 low"));

    // ── UART ──────────────────────────────────────────────────────────────
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("UART : UART serial interface (prerequisite: MODE uart)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  baud : set baud rate"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N   (baud rate, e.g. 9600 / 115200 / 921600)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.UART baud 115200"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  parity : set parity"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : none | even | odd"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.UART parity none"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.UART parity even"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  echo : enable/disable local echo of received bytes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : on | off"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.UART echo on"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  bridge : enter transparent UART bridge (blocking until UBTN on HydraBus is pressed)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.UART bridge"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : all host-side bytes are forwarded to UART TX and vice versa; press UBTN to exit"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : transmit raw bytes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA   (1-16 bytes)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.UART write 48454C4C4F    - send 'HELLO'"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : receive N bytes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N   (byte count)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.UART read 8"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : received bytes printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  script : run a command script from ARTEFACTS_PATH"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.UART script uart_init.txt"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  aux : control AUX GPIO pins (see SPI aux for full usage)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.UART aux 3 out 1"));

    // ── ONEWIRE ───────────────────────────────────────────────────────────
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("ONEWIRE : Dallas/Maxim 1-Wire bus (prerequisite: MODE onewire)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : set 1-Wire bus parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : pullup=[0|1]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.ONEWIRE cfg pullup=1   - enable internal pull-up on DQ"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.ONEWIRE cfg ?          - print current cfg"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  reset : send 1-Wire reset pulse and check for device presence"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.ONEWIRE reset"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : 1 = device(s) present, 0 = no device detected"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : write bytes onto the 1-Wire bus"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA   (1-16 bytes)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.ONEWIRE write CC       - send SKIP ROM command"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.ONEWIRE write CC44     - SKIP ROM + Convert T (DS18B20)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : read N bytes from the 1-Wire bus"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N   (byte count)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.ONEWIRE read 9         - read 9-byte scratchpad"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : received bytes printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  swio : ARM Serial Wire I/O register access (SWO/SWIO debug register bus)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : init"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           read  ADDR            (ADDR = 1 hex byte)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           write ADDR VALUE      (ADDR = 1 hex byte, VALUE = 4 hex bytes LE)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.ONEWIRE swio init               - initialise SWIO bus"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.ONEWIRE swio read 00             - read register 0x00"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.ONEWIRE swio write 04 50000000   - write 0x00000050 to reg 0x04"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : (read) register value printed as 0xXXXXXXXX"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  aux : control AUX GPIO pins (see SPI aux for full usage)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.ONEWIRE aux 0 out 1"));

    // ── RAWWIRE ───────────────────────────────────────────────────────────
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RAWWIRE : bit-bang 2/3-wire protocol (prerequisite: MODE rawwire)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : set RawWire bus parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : polarity=[0|1]  wires=[2|3]  gpio=[0|1]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.RAWWIRE cfg polarity=0 wires=2 gpio=0"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.RAWWIRE cfg ?          - print current cfg"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  speed : set bit-bang clock speed"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : 5kHz | 50kHz | 100kHz | 1MHz"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.RAWWIRE speed 100kHz"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  sda : drive SDA/MOSI pin level"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : 0 | 1"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.RAWWIRE sda 1         - drive SDA high"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.RAWWIRE sda 0         - drive SDA low"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  clk : drive CLK pin or generate a single clock pulse"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : 0 | 1 | tick"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.RAWWIRE clk tick      - generate one clock edge"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.RAWWIRE clk 0         - drive CLK low"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  bit : send N bits from a hex byte (MSB first)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N HEXBYTE   (N = 1-8, HEXBYTE = 1 hex byte)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.RAWWIRE bit 8 A5      - clock out all 8 bits of 0xA5"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.RAWWIRE bit 7 A5      - clock out upper 7 bits"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  ticks : generate N bare clock pulses (no data)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N   (1-16)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.RAWWIRE ticks 8"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : bulk write bytes; MISO bytes are printed"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA   (1-16 bytes)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.RAWWIRE write CAFE"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : MISO bytes printed if any were captured"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : read N bytes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N   (byte count)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.RAWWIRE read 4"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : received bytes printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  aux : control AUX GPIO pins (see SPI aux for full usage)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.RAWWIRE aux 1 in"));

    // ── SWD ───────────────────────────────────────────────────────────────
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SWD : ARM Serial Wire Debug (prerequisite: MODE swd)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : call init (or multidrop) before read/write; addresses are hex bytes unless noted"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  init : send JTAG-to-SWD switching sequence and sync clocks"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SWD init"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  multidrop : ADIv6 dormant-to-active sequence for multi-drop SWD"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [addr]   (optional hex 32-bit DP address; default = 0)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SWD multidrop               - activate with default address"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.SWD multidrop 01000000       - activate specific DP target"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read_dp : read a Debug Port register"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : addr   (hex byte, e.g. 00=DPIDR, 04=CTRL/STAT, 08=SELECT, 0C=RDBUFF)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SWD read_dp 00               - read DPIDR"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.SWD read_dp 04               - read CTRL/STAT"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : DP[0xADDR] = 0xXXXXXXXX"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write_dp : write a Debug Port register"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : addr value   (addr = hex byte, value = hex 32-bit)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SWD write_dp 04 50000000     - power up DP (CTRL/STAT)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.SWD write_dp 08 000000F0     - select AP 0, bank 0xF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read_ap : read an Access Port register"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : ap_addr bank   (both hex bytes; ap_addr = AP index 0-255, bank = register bank)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SWD read_ap 00 FC            - read AP 0 IDR (bank 0xFC)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : AP[idx][0xBANK] = 0xXXXXXXXX"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write_ap : write an Access Port register"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : ap_addr bank value   (hex byte, hex byte, hex 32-bit)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SWD write_ap 00 04 23000002  - configure MEM-AP CSW"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  scan : scan all 256 AP slots and report valid IDR values"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SWD scan"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : list of AP indices with non-zero IDR (component types printed)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  abort : write to DP ABORT register to clear sticky errors"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [flags]   (hex byte; default = 1F = clear all sticky bits)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SWD abort                    - clear all sticky bits"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.SWD abort 04                 - clear STKCMPCLR only"));

    // ── SMARTCARD ─────────────────────────────────────────────────────────
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SMARTCARD : ISO 7816 smartcard interface (prerequisite: MODE smartcard)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : set smartcard parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : pullup=[0|1]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SMARTCARD cfg pullup=1       - enable I/O pull-up"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.SMARTCARD cfg ?              - print current cfg"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  rst : drive the RST (reset) pin"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : 0 | 1"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SMARTCARD rst 0              - assert reset (cold reset)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.SMARTCARD rst 1              - release reset"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  baud : set communication baud rate"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SMARTCARD baud 9600"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  prescaler : set clock prescaler (divides card clock, 0-255)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N   (0-255)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SMARTCARD prescaler 10"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  guardtime : set extra guard time between bytes (0-255 ETUs)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N   (0-255)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SMARTCARD guardtime 2"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : send APDU bytes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA   (any length, no 16-byte cap)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SMARTCARD write 00A4040007A0000000031010  - SELECT FILE APDU"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : read N bytes from card"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N   (byte count)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SMARTCARD read 2              - read 2-byte status word"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : received bytes printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  atr : trigger card reset and retrieve ATR (Answer-To-Reset)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SMARTCARD atr"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : ATR bytes printed as hex dump (identifies card type and capabilities)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  aux : control AUX GPIO pins (see SPI aux for full usage)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SMARTCARD aux 0 out 1"));

    // ── NFC ───────────────────────────────────────────────────────────────
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("NFC : NFC reader (prerequisite: MODE nfc)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  mode : select NFC standard"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : 14443a | 15693"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.NFC mode 14443a               - ISO 14443-A (MIFARE, NFC-A)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.NFC mode 15693                - ISO 15693 (vicinity cards)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  rf : turn RF field on or off"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : on | off"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.NFC rf on                     - power up the RF field"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.NFC rf off                    - field off (card power down)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : transmit bytes; optional CRC appended by firmware"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA [crc]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.NFC write 6000               - send 2 bytes, no CRC"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.NFC write 30 00 crc          - READ BLOCK cmd with CRC"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : card response bytes printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write_bits : transmit a partial byte (anti-collision / REQA)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXBYTE N   (HEXBYTE = 1 hex byte, N = 1-7 bits to send)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.NFC write_bits 26 7          - send REQA (0x26, 7 bits)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : card ATQA response bytes printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  aux : control AUX GPIO pins (see SPI aux for full usage)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.NFC aux 0 out 1"));

    // ── MMC ───────────────────────────────────────────────────────────────
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("MMC : eMMC/MMC block device access (prerequisite: MODE mmc)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : block size is always 512 bytes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : set bus width"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : width=[1|4]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.MMC cfg width=4              - use 4-bit bus"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.MMC cfg ?                    - print current cfg"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cid : read 16-byte Card Identification register"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.MMC cid"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : 16-byte CID printed as hex dump (manufacturer, serial, date)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  csd : read 16-byte Card Specific Data register"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.MMC csd"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : 16-byte CSD printed as hex dump (capacity, timing, features)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  ext_csd : read 512-byte Extended CSD register"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.MMC ext_csd"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : 512-byte EXT_CSD printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : read a 512-byte block by block address"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : block_num   (decimal block address)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.MMC read 0                   - read boot sector (block 0)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.MMC read 2048                - read block 2048"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : 512-byte block content printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : write a 512-byte block"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : block_num HEXDATA   (block_num decimal, HEXDATA = 1024 hex chars = 512 bytes)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.MMC write 0 000102...        - write 512 bytes to block 0"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  aux : control AUX GPIO pins (see SPI aux for full usage)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.MMC aux 0 out 1"));

    // ── SDIO ──────────────────────────────────────────────────────────────
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SDIO : SD card command interface (prerequisite: MODE sdio)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : cmd_id = decimal 0-63; cmd_arg = hex 32-bit (e.g. 000001AA); block size = 512 bytes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : set bus width and frequency"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : width=[1|4]  freq=[slow|fast]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SDIO cfg width=4 freq=fast"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.SDIO cfg ?                   - print current cfg"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  send_no : send command with no response expected"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : cmd_id cmd_arg"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SDIO send_no 0 00000000      - CMD0 GO_IDLE_STATE"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  send_short : send command and receive 4-byte (R1/R3/R6/R7) response"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : cmd_id cmd_arg"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SDIO send_short 8 000001AA   - CMD8 SEND_IF_COND"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.SDIO send_short 55 00000000  - CMD55 APP_CMD"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : 4-byte response printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  send_long : send command and receive 16-byte (R2) response"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : cmd_id cmd_arg"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SDIO send_long 2 00000000    - CMD2 ALL_SEND_CID"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           HYDRABUS.SDIO send_long 9 00010000    - CMD9 SEND_CSD for RCA 0x0001"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : 16-byte response printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : send block-read command and capture 512-byte data block"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : cmd_id cmd_arg"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SDIO read 17 00000000        - CMD17 READ_SINGLE_BLOCK at addr 0"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : 512-byte block printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : send block-write command followed by 512-byte data"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : cmd_id cmd_arg HEXDATA   (HEXDATA = 1024 hex chars = 512 bytes)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SDIO write 24 00000000 000102...  - CMD24 WRITE_BLOCK at addr 0"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("--------------------"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  aux : control AUX GPIO pins (see SPI aux for full usage)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: HYDRABUS.SDIO aux 0 out 1"));

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
        LOG_PRINT(LOG_EMPTY, LOG_STRING("Available modes:"));
        for (const auto& m : m_mapModes) {
            LOG_PRINT(LOG_EMPTY, LOG_STRING("  -"); LOG_STRING(m.first));
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
