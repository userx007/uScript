#include "uSharedConfig.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include "dspki2c_setup.hpp"
#include "dspki2c_plugin.hpp"

#include "uPluginSettings.hpp"

#include "uDigisparkI2C.hpp"

#include "uNumeric.hpp"
#include "uFile.hpp"
#include "uString.hpp"
#include "uHexlify.hpp"
#include "uCommandExec.hpp"


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR     "DSPKI2C     |"
#define LOG_HDR    LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                 //
///////////////////////////////////////////////////////////////////

#define    ARTEFACTS_PATH     "ARTEFACTS_PATH"
#define    I2C_VID            "I2C_VID"
#define    I2C_PID            "I2C_PID"
#define    I2C_SLAVE_ADDR     "I2C_SLAVE_ADDR"
#define    READ_TIMEOUT       "READ_TIMEOUT"
#define    WRITE_TIMEOUT      "WRITE_TIMEOUT"
#define    READ_BUF_SIZE      "READ_BUF_SIZE"

///////////////////////////////////////////////////////////////////
//                          PLUGIN ENTRY POINT                   //
///////////////////////////////////////////////////////////////////


/**
  * \brief The plugin's entry points
*/
extern "C"
{
    EXPORTED DSPKi2cPlugin* pluginEntry()
    {
        return new DSPKi2cPlugin();
    }

    EXPORTED void pluginExit( DSPKi2cPlugin *ptrPlugin)
    {
        if (nullptr != ptrPlugin)
        {
            delete ptrPlugin;
        }
    }
}


///////////////////////////////////////////////////////////////////
//                          INIT / CLEANUP                       //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Function where to execute initialization of sub-modules
*/
/*--------------------------------------------------------------------------------------------------------*/

bool DSPKi2cPlugin::doInit(void *pvUserData)
{
    m_bIsInitialized = true;
    return m_bIsInitialized;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Function where to execute de-initialization of sub-modules
*/
/*--------------------------------------------------------------------------------------------------------*/


void DSPKi2cPlugin::doCleanup(void)
{
    m_bIsInitialized = false;
    m_bIsEnabled     = false;
}

///////////////////////////////////////////////////////////////////
//                          COMMAND HANDLERS                     //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief INFO command implementation; shows details about plugin and
  *        describes the supported functions with examples of usage.
  *        This command takes no arguments and is executed even if the plugin initialization fails.
  *
  * \note Usage example:
  *       DSPKI2C.INFO
  *
  * \param[in] args empty string (no arguments expected)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/


bool DSPKi2cPlugin::m_DSPKI2C_INFO (const std::string &args, std::stop_token st ) const
{
    // expected no arguments
    if (!args.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected no argument(s)"));
        return false;
    }

    // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
    if (!m_bIsEnabled)
    {
        return true;
    }

    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING(DSPKI2C_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: communicate with I2C devices via Digispark ATtiny85 USB bridge"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : configure the USB bridge and I2C parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [v=vid] [p=pid] [a=slave_addr] [r=read_tout] [w=write_tout] [s=recv_bufsize]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : DSPKI2C.CONFIG v=16C0 p=05DF a=48 r=2000 w=2000 s=64"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         DSPKI2C.CONFIG a=68 r=500"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : VID/PID are hex (no 0x prefix); slave_addr is 7-bit hex"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCAN   : discover all responding I2C slaves on the bus"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : (none)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : DSPKI2C.SCAN"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : send, receive or both over I2C"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : direction message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : DSPKI2C.CMD > H\"AABBCCDD\" | ok"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         DSPKI2C.CMD < \"Please send!\" | F\"data.bin, 64\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : can be both sent/received: (un)quoted strings, hex lines"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : can be only sent: files, only received: tokens"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : send commands from a file"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : script [|delay]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : DSPKI2C.SCRIPT script.txt"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         DSPKI2C.SCRIPT i2c_seq.txt |50"));
    LOG_SEP();

    return true;

}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CONFIG command implementation; overwrite the current I2C/USB bridge parameters.
  *
  * \note Usage examples:
  *       DSPKI2C.CONFIG v=16C0 p=05DF a=48 r=2000 w=2000 s=64
  *       DSPKI2C.CONFIG a=68 r=500
  *
  * \param[in] args  space-separated key=value pairs (see dspki2c_setup.hpp)
  *
  * \return true if all parameters were accepted, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/


