#include "uSharedConfig.hpp"
#include "PluginSpecOperations.hpp"

#include "dspki2c_plugin.hpp"

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
#define LT_HDR   "DSPKI2C     |"
#define LOG_HDR  LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                   INI FILE CONFIGURATION KEYS                 //
///////////////////////////////////////////////////////////////////

#define CFG_ARTEFACTS_PATH   "ARTEFACTS_PATH"
#define CFG_VID              "VID"
#define CFG_PID              "PID"
#define CFG_READ_TIMEOUT     "READ_TIMEOUT"
#define CFG_WRITE_TIMEOUT    "WRITE_TIMEOUT"

///////////////////////////////////////////////////////////////////
//                       PLUGIN ENTRY POINTS                     //
///////////////////////////////////////////////////////////////////

extern "C"
{
    EXPORTED DspkI2CPlugin* pluginEntry()
    {
        return new DspkI2CPlugin();
    }

    EXPORTED void pluginExit(DspkI2CPlugin *ptrPlugin)
    {
        if (nullptr != ptrPlugin)
            delete ptrPlugin;
    }
}

///////////////////////////////////////////////////////////////////
//                        INIT / CLEANUP                         //
///////////////////////////////////////////////////////////////////

bool DspkI2CPlugin::doInit(void * /*pvUserData*/)
{
    m_bIsInitialized = true;
    return m_bIsInitialized;
}

void DspkI2CPlugin::doCleanup(void)
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
 * Usage: DSPKI2C.INFO
 */
/*----------------------------------------------------------------------------*/
bool DspkI2CPlugin::m_DSPKI2C_INFO(const std::string& args) const
{
    if (!args.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("INFO: expected no argument(s)"));
        return false;
    }

    if (!m_bIsEnabled)
        return true;

    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING(DSPKI2C_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: Digispark ATtiny85 USB→I2C master bridge"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG    : set HID VID/PID and timeouts at runtime"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Args    : [v:<vid_hex>] [p:<pid_hex>] [r:<read_ms>] [w:<write_ms>]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Usage   : DSPKI2C.CONFIG v:0x16C0 p:0x05DF r:2000 w:2000"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCAN  : scan all 7-bit I2C addresses (1-126)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Args    : (none)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Usage   : DSPKI2C.SCAN"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("WRITE : write bytes to an I2C slave"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Args    : <addr_hex> <byte0_hex> [<byte1_hex> ...]  (max 5 bytes)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Usage   : DSPKI2C.WRITE 3C 00 AF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("          : DSPKI2C.WRITE 0x68 6B 00"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ  : read N bytes from an I2C slave"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Args    : <addr_hex> <n_bytes>  (max 6 bytes)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Usage   : DSPKI2C.READ 68 1"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("WRRD  : write register address, repeated-START read"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Args    : <addr_hex> <reg_hex> <n_bytes>  (max 5 read bytes)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Usage   : DSPKI2C.WRRD 68 75 1   (MPU-6050 WHO_AM_I)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("          : DSPKI2C.WRRD 68 3B 5   (MPU-6050 accel X+Y)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT    : run a sequence of commands from a file"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Args    : <filename> [<delay_ms>]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Usage   : DSPKI2C.SCRIPT i2c_init.txt"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("          : DSPKI2C.SCRIPT i2c_test.txt 50"));
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
 *   DSPKI2C.CONFIG v:0x16C0 p:0x05DF r:2000 w:2000
 *   DSPKI2C.CONFIG r:5000
 */
/*----------------------------------------------------------------------------*/
bool DspkI2CPlugin::m_DSPKI2C_CONFIG(const std::string& args) const
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

        char        cKey   = static_cast<char>(std::tolower(static_cast<unsigned char>(tok[0])));
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
 * @brief SCAN — probe all 7-bit I2C addresses and print responding ones.
 *        Takes no arguments.
 *
 * Usage: DSPKI2C.SCAN
 */
/*----------------------------------------------------------------------------*/
bool DspkI2CPlugin::m_DSPKI2C_SCAN(const std::string& args) const
{
    if (!args.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SCAN: expected no argument(s)"));
        return false;
    }

    if (!m_bIsEnabled)
        return true;

    bool bRetVal = false;

    try
    {
        auto shpDrv = std::make_shared<I2CBridge>(m_u16Vid, m_u16Pid);

        if (!shpDrv->is_open())
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SCAN: failed to open HID device"));
            return false;
        }

        auto result = shpDrv->scan(m_u32ReadTimeout == 0
                                   ? I2CBridge::I2C_SCAN_DEFAULT_TIMEOUT
                                   : m_u32ReadTimeout * 3u);  // scan touches 127 addrs

        if (result.status != I2CBridge::Status::SUCCESS)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SCAN: scan failed"));
            return false;
        }

        LOG_PRINT(LOG_EMPTY, LOG_HDR;
                  LOG_STRING("SCAN: found"); LOG_UINT32(result.addresses.size());
                  LOG_STRING("device(s)"));

        for (uint8_t addr : result.addresses)
        {
            LOG_PRINT(LOG_EMPTY, LOG_HDR;
                      LOG_STRING("  0x"); LOG_HEX8(addr));
        }

        // Store addresses as comma-separated hex in m_strResultData
        m_strResultData.clear();
        for (size_t i = 0; i < result.addresses.size(); ++i)
        {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "0x%02X", result.addresses[i]);
            if (i > 0) m_strResultData += ',';
            m_strResultData += buf;
        }

        bRetVal = true;
    }
    catch (const std::bad_alloc& e)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SCAN: alloc failed"); LOG_STRING(e.what()));
    }
    catch (const std::exception& e)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SCAN: exception"); LOG_STRING(e.what()));
    }

    return bRetVal;
}


