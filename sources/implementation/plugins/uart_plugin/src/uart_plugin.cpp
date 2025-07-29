#include "CommonSettings.hpp"
#include "PluginSpecOperations.hpp"
#include "PluginScriptClient.hpp"
#include "PluginScriptItemInterpreter.hpp"

#include "uart_plugin.hpp"

#include "uNumeric.hpp"
#include "uFile.hpp"
#include "uString.hpp"
#include "uHexlify.hpp"
#include "uUart.hpp"


///////////////////////////////////////////////////////////////////
//                     LOG DEFINES                               //
///////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR     "UART_PLUGIN:"
#define LOG_HDR    LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                 //
///////////////////////////////////////////////////////////////////

#define    ARTEFACTS_PATH     "ARTEFACTS_PATH"
#define    COM_PORT           "COM_PORT"
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
    EXPORTED UARTPlugin* pluginEntry()
    {
        return new UARTPlugin();
    }

    EXPORTED void pluginExit( UARTPlugin *ptrPlugin)
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

bool UARTPlugin::doInit(void *pvUserData)
{
    m_bIsInitialized = true;
    return m_bIsInitialized;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Function where to execute de-initialization of sub-modules
*/
/*--------------------------------------------------------------------------------------------------------*/


void UARTPlugin::doCleanup(void)
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
  *       UART.INFO
  *
  * \param[in] pstrArgs NULL (NULL means that no arguments are provided to this function)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/


bool UARTPlugin::m_UART_INFO (const std::string &args) const
{
    bool bRetVal = false;

    do {

        // expected no arguments
        if (false == args.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected no argument(s)"));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled) {
            bRetVal = true;
            break;
        }

        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Version:"); LOG_STRING(m_strPluginVersion));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Description: communicate with other apps/devices via UART"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("CONFIG : overwrite the default UART port"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : [p:port] [b:baudrate] [r:read_tout] [w:write_tout] [s:recv_bufsize]"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UART.CONFIG p:COM2 b:115200 r:2000 w:2000 s:1024"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       UART.CONFIG p:/dev/ttyUSB0 b:115200 s:2048"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("SCRIPT : send commands from a file"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : script"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UART.SCRIPT script.txt"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("CMD  : send, receive or both"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : direction message"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UART.CMD > H\"AABBCCDD\" | ok"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       UART.CMD < \"Please send!\" | F\"data.bin, 1024\""));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Note : can be both sent/received: (un)quoted strings, hex. lines"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Note : can be only sent: files, only received: tokens"));

        bRetVal = true;

    } while(false);

    return bRetVal;

}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CONFIG command implementation; overwrite the current UART port (m_strUartPort)
  *
  * \note If an empty string is provided then the command doesn't change anything
  *
  * \note Is intended to change the port when a virtual UART over USB is used
  *
  * \note Usage example: <br>
  *       UART.CONFIG p:COM2 b:115200 r:2000 w:2000 s:1024
  *       UART.CONFIG p:/dev/ttyUSB0 b:115200 r:2000 w:2000 s:1024
  *
  * \param[in] p:port b:baudrate r:readtout w:writetout s:readbuffersize
  *
  * \return true if reading succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/


bool UARTPlugin::m_UART_CONFIG ( const std::string &args) const
{
    return generic_uart_set_params<UARTPlugin>(this, args);

}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief m_UART_CMD command implementation;
  *
  * \note Usage example: <br>
  *       UART.CMD
  *       UART.CMD > Hello | ok                   // send "Hello" and expect to read back "ok"
  *       UART.CMD < "Please send!" | Sending...  // wait to receive "Please send!" and send back "Sending..."
  *
  * \param[in] pstrArgs - optional timeout
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/


