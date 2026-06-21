#ifndef CH341_PLUGIN_HPP
#define CH341_PLUGIN_HPP

#include "uSharedConfig.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
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

#define CH341_PLUGIN_VERSION    "1.0.0.0"
#define CH341_PLUGIN_NAME       "CH341"


///////////////////////////////////////////////////////////////////
//                          PLUGIN COMMANDS                      //
///////////////////////////////////////////////////////////////////

// CH341_GET_BLOCKING: picks the blocking flag when provided,
// defaults to false so non-blocking commands need no annotation.
#ifndef CH341_GET_BLOCKING
#define CH341_GET_BLOCKING(name, blocking, ...) blocking
#endif

#define CH341_PLUGIN_COMMANDS_CONFIG_TABLE    \
CH341_PLUGIN_CMD_RECORD( INFO               ) \
CH341_PLUGIN_CMD_RECORD( CONFIG             ) \
CH341_PLUGIN_CMD_RECORD( CMD                ) \
CH341_PLUGIN_CMD_RECORD( SCRIPT             ) \

///////////////////////////////////////////////////////////////////
//                          PLUGIN INTERFACE                     //
///////////////////////////////////////////////////////////////////

/**
  * \brief CH341 plugin class definition
*/
class CH341Plugin: public PluginInterface
{
    public:

        /**
          * \brief class constructor
        */
        CH341Plugin() : m_strVersion(CH341_PLUGIN_VERSION)
                     , m_bIsInitialized(false)
                     , m_bIsEnabled(false)
                     , m_bIsFaultTolerant(false)
                     , m_bIsPrivileged(false)
                     , m_strResultData("")
        {
            #define CH341_PLUGIN_CMD_RECORD(a, ...) m_mapCmds.insert( std::make_pair( #a, \
            PluginCommandEntry<CH341Plugin>{&CH341Plugin::m_CH341_##a, CH341_GET_BLOCKING(a, ##__VA_ARGS__, false)} ));
            CH341_PLUGIN_COMMANDS_CONFIG_TABLE
            #undef  CH341_PLUGIN_CMD_RECORD
        }

        /**
          * \brief class destructor
        */
        ~CH341Plugin()
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

            if (true == generic_setparams<CH341Plugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
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
            generic_getparams<CH341Plugin>(this, psGetParams);
        }

        /**
          * \brief dispatch commands
        */
        bool doDispatch( const std::string& strCmd, const std::string& strParams, std::stop_token st = {} ) const
        {
            return generic_dispatch<CH341Plugin>(this, strCmd, strParams, st);
        }

        /**
          * \brief get a pointer to the plugin map
        */
        const PluginCommandsMap<CH341Plugin> *getMap(void) const
        {
            return &m_mapCmds;
        }

        /**
          * \brief get the plugin version
        */
        const std::string& getVersion(void) const
        {
            return m_strVersion;
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
        bool doEnable(void)
        {
            m_bIsEnabled = true;
            return true;
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
          * \brief get CH341 port
        */
        const char *getCh341Port (void) const
        {
            return m_strCh341Port.c_str();
        }

        /**
          * \brief set CH341 port
        */
        void setCh341Port (const std::string& strCh341Port) const
        {
            m_strCh341Port.assign(strCh341Port);
        }

        /**
          * \brief set CH341 baudrate
        */
        bool setCh341Baudrate (const std::string& strCh341Baudrate) const
        {
            return numeric::str2uint32(strCh341Baudrate, m_u32Ch341Baudrate);
        }

        /**
          * \brief set CH341 read timeout
        */
        bool setCh341ReadTimeout (const std::string& strReadTimeout) const
        {
            return numeric::str2uint32(strReadTimeout, m_u32ReadTimeout);
        }

        /**
          * \brief set CH341 write timeout
        */
        bool setCh341WriteTimeout (const std::string& strWriteTimeout) const
        {
            return numeric::str2uint32(strWriteTimeout, m_u32WriteTimeout);
        }

        /**
          * \brief set CH341 buffer size
        */
        bool setCh341ReadBufferSize (const std::string& strCh341ReadBufferSize) const
        {
            return numeric::str2uint32(strCh341ReadBufferSize, m_u32Ch341ReadBufferSize);
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
        PluginCommandsMap<CH341Plugin> m_mapCmds;

        /**
          * \brief plugin version
        */
        std::string m_strVersion;

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
          * \brief the CH341 port got from command line
        */
        mutable std::string m_strCh341Port;

        /**
          * \brief the CH341 baudrate in used intialized from command line
        */
        mutable uint32_t m_u32Ch341Baudrate;

        /**
          * \brief the CH341 read timeout got from command line
        */
        mutable uint32_t m_u32ReadTimeout;

        /**
          * \brief the CH341 write timeout got from command line
        */
        mutable uint32_t m_u32WriteTimeout;

       /**
         * \brief size of the buffer where to read from CH341 (in order to empty the CH341 buffer)
        */
        mutable uint32_t m_u32Ch341ReadBufferSize;

        /**
          * \brief functions associated to the plugin commands
        */
        #define CH341_PLUGIN_CMD_RECORD(a, ...)  bool m_CH341_##a ( const std::string& args, std::stop_token st ) const;
        CH341_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  CH341_PLUGIN_CMD_RECORD
};

#endif /* CH341_PLUGIN_HPP */