/*----------------------------------------------------------------------------*/
/**
 * @brief WRITE — write bytes to an I2C slave (max 5 data bytes).
 *
 * Args: <addr_hex> <byte0_hex> [<byte1_hex> ...]
 *
 * Usage:
 *   DSPKI2C.WRITE 3C 00 AF       (SSD1306: cmd byte + display-ON)
 *   DSPKI2C.WRITE 0x68 6B 00     (MPU-6050: wake-up via PWR_MGMT_1)
 */
/*----------------------------------------------------------------------------*/
bool DspkI2CPlugin::m_DSPKI2C_WRITE(const std::string& args) const
{
    std::vector<std::string> vstrTok;
    ustring::tokenizeSpaceQuotesAware(args, vstrTok);

    if (vstrTok.size() < 2)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("WRITE: usage: <addr_hex> <byte0> [byte1 ...]"));
        return false;
    }

    uint8_t u8Addr;
    if (!m_ParseHexByte(vstrTok[0], u8Addr))
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("WRITE: invalid address"); LOG_STRING(vstrTok[0]));
        return false;
    }

    std::vector<uint8_t> vData;
    if (!m_ParseHexBytes(vstrTok, 1, vData))
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("WRITE: invalid data bytes"));
        return false;
    }

    if (vData.size() > I2CBridge::I2C_MAX_WRITE_PAYLOAD)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("WRITE: max"); LOG_UINT32(I2CBridge::I2C_MAX_WRITE_PAYLOAD);
                  LOG_STRING("bytes per write"));
        return false;
    }

    if (!m_bIsEnabled)
        return true;

    bool bRetVal = false;

    try
    {
        auto shpDrv = std::make_shared<I2CBridge>(m_u16Vid, m_u16Pid);
        if (!shpDrv->is_open())
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("WRITE: failed to open HID device"));
            return false;
        }
        bRetVal = m_I2CWrite(u8Addr, std::span<const uint8_t>(vData), shpDrv);
    }
    catch (const std::exception& e)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("WRITE: exception"); LOG_STRING(e.what()));
    }

    return bRetVal;
}


/*----------------------------------------------------------------------------*/
/**
 * @brief READ — read N bytes from an I2C slave (max 6).
 *
 * Args: <addr_hex> <n_bytes>
 *
 * Usage:
 *   DSPKI2C.READ 68 1    (read 1 byte from 0x68)
 *   DSPKI2C.READ 3C 6
 */
