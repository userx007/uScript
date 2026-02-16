#ifndef UART_PLUGIN_HPP
#define UART_PLUGIN_HPP

#include "uSharedConfig.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
//#include "IScriptInterpreter.hpp"
#include "ICommDriver.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "uNumeric.hpp"
#include "uLogger.hpp"

#include <string>
#include <utility>
#include <span>

///////////////////////////////////////////////////////////////////
//                          PLUGIN VERSION                       //
///////////////////////////////////////////////////////////////////

#define UART_PLUGIN_VERSION "1.0.0.0"

///////////////////////////////////////////////////////////////////
//                          PLUGIN COMMANDS                      //
///////////////////////////////////////////////////////////////////

#define UART_PLUGIN_COMMANDS_CONFIG_TABLE    \
UART_PLUGIN_CMD_RECORD( INFO               ) \
UART_PLUGIN_CMD_RECORD( CONFIG             ) \
UART_PLUGIN_CMD_RECORD( CMD                ) \
UART_PLUGIN_CMD_RECORD( SCRIPT             ) \

///////////////////////////////////////////////////////////////////
//                          PLUGIN INTERFACE                     //
///////////////////////////////////////////////////////////////////

/**
  * \brief Uart plugin class definition
*/
class UARTPlugin: public PluginInterface
{
    public:

        /**
          * \brief class constructor
        */
        UARTPlugin() : m_strPluginVersion(UART_PLUGIN_VERSION)
                     , m_bIsInitialized(false)
                     , m_bIsEnabled(false)
                     , m_bIsFaultTolerant(false)
                     , m_bIsPrivileged(false)
                     , m_strResultData("")
        {
            #define UART_PLUGIN_CMD_RECORD(a) m_mapCmds.insert( std::make_pair( #a, &UARTPlugin::m_UART_##a ));
            UART_PLUGIN_COMMANDS_CONFIG_TABLE
            #undef  UART_PLUGIN_CMD_RECORD
        }

        /**
          * \brief class destructor
        */
        ~UARTPlugin()
        {

        }

        /**
          * \brief get the plugin initialization status
        */
        bool isInitialized( void ) const
        {
            return m_bIsInitialized;
        }

        /**
          * \brief get enabling status
        */
        bool isEnabled (void) const
        {
            return m_bIsEnabled;
        }

        /**
          * \brief Import external settings into the plugin
        */
        bool setParams( const PluginDataSet *psSetParams )
        {
            bool bRetVal = false;

            if (true == generic_setparams<UARTPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
                if (true == m_LocalSetParams(psSetParams)) {
                    bRetVal = true;
                }
            }

            return bRetVal;
        }

        /**
          * \brief function to retrieve information from plugin
        */
        void getParams( PluginDataGet *psGetParams ) const
        {
            generic_getparams<UARTPlugin>(this, psGetParams);
        }

        /**
          * \brief dispatch commands
        */
        bool doDispatch( const std::string& strCmd, const std::string& strParams ) const
        {
            return generic_dispatch<UARTPlugin>(this, strCmd, strParams);
        }

        /**
          * \brief get a pointer to the plugin map
        */
        const PluginCommandsMap<UARTPlugin> *getMap(void) const
        {
            return &m_mapCmds;
        }

        /**
          * \brief get the plugin version
        */
        const std::string& getVersion(void) const
        {
            return m_strPluginVersion;
        }

        /**
          * \brief get the result data
        */
        const std::string& getData(void) const
        {
            return m_strResultData;
        }

        /**
          * \brief clear the result data (avoid that some data to be returned by other command)
        */
        void resetData(void) const
        {
            m_strResultData.clear();
        }

        /**
          * \brief perform the initialization of modules used by the plugin
          * \note public because it needs to be called explicitely after loading the plugin
        */
        bool doInit(void *pvUserData);

        /**
          * \brief perform the enabling of the plugin
          * \note The un-enabled plugin can validate the command's arguments but doesn't allow the real execution
          *       This mode is used for the command validation
        */
        void doEnable(void)
        {
            m_bIsEnabled = true;
        }

