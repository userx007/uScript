#pragma once

#include "CommonSettings.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "IScriptInterpreter.hpp"
#include "ICommDriver.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"

#include "uVectorValidator.hpp"
#include "uNumeric.hpp"
#include "uLogger.hpp"

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

#include <atomic>

///////////////////////////////////////////////////////////////////
//                          PLUGIN VERSION                       //
///////////////////////////////////////////////////////////////////

#define UTILS_PLUGIN_VERSION "1.8.5.0"

///////////////////////////////////////////////////////////////////
//                          PLUGIN COMMANDS                      //
///////////////////////////////////////////////////////////////////

#define UTILS_PLUGIN_COMMANDS_CONFIG_TABLE      \
UTILS_PLUGIN_CMD_RECORD( INFO                 ) \
UTILS_PLUGIN_CMD_RECORD( BREAKPOINT           ) \
UTILS_PLUGIN_CMD_RECORD( DELAY                ) \
UTILS_PLUGIN_CMD_RECORD( EVALUATE             ) \
UTILS_PLUGIN_CMD_RECORD( EVALUATE_BOOL_ARRAY  ) \
UTILS_PLUGIN_CMD_RECORD( FAIL                 ) \
UTILS_PLUGIN_CMD_RECORD( FORMAT               ) \
UTILS_PLUGIN_CMD_RECORD( MATH                 ) \
UTILS_PLUGIN_CMD_RECORD( MESSAGE              ) \
UTILS_PLUGIN_CMD_RECORD( PRINT                ) \
UTILS_PLUGIN_CMD_RECORD( RETURN               ) \
UTILS_PLUGIN_CMD_RECORD( VALIDATE             ) \

//UTILS_PLUGIN_CMD_RECORD( LIST_UART_PORTS      ) \
//UTILS_PLUGIN_CMD_RECORD( WAIT_UART_INSERT     ) \
//UTILS_PLUGIN_CMD_RECORD( WAIT_UART_REMOVE     ) \
//UTILS_PLUGIN_CMD_RECORD( START_UART_MONITORING) \


///////////////////////////////////////////////////////////////////
//                          PLUGIN INTERFACE                     //
///////////////////////////////////////////////////////////////////

/**
  * \brief Utils plugin class definition
*/
class UtilsPlugin: public PluginInterface
{
    public:

       /**
         * \brief class constructor
        */
        UtilsPlugin() : m_strPluginVersion(UTILS_PLUGIN_VERSION)
                      , m_bIsInitialized(false)
                      , m_bIsEnabled(false)
                      , m_bIsFaultTolerant(false)
                      , m_strResultData("")
        {
            #define UTILS_PLUGIN_CMD_RECORD(a) m_mapCmds.insert( std::make_pair( #a, &UtilsPlugin::m_Utils_##a));
            UTILS_PLUGIN_COMMANDS_CONFIG_TABLE
            #undef  UTILS_PLUGIN_CMD_RECORD
        }

        /**
          * \brief class destructor
        */
        ~UtilsPlugin()
        {

        }

        /**
          * \brief get the plugin initialization status
        */
        bool isInitialized( void) const
        {
            return m_bIsInitialized;
        }

        /**
          * \brief get enabling status
        */
        bool isEnabled ( void) const
        {
            return m_bIsEnabled;
        }

        /**
         * \brief Import external settings into the plugin
        */
        bool setParams( const PluginDataSet *psSetParams)
        {
            bool bRetVal = false;

            if (true == generic_setparams<UtilsPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
                if (true == m_LocalSetParams(psSetParams)) {
                    bRetVal = true;
                }
            }

            return bRetVal;
        }

        /**
          * \brief function to retrieve information from plugin
        */
        void getParams( PluginDataGet *psGetParams) const
        {
            generic_getparams<UtilsPlugin>(this, psGetParams);
        }

        /**
          * \brief dispatch commands
        */
        bool doDispatch( const std::string& strCmd, const std::string& strParams) const
        {
            return generic_dispatch<UtilsPlugin>(this, strCmd, strParams);
        }

        /**
          * \brief get a pointer to the plugin map
        */
        const PluginCommandsMap<UtilsPlugin> *getMap(void) const
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
          * \brief return the result data to the caller
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
            // restore the flags (because they could vave been set during validation to detect in that phase that no multiple instances were created for UART insertion/removal monitoring)
            m_bUartMonitoring.store(false);
            m_bIsEnabled = true;
        }

        /**
          * \brief perform the de-initialization of modules used by the plugin
          * \note public because need to be called explicitely before closing/freeing the shared library
        */
        void doCleanup(void);

        /**
          * \brief set fault tolerant flag status
        */
        void setFaultTolerant(void)
        {
            m_bIsFaultTolerant = true;
        }

        /**
          * \brief get fault tolerant flag status
        */
        bool isFaultTolerant ( void) const
        {
            return m_bIsFaultTolerant;
        }

        /**
          * \brief get the privileged status
        */
        bool isPrivileged ( void) const
        {
            return false;
        }

    private:

        /**
          * \brief map with association between the command string and the execution function
        */
        PluginCommandsMap<UtilsPlugin> m_mapCmds;

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
          * \brief Vector validator/math
        */
        VectorValidator m_validator;
        VectorMath      m_math;

#if 0
        /**
          * \brief array of threads used for monitoring things in background
        */
        mutable std::vector<THREAD_TYPE> m_vThreadArray;

        /**
          * \brief polling interval
        */
        static uint32_t m_u32PollingInterval;

        /**
          * \brief UART ports monitoring flags
        */
        static std::atomic<bool> m_bUartMonitoring;


        /**
          * \brief UART ports monitoring callbacks
        */
#if defined(_MSC_VER)
        static void m_threadUartMonitoring ( std::atomic<bool> & bRun);
#else // Linux & MINGW
        static void *m_threadUartMonitoring ( void *pvThreadArgs);
#endif

        /**
          * \brief generic handler for UART operations
        */
        bool m_GenericUartHandling ( const char *args, PFUARTHDL pfUartHdl) const;
#endif

        /**
          * \brief generic handler for message functions
        */
        bool m_GenericMessageHandling ( const char *args, bool bIsBreakpoint) const;

        /**
          * \brief generic handler for validation
        */
        bool m_GenericEvaluationHandling ( std::vector<std::string>& vstrArgs, const bool bIsStringRule, bool *pbResult ) const;

        /**
          * \brief evaluate the expression provided argument
        */
        bool m_EvaluateExpression ( const char *args, bool *pEvalResult) const;

        /**
          * \brief functions associated to the plugin commands
        */
        #define UTILS_PLUGIN_CMD_RECORD(a)     bool m_Utils_##a (const std::string &args) const;
        UTILS_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  UTILS_PLUGIN_CMD_RECORD
};

