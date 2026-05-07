#include "uSharedConfig.hpp"
#include "PluginSpecOperations.hpp"

#include "dspkspi_plugin.hpp"

#include "uFile.hpp"
#include "uNumeric.hpp"
#include "uString.hpp"
#include "uTimer.hpp"

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR   "DSPKSPI     |"
#define LOG_HDR  LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                   INI FILE CONFIGURATION KEYS                 //
///////////////////////////////////////////////////////////////////

#define CFG_ARTEFACTS_PATH   "ARTEFACTS_PATH"
#define CFG_VID              "VID"
#define CFG_PID              "PID"
#define CFG_READ_TIMEOUT     "READ_TIMEOUT"
#define CFG_WRITE_TIMEOUT    "WRITE_TIMEOUT"
#define CFG_SPI_MODE         "SPI_MODE"
#define CFG_SPI_CLK_DIV      "SPI_CLK_DIV"

///////////////////////////////////////////////////////////////////
//                       PLUGIN ENTRY POINTS                     //
///////////////////////////////////////////////////////////////////

extern "C"
{
    EXPORTED DspkspiPlugin* pluginEntry()
    {
        return new DspkspiPlugin();
    }

    EXPORTED void pluginExit(DspkspiPlugin *ptrPlugin)
    {
        if (nullptr != ptrPlugin)
            delete ptrPlugin;
    }
}

///////////////////////////////////////////////////////////////////
//                        INIT / CLEANUP                         //
///////////////////////////////////////////////////////////////////

bool DspkspiPlugin::doInit(void * /*pvUserData*/)
{
    m_bIsInitialized = true;
    return m_bIsInitialized;
}

void DspkspiPlugin::doCleanup(void)
{
    m_bIsInitialized = false;
    m_bIsEnabled     = false;
}

///////////////////////////////////////////////////////////////////
//                        COMMAND HANDLERS                       //
///////////////////////////////////////////////////////////////////

/*----------------------------------------------------------------------------*/
/**
 * @brief INFO — print plugin version, description, and command reference.
 *        Takes no arguments. Works even when doInit() has not been called.
 *
 * Usage: DSPKSPI.INFO
 */
/*----------------------------------------------------------------------------*/
bool DspkspiPlugin::m_DSPKSPI_INFO(const std::string& args) const
{
    if (!args.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("INFO: expected no argument(s)"));
        return false;
    }

    if (!m_bIsEnabled)
        return true;

    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING(DSPKSPI_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: Digispark ATtiny85 USB→SPI master bridge"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG  : set HID VID/PID and timeouts at runtime"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Args  : [v:<vid_hex>] [p:<pid_hex>] [r:<read_ms>] [w:<write_ms>]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Usage : DSPKSPI.CONFIG v:0x16C0 p:0x05DF r:2000 w:2000"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SPI_CFG   : configure SPI clock mode and divider"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Args    : [m:<mode_0_3>] [d:<div_0_3>]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Dividers: 0=DIV2(~8MHz) 1=DIV4(~4MHz) 2=DIV8(~2MHz) 3=DIV16(~1MHz)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Usage   : DSPKSPI.SPI_CFG m:0 d:1"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SPI_WRITE : write bytes on MOSI, MISO discarded  (max 6 bytes)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Args    : <byte0_hex> [<byte1_hex> ...]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Usage   : DSPKSPI.SPI_WRITE DE AD BE EF"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SPI_READ  : clock N bytes in, MOSI=0x00  (max 6 bytes)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Args    : <n_bytes>"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Usage   : DSPKSPI.SPI_READ 4"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SPI_XFER  : full-duplex transfer, print MISO  (max 6 bytes)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Args    : <byte0_hex> [<byte1_hex> ...]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Usage   : DSPKSPI.SPI_XFER 9F 00 00   (JEDEC ID)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SPI_WRREG : write one register (MSB=0 → write convention)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Args    : <reg_hex> <val_hex>"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Usage   : DSPKSPI.SPI_WRREG E0 B6   (BME280 reset)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SPI_RDREG : read N bytes from register (MSB=1 → read convention)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Args    : <reg_hex> <n_bytes>  (max 5 result bytes)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Usage   : DSPKSPI.SPI_RDREG D0 1   (BME280 chip-ID)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT    : run a sequence of commands from a file"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Args    : <filename> [<delay_ms>]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Usage   : DSPKSPI.SCRIPT spi_test.txt 50"));
    LOG_SEP();

    return true;
}


