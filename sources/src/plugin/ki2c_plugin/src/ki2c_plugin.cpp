#include "uSharedConfig.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include "ki2c_setup.hpp"
#include "ki2c_plugin.hpp"

#include "uPluginSettings.hpp"

#include "uNumeric.hpp"
#include "uFile.hpp"
#include "uString.hpp"
#include "uHexlify.hpp"
#include "uKI2C.hpp"
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
#define LT_HDR     "KKI2C        |"
#define LOG_HDR    LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                 //
///////////////////////////////////////////////////////////////////

#define    ARTEFACTS_PATH     "ARTEFACTS_PATH"
#define    KI2C_DEVICE        "I2C_DEVICE"
#define    KI2C_ADDRESS       "I2C_ADDRESS"
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
    EXPORTED KI2CPlugin* pluginEntry()
    {
        return new KI2CPlugin();
    }

    EXPORTED void pluginExit( KI2CPlugin *ptrPlugin)
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

bool KI2CPlugin::doInit(void *pvUserData)
{
    m_bIsInitialized = true;
    return m_bIsInitialized;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Function where to execute de-initialization of sub-modules
*/
/*--------------------------------------------------------------------------------------------------------*/

void KI2CPlugin::doCleanup(void)
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
  *       KI2C.INFO
  *
  * \param[in] args  empty string (no arguments expected)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool KI2CPlugin::m_KI2C_INFO (const std::string &args, std::stop_token st) const
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
    LOG_PRINT(LOG_EMPTY, LOG_STRING(KI2C_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: communicate with devices via KI2C (/dev/i2c-N)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the KI2C device, slave address and transfer parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [d:device] [a:address] [r:read_tout] [w:write_tout] [s:recv_bufsize]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : KI2C.CONFIG d:/dev/i2c-1 a:0x48 r:2000 w:2000 s:256"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         KI2C.CONFIG d:/dev/i2c-0 a:72"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : send commands from a script file"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : script"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : KI2C.SCRIPT script.txt"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : send, receive or both"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : direction message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : KI2C.CMD > H\"AABBCCDD\" | ok"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         KI2C.CMD < \"Please send!\" | F\"data.bin, 256\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : can be both sent/received: (un)quoted strings, hex lines"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : can be only sent: files, only received: tokens"));
    LOG_SEP();

    return true;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CONFIG command implementation; overwrite the current KI2C parameters at runtime.
  *
  * \note Any subset of parameters can be specified; omitted keys retain their current values.
  *
  * \note Usage example:
  *       KI2C.CONFIG d:/dev/i2c-1 a:0x48 r:2000 w:2000 s:256
  *       KI2C.CONFIG d:/dev/i2c-0 a:72 r:1000
  *
  * \param[in] args  [d:device] [a:address] [r:read_tout] [w:write_tout] [s:recv_bufsize]
  *
  * \return true if parameters were updated successfully, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool KI2CPlugin::m_KI2C_CONFIG (const std::string &args, std::stop_token st) const
{
    return generic_i2c_set_params<KI2CPlugin>(this, args);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CMD command implementation; execute a single send/receive operation over KI2C.
  *
  * \note The KI2C device is opened for the duration of the call and closed automatically on return (RAII).
  *
  * \note Usage example:
  *       KI2C.CMD > H\"AABBCCDD\" | H\"06\"
  *       KI2C.CMD < \"Ready\" | \"Go!\"
  *
  * \param[in] args  direction and data expression (see CommScriptCommandValidator grammar)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool KI2CPlugin::m_KI2C_CMD (const std::string &args, std::stop_token st) const
{
    (void)st;

    return ucmdexec::generic_cmd(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<KI2C> {
            // Open the KI2C device (RAII — closed automatically by destructor)
            auto shpDriver = std::make_shared<KI2C>(m_strKI2CDevice, m_u8KI2CAddress, m_strKI2CDevice);
            return shpDriver->is_open() ? shpDriver : nullptr;
        },
        KI2C_PLUGIN_NAME,
        m_u32KI2CReadBufferSize, m_u32ReadTimeout, LT_HDR, &m_strResultData);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief SCRIPT command implementation; execute a multi-command script file over KI2C.
  *
  * \note Usage example:
  *       KI2C.SCRIPT init_sequence.txt
  *       KI2C.SCRIPT sensor_poll.txt 100
  *
  * \param[in] args  filename [delay_ms]
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool KI2CPlugin::m_KI2C_SCRIPT (const std::string &args, std::stop_token st) const
{
    (void)st;

    return ucmdexec::generic_script(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<KI2C> {
            // Open the KI2C device (RAII — closed automatically by destructor)
            auto shpDriver = std::make_shared<KI2C>(m_strKI2CDevice, m_u8KI2CAddress, m_strKI2CDevice);
            return shpDriver->is_open() ? shpDriver : nullptr;
        },
        KI2C_PLUGIN_NAME,
        m_strArtefactsPath, m_u32KI2CReadBufferSize, m_u32ReadTimeout, LT_HDR);
}


///////////////////////////////////////////////////////////////////
//            PRIVATE INTERFACES IMPLEMENTATION                  //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------------------------------------*/

bool KI2CPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    if (true == psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        return true;
    }

    PluginSettingsBinder sSettings;
    sSettings.Bind(ARTEFACTS_PATH, m_strArtefactsPath);
    sSettings.Bind(KI2C_DEVICE,    m_strKI2CDevice);
    sSettings.Bind(KI2C_ADDRESS,   m_u8KI2CAddress);
    sSettings.Bind(READ_TIMEOUT,   m_u32ReadTimeout);
    sSettings.Bind(WRITE_TIMEOUT,  m_u32WriteTimeout);
    sSettings.Bind(READ_BUF_SIZE,  m_u32KI2CReadBufferSize);

    return sSettings.Apply(psSetParams->mapSettings,
        [](const std::string& strKey, const std::string& strRawValue) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strKey); LOG_STRING(":"); LOG_STRING(strRawValue));
        });

} /* m_LocalSetParams() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief message sender
*/
/*--------------------------------------------------------------------------------------------------------*/

bool KI2CPlugin::m_Send(std::span<const uint8_t> dataSpan, std::shared_ptr<const ICommDriver> shpDriver) const
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

bool KI2CPlugin::m_Receive(std::span<uint8_t> dataSpan, size_t& szSize, CommCommandReadType readType, std::shared_ptr<const ICommDriver> shpDriver) const
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
