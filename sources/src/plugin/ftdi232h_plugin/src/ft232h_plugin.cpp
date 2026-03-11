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

bool FT232HPlugin::m_FT232H_INFO(const std::string& args) const
{
    if (!args.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("INFO expects no arguments"));
        return false;
    }

    if (!m_bIsEnabled) return true;

    LOG_PRINT(LOG_EMPTY, LOG_STRING(FT232H_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: FTDI FT232H Hi-Speed USB to MPSSE/UART adapter (60 MHz, single MPSSE channel, PID 0x6014)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  DeviceIndex:"); LOG_UINT32(m_sIniValues.u8DeviceIndex);
                         LOG_STRING("  Note: open at most one MPSSE module (SPI/I2C/GPIO) at a time per chip"));

    // ── SPI ───────────────────────────────────────────────────────────────
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SPI : full-duplex SPI via MPSSE (must call open before any transfer)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : CS is asserted/deasserted automatically per transfer; max SCK 30 MHz"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  open : open SPI interface and apply initial configuration"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [clock=N] [mode=0-3] [bitorder=msb|lsb] [cspin=N] [cspol=low|high] [device=N]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           clock   : SCK frequency in Hz (default 1000000; presets: 100kHz 500kHz 1MHz 2MHz 5MHz 10MHz 20MHz 30MHz)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           mode    : SPI mode 0-3 (CPOL/CPHA, default 0)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           bitorder: msb (default) | lsb"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           cspin   : ADBUS pin used for CS (hex byte, default 0x08 = ADBUS3)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           cspol   : CS polarity low (default) | high"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           device  : zero-based FT232H device index (default 0)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.SPI open clock=10000000 mode=0"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT232H.SPI open clock=1000000 mode=3 bitorder=lsb cspol=high"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  close : release SPI interface"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.SPI close"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : update SPI configuration (takes effect on next open)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [clock=N] [mode=0-3] [bitorder=msb|lsb] [cspin=N] [cspol=low|high]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.SPI cfg clock=5000000 mode=1"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT232H.SPI cfg ?             - print current pending config"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cs : informational — CS is managed automatically per transfer"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.SPI cs               - prints CS management note"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : transmit bytes (MOSI only, MISO discarded)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA   (hex bytes, no spaces)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.SPI write DEADBEEF   - send 4 bytes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : receive N bytes (clocks 0x00 on MOSI, prints MISO)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N   (byte count)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.SPI read 4"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : MISO bytes printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  xfer : full-duplex transfer (MOSI written, MISO printed)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA   (hex bytes)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.SPI xfer DEADBEEF    - write 4 bytes, print MISO"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : MISO bytes printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  wrrd : write then read in one CS-guarded transaction"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA:rdlen"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.SPI wrrd 9F:3        - send cmd 0x9F, read 3 bytes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : read bytes printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  wrrdf : write/read using binary files from ARTEFACTS_PATH"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : filename[:wrchunk][:rdchunk]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.SPI wrrdf flash_cmd.bin"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  script : run a command script from ARTEFACTS_PATH (SPI must be open)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : scriptname"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.SPI script read_flash.txt"));

    // ── I2C ───────────────────────────────────────────────────────────────
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("I2C : I2C master via MPSSE (must call open before any transfer)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : ACKs all bytes except the last on read; max clock 3.4 MHz with suitable pull-ups"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  open : open I2C interface and set slave address / clock"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [addr=0xNN] [clock=N] [device=N]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           addr  : 7-bit slave address (default 0x50)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           clock : I2C clock in Hz (default 100000; presets: 50kHz 100kHz 400kHz 1MHz 3.4MHz)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           device: zero-based FT232H device index (default 0)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.I2C open addr=0x50 clock=400000"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT232H.I2C open addr=0x68 clock=100000 device=1"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  close : release I2C interface"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.I2C close"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : update pending I2C address / clock (takes effect on next open)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [addr=0xNN] [clock=N]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.I2C cfg addr=0x68 clock=400000"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT232H.I2C cfg ?            - print current pending config"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : write bytes (START + addr+W + data + STOP)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA   (hex bytes, no spaces)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.I2C write 0055       - write 2 bytes to slave"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : ACK/NACK status"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : read N bytes from current slave address"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N   (byte count)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.I2C read 2           - read 2 bytes (ACK first, NACK last)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : received bytes printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  wrrd : write then read in one sequence"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA:rdlen"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.I2C wrrd 0000:2      - write 0x00 0x00, read 2 bytes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : read bytes printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  wrrdf : write/read using binary files from ARTEFACTS_PATH"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : filename[:wrchunk][:rdchunk]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.I2C wrrdf i2c_seq.bin"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  scan : probe I2C addresses 0x08..0x77 for ACK"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.I2C scan             - uses current clock and device index; no open required"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : list of responding device addresses (e.g. Found device at 0x48)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  script : run a command script from ARTEFACTS_PATH (I2C must be open)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : scriptname"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.I2C script eeprom_test.txt"));

    // ── GPIO ──────────────────────────────────────────────────────────────
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("GPIO : dual-bank MPSSE GPIO (must call open before use)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : low bank = ADBUS[7:0]; high bank = ACBUS[7:0]; both are 8-bit"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : ADBUS[3:0] are shared with MPSSE clock/data — do not use them while SPI/I2C is open"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  open : open GPIO interface and apply initial direction / value"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [device=N] [lowdir=0xNN] [lowval=0xNN] [highdir=0xNN] [highval=0xNN]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           lowdir/highdir : direction mask — 1=output, 0=input (default 0x00 = all inputs)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           lowval/highval : initial output values (default 0x00)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.GPIO open lowdir=0xFF lowval=0x00"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT232H.GPIO open lowdir=0xF0 highdir=0xFF highval=0xAA"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  close : release GPIO interface"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.GPIO close"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : update pending GPIO configuration (takes effect on next open)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [lowdir=0xNN] [lowval=0xNN] [highdir=0xNN] [highval=0xNN]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.GPIO cfg lowdir=0x0F highdir=0xFF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT232H.GPIO cfg ?            - print current pending config"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  dir : set direction of a bank at runtime"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [low|high] MASK   (hex byte; 1=output, 0=input)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.GPIO dir low 0xFF    - all ADBUS pins as outputs"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT232H.GPIO dir high 0x0F   - ACBUS[3:0] outputs, [7:4] inputs"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : write a full byte to a bank"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [low|high] VALUE  (hex byte)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.GPIO write low 0xAA  - drive ADBUS to 0xAA"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT232H.GPIO write high 0x01 - drive ACBUS to 0x01"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  set : drive masked pins HIGH (leaves unmasked pins unchanged)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [low|high] MASK   (hex byte)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.GPIO set low 0x01    - ADBUS[0] high"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT232H.GPIO set high 0x80   - ACBUS[7] high"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  clear : drive masked pins LOW (leaves unmasked pins unchanged)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [low|high] MASK   (hex byte)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.GPIO clear low 0x01  - ADBUS[0] low"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT232H.GPIO clear high 0xF0 - ACBUS[7:4] all low"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  toggle : invert masked output pins"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [low|high] MASK   (hex byte)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.GPIO toggle low 0x01  - invert ADBUS[0]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT232H.GPIO toggle high 0xFF - invert all ACBUS outputs"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : read current logic levels from a bank"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [low|high]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.GPIO read low        - read ADBUS[7:0]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT232H.GPIO read high       - read ACBUS[7:0]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : Bank low/high: 0xNN  [BBBBBBBB]  (hex + binary, MSB first)"));

    // ── UART ──────────────────────────────────────────────────────────────
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("UART : async serial in VCP mode (mutually exclusive with MPSSE modules on the same chip)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : if SPI/I2C/GPIO are needed simultaneously, use a separate FT232H chip (device=N)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  open : open UART interface"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [baud=N] [data=8] [stop=0] [parity=none|odd|even|mark|space] [flow=none|hw] [device=N]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           baud   : baud rate (default 115200; presets: 9600 19200 38400 57600 115200 230400 460800 921600)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           data   : data bits (default 8)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           stop   : stop bits (default 0)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           parity : none (default) | odd | even | mark | space"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           flow   : none (default) | hw (RTS/CTS hardware flow control)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           device : zero-based FT232H device index (default 0)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.UART open baud=115200"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT232H.UART open baud=9600 data=8 stop=1 parity=none flow=hw device=1"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  close : release UART interface"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.UART close"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : update UART parameters (applied immediately if open, else stored for next open)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [baud=N] [data=8] [stop=0] [parity=none|odd|even|mark|space] [flow=none|hw]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.UART cfg baud=9600 parity=even"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT232H.UART cfg baud=115200 flow=hw"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : transmit hex bytes over UART"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA   (hex bytes, no spaces)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.UART write DEADBEEF  - send 4 bytes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT232H.UART write 48656C6C6F  - send ASCII 'Hello'"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : receive N bytes over UART (uses READ_TIMEOUT from config)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N   (byte count)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.UART read 4"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : received bytes printed as hex dump"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  script : run a command script from ARTEFACTS_PATH (UART must be open)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : scriptname"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT232H.UART script uart_test.txt"));

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