/*----------------------------------------------------------------------------*/
/**
 * @brief CONFIG — override HID VID/PID and timeouts at runtime.
 *
 * Args: [v:<vid_hex>] [p:<pid_hex>] [r:<read_ms>] [w:<write_ms>]
 *
 * Usage:
 *   DSPKSPI.CONFIG v:0x16C0 p:0x05DF r:2000 w:2000
 *   DSPKSPI.CONFIG r:5000
 */
/*----------------------------------------------------------------------------*/
bool DspkspiPlugin::m_DSPKSPI_CONFIG(const std::string& args) const
{
    if (args.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CONFIG: expected at least one argument"));
        return false;
    }

    if (!m_bIsEnabled)
        return true;

    std::vector<std::string> vstrTokens;
    ustring::tokenizeSpaceQuotesAware(args, vstrTokens);

    bool bRetVal = true;

    for (const auto& tok : vstrTokens)
    {
        if (tok.size() < 3 || tok[1] != ':')
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CONFIG: malformed token"); LOG_STRING(tok));
            bRetVal = false;
            continue;
        }

        char      cKey = static_cast<char>(std::tolower(static_cast<unsigned char>(tok[0])));
        std::string strVal = tok.substr(2);

        switch (cKey)
        {
            case 'v':
            {
                uint16_t u16Val;
                if (!m_ParseUint16(strVal, u16Val))
                {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CONFIG: invalid VID"); LOG_STRING(strVal));
                    bRetVal = false;
                }
                else
                {
                    m_u16Vid = u16Val;
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("CONFIG: VID ="); LOG_HEX16(m_u16Vid));
                }
                break;
            }
            case 'p':
            {
                uint16_t u16Val;
                if (!m_ParseUint16(strVal, u16Val))
                {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CONFIG: invalid PID"); LOG_STRING(strVal));
                    bRetVal = false;
                }
                else
                {
                    m_u16Pid = u16Val;
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("CONFIG: PID ="); LOG_HEX16(m_u16Pid));
                }
                break;
            }
            case 'r':
                if (!setReadTimeout(strVal))
                {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CONFIG: invalid read timeout"); LOG_STRING(strVal));
                    bRetVal = false;
                }
                else
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("CONFIG: ReadTimeout ="); LOG_UINT32(m_u32ReadTimeout));
                break;

            case 'w':
                if (!setWriteTimeout(strVal))
                {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CONFIG: invalid write timeout"); LOG_STRING(strVal));
                    bRetVal = false;
                }
                else
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("CONFIG: WriteTimeout ="); LOG_UINT32(m_u32WriteTimeout));
                break;

            default:
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CONFIG: unknown key"); LOG_STRING(tok));
                bRetVal = false;
                break;
        }
    }

    return bRetVal;
}


/*----------------------------------------------------------------------------*/
/**
 * @brief SPI_CFG — configure the SPI clock mode and divider on the firmware.
 *
 * Args: [m:<mode_0_3>] [d:<div_0_3>]
 *
 * Modes  : 0=MODE0(CPOL=0,CPHA=0)  1=MODE1  2=MODE2  3=MODE3
 * Dividers: 0=DIV2(~8MHz)  1=DIV4(~4MHz)  2=DIV8(~2MHz)  3=DIV16(~1MHz)
 *
 * Usage:
 *   DSPKSPI.SPI_CFG m:0 d:1
 *   DSPKSPI.SPI_CFG d:3
 */