bool DSPKi2cPlugin::m_DSPKI2C_CONFIG ( const std::string &args, std::stop_token st ) const
{
    return generic_i2c_set_params<DSPKi2cPlugin>(this, args);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief SCAN command implementation; discovers all responding I2C slave addresses.
  *
  * Opens a fresh I2CBridge connection, issues a bus scan (CMD_SCAN), logs each
  * found address, and closes the device.  The scan result is also appended to
  * m_strResultData so callers can retrieve addresses programmatically via getData().
  *
  * \note Usage example:
  *       DSPKI2C.SCAN
  *
  * \param[in] args  empty string (no arguments expected)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool DSPKi2cPlugin::m_DSPKI2C_SCAN ( const std::string &args, std::stop_token st ) const
{
    bool bRetVal = false;

    do {

        if (!args.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected no argument(s)"));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled) {
            bRetVal = true;
            break;
        }

        try {
            // RAII: open the Digispark bridge; destructor calls close()
            auto shpBridge = std::make_shared<I2CBridge>(m_u16Vid, m_u16Pid);

            if (!shpBridge->is_open()) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to open I2C bridge (VID/PID mismatch or device absent)"));
                break;
            }

            auto result = shpBridge->scan(I2CBridge::I2C_SCAN_DEFAULT_TIMEOUT);

            if (result.status != ICommDriver::Status::SUCCESS) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Bus scan failed:"); LOG_STRING(ICommDriver::to_string(result.status)));
                break;
            }

            if (result.addresses.empty()) {
                LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Bus scan complete — no devices found"));
            } else {
                LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Bus scan complete — found addresses:"));
                for (uint8_t addr : result.addresses) {
                    LOG_PRINT(LOG_EMPTY, LOG_HDR; LOG_HEX8(addr));
                    // append to result data for programmatic retrieval
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "0x%02X\n", addr);
                    m_strResultData += buf;
                }
            }

            bRetVal = true;

        } catch (const std::bad_alloc& e) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Memory allocation failed:"); LOG_STRING(e.what()));
        } catch (const std::exception& e) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Execution failed:"); LOG_STRING(e.what()));
        }

    } while(false);

    return bRetVal;

}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CMD command implementation; sends and/or receives I2C frames.
  *
  * Uses the same mini-language as UART.CMD.  The I2CBridge is opened
  * fresh per invocation (RAII) using the currently configured VID/PID
  * and slave address.
  *
  * \note Usage examples:
  *       DSPKI2C.CMD > H"AABBCCDD" | ok
  *       DSPKI2C.CMD < "Ping" | H"FF"
  *
  * \param[in] args  command string parsed by CommScriptCommandValidator
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/


bool DSPKi2cPlugin::m_DSPKI2C_CMD ( const std::string &args, std::stop_token st ) const
{
    (void)st;

    return ucmdexec::generic_cmd(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<I2CBridge> {
            // RAII: open the Digispark bridge; destructor calls close()
            auto shpBridge = std::make_shared<I2CBridge>(m_u16Vid, m_u16Pid);

            if (!shpBridge->is_open()) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to open I2C bridge (VID/PID mismatch or device absent)"));
                return nullptr;
            }

            return shpBridge;
        },
        DSPKI2C_PLUGIN_NAME,
        m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, &m_strResultData);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief SCRIPT command implementation; executes a file containing CMD lines.
  *
  * \note Usage examples:
  *       DSPKI2C.SCRIPT i2c_seq.txt
  *       DSPKI2C.SCRIPT i2c_seq.txt |50
  *
  * \param[in] args  scriptpathname [|delay_ms]
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool DSPKi2cPlugin::m_DSPKI2C_SCRIPT ( const std::string &args, std::stop_token st ) const
{
    (void)st;

    return ucmdexec::generic_script(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<I2CBridge> {
            // RAII: open the Digispark bridge; destructor calls close()
            auto shpBridge = std::make_shared<I2CBridge>(m_u16Vid, m_u16Pid);

            if (!shpBridge->is_open()) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to open I2C bridge (VID/PID mismatch or device absent)"));
                return nullptr;
            }

            return shpBridge;
        },
        DSPKI2C_PLUGIN_NAME,
        m_strArtefactsPath, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR);
}


///////////////////////////////////////////////////////////////////
//            PRIVATE INTERFACES IMPLEMENTATION                  //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Load and validate settings from the INI file / PluginDataSet.
  *
  * Recognised keys (case-sensitive):
  *   ARTEFACTS_PATH, I2C_VID, I2C_PID, I2C_SLAVE_ADDR,
  *   READ_TIMEOUT, WRITE_TIMEOUT, READ_BUF_SIZE
*/
/*--------------------------------------------------------------------------------------------------------*/

