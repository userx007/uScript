#ifndef W5500NET_PLUGIN_HPP
#define W5500NET_PLUGIN_HPP

#include "uSharedConfig.hpp"
#include "uCommandExec.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "ICommDriver.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "uNumeric.hpp"
#include "uLogger.hpp"

#include "uW5500Net.hpp"

#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include <string>
#include <utility>
#include <span>
#include <cstdint>

///////////////////////////////////////////////////////////////////
//                          PLUGIN VERSION                       //
///////////////////////////////////////////////////////////////////

#define W5500NET_PLUGIN_VERSION    "1.0.0.0"
#define W5500NET_PLUGIN_NAME       "W5500NET"

///////////////////////////////////////////////////////////////////
//                          PLUGIN COMMANDS                      //
///////////////////////////////////////////////////////////////////

#ifndef W5500NET_GET_BLOCKING
#define W5500NET_GET_BLOCKING(name, blocking, ...) blocking
#endif

#define W5500NET_PLUGIN_COMMANDS_CONFIG_TABLE    \
W5500NET_PLUGIN_CMD_RECORD( INFO               ) \
W5500NET_PLUGIN_CMD_RECORD( CONFIG             ) \
W5500NET_PLUGIN_CMD_RECORD( CMD                ) \
W5500NET_PLUGIN_CMD_RECORD( SCRIPT             ) \
W5500NET_PLUGIN_CMD_RECORD( CYCLIC             ) \

///////////////////////////////////////////////////////////////////
//                          PLUGIN INTERFACE                     //
///////////////////////////////////////////////////////////////////

/**
  * \brief W5500Net plugin class definition.
  *
  * Wraps the W5500Net TCP-client driver and exposes it through the standard
  * PluginInterface dispatch model. Connects to a W5500-based device over Ethernet.
*/
class W5500NetPlugin: public PluginInterface
{
    public:

        W5500NetPlugin() : m_strVersion(W5500NET_PLUGIN_VERSION)
                    , m_strInstanceName(W5500NET_PLUGIN_NAME)
                    , m_bIsInitialized(false)
                    , m_bIsEnabled(false)
                    , m_bIsFaultTolerant(false)
                    , m_bIsPrivileged(false)
                    , m_strResultData()
                    , m_bRawResult(false)
                    , m_u16ServerPort(0U)
                    , m_u32ReadTimeout(W5500Net::W5500NET_TIMEOUT_MS)
                    , m_u32WriteTimeout(W5500Net::W5500NET_TIMEOUT_MS)
                    , m_u32ReadBufferSize(1024U)
        {
            #define W5500NET_PLUGIN_CMD_RECORD(a, ...) m_mapCmds.insert( std::make_pair( #a, \
            PluginCommandEntry<W5500NetPlugin>{&W5500NetPlugin::m_W5500NET_##a, W5500NET_GET_BLOCKING(a, ##__VA_ARGS__, false)} ));
            W5500NET_PLUGIN_COMMANDS_CONFIG_TABLE
            #undef W5500NET_PLUGIN_CMD_RECORD // <--- Added this line
        }

        ~W5500NetPlugin() = default;

        bool isInitialized( void ) const { return m_bIsInitialized; }
        bool isEnabled (void) const { return m_bIsEnabled; }

        bool setParams( const PluginDataSet *psSetParams )
        {
            bool bRetVal = false;
            if (true == generic_setparams<W5500NetPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
                if (true == m_LocalSetParams(psSetParams)) {
                    bRetVal = true;
                }
            }
            return bRetVal;
        }

        void getParams( PluginDataGet *psGetParams ) const
        {
            generic_getparams<W5500NetPlugin>(this, psGetParams);
        }

        bool doDispatch( const std::string& strCmd, const std::string& strParams, std::stop_token st = {} ) const
        {
            return generic_dispatch<W5500NetPlugin>(this, strCmd, strParams, st);
        }

        const PluginCommandsMap<W5500NetPlugin> *getMap(void) const { return &m_mapCmds; }
        const std::string& getVersion(void) const { return m_strVersion; }
        const std::string& getData(void) const { return m_strResultData; }
        void resetData(void) const
 { m_strResultData.clear(); }
        
        /**
          * \brief CONFIG-command setter for the raw-result flag (see m_bRawResult)
        */
        bool setRawResult (const std::string& strValue) const
        {
            return ucmdexec::parseRawResultFlag(strValue, m_bRawResult);
        }
        bool isFaultTolerant (void) const { return m_bIsFaultTolerant; }
        bool isPrivileged (void) const { return m_bIsPrivileged; }

        bool doInit(void *pvUserData);
        bool doEnable(void) { m_bIsEnabled = true; return true; }
        void doCleanup(void);

        // Setters/Getters for configuration
        const char *getServerIp (void) const { return m_strServerIp.c_str(); }
        void setServerIp (const std::string& strServerIp) const { m_strServerIp.assign(strServerIp); }

        uint16_t getServerPort (void) const { return m_u16ServerPort; }
        bool setServerPort (const std::string& strServerPort) const;

        bool setReadTimeout (const std::string& strReadTimeout) const;
        bool setWriteTimeout (const std::string& strWriteTimeout) const;
        bool setReadBufferSize (const std::string& strReadBufferSize) const;

    private:

        bool m_LocalSetParams (const PluginDataSet *psSetParams);
        std::shared_ptr<W5500Net> m_OpenDriver (void) const;

        PluginCommandsMap<W5500NetPlugin> m_mapCmds;
        std::string m_strVersion;

        /**
          * \brief runtime instance identity used for the GUI comm-dump panel
          *        (e.g. "W5500NET" or "W5500NET:1" -- see
          *        PluginDataSet::strInstanceName). Falls back to the fixed plugin
          *        name macro when unset (e.g. standalone construction outside the
          *        script interpreter).
        */
        std::string m_strInstanceName;
        mutable std::string m_strResultData;

        /**
          * \brief when true, CMD returns the raw received bytes as-is instead of
          *        hexlifying them (see ucmdexec::generic_cmd()'s bRawResult parameter);
          *        settable via the ini file's RAW_RESULT key or the CONFIG command's
          *        raw= token (see ucmdexec::RAW_RESULT_INI_KEY / RAW_RESULT_CONFIG_KEY)
        */
        mutable bool m_bRawResult;
        bool m_bIsInitialized;
        bool m_bIsEnabled;
        bool m_bIsFaultTolerant;
        bool m_bIsPrivileged;
        std::string m_strArtefactsPath;

        mutable std::string m_strServerIp;
        mutable uint16_t m_u16ServerPort;
        mutable uint32_t m_u32ReadTimeout;
        mutable uint32_t m_u32WriteTimeout;
        mutable uint32_t m_u32ReadBufferSize;

        #define W5500NET_PLUGIN_CMD_RECORD(a, ...)  bool m_W5500NET_##a ( const std::string& args, std::stop_token st ) const;
        W5500NET_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef W5500NET_PLUGIN_CMD_RECORD

};

#endif /* W5500NET_PLUGIN_HPP */