/*----------------------------------------------------------------------------*/
bool DspkspiPlugin::m_DSPKSPI_SPI_CFG(const std::string& args) const
{
    if (args.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI_CFG: usage: [m:<mode>] [d:<div>]"));
        return false;
    }

    std::vector<std::string> vstrTok;
    ustring::tokenizeSpaceQuotesAware(args, vstrTok);

    bool bRetVal = true;

    for (const auto& tok : vstrTok)
    {
        if (tok.size() < 3 || tok[1] != ':')
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI_CFG: malformed token"); LOG_STRING(tok));
            bRetVal = false;
            continue;
        }

        char      cKey  = static_cast<char>(std::tolower(static_cast<unsigned char>(tok[0])));
        std::string strV = tok.substr(2);

        if (cKey == 'm')
        {
            if (!setSpiMode(strV))
            {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI_CFG: invalid mode (0-3)"); LOG_STRING(strV));
                bRetVal = false;
            }
        }
        else if (cKey == 'd')
        {
            if (!setSpiClkDiv(strV))
            {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI_CFG: invalid divider (0-3)"); LOG_STRING(strV));
                bRetVal = false;
            }
        }
        else
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI_CFG: unknown key"); LOG_STRING(tok));
            bRetVal = false;
        }
    }

    if (!bRetVal || !m_bIsEnabled)
        return bRetVal;

    // Push the new config to the firmware
    try
    {
        auto shpDrv = std::make_shared<SPIBridge>(m_u16Vid, m_u16Pid);
        if (!shpDrv->is_open())
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI_CFG: failed to open HID device"));
            return false;
        }
        auto st = shpDrv->configure(m_eSpiMode, m_eSpiClkDiv);
        bRetVal  = (st == SPIBridge::Status::SUCCESS);
        if (!bRetVal)
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI_CFG: firmware configure failed"));
    }
    catch (const std::exception& e)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI_CFG: exception"); LOG_STRING(e.what()));
        bRetVal = false;
    }

    return bRetVal;
}


/*----------------------------------------------------------------------------*/
/**
 * @brief SPI_WRITE — write bytes on MOSI, MISO is discarded (max 6 bytes).
 *
 * Args: <byte0_hex> [<byte1_hex> ...]
 *
 * Usage:
 *   DSPKSPI.SPI_WRITE DE AD BE EF
 *   DSPKSPI.SPI_WRITE 0x01 0x02
 */
/*----------------------------------------------------------------------------*/
bool DspkspiPlugin::m_DSPKSPI_SPI_WRITE(const std::string& args) const
{
    std::vector<std::string> vstrTok;
    ustring::tokenizeSpaceQuotesAware(args, vstrTok);

    if (vstrTok.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI_WRITE: usage: <byte0_hex> [byte1_hex ...]"));
        return false;
    }

    std::vector<uint8_t> vData;
    if (!m_ParseHexBytes(vstrTok, 0, vData))
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI_WRITE: invalid hex byte(s)"));
        return false;
    }

    if (vData.size() > SPIBridge::SPI_MAX_WRITE_PAYLOAD)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("SPI_WRITE: max"); LOG_UINT32(SPIBridge::SPI_MAX_WRITE_PAYLOAD); LOG_STRING("bytes"));
        return false;
    }

    if (!m_bIsEnabled)
        return true;

    bool bRetVal = false;

    try
    {
        auto shpDrv = std::make_shared<SPIBridge>(m_u16Vid, m_u16Pid);
        if (!shpDrv->is_open())
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI_WRITE: failed to open HID device"));
            return false;
        }
        bRetVal = m_SPIWrite(std::span<const uint8_t>(vData), shpDrv);
    }
    catch (const std::exception& e)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI_WRITE: exception"); LOG_STRING(e.what()));
    }

    return bRetVal;
}


/*----------------------------------------------------------------------------*/
/**
 * @brief SPI_READ — clock N bytes in, MOSI driven as 0x00 (max 6 bytes).
 *
 * Args: <n_bytes>
 *
 * Usage:
 *   DSPKSPI.SPI_READ 4
 */
