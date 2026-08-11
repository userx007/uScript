#include "uSharedConfig.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include "ch341_setup.hpp"
#include "ch341_plugin.hpp"

#include "uPluginSettings.hpp"

#include "uNumeric.hpp"
#include "uFile.hpp"
#include "uString.hpp"
#include "uHexlify.hpp"
#include "uCh341.hpp"
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
#define LT_HDR     "CH341       |"
#define LOG_HDR    LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                 //
///////////////////////////////////////////////////////////////////

#define    ARTEFACTS_PATH     "ARTEFACTS_PATH"
#define    CH341_PORT         "CH341_PORT"
#define    BAUDRATE           "BAUDRATE"
#define    READ_TIMEOUT       "READ_TIMEOUT"
#define    WRITE_TIMEOUT      "WRITE_TIMEOUT"
#define    READ_BUF_SIZE      "READ_BUF_SIZE"
#define    READ_BUF_TIMEOUT   "READ_BUF_TIMEOUT"

///////////////////////////////////////////////////////////////////
//                          PLUGIN ENTRY POINT                   //
///////////////////////////////////////////////////////////////////


/**
  * \brief The plugin's entry points
*/
extern "C"
{
    EXPORTED CH341Plugin* pluginEntry()
    {
        return new CH341Plugin();
    }

    EXPORTED void pluginExit( CH341Plugin *ptrPlugin)
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

bool CH341Plugin::doInit(void *pvUserData)
{
    m_bIsInitialized = true;
    return m_bIsInitialized;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Function where to execute de-initialization of sub-modules
*/
/*--------------------------------------------------------------------------------------------------------*/


void CH341Plugin::doCleanup(void)
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
  *        describe the supported functions with examples of usage.
  *        This command takes no arguments and is executed even if the plugin initialization fails
  *
  * \note Usage example: <br>
  *       CH341.INFO
  *
  * \param[in] pstrArgs NULL (NULL means that no arguments are provided to this function)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/


bool CH341Plugin::m_CH341_INFO (const std::string &args, std::stop_token st ) const
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
    LOG_PRINT(LOG_EMPTY, LOG_STRING(CH341_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: communicate with other apps/devices via CH341 (USB-to-serial)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : overwrite the default CH341 port"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args : [p=port] [b=baudrate] [r=read_tout] [w=write_tout] [s=recv_bufsize]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage: CH341.CONFIG p=COM2 b=115200 r=2000 w=2000 s=1024"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("       CH341.CONFIG p=/dev/ttyCH341USB0 b=115200 s=2048"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : send commands from a file"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args : script"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage: CH341.SCRIPT script.txt"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD  : send, receive or both"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args : direction message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage: CH341.CMD > H\"AABBCCDD\" | ok"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("       CH341.CMD < \"Please send!\" | F\"data.bin, 1024\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note : can be both sent/received: (un)quoted strings, hex. lines"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note : can be only sent: files, only received: tokens"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC : send one or more periodic messages"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args : time1 val1 [id1], time2 val2 [id2], ... (time_i in ms, val_i hex)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage: CH341.CYCLIC 100 AABBCCDD, 250 06"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("       CH341.CYCLIC 100 AABBCCDD, 250 06 &"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note : id has no meaning here (byte-stream) and is always omitted"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note : without '&' sends one full pattern (lcm of the time_i) then returns;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("       with '&' repeats forever until the script/thread is stopped"));
    LOG_SEP();

    return true;

}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CONFIG command implementation; overwrite the current CH341 port (m_strCh341Port)
  *
  * \note If an empty string is provided then the command doesn't change anything
  *
  * \note Is intended to change the port when the CH341 enumerates under a different /dev or COM node
  *
  * \note Usage example: <br>
  *       CH341.CONFIG p=COM2 b=115200 r=2000 w=2000 s=1024
  *       CH341.CONFIG p=/dev/ttyCH341USB0 b=115200 r=2000 w=2000 s=1024
  *
  * \param[in] p=port b=baudrate r=readtout w=writetout s=readbuffersize
  *
  * \return true if reading succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/


bool CH341Plugin::m_CH341_CONFIG ( const std::string &args, std::stop_token st ) const
{
    return generic_ch341_set_params<CH341Plugin>(this, args);

}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief m_CH341_CMD command implementation;
  *
  * \note Usage example: <br>
  *       CH341.CMD
  *       CH341.CMD > Hello | ok                   // send "Hello" and expect to read back "ok"
  *       CH341.CMD < "Please send!" | Sending...  // wait to receive "Please send!" and send back "Sending..."
  *
  * \param[in] pstrArgs - optional timeout
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/


bool CH341Plugin::m_CH341_CMD ( const std::string &args, std::stop_token st ) const
{
    (void)st;

    return ucmdexec::generic_cmd(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<CH341> {
            // open the CH341 port (RAII implementation, the close is done by destructor)
            auto shpDriver = std::make_shared<CH341>(m_strCh341Port, m_u32Ch341Baudrate, m_strCh341Port);
            return shpDriver->is_open() ? shpDriver : nullptr;
        },
        m_strInstanceName,
        m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, &m_strResultData);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief SCRIPT command implementation;
  *
  * \note Usage example: <br>
  *       CH341.SCRIPT scriptname [|delay]
  *
  * \param[in] filename<string>
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool CH341Plugin::m_CH341_SCRIPT ( const std::string &args, std::stop_token st ) const
{
    (void)st;

    return ucmdexec::generic_script(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<CH341> {
            // open the CH341 port (RAII implementation, the close is done by destructor)
            auto shpDriver = std::make_shared<CH341>(m_strCh341Port, m_u32Ch341Baudrate, m_strCh341Port);
            return shpDriver->is_open() ? shpDriver : nullptr;
        },
        m_strInstanceName,
        m_strArtefactsPath, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CYCLIC command implementation; send one or more periodic CH341 messages.
  *
  * \note The CH341 port is opened once for the whole CYCLIC session (like SCRIPT) and closed
  *       automatically on return (RAII). CH341 is a byte-stream with no addressing concept, so
  *       each entry's optional "id" is never sent on the wire — omit it — and "val" is the
  *       payload as a plain hex string (e.g. "AABBCCDD").
  *
  * \note Usage example:
  *       CH341.CYCLIC 100 AABBCCDD, 250 06
  *       CH341.CYCLIC 100 AABBCCDD, 250 06 &
  *
  * \param[in] args  "time1 val1 , time2 val2 , ..." (see generic_send_cyclic())
  * \param[in] st    stop_token; forwarded as-is (present/absent '&' selects run-once vs. forever)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool CH341Plugin::m_CH341_CYCLIC ( const std::string &args, std::stop_token st ) const
{
    return ucmdexec::generic_send_cyclic(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<CH341> {
            auto shpDriver = std::make_shared<CH341>(m_strCh341Port, m_u32Ch341Baudrate, m_strCh341Port);
            return shpDriver->is_open() ? shpDriver : nullptr;
        },
        m_strInstanceName, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, st);
}


///////////////////////////////////////////////////////////////////
//            PRIVATE INTERFACES IMPLEMENTATION                  //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------------------------------------*/

bool CH341Plugin::m_LocalSetParams( const PluginDataSet *psSetParams)
{
    // Runtime instance identity for the GUI comm-dump panel (e.g. "CH341:1"); falls back to the fixed plugin name if the
    // interpreter didn't supply one. Done before the "nothing loaded from ini"
    // early-return below so it's always captured.
    m_strInstanceName = psSetParams->strInstanceName.empty() ? CH341_PLUGIN_NAME : psSetParams->strInstanceName;

    if (true == psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        return true;
    }

    PluginSettingsBinder sSettings;
    sSettings.Bind(ARTEFACTS_PATH, m_strArtefactsPath);
    sSettings.Bind(CH341_PORT,     m_strCh341Port);
    sSettings.Bind(BAUDRATE,       m_u32Ch341Baudrate);
    sSettings.Bind(READ_TIMEOUT,   m_u32ReadTimeout);
    sSettings.Bind(WRITE_TIMEOUT,  m_u32WriteTimeout);
    sSettings.Bind(READ_BUF_SIZE,  m_u32ReadBufferSize);

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

bool CH341Plugin::m_Send( std::span<const uint8_t> dataSpan, std::shared_ptr<const ICommDriver> shpDriver ) const
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

bool CH341Plugin::m_Receive( std::span<uint8_t> dataSpan, size_t& szSize, CommCommandReadType readType, std::shared_ptr<const ICommDriver> shpDriver ) const
{
    bool bRetVal = false;
    ICommDriver::ReadOptions options;

    switch(readType)
    {
        case CommCommandReadType::LINE:
            options.mode = ICommDriver::ReadMode::UntilDelimiter;
            options.delimiter = '\n';  // CHAR_SEPARATOR_NEWLINE
            break;

        case CommCommandReadType::TOKEN_STRING:
            [[fallthrough]];
        case CommCommandReadType::TOKEN_HEXSTREAM:
            options.mode = ICommDriver::ReadMode::UntilToken;
            options.token = dataSpan;
            options.use_buffer = true;
            break;

        default:
            options.mode = ICommDriver::ReadMode::Exact;
            break;
    }

    auto result = shpDriver->tout_read(m_u32ReadTimeout, dataSpan, options);

    if (result.status == ICommDriver::Status::SUCCESS) {
        szSize = result.bytes_read;
        bRetVal = true;
    } else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Read failed:");
                  LOG_STRING(ICommDriver::to_string(result.status));
                  LOG_STRING("Bytes read:"); LOG_SIZET(result.bytes_read));
        szSize = result.bytes_read;
        bRetVal = false;
    }

    return bRetVal;
}