/*----------------------------------------------------------------------------*/
bool DspkI2CPlugin::m_DSPKI2C_READ(const std::string& args) const
{
    std::vector<std::string> vstrTok;
    ustring::tokenizeSpaceQuotesAware(args, vstrTok);

    if (vstrTok.size() != 2)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("READ: usage: <addr_hex> <n_bytes>"));
        return false;
    }

    uint8_t u8Addr;
    if (!m_ParseHexByte(vstrTok[0], u8Addr))
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("READ: invalid address"); LOG_STRING(vstrTok[0]));
        return false;
    }

    uint32_t u32Len;
    if (!numeric::str2uint32(vstrTok[1], u32Len) || u32Len == 0 ||
        u32Len > I2CBridge::I2C_MAX_READ_PAYLOAD)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("READ: n_bytes must be 1–"); LOG_UINT32(I2CBridge::I2C_MAX_READ_PAYLOAD));
        return false;
    }

    if (!m_bIsEnabled)
        return true;

    bool bRetVal = false;

    try
    {
        auto shpDrv = std::make_shared<I2CBridge>(m_u16Vid, m_u16Pid);
        if (!shpDrv->is_open())
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("READ: failed to open HID device"));
            return false;
        }

        std::vector<uint8_t> vBuf(u32Len, 0u);
        bRetVal = m_I2CRead(u8Addr, u32Len, std::span<uint8_t>(vBuf), shpDrv);

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
            LOG_PRINT(LOG_EMPTY, LOG_HDR;
                      LOG_STRING("READ [0x"); LOG_HEX8(u8Addr); LOG_STRING("]:"); LOG_STRING(m_strResultData));
        }
    }
    catch (const std::exception& e)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("READ: exception"); LOG_STRING(e.what()));
    }

    return bRetVal;
}


/*----------------------------------------------------------------------------*/
/**
 * @brief WRRD — write a register address then repeated-START read.
 *
 * Args: <addr_hex> <reg_hex> <n_bytes>
 *
 * Usage:
 *   DSPKI2C.WRRD 68 75 1    (MPU-6050 WHO_AM_I → expects 0x68)
 *   DSPKI2C.WRRD 68 3B 5    (MPU-6050 accel X/Y first 5 bytes)
 *   DSPKI2C.WRRD 76 D0 1    (BMP280 chip-ID → expects 0x60)
 */
/*----------------------------------------------------------------------------*/
bool DspkI2CPlugin::m_DSPKI2C_WRRD(const std::string& args) const
{
    std::vector<std::string> vstrTok;
    ustring::tokenizeSpaceQuotesAware(args, vstrTok);

    if (vstrTok.size() != 3)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("WRRD: usage: <addr_hex> <reg_hex> <n_bytes>"));
        return false;
    }

    uint8_t u8Addr, u8Reg;
    if (!m_ParseHexByte(vstrTok[0], u8Addr))
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("WRRD: invalid address"); LOG_STRING(vstrTok[0]));
        return false;
    }
    if (!m_ParseHexByte(vstrTok[1], u8Reg))
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("WRRD: invalid register"); LOG_STRING(vstrTok[1]));
        return false;
    }

    uint32_t u32Len;
    if (!numeric::str2uint32(vstrTok[2], u32Len) || u32Len == 0 ||
        u32Len > I2CBridge::I2C_MAX_WRITE_READ_RLEN)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("WRRD: n_bytes must be 1–"); LOG_UINT32(I2CBridge::I2C_MAX_WRITE_READ_RLEN));
        return false;
    }

    if (!m_bIsEnabled)
        return true;

    bool bRetVal = false;

    try
    {
        auto shpDrv = std::make_shared<I2CBridge>(m_u16Vid, m_u16Pid);
        if (!shpDrv->is_open())
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("WRRD: failed to open HID device"));
            return false;
        }

        const std::vector<uint8_t> vWriteData = { u8Reg };
        std::vector<uint8_t>       vBuf(u32Len, 0u);

        bRetVal = m_I2CWriteRead(u8Addr,
                                 std::span<const uint8_t>(vWriteData),
                                 u32Len,
                                 std::span<uint8_t>(vBuf),
                                 shpDrv);

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
            LOG_PRINT(LOG_EMPTY, LOG_HDR;
                      LOG_STRING("WRRD [0x"); LOG_HEX8(u8Addr);
                      LOG_STRING("] reg=0x"); LOG_HEX8(u8Reg);
                      LOG_STRING(":"); LOG_STRING(m_strResultData));
        }
    }
    catch (const std::exception& e)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("WRRD: exception"); LOG_STRING(e.what()));
    }

    return bRetVal;
}