/*----------------------------------------------------------------------------*/
bool DspkspiPlugin::m_DSPKSPI_SPI_READ(const std::string& args) const
{
    std::vector<std::string> vstrTok;
    ustring::tokenizeSpaceQuotesAware(args, vstrTok);

    if (vstrTok.size() != 1)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI_READ: usage: <n_bytes>"));
        return false;
    }

    uint32_t u32Len;
    if (!numeric::str2uint32(vstrTok[0], u32Len) || u32Len == 0 ||
        u32Len > SPIBridge::SPI_MAX_READ_PAYLOAD)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("SPI_READ: n_bytes must be 1–"); LOG_UINT32(SPIBridge::SPI_MAX_READ_PAYLOAD));
        return false;
    }

    if (!m_bIsEnabled)
        return true;

    bool bRetVal = false;

    try
    {
        auto shpDrv = std::make_shared<SPIBridge>(m_u16Vid, m_u16Pid);
        if (!shpDrv->is_open())
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI_READ: failed to open HID device"));
            return false;
        }

        std::vector<uint8_t> vBuf(u32Len, 0u);
        bRetVal = m_SPIRead(u32Len, std::span<uint8_t>(vBuf), shpDrv);

        if (bRetVal)
        {
            m_strResultData.clear();
            for (size_t i = 0; i < vBuf.size(); ++i)
            {
                char hex[6];
                std::snprintf(hex, sizeof(hex), "0x%02X", vBuf[i]);
                if (i > 0) m_strResultData += ' ';
                m_strResultData += hex;
            }
            LOG_PRINT(LOG_EMPTY, LOG_HDR; LOG_STRING("SPI_READ:"); LOG_STRING(m_strResultData));
        }
    }
    catch (const std::exception& e)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI_READ: exception"); LOG_STRING(e.what()));
    }

    return bRetVal;
}


/*----------------------------------------------------------------------------*/
/**
 * @brief SPI_XFER — full-duplex transfer, print MISO (max 6 bytes).
 *
 * Args: <byte0_hex> [<byte1_hex> ...]
 *
 * Usage:
 *   DSPKSPI.SPI_XFER 9F 00 00    (JEDEC ID — 3 bytes out, 3 bytes in)
 *   DSPKSPI.SPI_XFER 06 00 00 00 (MCP3204 ADC CH0)
 */
/*----------------------------------------------------------------------------*/
bool DspkspiPlugin::m_DSPKSPI_SPI_XFER(const std::string& args) const
{
    std::vector<std::string> vstrTok;
    ustring::tokenizeSpaceQuotesAware(args, vstrTok);

    if (vstrTok.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI_XFER: usage: <byte0_hex> [byte1_hex ...]"));
        return false;
    }

    std::vector<uint8_t> vMosi;
    if (!m_ParseHexBytes(vstrTok, 0, vMosi))
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI_XFER: invalid hex byte(s)"));
        return false;
    }

    if (vMosi.size() > SPIBridge::SPI_MAX_TRANSFER_PAYLOAD)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("SPI_XFER: max"); LOG_UINT32(SPIBridge::SPI_MAX_TRANSFER_PAYLOAD); LOG_STRING("bytes"));
        return false;
    }

    if (!m_bIsEnabled)
        return true;

    bool bRetVal = false;

    try
    {
        auto shpDrv = std::make_shared<SPIBridge>(m_u16Vid, m_u16Pid);
        if (!shpDrv->is_open())
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI_XFER: failed to open HID device"));
            return false;
        }

        std::vector<uint8_t> vMiso(vMosi.size(), 0u);
        bRetVal = m_SPITransfer(std::span<const uint8_t>(vMosi),
                                std::span<uint8_t>(vMiso),
                                shpDrv);

        if (bRetVal)
        {
            m_strResultData.clear();
            for (size_t i = 0; i < vMiso.size(); ++i)
            {
                char hex[6];
                std::snprintf(hex, sizeof(hex), "0x%02X", vMiso[i]);
                if (i > 0) m_strResultData += ' ';
                m_strResultData += hex;
            }
            LOG_PRINT(LOG_EMPTY, LOG_HDR; LOG_STRING("SPI_XFER MISO:"); LOG_STRING(m_strResultData));
        }
    }
    catch (const std::exception& e)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI_XFER: exception"); LOG_STRING(e.what()));
    }

    return bRetVal;
}


