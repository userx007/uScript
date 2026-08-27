#ifndef CH341_PLUGIN_HPP
#define CH341_PLUGIN_HPP

#include "uSharedConfig.hpp"
#include "uCommandExec.hpp"
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
#include <regex>

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
CH341_PLUGIN_CMD_RECORD( CYCLIC             ) \

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
                     , m_strInstanceName(CH341_PLUGIN_NAME)
                     , m_bIsInitialized(false)
                     , m_bIsEnabled(false)
                     , m_bIsFaultTolerant(false)
                     , m_bIsPrivileged(false)
                     , m_strResultData("")
                     , m_bRawResult(false)
                     , m_bCyclicCached(true)
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
          * \brief CONFIG-command setter for the raw-result flag (see m_bRawResult)
        */
        bool setRawResult (const std::string& strValue) const
        {
            return ucmdexec::parseRawResultFlag(strValue, m_bRawResult);
        }

        /**
          * \brief CONFIG-command setter for the CYCLIC caching mode (see m_bCyclicCached)
        */
        bool setCyclicCached (const std::string& strValue) const
        {
            return ucmdexec::parseCyclicCachedFlag(strValue, m_bCyclicCached);
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
          * \brief set CH341 port (CONFIG "p=" key).
          *
          * Validates the port syntax before storing it - on Linux/macOS it must match
          * "/dev/ttyCH341USBx" (also accepting the generic "ttyUSB"/"ttyACM" naming some
          * distros use); on Windows it must match "COMx", with the "\\.\" prefix applied
          * automatically for port numbers above 9 that need it.
          *
          * \note Only the CONFIG command routes through this validated setter - the ini
          *       file's CH341_PORT key is bound directly to m_strCh341Port (unvalidated),
          *       matching this plugin's existing ini-vs-CONFIG behavior.
          *
          * \param[in] strCh341Port  candidate port string, e.g. "/dev/ttyUSB0" or "COM3"
          * \return true if strCh341Port has valid CH341 port syntax, false otherwise
        */
        bool setCh341Port (const std::string& strCh341Port) const
        {
            if (true == strCh341Port.empty()) {
                LOG_PRINT(LOG_INFO, LOG_STRING("PLUGSPECOPS |"); LOG_STRING("Missing port"));
                return false;
            }

#ifdef _WIN32
            static const std::string strPrefix("\\\\.\\");
            const bool bHasPrefix = std::equal(strPrefix.begin(), strPrefix.end(), strCh341Port.begin());
            const std::string strPortToCheck = (false == bHasPrefix) ? strCh341Port : strCh341Port.substr(strPrefix.size());
#else
            const std::string& strPortToCheck = strCh341Port;
#endif
            if (false == m_IsValidCh341Port(strPortToCheck)) {
                LOG_PRINT(LOG_ERROR, LOG_STRING("PLUGSPECOPS |"); LOG_STRING("Invalid port syntax:"); LOG_STRING(strCh341Port));
                return false;
            }

#ifdef _WIN32
            // modify the format in order to support ports with number higher than 9
            m_strCh341Port = (false == bHasPrefix) ? strPrefix + strCh341Port : strCh341Port;
#else
            m_strCh341Port = strCh341Port;
#endif
            LOG_PRINT(LOG_INFO, LOG_STRING("PLUGSPECOPS |"); LOG_STRING("CH341 port changed to:"); LOG_STRING(m_strCh341Port));
            return true;
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
            return numeric::str2uint32(strCh341ReadBufferSize, m_u32ReadBufferSize);
        }

    private:

        /**
          * \brief Check if a string represents a CH341 tty/COM port (see setCh341Port()).
          * \param[in] strInput string to be evaluated
          * \return true if the string matches the expected syntax, false otherwise
          * \note On Linux the ch341 kernel driver registers tty nodes named
          *       "/dev/ttyCH341USBx" (it shares the generic "ttyUSB"/"ttyACM"
          *       naming scheme on some distros too, so both are accepted).
        */
        static bool m_IsValidCh341Port (const std::string& strInput)
        {
#ifndef _WIN32
            static const std::regex pattern("^/dev/(ttyCH341USB|ttyUSB|ttyACM)(?:1\\d{2}|2[0-4]\\d|[1-9]?\\d|25[0-5])$");
#else
            static const std::regex pattern("^COM(?:1\\d{2}|2[0-4]\\d|[1-9]?\\d|25[0-5])$");
#endif
            return std::regex_match(strInput, pattern);
        }

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
          * \brief runtime instance identity used for the GUI comm-dump panel
          *        (e.g. "CH341" or "CH341:1" -- see
          *        PluginDataSet::strInstanceName). Falls back to the fixed plugin
          *        name macro when unset (e.g. standalone construction outside the
          *        script interpreter).
        */
        std::string m_strInstanceName;
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
          * \brief data returned by plugin
        */
        mutable std::string m_strResultData;

        /**
          * \brief when true, CMD returns the raw received bytes as-is instead of
          *        hexlifying them (see ucmdexec::generic_cmd()'s bRawResult parameter);
          *        settable via the ini file's RAW_RESULT key or the CONFIG command's
          *        raw= token (see ucmdexec::RAW_RESULT_INI_KEY / RAW_RESULT_CONFIG_KEY)
        */
        mutable bool m_bRawResult;

        /**
          * \brief CYCLIC caching mode: true (default) validates/parses each CYCLIC entry's
          *        command exactly once for the whole session; false re-resolves and re-validates
          *        every due entry on every tick, needed to track a volatile ("?=") macro used as
          *        one entry's val/id - settable via the ini file's CYCLIC_CACHED key or the CONFIG
          *        command's cached= token (see ucmdexec::CYCLIC_CACHED_INI_KEY / CYCLIC_CACHED_CONFIG_KEY
          *        and ucmdexec::generic_send_cyclic()'s bCached parameter)
        */
        mutable bool m_bCyclicCached;

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
        mutable uint32_t m_u32ReadBufferSize;

        /**
          * \brief functions associated to the plugin commands
        */
        #define CH341_PLUGIN_CMD_RECORD(a, ...)  bool m_CH341_##a ( const std::string& args, std::stop_token st ) const;
        CH341_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  CH341_PLUGIN_CMD_RECORD
};

#endif /* CH341_PLUGIN_HPP */
