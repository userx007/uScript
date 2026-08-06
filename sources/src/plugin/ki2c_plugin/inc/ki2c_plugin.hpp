#ifndef KI2C_PLUGIN_HPP
#define KI2C_PLUGIN_HPP

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

#define KI2C_PLUGIN_VERSION    "1.0.0.0"
#define KI2C_PLUGIN_NAME       "KI2C"


///////////////////////////////////////////////////////////////////
//                          PLUGIN COMMANDS                      //
///////////////////////////////////////////////////////////////////

// KI2C_GET_BLOCKING: picks the blocking flag when provided,
// defaults to false so non-blocking commands need no annotation.
#ifndef KI2C_GET_BLOCKING
#define KI2C_GET_BLOCKING(name, blocking, ...) blocking
#endif

#define KI2C_PLUGIN_COMMANDS_CONFIG_TABLE    \
KI2C_PLUGIN_CMD_RECORD( INFO               ) \
KI2C_PLUGIN_CMD_RECORD( CONFIG             ) \
KI2C_PLUGIN_CMD_RECORD( CMD                ) \
KI2C_PLUGIN_CMD_RECORD( SCRIPT             ) \
KI2C_PLUGIN_CMD_RECORD( CYCLIC             ) \


///////////////////////////////////////////////////////////////////
//                          PLUGIN INTERFACE                     //
///////////////////////////////////////////////////////////////////

/**
  * \brief KI2C plugin class definition
*/
class KI2CPlugin: public PluginInterface
{
    public:

        /**
          * \brief class constructor
        */
        KI2CPlugin() : m_strVersion(KI2C_PLUGIN_VERSION)
                    , m_bIsInitialized(false)
                    , m_bIsEnabled(false)
                    , m_bIsFaultTolerant(false)
                    , m_bIsPrivileged(false)
                    , m_strResultData("")
        {
            #define KI2C_PLUGIN_CMD_RECORD(a, ...) m_mapCmds.insert( std::make_pair( #a, \
            PluginCommandEntry<KI2CPlugin>{&KI2CPlugin::m_KI2C_##a, KI2C_GET_BLOCKING(a, ##__VA_ARGS__, false)} ));
            KI2C_PLUGIN_COMMANDS_CONFIG_TABLE
            #undef  KI2C_PLUGIN_CMD_RECORD
        }

        /**
          * \brief class destructor
        */
        ~KI2CPlugin()
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

            if (true == generic_setparams<KI2CPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
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
            generic_getparams<KI2CPlugin>(this, psGetParams);
        }

        /**
          * \brief dispatch commands
        */
        bool doDispatch( const std::string& strCmd, const std::string& strParams, std::stop_token st = {} ) const
        {
            return generic_dispatch<KI2CPlugin>(this, strCmd, strParams, st);
        }

        /**
          * \brief get a pointer to the plugin map
        */
        const PluginCommandsMap<KI2CPlugin> *getMap(void) const
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
          * \note public because it needs to be called explicitly after loading the plugin
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
          * \note public because need to be called explicitly before closing/freeing the shared library
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
          * \brief get KI2C device path
        */
        const char *getKI2CDevice (void) const
        {
            return m_strKI2CDevice.c_str();
        }

        /**
          * \brief set KI2C device path
        */
        void setI2CDevice (const std::string& strKI2CDevice) const
        {
            m_strKI2CDevice.assign(strKI2CDevice);
        }

        /**
          * \brief set KI2C slave address (accepts decimal or 0x-prefixed hex strings)
        */
        bool setI2CAddress (const std::string& strAddress) const
        {
            return numeric::str2uint8(strAddress, m_u8KI2CAddress);
        }

        /**
          * \brief set KI2C read timeout
        */
        bool setI2CReadTimeout (const std::string& strReadTimeout) const
        {
            return numeric::str2uint32(strReadTimeout, m_u32ReadTimeout);
        }

        /**
          * \brief set KI2C write timeout
        */
        bool setI2CWriteTimeout (const std::string& strWriteTimeout) const
        {
            return numeric::str2uint32(strWriteTimeout, m_u32WriteTimeout);
        }

        /**
          * \brief set KI2C read buffer size
        */
        bool setI2CReadBufferSize (const std::string& strReadBufferSize) const
        {
            return numeric::str2uint32(strReadBufferSize, m_u32ReadBufferSize);
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
        PluginCommandsMap<KI2CPlugin> m_mapCmds;

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
          * \brief plugin is privileged
        */
        bool m_bIsPrivileged;

        /**
          * \brief the artefacts path got from configuration
        */
        std::string m_strArtefactsPath;

        /**
          * \brief the KI2C device node (e.g. /dev/i2c-1)
        */
        mutable std::string m_strKI2CDevice;

        /**
          * \brief the 7-bit KI2C slave address (e.g. 0x48)
        */
        mutable uint8_t m_u8KI2CAddress;

        /**
          * \brief KI2C read timeout in milliseconds
        */
        mutable uint32_t m_u32ReadTimeout;

        /**
          * \brief KI2C write timeout in milliseconds
        */
        mutable uint32_t m_u32WriteTimeout;

        /**
          * \brief size of the buffer used for KI2C read operations
        */
        mutable uint32_t m_u32ReadBufferSize;

        /**
          * \brief functions associated to the plugin commands
        */
        #define KI2C_PLUGIN_CMD_RECORD(a, ...)  bool m_KI2C_##a ( const std::string& args, std::stop_token st ) const;
        KI2C_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  KI2C_PLUGIN_CMD_RECORD
};

#endif /* KI2C_PLUGIN_HPP */