/*----------------------------------------------------------------------------*/
/**
 * @brief SPI_WRREG — write one byte to a register (MSB=0 → write convention).
 *
 * Sends [reg & 0x7F, value].
 *
 * Args: <reg_hex> <val_hex>
 *
 * Usage:
 *   DSPKSPI.SPI_WRREG E0 B6    (BME280 soft-reset)
 *   DSPKSPI.SPI_WRREG 20 97    (LIS3DH CTRL_REG1: ODR=1.344kHz, all axes on)
 */
/*----------------------------------------------------------------------------*/
bool DspkspiPlugin::m_DSPKSPI_SPI_WRREG(const std::string& args) const
{
    std::vector<std::string> vstrTok;
    ustring::tokenizeSpaceQuotesAware(args, vstrTok);

    if (vstrTok.size() != 2)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI_WRREG: usage: <reg_hex> <val_hex>"));
        return false;
    }

    uint8_t u8Reg, u8Val;
    if (!m_ParseHexByte(vstrTok[0], u8Reg))
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI_WRREG: invalid register"); LOG_STRING(vstrTok[0]));
        return false;
    }
    if (!m_ParseHexByte(vstrTok[1], u8Val))
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI_WRREG: invalid value"); LOG_STRING(vstrTok[1]));
        return false;
    }

    if (!m_bIsEnabled)
        return true;

    bool bRetVal = false;

    try
    {
        auto shpDrv = std::make_shared<SPIBridge>(m_u16Vid, m_u16Pid);
        if (!shpDrv->is_open())
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI_WRREG: failed to open HID device"));
            return false;
        }

        // [reg & 0x7F, val] — MSB=0 signals write on most SPI devices
        const std::array<uint8_t, 2> frame = {
            static_cast<uint8_t>(u8Reg & 0x7Fu), u8Val
        };
        bRetVal = m_SPIWrite(std::span<const uint8_t>(frame), shpDrv);
        if (bRetVal)
            LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                      LOG_STRING("SPI_WRREG reg=0x"); LOG_HEX8(u8Reg);
                      LOG_STRING("val=0x"); LOG_HEX8(u8Val));
    }
    catch (const std::exception& e)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI_WRREG: exception"); LOG_STRING(e.what()));
    }

    return bRetVal;
}


/*----------------------------------------------------------------------------*/
/**
 * @brief SPI_RDREG — read N bytes starting at a register (MSB=1 → read convention).
 *
 * Sends [reg | 0x80, 0x00 × N] and returns the N MISO bytes that follow the
 * address byte. Compatible with BME280, ICM-42688-P, LIS3DH, MAX31865, etc.
 *
 * Args: <reg_hex> <n_bytes>   (max 5 result bytes)
 *
 * Usage:
 *   DSPKSPI.SPI_RDREG D0 1    (BME280 chip-ID → expect 0x60)
 *   DSPKSPI.SPI_RDREG 28 5    (LIS3DH OUT_X_L + 4 more)
 */
