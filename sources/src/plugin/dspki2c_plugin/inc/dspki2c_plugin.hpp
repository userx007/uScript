#ifndef DSPKI2C_PLUGIN_HPP
#define DSPKI2C_PLUGIN_HPP

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

#define DSPKI2C_PLUGIN_VERSION    "1.0.0.0"
#define DSPKI2C_PLUGIN_NAME       "DSPKI2C"

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN MACROS                                      //
/////////////////////////////////////////////////////////////////////////////////

// DSPKI2C_GET_BLOCKING: picks the blocking flag when provided,
// defaults to false so non-blocking commands need no annotation.
#ifndef DSPKI2C_GET_BLOCKING
#define DSPKI2C_GET_BLOCKING(name, blocking, ...) blocking
#endif

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN COMMANDS                                    //
/////////////////////////////////////////////////////////////////////////////////

#define DSPKI2C_PLUGIN_COMMANDS_CONFIG_TABLE    \
DSPKI2C_PLUGIN_CMD_RECORD( INFO               ) \
DSPKI2C_PLUGIN_CMD_RECORD( CONFIG             ) \
DSPKI2C_PLUGIN_CMD_RECORD( SCAN               ) \
DSPKI2C_PLUGIN_CMD_RECORD( CMD               ) \
DSPKI2C_PLUGIN_CMD_RECORD( SCRIPT             ) \
DSPKI2C_PLUGIN_CMD_RECORD( CYCLIC             ) \

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN INTERFACE                                   //
/////////////////////////////////////////////////////////////////////////////////

/**
  * \brief Digispark I2C bridge plugin class definition.
  *
  * Wraps the I2CBridge driver (uDigisparkI2C.hpp) and exposes the
  * same command-dispatch architecture as the UART plugin.  The
  * Digispark ATtiny85 acts as a USB→I2C bridge; all USB-HID
  * transport details are hidden inside I2CBridge.
  *
  * Commands
  * --------
  *   INFO   – print plugin description and usage examples
  *   CONFIG – set VID/PID, slave address, read/write timeouts
  *   SCAN   – discover all responding I2C slaves on the bus
  *   CMD    – send / receive I2C frames (same mini-language as UART.CMD)
  *   SCRIPT – execute a script file of CMD lines
  *
  * CONFIG argument keys (space-separated key=value pairs)
  * -------------------------------------------------------
  *   v  – USB VID            (hex, e.g. v=16C0)
  *   p  – USB PID            (hex, e.g. p=05DF)
  *   a  – default slave addr (hex, e.g. a=48)
  *   r  – read timeout  [ms] (decimal)
  *   w  – write timeout [ms] (decimal)
  *   s  – read buffer size   (decimal)
*/
class DSPKi2cPlugin: public PluginInterface
{
    public:

