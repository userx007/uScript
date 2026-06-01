#include "uSharedConfig.hpp"
#include "PluginSpecOperations.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include "spi_plugin.hpp"

#include "uNumeric.hpp"
#include "uFile.hpp"
#include "uString.hpp"
#include "uHexlify.hpp"
#include "uSpi.hpp"


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR     "SPI         |"
#define LOG_HDR    LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                 //
///////////////////////////////////////////////////////////////////

#define    ARTEFACTS_PATH     "ARTEFACTS_PATH"
#define    SPI_DEVICE         "SPI_DEVICE"
#define    SPI_MODE           "SPI_MODE"
#define    SPI_SPEED_HZ       "SPI_SPEED_HZ"
#define    SPI_BITS_PER_WORD  "SPI_BITS_PER_WORD"
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
    EXPORTED SPIPlugin* pluginEntry()
    {
        return new SPIPlugin();
    }

    EXPORTED void pluginExit( SPIPlugin *ptrPlugin)
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

bool SPIPlugin::doInit(void *pvUserData)
{
    m_bIsInitialized = true;
    return m_bIsInitialized;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Function where to execute de-initialization of sub-modules
*/
/*--------------------------------------------------------------------------------------------------------*/

void SPIPlugin::doCleanup(void)
{
    m_bIsInitialized = false;
    m_bIsEnabled     = false;
}

///////////////////////////////////////////////////////////////////
//                          COMMAND HANDLERS                     //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief INFO command implementation; shows details about the plugin and
  *        describes the supported functions with examples of usage.
  *        This command takes no arguments and is executed even if plugin initialization fails.
  *
  * \note Usage example:
  *       SPI.INFO
  *
  * \param[in] args  empty string (no arguments expected)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool SPIPlugin::m_SPI_INFO (const std::string &args, std::stop_token st) const
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
    LOG_PRINT(LOG_EMPTY, LOG_STRING(SPI_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: communicate with devices via SPI (/dev/spidevB.C)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the SPI device and bus parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [d:device] [m:mode] [z:speed_hz] [b:bits_per_word] [r:read_tout] [w:write_tout] [s:recv_bufsize]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : SPI.CONFIG d:/dev/spidev0.0 m:0 z:1000000 b:8 r:2000 w:2000 s:256"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SPI.CONFIG d:/dev/spidev0.1 m:1 z:4000000"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : send commands from a script file"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : script"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : SPI.SCRIPT script.txt"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : send, receive or both"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : direction message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : SPI.CMD > H\"AABBCCDD\" | H\"00000000\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SPI.CMD < \"Please send!\" | F\"data.bin, 256\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : can be both sent/received: (un)quoted strings, hex lines"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : can be only sent: files, only received: tokens"));
    LOG_SEP();

    return true;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CONFIG command implementation; overwrite the current SPI parameters at runtime.
  *
  * \note Any subset of parameters can be specified; omitted keys retain their current values.
  *
  * \note Usage example:
  *       SPI.CONFIG d:/dev/spidev0.0 m:0 z:1000000 b:8 r:2000 w:2000 s:256
  *       SPI.CONFIG d:/dev/spidev0.1 m:3 z:8000000
  *
  * \param[in] args  [d:device] [m:mode] [z:speed_hz] [b:bits_per_word] [r:read_tout] [w:write_tout] [s:recv_bufsize]
  *
  * \return true if parameters were updated successfully, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool SPIPlugin::m_SPI_CONFIG (const std::string &args, std::stop_token st) const
{
    return generic_spi_set_params<SPIPlugin>(this, args);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CMD command implementation; execute a single send/receive operation over SPI.
  *
  * \note The SPI device is opened for the duration of the call and closed automatically on return (RAII).
  *
  * \note Usage example:
  *       SPI.CMD > H\"01\" | H\"00\"
  *       SPI.CMD < \"Ready\" | \"Go!\"
  *
  * \param[in] args  direction and data expression (see CommScriptCommandValidator grammar)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool SPIPlugin::m_SPI_CMD (const std::string &args, std::stop_token st) const
{
    bool bRetVal = false;

    do {

        if (true == args.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing command"));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled) {
            bRetVal = true;
            break;
        }

        try {
            // Build the SpiConfig from the current plugin settings
            SPI::SpiConfig config;
            config.mode          = m_u8SpiMode;
            config.speed_hz      = m_u32SpiSpeedHz;
            config.bits_per_word = m_u8SpiBitsPerWord;

            // Open the SPI device (RAII — closed automatically by destructor)
            auto shpDriver = std::make_shared<SPI>(m_strSpiDevice, config);

            if (shpDriver->is_open()) {
                CommScriptCommandValidator validator;
                CommCommand command;

                if (true == validator.validateCommand(0, args, command)) {
                    CommScriptCommandInterpreter<SPI> interpreter(
                        shpDriver,
                        m_u32SpiReadBufferSize,
                        m_u32ReadTimeout
                    );
                    bRetVal = interpreter.interpretCommand(command, m_bIsEnabled);
                }
            }
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
  * \brief SCRIPT command implementation; execute a multi-command script file over SPI.
  *
  * \note Usage example:
  *       SPI.SCRIPT init_sequence.txt
  *       SPI.SCRIPT flash_write.txt 100
  *
  * \param[in] args  filename [delay_ms]
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool SPIPlugin::m_SPI_SCRIPT (const std::string &args, std::stop_token st) const
{
    bool bRetVal = false;

    do {

        // expected to have as parameter the name of the script
        if (true == args.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing arg(s): scriptpathname [|delay]"));
            break;
        }

        std::vector<std::string> vstrArgs;
        ustring::tokenizeSpaceQuotesAware(args, vstrArgs);
        size_t szNrArgs = vstrArgs.size();

        if ((szNrArgs < 1) || (szNrArgs > 2)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected: scriptpathname [|delay] "));
            break;
        }

        size_t szDelay = 0;
        if (2 == szNrArgs) {
            if (false == numeric::str2sizet(vstrArgs[1], szDelay)) {
                break;
            }
        }

        std::string strScriptPathName;
        ufile::buildFilePath(m_strArtefactsPath, vstrArgs[0], strScriptPathName);

        // Check file existence and size
        if (false == ufile::fileExistsAndNotEmpty(strScriptPathName)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Script not found or empty:"); LOG_STRING(strScriptPathName));
            break;
        }

        try {
            // Build the SpiConfig from the current plugin settings
            SPI::SpiConfig config;
            config.mode          = m_u8SpiMode;
            config.speed_hz      = m_u32SpiSpeedHz;
            config.bits_per_word = m_u8SpiBitsPerWord;

            // Open the SPI device (RAII — closed automatically by destructor)
            auto shpDriver = std::make_shared<SPI>(m_strSpiDevice, config);

            if (shpDriver->is_open()) {
                CommScriptClient<SPI> client(
                    strScriptPathName,
                    shpDriver,
                    m_u32SpiReadBufferSize,  // szMaxRecvSize
                    m_u32ReadTimeout,        // u32DefaultTimeout
                    szDelay                  // szDelay
                );
                bRetVal = client.execute(m_bIsEnabled);
            }
        } catch (const std::bad_alloc& e) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Memory allocation failed:"); LOG_STRING(e.what()));
        } catch (const std::exception& e) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Execution failed:"); LOG_STRING(e.what()));
        }

    } while(false);

    return bRetVal;
}


///////////////////////////////////////////////////////////////////
//            PRIVATE INTERFACES IMPLEMENTATION                  //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------------------------------------*/

bool SPIPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    bool bRetVal = false;

    if (false == psSetParams->mapSettings.empty()) {
        do {
            if (psSetParams->mapSettings.count(ARTEFACTS_PATH) > 0) {
                m_strArtefactsPath = psSetParams->mapSettings.at(ARTEFACTS_PATH);
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ArtefactsPath :"); LOG_STRING(m_strArtefactsPath));
            }

            if (psSetParams->mapSettings.count(SPI_DEVICE) > 0) {
                m_strSpiDevice = psSetParams->mapSettings.at(SPI_DEVICE);
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Device :"); LOG_STRING(m_strSpiDevice));
            }

            if (psSetParams->mapSettings.count(SPI_MODE) > 0) {
                if (false == setSpiMode(psSetParams->mapSettings.at(SPI_MODE))) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Mode :"); LOG_UINT32(m_u8SpiMode));
            }

            if (psSetParams->mapSettings.count(SPI_SPEED_HZ) > 0) {
                if (false == numeric::str2uint32(psSetParams->mapSettings.at(SPI_SPEED_HZ), m_u32SpiSpeedHz)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("SpeedHz :"); LOG_UINT32(m_u32SpiSpeedHz));
            }

            if (psSetParams->mapSettings.count(SPI_BITS_PER_WORD) > 0) {
                if (false == setSpiBitsPerWord(psSetParams->mapSettings.at(SPI_BITS_PER_WORD))) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("BitsPerWord :"); LOG_UINT32(m_u8SpiBitsPerWord));
            }

            if (psSetParams->mapSettings.count(READ_TIMEOUT) > 0) {
                if (false == numeric::str2uint32(psSetParams->mapSettings.at(READ_TIMEOUT), m_u32ReadTimeout)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ReadTimeout :"); LOG_UINT32(m_u32ReadTimeout));
            }

            if (psSetParams->mapSettings.count(WRITE_TIMEOUT) > 0) {
                if (false == numeric::str2uint32(psSetParams->mapSettings.at(WRITE_TIMEOUT), m_u32WriteTimeout)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("WriteTimeout :"); LOG_UINT32(m_u32WriteTimeout));
            }

            if (psSetParams->mapSettings.count(READ_BUF_SIZE) > 0) {
                if (false == numeric::str2uint32(psSetParams->mapSettings.at(READ_BUF_SIZE), m_u32SpiReadBufferSize)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ReadBufSize :"); LOG_UINT32(m_u32SpiReadBufferSize));
            }

            bRetVal = true;

        } while(false);
    } else {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        bRetVal = true;
    }

    return bRetVal;

} /* m_LocalSetParams() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief message sender
*/
/*--------------------------------------------------------------------------------------------------------*/

bool SPIPlugin::m_Send(std::span<const uint8_t> dataSpan, std::shared_ptr<const ICommDriver> shpDriver) const
{
    auto result = shpDriver->tout_write(m_u32WriteTimeout, dataSpan);

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
  * \brief message receiver
*/
/*--------------------------------------------------------------------------------------------------------*/

bool SPIPlugin::m_Receive(std::span<uint8_t> dataSpan, size_t& szSize, CommCommandReadType readType, std::shared_ptr<const ICommDriver> shpDriver) const
{
    bool bRetVal = false;
    ICommDriver::ReadOptions options;

    switch(readType)
    {
        case CommCommandReadType::LINE:
            options.mode      = ICommDriver::ReadMode::UntilDelimiter;
            options.delimiter = '\n';
            break;

        case CommCommandReadType::TOKEN_STRING:
            [[fallthrough]];
        case CommCommandReadType::TOKEN_HEXSTREAM:
            options.mode       = ICommDriver::ReadMode::UntilToken;
            options.token      = dataSpan;
            options.use_buffer = true;
            break;

        default:
            options.mode = ICommDriver::ReadMode::Exact;
            break;
    }

    auto result = shpDriver->tout_read(m_u32ReadTimeout, dataSpan, options);

    if (result.status == ICommDriver::Status::SUCCESS) {
        szSize  = result.bytes_read;
        bRetVal = true;
    } else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Read failed:");
                  LOG_STRING(ICommDriver::to_string(result.status));
                  LOG_STRING("Bytes read:"); LOG_SIZET(result.bytes_read));
        szSize  = result.bytes_read;
        bRetVal = false;
    }

    return bRetVal;
}