/*----------------------------------------------------------------------------*/
bool DspkspiPlugin::m_DSPKSPI_SPI_RDREG(const std::string& args) const
{
    std::vector<std::string> vstrTok;
    ustring::tokenizeSpaceQuotesAware(args, vstrTok);

    if (vstrTok.size() != 2)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI_RDREG: usage: <reg_hex> <n_bytes>"));
        return false;
    }

    uint8_t u8Reg;
    if (!m_ParseHexByte(vstrTok[0], u8Reg))
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI_RDREG: invalid register"); LOG_STRING(vstrTok[0]));
        return false;
    }

    uint32_t u32Len;
    // Transfer payload = address byte + N data bytes → max SPIBridge::SPI_MAX_TRANSFER_PAYLOAD - 1
    const size_t szMaxRd = SPIBridge::SPI_MAX_TRANSFER_PAYLOAD - 1u;
    if (!numeric::str2uint32(vstrTok[1], u32Len) || u32Len == 0 || u32Len > szMaxRd)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("SPI_RDREG: n_bytes must be 1–"); LOG_UINT32(szMaxRd));
        return false;
    }

    if (!m_bIsEnabled)
        return true;

    bool bRetVal = false;

    try
    {
        auto shpDrv = std::make_shared<SPIBridge>(m_u16Vid, m_u16Pid);
        if (!shpDrv->is_open())
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI_RDREG: failed to open HID device"));
            return false;
        }

        // MOSI: [reg | 0x80, 0x00 × N]
        std::vector<uint8_t> vMosi(u32Len + 1u, 0x00u);
        vMosi[0] = u8Reg | 0x80u;

        std::vector<uint8_t> vMiso(vMosi.size(), 0u);
        bRetVal = m_SPITransfer(std::span<const uint8_t>(vMosi),
                                std::span<uint8_t>(vMiso),
                                shpDrv);

        if (bRetVal)
        {
            // Discard the dummy byte received while clocking the address out
            m_strResultData.clear();
            for (size_t i = 1; i < vMiso.size(); ++i)
            {
                char hex[6];
                std::snprintf(hex, sizeof(hex), "0x%02X", vMiso[i]);
                if (i > 1) m_strResultData += ' ';
                m_strResultData += hex;
            }
            LOG_PRINT(LOG_EMPTY, LOG_HDR;
                      LOG_STRING("SPI_RDREG reg=0x"); LOG_HEX8(u8Reg);
                      LOG_STRING(":"); LOG_STRING(m_strResultData));
        }
    }
    catch (const std::exception& e)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI_RDREG: exception"); LOG_STRING(e.what()));
    }

    return bRetVal;
}


/*----------------------------------------------------------------------------*/
/**
 * @brief SCRIPT — execute a sequence of DSPKSPI commands from a file.
 *
 * Each non-empty, non-comment line contains a full command string in the form:
 *   DSPKSPI.<CMD> [args]
 * or a bare sub-command (CMD + args without the plugin prefix):
 *   SPI_WRITE DE AD BE EF
 *
 * Lines beginning with '#' are treated as comments and skipped.
 * An optional inter-command delay can be specified as the second argument.
 *
 * Args: <filename> [<delay_ms>]
 *
 * Usage:
 *   DSPKSPI.SCRIPT spi_bme280.txt 50
 */
/*----------------------------------------------------------------------------*/
bool DspkspiPlugin::m_DSPKSPI_SCRIPT(const std::string& args) const
{
    if (args.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SCRIPT: usage: <filename> [<delay_ms>]"));
        return false;
    }

    std::vector<std::string> vstrArgs;
    ustring::tokenizeSpaceQuotesAware(args, vstrArgs);

    if (vstrArgs.size() < 1 || vstrArgs.size() > 2)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SCRIPT: expected: <filename> [<delay_ms>]"));
        return false;
    }

    size_t szDelay = 0;
    if (vstrArgs.size() == 2)
    {
        if (!numeric::str2sizet(vstrArgs[1], szDelay))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SCRIPT: invalid delay"); LOG_STRING(vstrArgs[1]));
            return false;
        }
    }

    std::string strScriptPath;
    ufile::buildFilePath(m_strArtefactsPath, vstrArgs[0], strScriptPath);

    if (!ufile::fileExistsAndNotEmpty(strScriptPath))
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SCRIPT: not found or empty"); LOG_STRING(strScriptPath));
        return false;
    }

    if (!m_bIsEnabled)
        return true;

    bool bRetVal = true;

