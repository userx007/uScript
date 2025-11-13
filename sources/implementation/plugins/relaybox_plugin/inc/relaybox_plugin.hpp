#ifndef RELAYBOX_PLUGIN_HPP
#define RELAYBOX_PLUGIN_HPP

#include "CommonSettings.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "uLogger.hpp"

#include "ftdi245.hpp"

#include <string>

#if defined(_MSC_VER)
    #include <thread>
    #include <mutex>
    #define THREAD_TYPE std::thread
    #define MUTEX_TYPE  std::mutex
#else
    #include <pthread.h>
    #define THREAD_TYPE pthread_t
    #define MUTEX_TYPE  pthread_mutex_t
#endif

///////////////////////////////////////////////////////////////////
//                          PLUGIN VERSION                       //
///////////////////////////////////////////////////////////////////

#define RELAYBOX_PLUGIN_VERSION "1.9.0.1"

///////////////////////////////////////////////////////////////////
//                   PLUGIN COMMANDS                             //
///////////////////////////////////////////////////////////////////

#define RELAYBOX_PLUGIN_COMMANDS_CONFIG_TABLE  \
RELAYBOX_PLUGIN_CMD_RECORD( INFO            )  \
RELAYBOX_PLUGIN_CMD_RECORD( SWITCH          )  \
RELAYBOX_PLUGIN_CMD_RECORD( SWITCHALL      )  \
RELAYBOX_PLUGIN_CMD_RECORD( STATUS          )  \


///////////////////////////////////////////////////////////////////
//                   PLUGIN INTERFACE                            //
///////////////////////////////////////////////////////////////////

class RelayboxPlugin: public PluginInterface
{
    public:

        /**
          * \brief class constructor
        */
        RelayboxPlugin() : m_strPluginVersion(RELAYBOX_PLUGIN_VERSION)
                         , m_bIsInitialized(false)
                         , m_bIsEnabled(false)
                         , m_bIsFaultTolerant(false)
                         , m_bIsPrivileged(false)
                         , m_strResultData("")
                         , m_hFtdi245hdl(nullptr)
                         , m_u32Channel(0)
                         , m_u32State(0)
                         , m_u32Delay(0)
                         , m_strSerialNumber("")
                         , m_iProductID(0)
        {
            #define RELAYBOX_PLUGIN_CMD_RECORD(a) m_mapCmds.insert( std::make_pair( #a, &RelayboxPlugin::m_Relaybox_##a ));
            RELAYBOX_PLUGIN_COMMANDS_CONFIG_TABLE
            #undef  RELAYBOX_PLUGIN_CMD_RECORD
        }

        /**
          * \brief class destructor
        */
        ~RelayboxPlugin()
        {

        }

        /**
          * \brief get the plugin initialization status
        */
        bool is_initialized (void) const
        {
            return m_bIsInitialized;
        }

        /**
          * \brief get enabling status
        */
        bool is_enabled (void) const
        {
            return m_bIsEnabled;
        }

        /**
          * \brief function to provide various parameters to plugin
        */
        bool set_params (const PluginDataSet *psSetParams)
        {
            bool bRetVal = false;

            if (true == generic_setparams<RelayboxPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
                if (true == m_LocalSetParams(psSetParams)) {
                    bRetVal = true;
                }
            }

            return bRetVal;
        }

        /**
          * \brief function to retrieve information from plugin
        */
        void get_params (PluginDataGet *psGetParams) const
        {
            generic_getparams<RelayboxPlugin>(this, psGetParams);
        }

        /**
          * \brief dispatch commands
        */
        bool do_dispatch (const std::string& strCmd, const std::string& strParams) const
        {
            return generic_dispatch<RelayboxPlugin>(this, strCmd, strParams);
        }

        /**
          * \brief get a pointer to the plugin map
        */
        const PluginCommandsMap<RelayboxPlugin> *getMap (void) const
        {
            return &m_mapCmds;
        }

        /**
          * \brief get the plugin version
        */
        const std::string& getVersion (void) const
        {
            return m_strPluginVersion;
        }

        /**
          * \brief get the result data
        */
        const std::string& get_data (void) const
        {
            return m_strResultData;
        }

        /**
          * \brief clear the result data (avoid that some data to be returned by other command)
        */
        void reset_data (void) const
        {
            m_strResultData.clear();
        }

        /**
          * \brief perform the initialization of modules used by the plugin
          * \note public because it needs to be called explicitely after loading the plugin
        */
        bool do_init (void *pvUserData);

        /**
          * \brief perform the enabling of the plugin
          * \note The un-enabled plugin can validate the command's arguments but doesn't allow the real execution
          *       This mode is used for the command validation
        */
        void do_enable (void)
        {
            m_bIsEnabled = true;
        }

        /**
          * \brief perform the de-initialization of modules used by the plugin
          * \note public because need to be called explicitely before closing/freeing the shared library
        */
        void do_cleanup (void);

        /**
          * \brief set fault tolerant flag status
        */
        void setFaultTolerant (void)
        {
            m_bIsFaultTolerant = true;
        }

        /**
          * \brief get fault tolerant flag status
        */
        bool is_fault_tolerant (void) const
        {
            return m_bIsFaultTolerant;
        }

        /**
          * \brief get the privileged status
        */
        bool is_privileged (void) const
        {
            return false;
        }

        /**
          * \brief get the relay parameters
        */
        void m_getRelayParams (uint32_t *pu32Channel, uint32_t *pu32State, uint32_t *pu32Delay) const
        {
            *pu32Channel = m_u32Channel;
            *pu32State   = m_u32State;
            *pu32Delay   = m_u32Delay;
        }

    private:

        /**
          * \brief processing of the plugin specific settings
        */
        bool m_LocalSetParams (const PluginDataSet *psSetParams);

        /**
          * \brief map with association between the command string and the execution function
        */
        PluginCommandsMap<RelayboxPlugin> m_mapCmds;

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
          * \brief relay box hardware serial number
        */
        std::string m_strSerialNumber;

        /**
          * \brief relay box hardware ProductID
        */
        int m_iProductID;

        /**
          * \brief relay box hardware VendorID
        */
        int m_iVendorID;

        /**
          * \brief maximum number of relays
        */
        int m_iMaxNrRelays;

        /**
          * \brief instance of the chip handler
        */
        ftdi245hdl *m_hFtdi245hdl;

        /**
          * \brief items to be accessed from the Thread
          * \note declared mutable because they need to be modified by "const" interfaces
        */
        mutable uint32_t m_u32Channel;
        mutable uint32_t m_u32State;
        mutable uint32_t m_u32Delay;

        /**
          * \brief array of threads used for delayed execution
          *        need to store the threads in order to join them later ..
        */
        mutable std::vector<THREAD_TYPE> m_vThreadArray;

        /**
          * \brief mutex used for thread initialization
        */
        mutable MUTEX_TYPE m_mutexLock;

        /**
          * \brief callback for the thread used to delay the execution
        */
        static void *m_threadCB (void *pvThreadArgs );

        /**
          * \brief handle the relay
        */
        bool m_RelayHandling (const uint32_t u32Channel, const uint32_t u32State, const uint32_t u32Delay ) const ;

        /**
          * \brief functions associated to the plugin commands
        */
        #define RELAYBOX_PLUGIN_CMD_RECORD(a)     bool m_Relaybox_##a (const std::string &args) const;
        RELAYBOX_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  RELAYBOX_PLUGIN_CMD_RECORD
};

#endif // RELAYBOX_PLUGIN_HPP