bool UARTPlugin::m_UART_CMD ( const std::string &args) const
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
            // open the UART port (RAII implementation, the close is done by destructor)
            auto shpDriver = std::make_shared<UART>(m_strUartPort, m_u32UartBaudrate);

            /* if driver opened successfully */
            if (shpDriver->is_open()) {
                PluginScriptItemValidator validator;
                PToken item;

                if (true == validator.validateItem(args, item)) {
                    PluginScriptItemInterpreter<ICommDriver> interpreter (
                        shpDriver,
                        [this, shpDriver](std::span<const uint8_t> data, std::shared_ptr<ICommDriver>) {
                            return this->m_Send(data, shpDriver);
                        },

                        [this, shpDriver](std::span<uint8_t> data, size_t& size, ReadType type, std::shared_ptr<ICommDriver>) {
                            return this->m_Receive(data, size, type, shpDriver);
                        },
                        m_u32UartReadBufferSize
                    );
                    bRetVal = interpreter.interpretItem(item);
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
  * \brief SCRIPT command implementation;
  *
  * \note Usage example: <br>
  *       UART.SCRIPT scriptname [|delay]
  *
  * \param[in] filename<string>
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool UARTPlugin::m_UART_SCRIPT ( const std::string &args) const
{
    bool bRetVal = false;

    do {

        // expected to have as parameter the name of the script
        if (true == args.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing arg(s): scriptpathname [|delay]"));
            break;
        }

        std::vector<std::string> vstrArgs;
        ustring::tokenizeSpace(args, vstrArgs);
        size_t szNrArgs = vstrArgs.size();

        if ((szNrArgs < 1) || (szNrArgs > 2)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected: scriptpathname [|delay] "));
            break;
        }

        size_t szDelay = 0;
        if (2 == szNrArgs) {
            if (false == numeric::str2size_t(vstrArgs[1], szDelay)) {
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

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled) {
            bRetVal = true;
            break;
        }

        try {
            // open the UART port (RAII implementation, the close is done by destructor)
            auto shpDriver = std::make_shared<UART>(m_strUartPort, m_u32UartBaudrate);

            // driver opened successfully
            if (shpDriver->is_open()) {
                PluginScriptClient<ICommDriver> client (
                    strScriptPathName,
                    shpDriver,

                    [this, shpDriver](std::span<const uint8_t> data, std::shared_ptr<ICommDriver>) {
                        return this->m_Send(data, shpDriver);
                    },

                    [this, shpDriver](std::span<uint8_t> data, size_t& size, ReadType type, std::shared_ptr<ICommDriver>) {
                        return this->m_Receive(data, size, type, shpDriver);
                    },

                    szDelay,
                    m_u32UartReadBufferSize
                );
                bRetVal = client.execute();
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

bool UARTPlugin::m_LocalSetParams( const PluginDataSet *psSetParams)
{
    bool bRetVal = false;

    if (false == psSetParams->mapSettings.empty()) {
        do {
            if (psSetParams->mapSettings.count(ARTEFACTS_PATH) > 0) {
                m_strArtefactsPath = psSetParams->mapSettings.at(ARTEFACTS_PATH);
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ArtefactsPath :"); LOG_STRING(m_strArtefactsPath));
            }

            if (psSetParams->mapSettings.count(COM_PORT) > 0) {
                m_strUartPort = psSetParams->mapSettings.at(COM_PORT);
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Port :"); LOG_STRING(m_strUartPort));
            }

            if (psSetParams->mapSettings.count(BAUDRATE) > 0) {
                if (false == numeric::str2uint32(psSetParams->mapSettings.at(BAUDRATE), m_u32UartBaudrate)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Baudrate :"); LOG_UINT32(m_u32UartBaudrate));
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
                if (false == numeric::str2uint32(psSetParams->mapSettings.at(READ_BUF_SIZE), m_u32UartReadBufferSize)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ReadBufSize :"); LOG_UINT32(m_u32UartReadBufferSize));
            }

            bRetVal = true;

        } while(false);
    }

    return bRetVal;

} /* m_LocalSetParams() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief message sender
*/
/*--------------------------------------------------------------------------------------------------------*/

bool UARTPlugin::m_Send( std::span<const uint8_t> dataSpan, std::shared_ptr<ICommDriver> shpDriver ) const
{
    return (UART::Status::SUCCESS == shpDriver->timeout_write(m_u32WriteTimeout, dataSpan));
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief message receiver
*/
/*--------------------------------------------------------------------------------------------------------*/

bool UARTPlugin::m_Receive( std::span<uint8_t> dataSpan, size_t& szSize, ReadType readType, std::shared_ptr<ICommDriver> shpDriver ) const
{
    bool bRetVal = false;
    size_t szBytesRead = 0;

    switch(readType)
    {
        case ReadType::LINE:
            bRetVal = (UART::Status::SUCCESS == shpDriver->timeout_readline(m_u32ReadTimeout, dataSpan));
            break;

        case ReadType::TOKEN:
            bRetVal = (UART::Status::SUCCESS == shpDriver->timeout_wait_for_token(m_u32ReadTimeout, dataSpan));
            break;

        default:
            bRetVal = (UART::Status::SUCCESS == shpDriver->timeout_read(m_u32ReadTimeout, dataSpan, &szBytesRead));
            break;
    }
    return bRetVal;
}
