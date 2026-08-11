#ifndef LAN8720NET_PLUGIN_HPP
#define LAN8720NET_PLUGIN_HPP

#include "uSharedConfig.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "ICommDriver.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "uNumeric.hpp"
#include "uLogger.hpp"

#include "uLan8720Net.hpp"

#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include <string>
#include <utility>
#include <span>
#include <cstdint>

#define LAN8720NET_PLUGIN_VERSION    "1.0.0.0"
#define LAN8720NET_PLUGIN_NAME       "LAN8720NET"

#ifndef LAN8720NET_GET_BLOCKING
#define LAN8720NET_GET_BLOCKING(name, blocking, ...) blocking
#endif

#define LAN8720NET_PLUGIN_COMMANDS_CONFIG_TABLE    \
LAN8720NET_PLUGIN_CMD_RECORD( INFO               ) \
LAN8720NET_PLUGIN_CMD_RECORD( CONFIG             ) \
LAN8720NET_PLUGIN_CMD_RECORD( CMD                ) \
LAN8720NET_PLUGIN_CMD_RECORD( SCRIPT             ) \
LAN8720NET_PLUGIN_CMD_RECORD( CYCLIC             ) \

class Lan8720NetPlugin: public PluginInterface
{
    public:
        Lan8720NetPlugin() : m_strVersion(LAN8720NET_PLUGIN_VERSION)
                    , m_strInstanceName(LAN8720NET_PLUGIN_NAME)
                    , m_bIsInitialized(false)
                    , m_bIsEnabled(false)
                    , m_bIsFaultTolerant(false)
                    , m_bIsPrivileged(false)
                    , m_strResultData()
                    , m_u16ServerPort(0U)
                    , m_u32ReadTimeout(Lan8720Net::LAN8720NET_TIMEOUT_MS)
                    , m_u32WriteTimeout(Lan8720Net::LAN8720NET_TIMEOUT_MS)
                    , m_u32ReadBufferSize(1460U)
        {
            #define LAN8720NET_PLUGIN_CMD_RECORD(a, ...) m_mapCmds.insert( std::make_pair( #a, \
            PluginCommandEntry<Lan8720NetPlugin>{&Lan8720NetPlugin::m_LAN8720NET_##a, LAN8720NET_GET_BLOCKING(a, ##__VA_ARGS__, false)} ));
            LAN8720NET_PLUGIN_COMMANDS_CONFIG_TABLE
            #undef LAN8720NET_PLUGIN_CMD_RECORD
        }

        ~Lan8720NetPlugin() = default;

        bool isInitialized( void ) const { return m_bIsInitialized; }
        bool isEnabled (void) const { return m_bIsEnabled; }

        bool setParams( const PluginDataSet *psSetParams )
        {
            bool bRetVal = false;
            if (true == generic_setparams<Lan8720NetPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
                if (true == m_LocalSetParams(psSetParams)) {
                    bRetVal = true;
                }
            }
            return bRetVal;
        }

        void getParams( PluginDataGet *psGetParams ) const
        {
            generic_getparams<Lan8720NetPlugin>(this, psGetParams);
        }

        bool doDispatch( const std::string& strCmd, const std::string& strParams, std::stop_token st = {} ) const
        {
            return generic_dispatch<Lan8720NetPlugin>(this, strCmd, strParams, st);
        }

        const PluginCommandsMap<Lan8720NetPlugin> *getMap(void) const { return &m_mapCmds; }
        const std::string& getVersion(void) const { return m_strVersion; }
        const std::string& getData(void) const { return m_strResultData; }
        void resetData(void) const { m_strResultData.clear(); }
        bool isFaultTolerant (void) const { return m_bIsFaultTolerant; }
        bool isPrivileged (void) const { return m_bIsPrivileged; }

        bool doInit(void *pvUserData);
        bool doEnable(void) { m_bIsEnabled = true; return true; }
        void doCleanup(void);

        const char *getServerIp (void) const { return m_strServerIp.c_str(); }
        void setServerIp (const std::string& strServerIp) const { m_strServerIp.assign(strServerIp); }

        uint16_t getServerPort (void) const { return m_u16ServerPort; }
        bool setServerPort (const std::string& strServerPort) const;

        bool setReadTimeout (const std::string& strReadTimeout) const;
        bool setWriteTimeout (const std::string& strWriteTimeout) const;
        bool setReadBufferSize (const std::string& strReadBufferSize) const;

    private:
        bool m_LocalSetParams (const PluginDataSet *psSetParams);
        std::shared_ptr<Lan8720Net> m_OpenDriver (void) const;

        PluginCommandsMap<Lan8720NetPlugin> m_mapCmds;
        std::string m_strVersion;

        /**
          * \brief runtime instance identity used for the GUI comm-dump panel
          *        (e.g. "LAN8720NET" or "LAN8720NET:1" -- see
          *        PluginDataSet::strInstanceName). Falls back to the fixed plugin
          *        name macro when unset (e.g. standalone construction outside the
          *        script interpreter).
        */
        std::string m_strInstanceName;
        mutable std::string m_strResultData;
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

        #define LAN8720NET_PLUGIN_CMD_RECORD(a, ...)  bool m_LAN8720NET_##a ( const std::string& args, std::stop_token st ) const;
        LAN8720NET_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef LAN8720NET_PLUGIN_CMD_RECORD
};

#endif /* LAN8720NET_PLUGIN_HPP */
