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

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "FT2232      |"
#define LOG_HDR    LOG_STRING(LT_HDR)

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
#define UART_BAUD        "UART_BAUD"       // default baud rate for UART module

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

    m_sUartCfg.baudRate = m_sIniValues.u32UartBaudRate;
    m_sUartCfg.variant  = m_sIniValues.eDefaultVariant;

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
    if (m_pUART) { m_pUART->close(); m_pUART.reset(); }
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

FT2232UART* FT2232Plugin::m_uart() const
{
    if (!m_pUART || !m_pUART->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("UART not open — call FT2232.UART open [...]"));
        return nullptr;
    }
    return m_pUART.get();
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

    if (module == "UART") {
        m_sUartCfg.baudRate = static_cast<uint32_t>(hz);
        if (m_pUART && m_pUART->is_open()) {
            auto s = m_pUART->set_baud(static_cast<uint32_t>(hz));
            if (s != FT2232UART::Status::SUCCESS) {
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

bool FT2232Plugin::m_FT2232_INFO(const std::string& args) const
{
    if (!args.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("INFO expects no arguments"));
        return false;
    }

    if (!m_bIsEnabled) return true;

    LOG_PRINT(LOG_EMPTY, LOG_STRING(FT2232_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: FTDI FT2232 dual-channel USB-to-MPSSE/UART adapter"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Variants: FT2232H (60 MHz, channels A+B, MPSSE on both)  |  FT2232D (6 MHz, channel A MPSSE, channel B UART)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  DeviceIndex:"); LOG_UINT32(m_sIniValues.u8DeviceIndex);
                         LOG_STRING("  Default variant:"); LOG_STRING(m_sIniValues.eDefaultVariant == FT2232Base::Variant::FT2232H ? "H (FT2232H)" : "D (FT2232D)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note: variant= and channel= are per-command overrides; FT2232D channel B is UART-only (no MPSSE)"));

    // ── SPI ───────────────────────────────────────────────────────────────
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SPI : full-duplex SPI via MPSSE (must call open before any transfer)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : FT2232H max SCK 30 MHz; FT2232D max SCK 3 MHz — variant check enforced at open"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : CS is asserted/deasserted automatically per transfer"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  open : open SPI interface and apply initial configuration"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [variant=H|D] [channel=A|B] [clock=N] [mode=0-3] [bitorder=msb|lsb] [cspin=N] [cspol=low|high] [device=N]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           variant  : H=FT2232H (default) | D=FT2232D"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           channel  : A (default) | B  (FT2232D: channel A only)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           clock    : SCK in Hz (default 1000000; presets: 100kHz 500kHz 1MHz 2MHz 3MHz 5MHz 10MHz 30MHz)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           mode     : SPI mode 0-3 (CPOL/CPHA, default 0)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           bitorder : msb (default) | lsb"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           cspin    : ADBUS pin for CS as hex byte (default 0x08 = ADBUS3)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           cspol    : CS polarity low (default) | high"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           device   : zero-based FT2232 device index (default 0)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.SPI open variant=H clock=10000000 mode=0 channel=A"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT2232.SPI open variant=D clock=1000000 channel=A"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT2232.SPI open clock=5000000 mode=3 bitorder=lsb cspol=high"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  close : release SPI interface"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.SPI close"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : update SPI configuration (takes effect on next open)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [variant=H|D] [clock=N] [mode=0-3] [bitorder=msb|lsb] [cspin=N] [cspol=low|high]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.SPI cfg clock=5000000 mode=1"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT2232.SPI cfg ?             - print current pending config"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cs : informational — CS is managed automatically per transfer"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.SPI cs               - prints CS management note"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : transmit bytes (MOSI only, MISO discarded)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA   (hex bytes, no spaces)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.SPI write DEADBEEF   - send 4 bytes"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : receive N bytes (clocks 0x00 on MOSI, prints MISO)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N   (byte count)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.SPI read 4"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : MISO bytes printed as hex dump"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  xfer : full-duplex transfer (MOSI written, MISO printed)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA   (hex bytes)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.SPI xfer DEADBEEF    - write 4 bytes, print MISO"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : MISO bytes printed as hex dump"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  wrrd : write then read in one CS-guarded transaction"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA:rdlen"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.SPI wrrd 9F:3        - send cmd 0x9F, read 3 bytes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : read bytes printed as hex dump"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  wrrdf : write/read using binary files from ARTEFACTS_PATH"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : filename[:wrchunk][:rdchunk]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.SPI wrrdf flash_cmd.bin"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  script : run a command script from ARTEFACTS_PATH (SPI must be open)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : scriptname"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.SPI script read_flash.txt"));

    // ── I2C ───────────────────────────────────────────────────────────────
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("I2C : I2C master via MPSSE (must call open before any transfer)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : FT2232H max clock 1 MHz (higher with suitable pull-ups); FT2232D max 400 kHz"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : FT2232D supports channel A only; FT2232H supports A or B"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  open : open I2C interface and set slave address / clock"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [variant=H|D] [channel=A|B] [addr=0xNN] [clock=N] [device=N]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           variant : H=FT2232H (default) | D=FT2232D"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           channel : A (default) | B  (FT2232D: A only)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           addr    : 7-bit slave address (default 0x50)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           clock   : I2C clock in Hz (default 100000; presets: 50kHz 100kHz 400kHz 1MHz)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           device  : zero-based device index (default 0)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.I2C open variant=H addr=0x50 clock=400000 channel=A"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT2232.I2C open variant=D addr=0x68 clock=100000"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT2232.I2C open addr=0x50 clock=1MHz channel=B  - FT2232H channel B"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  close : release I2C interface"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.I2C close"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : update pending I2C configuration (takes effect on next open)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [variant=H|D] [addr=0xNN] [clock=N]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.I2C cfg addr=0x68 clock=400000"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT2232.I2C cfg ?            - print current pending config"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : write bytes (START + addr+W + data + STOP)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA   (hex bytes, no spaces)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.I2C write 0055       - write 2 bytes to slave"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : ACK/NACK status"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : read N bytes from current slave address (ACKs all but last)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N   (byte count)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.I2C read 2"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : received bytes printed as hex dump"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  wrrd : write then read in one sequence"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA:rdlen"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.I2C wrrd 0000:2      - write 0x00 0x00, read 2 bytes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : read bytes printed as hex dump"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  wrrdf : write/read using binary files from ARTEFACTS_PATH"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : filename[:wrchunk][:rdchunk]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.I2C wrrdf i2c_seq.bin"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  scan : probe I2C addresses 0x08..0x77 for ACK (uses current clock/device; no open required)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.I2C scan"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : list of responding device addresses (e.g. Found device at 0x48)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  script : run a command script from ARTEFACTS_PATH (I2C must be open)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : scriptname"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.I2C script eeprom_test.txt"));

    // ── GPIO ──────────────────────────────────────────────────────────────
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("GPIO : dual-bank MPSSE GPIO per channel (must call open before use)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : low bank = ADBUS[7:0]; high bank = ACBUS[7:0] per selected channel"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : FT2232D: channel A only; FT2232H: A or B — default channel B (INI: GPIO_CHANNEL)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : ADBUS[3:0] are shared with MPSSE clock/data — avoid while SPI/I2C is open on same channel"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  open : open GPIO interface and apply initial direction / value"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [variant=H|D] [channel=A|B] [device=N] [lowdir=0xNN] [lowval=0xNN] [highdir=0xNN] [highval=0xNN]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           variant        : H=FT2232H (default) | D=FT2232D"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           channel        : A | B (default B; FT2232D: A only)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           lowdir/highdir : direction mask — 1=output, 0=input (default 0x00 = all inputs)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           lowval/highval : initial output values (default 0x00)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.GPIO open variant=H channel=B lowdir=0xFF lowval=0x00"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT2232.GPIO open variant=D channel=A lowdir=0xF0 highdir=0xFF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT2232.GPIO open channel=B lowdir=0xFF highval=0xAA"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  close : release GPIO interface"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.GPIO close"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : update pending GPIO configuration (takes effect on next open)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [variant=H|D] [channel=A|B] [lowdir=0xNN] [lowval=0xNN] [highdir=0xNN] [highval=0xNN]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.GPIO cfg lowdir=0x0F highdir=0xFF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT2232.GPIO cfg ?            - print current pending config"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  dir : set direction of a bank at runtime"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [low|high] MASK   (hex byte; 1=output, 0=input)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.GPIO dir low 0xFF    - all ADBUS pins as outputs"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT2232.GPIO dir high 0x0F   - ACBUS[3:0] outputs, [7:4] inputs"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : write a full byte to a bank"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [low|high] VALUE  (hex byte)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.GPIO write low 0xAA  - drive ADBUS to 0xAA"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT2232.GPIO write high 0x01 - drive ACBUS to 0x01"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  set : drive masked pins HIGH (leaves unmasked pins unchanged)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [low|high] MASK   (hex byte)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.GPIO set low 0x01    - ADBUS[0] high"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT2232.GPIO set high 0x80   - ACBUS[7] high"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  clear : drive masked pins LOW (leaves unmasked pins unchanged)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [low|high] MASK   (hex byte)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.GPIO clear low 0x01  - ADBUS[0] low"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT2232.GPIO clear high 0xF0 - ACBUS[7:4] all low"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  toggle : invert masked output pins"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [low|high] MASK   (hex byte)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.GPIO toggle low 0x01  - invert ADBUS[0]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT2232.GPIO toggle high 0xFF - invert all ACBUS outputs"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : read current logic levels from a bank"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [low|high]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.GPIO read low        - read ADBUS[7:0]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT2232.GPIO read high       - read ACBUS[7:0]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : Bank low/high: 0xNN  [BBBBBBBB]  (hex + binary, MSB first)"));

    // ── UART ──────────────────────────────────────────────────────────────
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("UART : async serial on FT2232D channel B (FT2232H is rejected — no dedicated UART channel)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note : variant=D is required; attempting to open UART with variant=H returns an error"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  open : open UART interface (FT2232D only)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [baud=N] [data=8] [stop=1] [parity=none|odd|even|mark|space] [flow=none|hw] [variant=D] [device=N]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           baud    : baud rate (default 115200; presets: 9600 19200 38400 57600 115200 230400 460800 921600)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           data    : data bits (default 8)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           stop    : stop bits (default 1)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           parity  : none (default) | odd | even | mark | space"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           flow    : none (default) | hw (RTS/CTS hardware flow control)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           variant : must be D (FT2232D); H will be rejected"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           device  : zero-based device index (default 0)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.UART open variant=D baud=115200"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT2232.UART open variant=D baud=9600 parity=even stop=1 flow=hw"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  close : release UART interface"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.UART close"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  cfg : update UART parameters (applied immediately if open, else stored for next open)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : [baud=N] [data=8] [stop=1] [parity=none|odd|even|mark|space] [flow=none|hw]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.UART cfg baud=9600 parity=even"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT2232.UART cfg baud=115200 flow=hw"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  write : transmit hex bytes over UART"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : HEXDATA   (hex bytes, no spaces)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.UART write DEADBEEF  - send 4 bytes"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           FT2232.UART write 48656C6C6F  - send ASCII 'Hello'"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  read : receive N bytes over UART (uses READ_TIMEOUT from config)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : N   (byte count)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.UART read 4"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Return : received bytes printed as hex dump"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  script : run a command script from ARTEFACTS_PATH (UART must be open)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Args : scriptname"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("    Usage: FT2232.UART script uart_test.txt"));

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

bool FT2232Plugin::m_FT2232_UART(const std::string& args) const
{
    return generic_module_dispatch<FT2232Plugin>(this, "UART", args);
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
    getU32  (UART_BAUD,         m_sIniValues.u32UartBaudRate);

    if (!ok)
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("One or more config values failed to parse"));

    return ok;
}

///////////////////////////////////////////////////////////////////
//              UART params parse helper                         //
///////////////////////////////////////////////////////////////////

static bool parseParity_ft2232(const std::string& s, uint8_t& out)
{
    if (s=="none"||s=="NONE") { out=0; return true; }
    if (s=="odd" ||s=="ODD" ) { out=1; return true; }
    if (s=="even"||s=="EVEN") { out=2; return true; }
    if (s=="mark"||s=="MARK") { out=3; return true; }
    if (s=="space"||s=="SPACE"){ out=4; return true; }
    return false;
}

bool FT2232Plugin::parseUartParams(const std::string& args, UartPendingCfg& cfg,
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
        else if (k == "parity" ) ok &= parseParity_ft2232 (v, cfg.parity);
        else if (k == "flow"   ) cfg.hwFlowCtrl = (v=="hw"||v=="HW"||v=="rtscts");
        else if (k == "variant") ok &= parseVariant(v, cfg.variant);
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
