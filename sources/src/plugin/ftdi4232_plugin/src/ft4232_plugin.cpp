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

bool FT4232Plugin::m_FT4232_INFO(const std::string& args) const
{
    if (!args.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("INFO expects no arguments"));
        return false;
    }

    if (!m_bIsEnabled) return true;

    LOG_PRINT(LOG_EMPTY, LOG_STRING(FT4232_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: FTDI FT4232H Hi-Speed USB adapter (60 MHz, 4 channels: A/B MPSSE, C/D async UART, PID 0x6011)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  DeviceIndex:"); LOG_UINT32(m_sIniValues.u8DeviceIndex));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Channels A and B support MPSSE (SPI / I2C / GPIO)."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Channels C and D support async UART only."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  All four modules own independent USB handles and may be open simultaneously."));

    // ── SPI ───────────────────────────────────────────────────────────────
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SPI : full-duplex SPI via MPSSE on channel A or B (must call open before any transfer)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : max SCK 30 MHz; CS is asserted/deasserted automatically per transfer"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  open : open SPI interface and apply initial configuration"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [channel=A|B] [clock=N] [mode=0-3] [bitorder=msb|lsb] [cspin=N] [cspol=low|high] [device=N]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           channel  : A (default) | B"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           clock    : SCK in Hz (default 1000000; presets: 100kHz 500kHz 1MHz 2MHz 5MHz 10MHz 20MHz 30MHz)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           mode     : SPI mode 0-3 (CPOL/CPHA, default 0)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           bitorder : msb (default) | lsb"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           cspin    : ADBUS pin for CS as hex byte (default 0x08 = ADBUS3)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           cspol    : CS polarity low (default) | high"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           device   : zero-based FT4232H device index (default 0)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.SPI open channel=A clock=10000000 mode=0"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT4232.SPI open channel=B clock=5000000 mode=3 bitorder=lsb cspol=high"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  close : release SPI interface"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.SPI close"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : update SPI configuration (stored, applied on next open)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [clock=N] [mode=0-3] [bitorder=msb|lsb] [cspin=N] [cspol=low|high]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.SPI cfg clock=5000000 mode=1"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT4232.SPI cfg ?             - print current pending config"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cs : informational — CS is managed automatically per transfer"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.SPI cs               - prints CS management note"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : transmit bytes (MOSI only, MISO discarded)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA   (hex bytes, no spaces)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.SPI write DEADBEEF   - send 4 bytes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : receive N bytes (clocks 0x00 on MOSI, prints MISO)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N   (byte count)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.SPI read 4"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : MISO bytes printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  xfer : full-duplex transfer (MOSI written, MISO printed)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA   (hex bytes)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.SPI xfer DEADBEEF    - write 4 bytes, print MISO"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : MISO bytes printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  wrrd : write then read in one CS-guarded transaction"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA:rdlen"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.SPI wrrd 9F:3        - send cmd 0x9F, read 3 bytes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : read bytes printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  wrrdf : write/read using binary files from ARTEFACTS_PATH"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : filename[:wrchunk][:rdchunk]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.SPI wrrdf flash_cmd.bin"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  script : run a command script from ARTEFACTS_PATH (SPI must be open)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : scriptname"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.SPI script read_flash.txt"));

    // ── I2C ───────────────────────────────────────────────────────────────
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("I2C : I2C master via MPSSE on channel A or B (must call open before any transfer)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : max clock 3.4 MHz with suitable pull-ups; ACKs all bytes except the last on read"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  open : open I2C interface and set slave address / clock"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [channel=A|B] [addr=0xNN] [clock=N] [device=N]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           channel : A (default) | B"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           addr    : 7-bit slave address (default 0x50)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           clock   : I2C clock in Hz (default 100000; presets: 50kHz 100kHz 400kHz 1MHz 3.4MHz)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           device  : zero-based device index (default 0)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.I2C open channel=A addr=0x50 clock=400000"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT4232.I2C open channel=B addr=0x68 clock=100000 device=1"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  close : release I2C interface"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.I2C close"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : update pending I2C configuration (takes effect on next open)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [addr=0xNN] [clock=N]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.I2C cfg addr=0x68 clock=400000"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT4232.I2C cfg ?            - print current pending config"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : write bytes (START + addr+W + data + STOP)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA   (hex bytes, no spaces)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.I2C write 0055       - write 2 bytes to slave"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : ACK/NACK status"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : read N bytes from current slave address (ACKs all but last)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N   (byte count)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.I2C read 2"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : received bytes printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  wrrd : write then read in one sequence"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA:rdlen"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.I2C wrrd 0000:2      - write 0x00 0x00, read 2 bytes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : read bytes printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  wrrdf : write/read using binary files from ARTEFACTS_PATH"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : filename[:wrchunk][:rdchunk]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.I2C wrrdf i2c_seq.bin"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  scan : probe I2C addresses 0x08..0x77 for ACK (uses current clock/device; no open required)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.I2C scan"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : list of responding device addresses (e.g. Found device at 0x48)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  script : run a command script from ARTEFACTS_PATH (I2C must be open)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : scriptname"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.I2C script eeprom_test.txt"));

    // ── GPIO ──────────────────────────────────────────────────────────────
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("GPIO : dual-bank MPSSE GPIO on channel A or B (must call open before use)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : low bank = ADBUS[7:0]; high bank = ACBUS[7:0] per selected channel"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : ADBUS[3:0] are shared with MPSSE clock/data — avoid while SPI/I2C is open on the same channel"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : default channel is B (INI: GPIO_CHANNEL), keeping GPIO separate from SPI/I2C on A"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  open : open GPIO interface and apply initial direction / value"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [channel=A|B] [device=N] [lowdir=0xNN] [lowval=0xNN] [highdir=0xNN] [highval=0xNN]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           channel        : A | B (default B)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           lowdir/highdir : direction mask — 1=output, 0=input (default 0x00 = all inputs)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           lowval/highval : initial output values (default 0x00)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.GPIO open channel=B lowdir=0xFF lowval=0x00"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT4232.GPIO open channel=A lowdir=0xF0 highdir=0xFF highval=0xAA"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  close : release GPIO interface"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.GPIO close"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : update pending GPIO configuration (takes effect on next open)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [channel=A|B] [lowdir=0xNN] [lowval=0xNN] [highdir=0xNN] [highval=0xNN]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.GPIO cfg lowdir=0x0F highdir=0xFF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT4232.GPIO cfg ?            - print current pending config"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  dir : set direction of a bank at runtime"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [low|high] MASK   (hex byte; 1=output, 0=input)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.GPIO dir low 0xFF    - all ADBUS pins as outputs"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT4232.GPIO dir high 0x0F   - ACBUS[3:0] outputs, [7:4] inputs"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : write a full byte to a bank"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [low|high] VALUE  (hex byte)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.GPIO write low 0xAA  - drive ADBUS to 0xAA"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT4232.GPIO write high 0x01 - drive ACBUS to 0x01"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  set : drive masked pins HIGH (leaves unmasked pins unchanged)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [low|high] MASK   (hex byte)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.GPIO set low 0x01    - ADBUS[0] high"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT4232.GPIO set high 0x80   - ACBUS[7] high"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  clear : drive masked pins LOW (leaves unmasked pins unchanged)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [low|high] MASK   (hex byte)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.GPIO clear low 0x01  - ADBUS[0] low"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT4232.GPIO clear high 0xF0 - ACBUS[7:4] all low"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  toggle : invert masked output pins"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [low|high] MASK   (hex byte)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.GPIO toggle low 0x01  - invert ADBUS[0]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT4232.GPIO toggle high 0xFF - invert all ACBUS outputs"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : read current logic levels from a bank"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [low|high]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.GPIO read low        - read ADBUS[7:0]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT4232.GPIO read high       - read ACBUS[7:0]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : Bank low/high: 0xNN  [BBBBBBBB]  (hex + binary, MSB first)"));

    // ── UART ──────────────────────────────────────────────────────────────
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("UART : async serial on channel C or D (channels A/B are MPSSE-only and will be rejected)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : max baud ~3 Mbps (60 MHz / 20); default channel C (INI: UART_CHANNEL)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : two independent UART ports available simultaneously — open one per channel"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  open : open UART interface on channel C or D"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [channel=C|D] [baud=N] [data=8] [stop=1] [parity=none|odd|even|mark|space] [flow=none|hw] [device=N]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           channel : C (default) | D"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           baud    : baud rate (default 115200; presets: 9600 19200 38400 57600 115200 230400 460800 921600 1M 3M)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           data    : data bits (default 8)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           stop    : stop bits (default 1)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           parity  : none (default) | odd | even | mark | space"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           flow    : none (default) | hw (RTS/CTS hardware flow control)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           device  : zero-based device index (default 0)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.UART open channel=C baud=115200"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT4232.UART open channel=D baud=921600 data=8 stop=1 parity=none"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT4232.UART open channel=C baud=3M flow=hw"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  close : release UART interface"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.UART close"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : update UART parameters (applied immediately if open, else stored for next open)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [channel=C|D] [baud=N] [data=8] [stop=1] [parity=none|odd|even|mark|space] [flow=none|hw]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.UART cfg baud=9600 parity=even"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT4232.UART cfg baud=115200 flow=hw"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : transmit hex bytes over UART"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA   (hex bytes, no spaces)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.UART write DEADBEEF  - send 4 bytes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT4232.UART write 48656C6C6F  - send ASCII 'Hello'"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : receive N bytes over UART (uses READ_TIMEOUT from config)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N   (byte count)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.UART read 4"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : received bytes printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  script : run a command script from ARTEFACTS_PATH (UART must be open)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : scriptname"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT4232.UART script comms_sequence.txt"));

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
