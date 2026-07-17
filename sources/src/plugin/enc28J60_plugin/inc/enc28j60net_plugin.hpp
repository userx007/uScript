#ifndef ENC28J60NET_PLUGIN_HPP
#define ENC28J60NET_PLUGIN_HPP

#include "uSharedConfig.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "ICommDriver.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "uNumeric.hpp"
#include "uLogger.hpp"

#include "uEnc28J60Net.hpp"

#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include <string>
#include <utility>
#include <span>
#include <cstdint>

#define ENC28J60NET_PLUGIN_VERSION    "1.0.0.0"
#define ENC28J60NET_PLUGIN_NAME       "ENC28J60NET"

#ifndef ENC28J60NET_GET_BLOCKING
#define ENC28J60NET_GET_BLOCKING(name, blocking, ...) blocking
#endif

#define ENC28J60NET_PLUGIN_COMMANDS_CONFIG_TABLE    \
ENC28J60NET_PLUGIN_CMD_RECORD( INFO               ) \
ENC28J60NET_PLUGIN_CMD_RECORD( CONFIG             ) \
ENC28J60NET_PLUGIN_CMD_RECORD( CMD                ) \
ENC28J60NET_PLUGIN_CMD_RECORD( SCRIPT             ) \

class Enc28J60NetPlugin: public PluginInterface
{
    public:
        Enc28J60NetPlugin() : m_strVersion(ENC28J60NET_PLUGIN_VERSION)
                    , m_bIsInitialized(false)
                    , m_bIsEnabled(false)
                    , m_bIsFaultTolerant(false)
                    , m_bIsPrivileged(false)
                    , m_strResultData()
                    , m_u16ServerPort(0U)
                    , m_u32ReadTimeout(Enc28J60Net::ENC28J60NET_TIMEOUT_MS)
                    , m_u32WriteTimeout(Enc28J60Net::ENC28J60NET_TIMEOUT_MS)
                    , m_u32ReadBufferSize(1460U)
        {
            #define ENC28J60NET_PLUGIN_CMD_RECORD(a, ...) m_mapCmds.insert( std::make_pair( #a, \
            PluginCommandEntry<Enc28J60NetPlugin>{&Enc28J60NetPlugin::m_ENC28J60NET_##a, ENC28J60NET_GET_BLOCKING(a, ##__VA_ARGS__, false)} ));
            ENC28J60NET_PLUGIN_COMMANDS_CONFIG_TABLE
            #undef ENC28J60NET_PLUGIN_CMD_RECORD
        }

        ~Enc28J60NetPlugin() = default;

        bool isInitialized( void ) const { return m_bIsInitialized; }
        bool isEnabled (void) const { return m_bIsEnabled; }

        bool setParams( const PluginDataSet *psSetParams )
        {
            bool bRetVal = false;
            if (true == generic_setparams<Enc28J60NetPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
                if (true == m_LocalSetParams(psSetParams)) {
                    bRetVal = true;
                }
            }
            return bRetVal;
        }

        void getParams( PluginDataGet *psGetParams ) const
        {
            generic_getparams<Enc28J60NetPlugin>(this, psGetParams);
        }

        bool doDispatch( const std::string& strCmd, const std::string& strParams, std::stop_token st = {} ) const
        {
            return generic_dispatch<Enc28J60NetPlugin>(this, strCmd, strParams, st);
        }

        const PluginCommandsMap<Enc28J60NetPlugin> *getMap(void) const { return &m_mapCmds; }
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
        std::shared_ptr<Enc28J60Net> m_OpenDriver (void) const;

        PluginCommandsMap<Enc28J60NetPlugin> m_mapCmds;
        std::string m_strVersion;
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

        #define ENC28J60NET_PLUGIN_CMD_RECORD(a, ...)  bool m_ENC28J60NET_##a ( const std::string& args, std::stop_token st ) const;
        ENC28J60NET_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef ENC28J60NET_PLUGIN_CMD_RECORD
};

#endif /* ENC28J60NET_PLUGIN_HPP */