        /**
          * \brief class constructor
        */
        DSPKi2cPlugin() : m_strVersion(DSPKI2C_PLUGIN_VERSION)
                        , m_strInstanceName(DSPKI2C_PLUGIN_NAME)
                        , m_bIsInitialized(false)
                        , m_bIsEnabled(false)
                        , m_bIsFaultTolerant(false)
                        , m_bIsPrivileged(false)
                        , m_strResultData("")
                        , m_bRawResult(false)
                        , m_bCyclicCached(true)
        {
            #define DSPKI2C_PLUGIN_CMD_RECORD(a, ...) m_mapCmds.insert( std::make_pair( #a, \
            PluginCommandEntry<DSPKi2cPlugin>{&DSPKi2cPlugin::m_DSPKI2C_##a, DSPKI2C_GET_BLOCKING(a, ##__VA_ARGS__, false)} ));
            DSPKI2C_PLUGIN_COMMANDS_CONFIG_TABLE
            #undef  DSPKI2C_PLUGIN_CMD_RECORD
        }

        /**
          * \brief class destructor
        */
        ~DSPKi2cPlugin()
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

            if (true == generic_setparams<DSPKi2cPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
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
            generic_getparams<DSPKi2cPlugin>(this, psGetParams);
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
          * \brief perform the de-initialization of modules used by the plugin
          * \note public because it needs to be called explicitly before closing/freeing the shared library
        */
        void doCleanup(void)
        {
            m_bIsInitialized = false;
            m_bIsEnabled     = false;
        }

        /**
          * \brief perform the enabling of the plugin
          * \note The un-enabled plugin can validate the command's arguments but doesn't allow the real execution.
          *       This mode is used for command validation.
        */
        bool doEnable(void)
        {
            m_bIsEnabled = true;
            return true;
        }

        /**
          * \brief dispatch commands
        */
        bool doDispatch( const std::string& strCmd, const std::string& strParams, std::stop_token st = {} ) const
        {
            return generic_dispatch<DSPKi2cPlugin>(this, strCmd, strParams, st);
        }

        /**
          * \brief get a pointer to the plugin map
        */
        const PluginCommandsMap<DSPKi2cPlugin> *getMap(void) const
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

        // ── CONFIG setters (called by dspki2c_setup.hpp generic helpers) ────────

        /**
          * \brief set USB VID (hex string, e.g. "16C0")
        */
        bool setVid (const std::string& strVid) const
        {
            return numeric::str2uint16(strVid, m_u16Vid);
        }

        /**
          * \brief set USB PID (hex string, e.g. "05DF")
        */
        bool setPid (const std::string& strPid) const
        {
            return numeric::str2uint16(strPid, m_u16Pid);
        }

        /**
          * \brief set default I2C slave address (hex string, e.g. "48")
        */
        bool setSlaveAddr (const std::string& strAddr) const
        {
            uint16_t u16Tmp = 0;
            if (false == numeric::str2uint16(strAddr, u16Tmp)) {
                return false;
            }
            if (u16Tmp > 0x7F) {
                return false;   // 7-bit address range check
            }
            m_u8SlaveAddr = static_cast<uint8_t>(u16Tmp);
            return true;
        }

        /**
          * \brief set I2C read timeout [ms]
        */
        bool setReadTimeout (const std::string& strTimeout) const
        {
            return numeric::str2uint32(strTimeout, m_u32ReadTimeout);
        }

        /**
          * \brief set I2C write timeout [ms]
        */
        bool setWriteTimeout (const std::string& strTimeout) const
        {
            return numeric::str2uint32(strTimeout, m_u32WriteTimeout);
        }

        /**
          * \brief set receive buffer size
        */
        bool setReadBufferSize (const std::string& strSize) const
        {
            return numeric::str2uint32(strSize, m_u32ReadBufferSize);
        }

        /**
          * \brief get current USB VID
        */
        uint16_t getVid  (void) const { return m_u16Vid;       }

        /**
          * \brief get current USB PID
        */
        uint16_t getPid  (void) const { return m_u16Pid;       }

        /**
          * \brief get current default slave address
        */
        uint8_t  getSlaveAddr (void) const { return m_u8SlaveAddr; }


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
        PluginCommandsMap<DSPKi2cPlugin> m_mapCmds;

        /**
          * \brief plugin version
        */
        std::string m_strVersion;


        /**
          * \brief runtime instance identity used for the GUI comm-dump panel
          *        (e.g. "DSPKI2C" or "DSPKI2C:1" -- see
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
          * \brief the artefacts path from command line
        */
        std::string m_strArtefactsPath;

        // ── I2C / USB bridge configuration ──────────────────────────────────────

        /**
          * \brief USB Vendor ID of the Digispark bridge (default: V-USB shared HID VID)
        */
        mutable uint16_t m_u16Vid           = 0x16C0;

        /**
          * \brief USB Product ID of the Digispark bridge (default: V-USB shared HID PID)
        */
        mutable uint16_t m_u16Pid           = 0x05DF;

        /**
          * \brief Default 7-bit I2C slave address used when no address is embedded in the CMD arguments
        */
        mutable uint8_t  m_u8SlaveAddr      = 0x00;

        /**
          * \brief Read timeout [ms]
        */
        mutable uint32_t m_u32ReadTimeout   = 2000;

        /**
          * \brief Write timeout [ms]
        */
        mutable uint32_t m_u32WriteTimeout  = 2000;

        /**
          * \brief Maximum number of bytes to receive in a single read operation
        */
        mutable uint32_t m_u32ReadBufferSize = 64;

        /**
          * \brief functions associated to the plugin commands
        */
        #define DSPKI2C_PLUGIN_CMD_RECORD(a, ...)  bool m_DSPKI2C_##a ( const std::string& args, std::stop_token st ) const;
        DSPKI2C_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  DSPKI2C_PLUGIN_CMD_RECORD
};

#endif /* DSPKI2C_PLUGIN_HPP */