/*----------------------------------------------------------------------------*/
/**
 * @brief SCRIPT — execute a sequence of DSPKI2C commands from a file.
 *
 * Each non-empty, non-comment line contains a full command string in the form:
 *   DSPKI2C.<CMD> [args]
 * or a bare sub-command (CMD + args without the plugin prefix):
 *   WRITE 3C 00 AF
 *
 * Lines beginning with '#' are treated as comments and skipped.
 * An optional inter-command delay can be specified as the second argument.
 *
 * Args: <filename> [<delay_ms>]
 *
 * Usage:
 *   DSPKI2C.SCRIPT i2c_oled_init.txt
 *   DSPKI2C.SCRIPT i2c_mpu6050.txt 50
 */
/*----------------------------------------------------------------------------*/
bool DspkI2CPlugin::m_DSPKI2C_SCRIPT(const std::string& args) const
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
    std::vector<std::string> vLines;
    if (!ufile::readLines(strScriptPath, vLines))
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SCRIPT: failed to read"); LOG_STRING(strScriptPath));
        return false;
    }

    for (const auto& strLine : vLines)
    {
        std::string strTrimmed = ustring::trim(strLine);
        if (strTrimmed.empty() || strTrimmed[0] == '#')
            continue;

        std::string strCmd, strCmdArgs;
        const std::string kPrefix = std::string(DSPKI2C_PLUGIN_NAME) + ".";

        if (strTrimmed.rfind(kPrefix, 0) == 0)
        {
            std::string strRest = strTrimmed.substr(kPrefix.size());
            size_t szSpace = strRest.find(' ');
            strCmd     = (szSpace == std::string::npos) ? strRest : strRest.substr(0, szSpace);
            strCmdArgs = (szSpace == std::string::npos) ? ""      : strRest.substr(szSpace + 1);
        }
        else
        {
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
bool DspkI2CPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
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
// I2C driver helpers
/*----------------------------------------------------------------------------*/

bool DspkI2CPlugin::m_I2CWrite(uint8_t                    u8Addr,
                                 std::span<const uint8_t>   data,
                                 std::shared_ptr<I2CBridge>  shpDrv) const
{
    auto result = shpDrv->tout_write(m_u32WriteTimeout, u8Addr, data);

    if (result.status != I2CBridge::Status::SUCCESS)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("m_I2CWrite failed addr=0x"); LOG_HEX8(u8Addr);
                  LOG_STRING("nack="); LOG_UINT32(result.nack ? 1u : 0u));
        return false;
    }
    return true;
}


bool DspkI2CPlugin::m_I2CRead(uint8_t                    u8Addr,
                                size_t                     szLen,
                                std::span<uint8_t>          buffer,
                                std::shared_ptr<I2CBridge>  shpDrv) const
{
    I2CBridge::I2CReadOptions opts;
    opts.mode       = I2CBridge::I2CReadMode::Read;
    opts.slave_addr = u8Addr;
    opts.read_len   = szLen;

    auto result = shpDrv->tout_read(m_u32ReadTimeout, buffer, opts);

    if (result.status != I2CBridge::Status::SUCCESS)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("m_I2CRead failed addr=0x"); LOG_HEX8(u8Addr));
        return false;
    }
    return true;
}


bool DspkI2CPlugin::m_I2CWriteRead(uint8_t                    u8Addr,
                                     std::span<const uint8_t>   writeData,
                                     size_t                     szReadLen,
                                     std::span<uint8_t>          buffer,
                                     std::shared_ptr<I2CBridge>  shpDrv) const
{
    I2CBridge::I2CReadOptions opts;
    opts.mode       = I2CBridge::I2CReadMode::WriteRead;
    opts.slave_addr = u8Addr;
    opts.read_len   = szReadLen;
    opts.write_data.assign(writeData.begin(), writeData.end());

    auto result = shpDrv->tout_read(m_u32ReadTimeout, buffer, opts);

    if (result.status != I2CBridge::Status::SUCCESS)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("m_I2CWriteRead failed addr=0x"); LOG_HEX8(u8Addr);
                  LOG_STRING("nack="); LOG_UINT32(result.nack ? 1u : 0u));
        return false;
    }
    return true;
}


/*----------------------------------------------------------------------------*/
// Argument parsing helpers
/*----------------------------------------------------------------------------*/

bool DspkI2CPlugin::m_ParseHexByte(const std::string& str, uint8_t& out)
{
    if (str.empty())
        return false;

    try
    {
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


bool DspkI2CPlugin::m_ParseHexBytes(const std::vector<std::string>& tokens,
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


bool DspkI2CPlugin::m_ParseUint16(const std::string& str, uint16_t& out)
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
