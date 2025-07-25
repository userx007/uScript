#include "uart_plugin.hpp"

#include "CommonSettings.hpp"
#include "PluginSpecOperations.hpp"
#include "PluginScriptClient.hpp"

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


bool UARTPlugin::m_UART_INFO ( const std::string &args) const
{
    bool bRetVal = false;

    do {

        // expected no arguments
        if (false == args.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected no argument(s)"));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled)
        {
            bRetVal = true;
            break;
        }

        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Version:"); LOG_STRING(m_strPluginVersion));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Description: communicate with other apps/devices via UART"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("CONFIG : overwrite the default UART port"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : [port]"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UART.CONFIG COM5"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       UART.CONFIG $NEW_PORT"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Note : If no port is given then the default port remains unchanged"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("READ : read and print data from the UART port until the read timeout occurs"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : [timeout]"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UART.READ"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       UART.READ 5000"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Note : If timeout is not specified then the default UART read timeout is used"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("WRITE : send an item and wait for an answer "));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : item [| answer]"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       item: string, hexstream, filename"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       answer: string, hexstream, regex"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UART.WRITE gpt list --known"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       UART.WRITE gpt list --known | return value 0"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("SCRIPT : send commands from a file"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : script"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UART.SCRIPT script.txt"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("WAIT : wait for an item"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Args : item [timeout]"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Usage: UART.WAIT\"return value 0\""));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       UART.WAIT \"return value 0\"  2000"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Note : If timeout is not specified then the default UART read timeout is used"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("       The string has to be placed in quotes "));
        bRetVal = true;

    } while(false);

    return bRetVal;

}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief READ command implementation;
  *
  * \note Usage example: <br>
  *       UART.READ
  *       UART.READ 100
  *
  * \param[in] pstrArgs - optional timeout
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/


bool UARTPlugin::m_UART_READ ( const std::string &args) const
{
    bool bRetVal = false;

    do {

        if (true == args.empty())
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing: timeout"));
            break;
        }

        uint32_t uiReadTimeout = m_u32ReadTimeout;
        if (false == numeric::str2uint32(args ,uiReadTimeout))
        {
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled)
        {
            bRetVal = true;
            break;
        }

        /* open the UART port (RAII implementation, the close is done by destructor) */
        UART uartdrv(m_strUartPort, m_u32UartBaudrate);
        char *pstrReadBuffer = nullptr;

        if (true == uartdrv.is_open())
        {
            if (nullptr == (pstrReadBuffer = new (std::nothrow) char[m_u32UartReadBufferSize]))
            {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to allocate memory, bytes:"); LOG_UINT32(m_u32UartReadBufferSize));
                break;
            }

            bRetVal = (UART::Status::SUCCESS == uartdrv.timeout_readline(uiReadTimeout, pstrReadBuffer, m_u32UartReadBufferSize));

            delete [] pstrReadBuffer;
            pstrReadBuffer = nullptr;
        }

    } while(false);

    return bRetVal;

}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief WRITE command implementation;
  *
  * \note Usage example: <br>
  *       UART.WRITE item [| item]
  *
  * \param[in] pstrArgs NULL (NULL means that no arguments are provided to this function)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

#if 0
bool UARTPlugin::m_UART_WRITE ( const std::string &args) const
{
    bool bRetVal = false;

    do {

        if (true == args.empty())
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing item [|answer]"));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled)
        {
            bRetVal = true;
            break;
        }

        /* open the UART port (RAII implementation, the close is done by destructor) */
        UART uartdrv(m_strUartPort, m_u32UartBaudrate);

        if (true == uartdrv.is_open())
        {
            bRetVal = m_UART_CommandProcessor(args, 0, 0);
        }

    } while(false);

    return bRetVal;

}



/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief WAIT command implementation;
  *
  * \note Usage example: <br>
  *       UART.WAIT some_string
  *
  * \param[in] string to wait for
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool UARTPlugin::m_UART_WAIT (const std::string &args) const
{
    bool bRetVal = false;

    do {

        if (true == args.empty())
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing arg(s): message [timeout]"));
            break;
        }

        std::vector<std::string> vstrArgs;
        tokenizeSpace(args, vstrArgs);
        size_t szNrArgs = vstrArgs.size();
        uint32_t uiReadTimeout = m_u32ReadTimeout;

        // Expected: item [| delay]
        if ((szNrArgs < 1) || (szNrArgs > 2))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid arg(s), expected: message [timeout]"));
            break;
        }

        // timeout provided
        if (2 == szNrArgs)
        {
            if (false == numeric::str2uint32(vstrArgs[1] ,uiReadTimeout))
            {
                break;
            }
        }

        // get the type of item to wait for
        std::string strOutItem;
        std::vector<uint8_t> vDataItem;
        ItemType_e eTypeWaited = m_UART_GetItemType (vstrArgs[0], strOutItem, vDataItem);

        // invalid item provided, abort execution during the validation phase
        if (ItemType_e::ITEM_TYPE_LAST == eTypeWaited)
        {
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled)
        {
            bRetVal = true;
            break;
        }

        /* open the UART port (RAII implementation, the close is done by destructor) */
        UART uartdrv(m_strUartPort, m_u32UartBaudrate);

        if (true == uartdrv.is_open())
        {
            bRetVal = m_UART_WaitItem (strOutItem, vDataItem, eTypeWaited, uiReadTimeout);
        }

    } while(false);

    return bRetVal;

}

#endif

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
        if (true == args.empty())
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing arg(s): scriptpathname [|delay]"));
            break;
        }

        std::vector<std::string> vstrArgs;
        ustring::tokenizeSpace(args, vstrArgs);
        size_t szNrArgs = vstrArgs.size();

        if ((szNrArgs < 1) || (szNrArgs > 2))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected: scriptpathname [|delay] "));
            break;
        }

        size_t szDelay = 0;
        if (2 == szNrArgs)
        {
            if (false == numeric::str2size_t(vstrArgs[1], szDelay))
            {
                break;
            }
        }

        std::string strScriptPathName;
        ufile::buildFilePath(m_strArtefactsPath, vstrArgs[0], strScriptPathName);

        // Check file existence and size
        if (false == ufile::fileExistsAndNotEmpty(strScriptPathName))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Script not found or empty:"); LOG_STRING(strScriptPathName));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled)
        {
            bRetVal = true;
            break;
        }

        try {
            /* open the UART port (RAII implementation, the close is done by destructor) */
            auto shpDriver = std::make_shared<UART>(m_strUartPort, m_u32UartBaudrate);

            /* if driver opened successfully */
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
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Operation failed:"); LOG_STRING(e.what()));
        }
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
    return (UART::Status::SUCCESS == shpDriver->timeout_write(m_u32WriteTimeout, reinterpret_cast<const char *>(dataSpan.data()), dataSpan.size()));
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
            bRetVal = (UART::Status::SUCCESS == shpDriver->timeout_readline(m_u32ReadTimeout, reinterpret_cast<char*>(dataSpan.data()), dataSpan.size()));
            break;

        case ReadType::TOKEN:
            bRetVal = (UART::Status::SUCCESS == shpDriver->timeout_wait_for_token_buffer(m_u32ReadTimeout, reinterpret_cast<char*>(dataSpan.data()), dataSpan.size()));
            break;

        default:
            bRetVal = (UART::Status::SUCCESS == shpDriver->timeout_read(m_u32ReadTimeout, reinterpret_cast<char*>(dataSpan.data()), dataSpan.size(), &szBytesRead));
            break;
    }
    return bRetVal;
}
