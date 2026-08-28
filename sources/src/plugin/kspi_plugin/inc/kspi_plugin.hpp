#ifndef KSPI_PLUGIN_HPP
#define KSPI_PLUGIN_HPP

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

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN NAME / VERSION                              //
/////////////////////////////////////////////////////////////////////////////////

#define KSPI_PLUGIN_VERSION    "1.0.0.0"
#define KSPI_PLUGIN_NAME       "KSPI"

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN MACROS                                      //
/////////////////////////////////////////////////////////////////////////////////

// KSPI_GET_BLOCKING: picks the blocking flag when provided,
// defaults to false so non-blocking commands need no annotation.
#ifndef KSPI_GET_BLOCKING
#define KSPI_GET_BLOCKING(name, blocking, ...) blocking
#endif

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN COMMANDS                                    //
/////////////////////////////////////////////////////////////////////////////////

#define KSPI_PLUGIN_COMMANDS_CONFIG_TABLE    \
KSPI_PLUGIN_CMD_RECORD( INFO               ) \
KSPI_PLUGIN_CMD_RECORD( CONFIG             ) \
KSPI_PLUGIN_CMD_RECORD( CMD                ) \
KSPI_PLUGIN_CMD_RECORD( SCRIPT             ) \
KSPI_PLUGIN_CMD_RECORD( CYCLIC             ) \

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN INTERFACE                                   //
/////////////////////////////////////////////////////////////////////////////////

class KSPIPlugin: public PluginInterface
{
    public:

        /**
          * \brief class constructor
        */
        KSPIPlugin() : m_strVersion(KSPI_PLUGIN_VERSION)
                     , m_strInstanceName(KSPI_PLUGIN_NAME)
                     , m_bIsInitialized(false)
                     , m_bIsEnabled(false)
                     , m_bIsFaultTolerant(false)
                     , m_bIsPrivileged(false)
                     , m_strResultData("")
                     , m_bRawResult(false)
                     , m_bCyclicCached(true)
        {
            #define KSPI_PLUGIN_CMD_RECORD(a, ...) m_mapCmds.insert( std::make_pair( #a, \
            PluginCommandEntry<KSPIPlugin>{&KSPIPlugin::m_KSPI_##a, KSPI_GET_BLOCKING(a, ##__VA_ARGS__, false)} ));
            KSPI_PLUGIN_COMMANDS_CONFIG_TABLE
            #undef  KSPI_PLUGIN_CMD_RECORD
        }

        /**
          * \brief class destructor
        */
        ~KSPIPlugin()
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

            if (true == generic_setparams<KSPIPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
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
            generic_getparams<KSPIPlugin>(this, psGetParams);
        }

        /**
          * \brief dispatch commands
        */
        bool doDispatch( const std::string& strCmd, const std::string& strParams, std::stop_token st = {} ) const
        {
            return generic_dispatch<KSPIPlugin>(this, strCmd, strParams, st);
        }

        /**
          * \brief get a pointer to the plugin map
        */
        const PluginCommandsMap<KSPIPlugin> *getMap(void) const
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
          * \note public because it needs to be called explicitly after loading the plugin
        */
        bool doInit(void *pvUserData)
        {
            m_bIsInitialized = true;
            return m_bIsInitialized;
        }

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
        void doCleanup(void)
        {
            m_bIsInitialized = false;
            m_bIsEnabled     = false;
        }

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
          * \brief get KSPI device path
        */
        const char *getSpiDevice (void) const
        {
            return m_strSpiDevice.c_str();
        }

        /**
          * \brief set KSPI device path
        */
        void setSpiDevice (const std::string& strSpiDevice) const
        {
            m_strSpiDevice.assign(strSpiDevice);
        }

        /**
          * \brief set KSPI mode (0–3, encoding CPOL/CPHA)
        */
        bool setSpiMode (const std::string& strMode) const
        {
            uint32_t u32Mode = 0;
            if (false == numeric::str2uint32(strMode, u32Mode)) {
                return false;
            }
            if (u32Mode > 3) {
                return false;
            }
            m_u8SpiMode = static_cast<uint8_t>(u32Mode);
            return true;
        }

        /**
          * \brief set KSPI bus speed in Hz
        */
        bool setSpiSpeedHz (const std::string& strSpeedHz) const
        {
            return numeric::str2uint32(strSpeedHz, m_u32SpiSpeedHz);
        }

        /**
          * \brief set KSPI bits per word
        */
        bool setSpiBitsPerWord (const std::string& strBitsPerWord) const
        {
            uint32_t u32Bpw = 0;
            if (false == numeric::str2uint32(strBitsPerWord, u32Bpw)) {
                return false;
            }
            m_u8SpiBitsPerWord = static_cast<uint8_t>(u32Bpw);
            return true;
        }

        /**
          * \brief set KSPI read timeout
        */
        bool setSpiReadTimeout (const std::string& strReadTimeout) const
        {
            return numeric::str2uint32(strReadTimeout, m_u32ReadTimeout);
        }

        /**
          * \brief set KSPI write timeout
        */
        bool setSpiWriteTimeout (const std::string& strWriteTimeout) const
        {
            return numeric::str2uint32(strWriteTimeout, m_u32WriteTimeout);
        }

        /**
          * \brief set KSPI read buffer size
        */
        bool setSpiReadBufferSize (const std::string& strReadBufferSize) const
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
        PluginCommandsMap<KSPIPlugin> m_mapCmds;

        /**
          * \brief plugin version
        */
        std::string m_strVersion;


        /**
          * \brief runtime instance identity used for the GUI comm-dump panel
          *        (e.g. "KSPI" or "KSPI:1" -- see
          *        PluginDataSet::strInstanceName). Falls back to the fixed plugin
          *        name macro when unset (e.g. standalone construction outside the
          *        script interpreter).
        */
        std::string m_strInstanceName;
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
          * \brief the KSPI device node (e.g. /dev/spidev0.0)
        */
        mutable std::string m_strSpiDevice;

        /**
          * \brief KSPI mode: 0–3 (CPOL/CPHA)
        */
        mutable uint8_t m_u8SpiMode;

        /**
          * \brief KSPI bus clock speed in Hz (e.g. 1000000 for 1 MHz)
        */
        mutable uint32_t m_u32SpiSpeedHz;

        /**
          * \brief KSPI bits per word (typically 8)
        */
        mutable uint8_t m_u8SpiBitsPerWord;

        /**
          * \brief KSPI read timeout in milliseconds
        */
        mutable uint32_t m_u32ReadTimeout;

        /**
          * \brief KSPI write timeout in milliseconds
        */
        mutable uint32_t m_u32WriteTimeout;

        /**
          * \brief size of the buffer used for KSPI read operations
        */
        mutable uint32_t m_u32ReadBufferSize;

        /**
          * \brief functions associated to the plugin commands
        */
        #define KSPI_PLUGIN_CMD_RECORD(a, ...)  bool m_KSPI_##a ( const std::string& args, std::stop_token st ) const;
        KSPI_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  KSPI_PLUGIN_CMD_RECORD
};

#endif /* KSPI_PLUGIN_HPP */