#if 0
    // Read all lines first
    std::vector<std::string> vLines;
    if (!ufile::readLines(strScriptPath, vLines))
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SCRIPT: failed to read"); LOG_STRING(strScriptPath));
        return false;
    }

    for (const auto& strLine : vLines)
    {
        // Skip empty lines and comments
        std::string strTrimmed = ustring::trim(strLine);
        if (strTrimmed.empty() || strTrimmed[0] == '#')
            continue;

        // Accept both  "DSPKSPI.CMD args"  and bare  "CMD args"
        std::string strCmd, strCmdArgs;
        const std::string kPrefix = std::string(DSPKSPI_PLUGIN_NAME) + ".";

        if (strTrimmed.rfind(kPrefix, 0) == 0)
        {
            // Full form: DSPKSPI.CMD args
            std::string strRest = strTrimmed.substr(kPrefix.size());
            size_t szSpace = strRest.find(' ');
            strCmd     = (szSpace == std::string::npos) ? strRest : strRest.substr(0, szSpace);
            strCmdArgs = (szSpace == std::string::npos) ? ""      : strRest.substr(szSpace + 1);
        }
        else
        {
            // Bare form: CMD args
            size_t szSpace = strTrimmed.find(' ');
            strCmd     = (szSpace == std::string::npos) ? strTrimmed : strTrimmed.substr(0, szSpace);
            strCmdArgs = (szSpace == std::string::npos) ? ""         : strTrimmed.substr(szSpace + 1);
        }

        LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                  LOG_STRING("SCRIPT >> "); LOG_STRING(strCmd); LOG_STRING(strCmdArgs));

        if (!doDispatch(strCmd, strCmdArgs))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("SCRIPT: command failed:"); LOG_STRING(strTrimmed));
            if (!m_bIsFaultTolerant)
            {
                bRetVal = false;
                break;
            }
        }

        if (szDelay > 0)
            utime::delay_ms(szDelay);
    }
#endif

    return bRetVal;
}


///////////////////////////////////////////////////////////////////
//               PRIVATE INTERFACES IMPLEMENTATION              //
///////////////////////////////////////////////////////////////////

/*----------------------------------------------------------------------------*/
bool DspkspiPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    bool bRetVal = false;

    if (!psSetParams->mapSettings.empty())
    {
        do
        {
            if (psSetParams->mapSettings.count(CFG_ARTEFACTS_PATH) > 0)
            {
                m_strArtefactsPath = psSetParams->mapSettings.at(CFG_ARTEFACTS_PATH);
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ArtefactsPath:"); LOG_STRING(m_strArtefactsPath));
            }

            if (psSetParams->mapSettings.count(CFG_VID) > 0)
            {
                if (!m_ParseUint16(psSetParams->mapSettings.at(CFG_VID), m_u16Vid))
                {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("LocalSetParams: invalid VID"));
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("VID:"); LOG_HEX16(m_u16Vid));
            }

            if (psSetParams->mapSettings.count(CFG_PID) > 0)
            {
                if (!m_ParseUint16(psSetParams->mapSettings.at(CFG_PID), m_u16Pid))
                {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("LocalSetParams: invalid PID"));
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("PID:"); LOG_HEX16(m_u16Pid));
            }

            if (psSetParams->mapSettings.count(CFG_READ_TIMEOUT) > 0)
            {
                if (!numeric::str2uint32(psSetParams->mapSettings.at(CFG_READ_TIMEOUT),
                                         m_u32ReadTimeout))
                {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("LocalSetParams: invalid READ_TIMEOUT"));
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ReadTimeout:"); LOG_UINT32(m_u32ReadTimeout));
            }

            if (psSetParams->mapSettings.count(CFG_WRITE_TIMEOUT) > 0)
            {
                if (!numeric::str2uint32(psSetParams->mapSettings.at(CFG_WRITE_TIMEOUT),
                                         m_u32WriteTimeout))
                {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("LocalSetParams: invalid WRITE_TIMEOUT"));
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("WriteTimeout:"); LOG_UINT32(m_u32WriteTimeout));
            }

            if (psSetParams->mapSettings.count(CFG_SPI_MODE) > 0)
            {
                if (!setSpiMode(psSetParams->mapSettings.at(CFG_SPI_MODE)))
                {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("LocalSetParams: invalid SPI_MODE (0-3)"));
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                          LOG_STRING("SpiMode:"); LOG_UINT32(static_cast<uint8_t>(m_eSpiMode)));
            }

            if (psSetParams->mapSettings.count(CFG_SPI_CLK_DIV) > 0)
            {
                if (!setSpiClkDiv(psSetParams->mapSettings.at(CFG_SPI_CLK_DIV)))
                {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("LocalSetParams: invalid SPI_CLK_DIV (0-3)"));
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                          LOG_STRING("SpiClkDiv:"); LOG_UINT32(static_cast<uint8_t>(m_eSpiClkDiv)));
            }

            bRetVal = true;

        } while (false);
    }
    else
    {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("LocalSetParams: settings map is empty — using defaults"));
        bRetVal = true;
    }

    return bRetVal;
}


