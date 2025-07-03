#ifndef UART_PLUGIN_HPP
#define UART_PLUGIN_HPP

#include "CommonSettings.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "uLogger.hpp"

#include <string>

///////////////////////////////////////////////////////////////////
//                          PLUGIN VERSION                       //
///////////////////////////////////////////////////////////////////

#define UART_PLUGIN_VERSION "1.0.0.0"

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
//                          PLUGIN COMMANDS                      //
///////////////////////////////////////////////////////////////////

#define UART_PLUGIN_COMMANDS_CONFIG_TABLE    \
UART_PLUGIN_CMD_RECORD( INFO               ) \
UART_PLUGIN_CMD_RECORD( SET_UART_PORT      ) \
UART_PLUGIN_CMD_RECORD( READ               ) \
UART_PLUGIN_CMD_RECORD( WRITE              ) \
UART_PLUGIN_CMD_RECORD( WAIT               ) \
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

#ifndef DOXYGEN_SHOULD_SKIP_THIS

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
        bool isEnabled ( void ) const
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
        void getParams( getparams_s *psGetParams ) const
        {
            generic_getparams<UARTPlugin>( this, psGetParams );
        }

        /**
          * \brief dispatch commands
        */
        bool doDispatch( const std::string& strParams ) const
        {
            return generic_dispatch<UARTPlugin>( this, strParams );
        }

        /**
          * \brief get a pointer to the plugin map
        */
        const PluginCommandsMap<UARTPlugin> *getMap( void ) const
        {
            return &m_mapCmds;
        }

        /**
          * \brief get the plugin version
        */
        const std::string& getVersion( void ) const
        {
            return m_strPluginVersion;
        }

        /**
          * \brief get the result data
        */
        const std::string& getData( void ) const
        {
            return m_strResultData;
        }

        /**
          * \brief clear the result data (avoid that some data to be returned by other command)
        */
        void resetData( void ) const
        {
            m_strResultData.clear();
        }

        /**
          * \brief perform the initialization of modules used by the plugin
          * \note public because it needs to be called explicitely after loading the plugin
        */
        bool doInit( void *pvUserData );

        /**
          * \brief perform the enabling of the plugin
          * \note The un-enabled plugin can validate the command's arguments but doesn't allow the real execution
          *       This mode is used for the command validation
        */
        void doEnable( void )
        {
            m_bIsEnabled = true;
        }

        /**
          * \brief perform the de-initialization of modules used by the plugin
          * \note public because need to be called explicitely before closing/freeing the shared library
        */
        void doCleanup( void );

        /**
          * \brief set fault tolerant flag status
        */
        void setFaultTolerant( void )
        {
            m_bIsFaultTolerant = true;
        }

        /**
          * \brief get fault tolerant flag status
        */
        bool isFaultTolerant ( void ) const
        {
            return m_bIsFaultTolerant;
        }

        /**
          * \brief get the privileged status
        */
        bool isPrivileged ( void ) const
        {
            return false;
        }

        /**
          * \brief get UART port
        */
        const char *getUartPort( void ) const
        {
            return m_strUartPort.c_str();
        }

        /**
          * \brief set UART port
        */
        void setUartPort( const char* pstrUartPort ) const
        {
            m_strUartPort.assign(pstrUartPort);
        }

    private:

        enum class ItemType_e : int
        {
            ITEM_TYPE_HEXSTREAM,
            ITEM_TYPE_REGEX,
            ITEM_TYPE_STRING,
            ITEM_TYPE_FILENAME,
            ITEM_TYPE_EMPTY_STRING,
            ITEM_TYPE_LAST
        };

        /**
          * \brief processing of the plugin specific settings
        */
        bool m_LocalSetParams( const PluginDataSet *psSetParams );

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
        uint32_t m_u32UartBaudrate;

        /**
          * \brief the UART read timeout got from command line
        */
        uint32_t m_u32ReadTimeout;

        /**
          * \brief the UART write timeout got from command line
        */
        uint32_t m_u32WriteTimeout;

       /**
         * \brief size of the buffer where to read from UART (in order to empty the UART buffer)
        */
        uint32_t m_u32UartReadBufferSize;

       /**
         * \brief timeout used to read the UART buffer content (in order to empty the UART buffer)
        */
        uint32_t m_u32UartReadBufferTout;

        /**
         * \brief instance of a script parser
        */
        mutable ScriptParser *m_pScriptParser;

        /**
          * \brief the UART drive handle
        */
        mutable int32_t  m_i32UartHandle;


#endif // DOXYGEN_SHOULD_SKIP_THIS

        /**
          * \brief script line processing
        */
        bool m_UART_CommandProcessor ( const std::string& strLine, const uint32_t uiLineIdx, const uint8_t u8FieldWidth ) const;

        /**
          * \brief validate an expression and return it undecorated
        */
        ItemType_e m_UART_GetItemType ( const std::string& strItem, std::string& strOutItem, std::vector<uint8_t>& vData) const;

        /**
          * \brief Write a message
        */
        bool m_UART_WriteItem( const std::string& strItem, std::vector<uint8_t>& vDataItem, UARTPlugin::ItemType_e eTypeSend ) const;

        /**
          * \brief Wait for an item
        */
        bool m_UART_WaitItem( const std::string& strItem, std::vector<uint8_t>& vDataItem, UARTPlugin::ItemType_e eTypeWaited, const uint32_t uiReadTimeout ) const;

        /**
          * \brief Validate a file
        */
        bool m_UART_ValidateFile( const std::string& strItem, char **ppstrFilePathName, int64_t *pi64FileSize, uint32_t *puiChunkSize ) const;

        /**
          * \brief Upload a file
        */
        bool m_UART_WriteFile( const std::string& strItem ) const;

        /**
          * \brief functions associated to the plugin commands
        */
        #define UART_PLUGIN_CMD_RECORD(a)     bool m_UART_##a ( const char *pstrArgs ) const;
        UART_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  UART_PLUGIN_CMD_RECORD
};

#endif /* UART_PLUGIN_HPP */