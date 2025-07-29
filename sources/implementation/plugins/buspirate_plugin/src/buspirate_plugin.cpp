
#include "buspirate_plugin.hpp"
#include "buspirate_generic.hpp"

#include "uUart.hpp"
#include "uNumeric.hpp"

///////////////////////////////////////////////////////////////////
//                 DLT DEFINES                                   //
///////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR     "BUSPIRATE  :"
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
    EXPORTED BuspiratePlugin* pluginEntry()
    {
        return new BuspiratePlugin();
    }

    EXPORTED void pluginExit( BuspiratePlugin *ptrPlugin )
    {
        if( nullptr != ptrPlugin )
        {
            delete ptrPlugin;
        }
    }
}

///////////////////////////////////////////////////////////////////
//                          INIT / CLEANUP                       //
///////////////////////////////////////////////////////////////////


/**
  * \brief Function where to execute initialization of sub-modules
*/

bool BuspiratePlugin::doInit(void *pvUserData)
{
    drvUart.open (m_strUartPort, m_u32UartBaudrate);

    return m_bIsInitialized = drvUart.is_open();
}


/**
  * \brief Function where to execute de-initialization of sub-modules
*/

void BuspiratePlugin::doCleanup(void)
{
    if (true == m_bIsInitialized)
    {
        drvUart.close();
    }

    m_bIsInitialized = false;
    m_bIsEnabled     = false;

}

///////////////////////////////////////////////////////////////////
//                          COMMAND HANDLERS                     //
///////////////////////////////////////////////////////////////////


/**
  * \brief INFO command implementation; shows details about plugin and
  *        describe the supported functions with examples of usage.
  *        This command takes no arguments and is executed even if the plugin initialization fails
  *
  * \note Usage example: <br>
  *       BUSPIRATE.INFO
  *
  * \param[in] args NULL (NULL means that no arguments are provided to this function)
  *
  * \return true on success, false otherwise
*/

bool BuspiratePlugin::m_Buspirate_INFO (const std::string &args) const
{
    bool bRetVal = false;

    do {

        // expected no arguments
        if (false == args.empty())
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected no argument(s)"));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if( false == m_bIsEnabled )
        {
            bRetVal = true;
            break;
        }

        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Version:"); LOG_STRING(m_strPluginVersion));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Description: Control a buspirate device"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("Note: Not implemented"));
        LOG_PRINT(LOG_FIXED, LOG_HDR; LOG_STRING("INFO : Shows the plugin's help"));

        bRetVal = true;

    } while(false);

    return bRetVal;

}

/**
 * \brief MODE command implementation
 *
 * \note Supported modes: bin reset spi i2c uart 1wire rawire jtag exit
 * \note Example BUSPIRATE.MODE bin
 *               BUSPIRATE.MODE spi
 *               BUSPIRATE.MODE exit
 *
 */
bool BuspiratePlugin::m_Buspirate_MODE (const std::string &args) const
{
    bool bRetVal = false;

    do {

        if (true == args.empty())
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Argument expected: mode. Abort!"));
            break;
        }

        // if plugin is not enabled then stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled)
        {
            bRetVal = true;
            break;
        }

        bRetVal = m_handle_mode(args);

    } while(false);

    return bRetVal;

}

bool BuspiratePlugin::m_LocalSetParams( const PluginDataSet *psSetParams)
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