/*----------------------------------------------------------------------------*/
// SPI driver helpers
/*----------------------------------------------------------------------------*/

bool DspkspiPlugin::m_SPIWrite(std::span<const uint8_t>   data,
                                std::shared_ptr<SPIBridge>  shpDrv) const
{
    auto result = shpDrv->tout_write(m_u32WriteTimeout, data);

    if (result.status != SPIBridge::Status::SUCCESS)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("m_SPIWrite failed"));
        return false;
    }
    return true;
}


bool DspkspiPlugin::m_SPIRead(size_t                    szLen,
                               std::span<uint8_t>         buffer,
                               std::shared_ptr<SPIBridge>  shpDrv) const
{
    SPIBridge::SPIReadOptions opts;
    opts.mode   = SPIBridge::SPIReadMode::Read;
    opts.length = szLen;

    auto result = shpDrv->tout_read(m_u32ReadTimeout, buffer, opts);

    if (result.status != SPIBridge::Status::SUCCESS)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("m_SPIRead failed"));
        return false;
    }
    return true;
}


bool DspkspiPlugin::m_SPITransfer(std::span<const uint8_t>   mosi,
                                   std::span<uint8_t>          miso,
                                   std::shared_ptr<SPIBridge>  shpDrv) const
{
    SPIBridge::SPIReadOptions opts;
    opts.mode      = SPIBridge::SPIReadMode::Transfer;
    opts.length    = mosi.size();
    opts.mosi_data.assign(mosi.begin(), mosi.end());

    auto result = shpDrv->tout_read(m_u32ReadTimeout, miso, opts);

    if (result.status != SPIBridge::Status::SUCCESS)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("m_SPITransfer failed"));
        return false;
    }
    return true;
}


/*----------------------------------------------------------------------------*/
// Argument parsing helpers
/*----------------------------------------------------------------------------*/

bool DspkspiPlugin::m_ParseHexByte(const std::string& str, uint8_t& out)
{
    if (str.empty())
        return false;

    try
    {
        // Accept "0xNN", "0XNN", or plain "NN"
        size_t pos = 0;
        unsigned long val = std::stoul(str, &pos, 16);
        if (pos != str.size() && !(str.size() > 2 && str[1] == 'x'))
            return false;
        if (val > 0xFF)
            return false;
        out = static_cast<uint8_t>(val);
        return true;
    }
    catch (...)
    {
        return false;
    }
}


bool DspkspiPlugin::m_ParseHexBytes(const std::vector<std::string>& tokens,
                                     size_t                          szStart,
                                     std::vector<uint8_t>&           out)
{
    out.clear();
    for (size_t i = szStart; i < tokens.size(); ++i)
    {
        uint8_t byte;
        if (!m_ParseHexByte(tokens[i], byte))
            return false;
        out.push_back(byte);
    }
    return !out.empty();
}


bool DspkspiPlugin::m_ParseUint16(const std::string& str, uint16_t& out)
{
    if (str.empty())
        return false;

    try
    {
        size_t pos = 0;
        int base = (str.size() > 2 && str[1] == 'x') ? 16 : 10;
        unsigned long val = std::stoul(str, &pos, base);
        if (val > 0xFFFF)
            return false;
        out = static_cast<uint16_t>(val);
        return true;
    }
    catch (...)
    {
        return false;
    }
}