bool DSPKi2cPlugin::m_LocalSetParams( const PluginDataSet *psSetParams)
{
    if (true == psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        return true;
    }

    PluginSettingsBinder sSettings;
    sSettings.Bind(ARTEFACTS_PATH, m_strArtefactsPath);
    sSettings.Bind(I2C_VID, [this](const std::string& v) {
        if (false == setVid(v)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid I2C_VID value"));
            return false;
        }
        return true;
    });
    sSettings.Bind(I2C_PID, [this](const std::string& v) {
        if (false == setPid(v)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid I2C_PID value"));
            return false;
        }
        return true;
    });
    sSettings.Bind(I2C_SLAVE_ADDR, [this](const std::string& v) {
        if (false == setSlaveAddr(v)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid I2C_SLAVE_ADDR (must be 7-bit hex, 00-7F)"));
            return false;
        }
        return true;
    });
    sSettings.Bind(READ_TIMEOUT,  m_u32ReadTimeout);
    sSettings.Bind(WRITE_TIMEOUT, m_u32WriteTimeout);
    sSettings.Bind(READ_BUF_SIZE, m_u32ReadBufferSize);

    return sSettings.Apply(psSetParams->mapSettings,
        [](const std::string& strKey, const std::string& strRawValue) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strKey); LOG_STRING(":"); LOG_STRING(strRawValue));
        });

} /* m_LocalSetParams() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief message sender — delegates to ICommDriver::tout_write().
  *
  * The I2CBridge base-interface write convention requires the first byte of
  * the buffer to be the 7-bit slave address.  This wrapper prepends the
  * configured m_u8SlaveAddr automatically so callers can pass raw payload.
*/
/*--------------------------------------------------------------------------------------------------------*/

bool DSPKi2cPlugin::m_Send( std::span<const uint8_t> dataSpan, std::shared_ptr<const ICommDriver> shpDriver ) const
{
    // Build a buffer with the slave address as the leading byte
    std::vector<uint8_t> buf;
    buf.reserve(1 + dataSpan.size());
    buf.push_back(m_u8SlaveAddr);
    buf.insert(buf.end(), dataSpan.begin(), dataSpan.end());

    auto result = shpDriver->tout_write(m_u32WriteTimeout, std::span<const uint8_t>(buf));

    if (result.status != ICommDriver::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Write failed:");
                  LOG_STRING(ICommDriver::to_string(result.status));
                  LOG_STRING("Bytes written:"); LOG_SIZET(result.bytes_written));
        return false;
    }

    return true;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief message receiver — delegates to ICommDriver::tout_read().
  *
  * Maps CommCommandReadType onto ICommDriver::ReadMode following the same
  * convention used by the UART plugin:
  *   LINE            → UntilDelimiter  (delimiter = '\\n')
  *   TOKEN_STRING /
  *   TOKEN_HEXSTREAM → UntilToken      (token = expected payload)
  *   default         → Exact
  *
  * For I2CBridge the slave address is passed through ReadOptions::delimiter
  * (base-interface convention documented in uDigisparkI2C.hpp).
*/
/*--------------------------------------------------------------------------------------------------------*/

bool DSPKi2cPlugin::m_Receive( std::span<uint8_t> dataSpan, size_t& szSize, CommCommandReadType readType, std::shared_ptr<const ICommDriver> shpDriver ) const
{
    bool bRetVal = false;
    ICommDriver::ReadOptions options;

    switch(readType)
    {
        case CommCommandReadType::LINE:
            options.mode      = ICommDriver::ReadMode::UntilDelimiter;
            options.delimiter = m_u8SlaveAddr;  // slave addr carried in delimiter field
            break;

        case CommCommandReadType::TOKEN_STRING:
            [[fallthrough]];
        case CommCommandReadType::TOKEN_HEXSTREAM:
            options.mode       = ICommDriver::ReadMode::UntilToken;
            options.token      = dataSpan;
            options.use_buffer = true;
            options.delimiter  = m_u8SlaveAddr;
            break;

        default:
            options.mode      = ICommDriver::ReadMode::Exact;
            options.delimiter = m_u8SlaveAddr;
            break;
    }

    auto result = shpDriver->tout_read(m_u32ReadTimeout, dataSpan, options);

    if (result.status == ICommDriver::Status::SUCCESS) {
        szSize   = result.bytes_read;
        bRetVal  = true;
    } else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Read failed:");
                  LOG_STRING(ICommDriver::to_string(result.status));
                  LOG_STRING("Bytes read:"); LOG_SIZET(result.bytes_read));
        szSize  = result.bytes_read;
        bRetVal = false;
    }

    return bRetVal;
}