        /**
          * \brief perform the de-initialization of modules used by the plugin
          * \note public because need to be called explicitely before closing/freeing the shared library
        */
        void doCleanup(void);

	    /**
	      * \brief get fault tolerant flag status
	    */
	    bool isFaultTolerant (void) const
	    {
	        return m_bIsFaultTolerant;
	    }

        /**
          * \brief get the privileged status
        */
        bool isPrivileged (void) const
        {
        	return m_bIsPrivileged;
        }

        /**
          * \brief get UART port
        */
        const char *getUartPort (void) const
        {
            return m_strUartPort.c_str();
        }

        /**
          * \brief set UART port
        */
        void setUartPort (const std::string& strUartPort) const
        {
            m_strUartPort.assign(strUartPort);
        }

        /**
          * \brief set UART baudrate
        */
        bool setUartBaudrate (const std::string& strUartBaudrate) const
        {
            return numeric::str2uint32(strUartBaudrate, m_u32UartBaudrate);
        }

        /**
          * \brief set UART read timeout
        */
        bool setUartReadTimeout (const std::string& strReadTimeout) const
        {
            return numeric::str2uint32(strReadTimeout, m_u32ReadTimeout);
        }

        /**
          * \brief set UART write timeout
        */
        bool setUartWriteTimeout (const std::string& strWriteTimeout) const
        {
            return numeric::str2uint32(strWriteTimeout, m_u32WriteTimeout);
        }

        /**
          * \brief set UART buffer size
        */
        bool setUartReadBufferSize (const std::string& strUartReadBufferSize) const
        {
            return numeric::str2uint32(strUartReadBufferSize, m_u32UartReadBufferSize);
        }

    private:

        /**
          * \brief message sender
        */
        bool m_Send (std::span<const uint8_t> data, std::shared_ptr<const ICommDriver> shpDriver) const;

        /**
          * \brief message receiver
        */
        bool m_Receive (std::span<uint8_t> data, size_t& szSize, CommCommandReadType readType, std::shared_ptr<const ICommDriver> shpDriver) const;

        /**
          * \brief processing of the plugin specific settings
        */
        bool m_LocalSetParams (const PluginDataSet *psSetParams);

        /**
          * \brief map with association between the command string and the execution function
        */
        PluginCommandsMap<UARTPlugin> m_mapCmds;

        /**
          * \brief plugin version
        */
        std::string m_strPluginVersion;

        /**
          * \brief data returned by plugin
        */
        mutable std::string m_strResultData;

        /**
          * \brief plugin initialization status
        */
        bool m_bIsInitialized;

        /**
          * \brief plugin enabling status
        */
        bool m_bIsEnabled;

        /**
          * \brief plugin fault tolerant mode
        */
        bool m_bIsFaultTolerant;

        /**
          * \brief plugin is priviledged
        */
        bool m_bIsPrivileged;

        /**
          * \brief the artefacts path got from command line
        */
        std::string m_strArtefactsPath;

        /**
          * \brief the UART port got from command line
        */
        mutable std::string m_strUartPort;

        /**
          * \brief the UART baudrate in used intialized from u32UartBaudrateHigh got from command line
        */
        mutable uint32_t m_u32UartBaudrate;

        /**
          * \brief the UART read timeout got from command line
        */
        mutable uint32_t m_u32ReadTimeout;

        /**
          * \brief the UART write timeout got from command line
        */
        mutable uint32_t m_u32WriteTimeout;

       /**
         * \brief size of the buffer where to read from UART (in order to empty the UART buffer)
        */
        mutable uint32_t m_u32UartReadBufferSize;

        /**
          * \brief functions associated to the plugin commands
        */
        #define UART_PLUGIN_CMD_RECORD(a)     bool m_UART_##a (const std::string &args) const;
        UART_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  UART_PLUGIN_CMD_RECORD
};

#endif /* UART_PLUGIN_HPP */