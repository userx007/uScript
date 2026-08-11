#ifndef DSPKSPI_PLUGIN_HPP
#define DSPKSPI_PLUGIN_HPP

#include "uSharedConfig.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "ICommDriver.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"
#include "uNumeric.hpp"
#include "uLogger.hpp"

#include "uDigisparkSPI.hpp"

#include <string>
#include <utility>
#include <span>

///////////////////////////////////////////////////////////////////
//                          PLUGIN VERSION                       //
///////////////////////////////////////////////////////////////////

#define DSPKSPI_PLUGIN_VERSION    "1.0.0.0"
#define DSPKSPI_PLUGIN_NAME       "DSPKSPI"


///////////////////////////////////////////////////////////////////
//                          PLUGIN COMMANDS                      //
///////////////////////////////////////////////////////////////////

// DSPKSPI_GET_BLOCKING: picks the blocking flag when provided,
// defaults to false so non-blocking commands need no annotation.
#ifndef DSPKSPI_GET_BLOCKING
#define DSPKSPI_GET_BLOCKING(name, blocking, ...) blocking
#endif

#define DSPKSPI_PLUGIN_COMMANDS_CONFIG_TABLE    \
DSPKSPI_PLUGIN_CMD_RECORD( INFO               ) \
DSPKSPI_PLUGIN_CMD_RECORD( CONFIG             ) \
DSPKSPI_PLUGIN_CMD_RECORD( CMD                ) \
DSPKSPI_PLUGIN_CMD_RECORD( SCRIPT             ) \
DSPKSPI_PLUGIN_CMD_RECORD( CYCLIC             ) \

///////////////////////////////////////////////////////////////////
//                          PLUGIN INTERFACE                     //
///////////////////////////////////////////////////////////////////

/**
  * \brief Digispark SPI bridge plugin class definition.
  *
  * Wraps SPIBridge (uDigisparkSPI.hpp) as a plugin, exposing the same
  * INFO / CONFIG / CMD / SCRIPT command surface as the UART plugin.
  *
  * Transport : USB HID via SPIBridge / hidapi
  * Default   : VID 0x16C0, PID 0x05DF, Mode0, Div4
*/
class DSPKSPIPlugin: public PluginInterface
{
    public:

        /**
          * \brief class constructor
        */
        DSPKSPIPlugin() : m_strVersion(DSPKSPI_PLUGIN_VERSION)
                        , m_strInstanceName(DSPKSPI_PLUGIN_NAME)
                        , m_bIsInitialized(false)
                        , m_bIsEnabled(false)
                        , m_bIsFaultTolerant(false)
                        , m_bIsPrivileged(false)
                        , m_strResultData("")
                        , m_u16Vid(SPIBridge::SPI_DIGISPARK_VID)
                        , m_u16Pid(SPIBridge::SPI_DIGISPARK_PID)
                        , m_eSpiMode(SPIBridge::SPIMode::Mode0)
                        , m_eClockDiv(SPIBridge::SPIClockDiv::Div4)
                        , m_u32ReadTimeout(SPIBridge::SPI_READ_DEFAULT_TIMEOUT)
                        , m_u32WriteTimeout(SPIBridge::SPI_WRITE_DEFAULT_TIMEOUT)
                        , m_u32ReadBufferSize(SPIBridge::SPI_MAX_READ_PAYLOAD)
        {
            #define DSPKSPI_PLUGIN_CMD_RECORD(a, ...) m_mapCmds.insert( std::make_pair( #a, \
            PluginCommandEntry<DSPKSPIPlugin>{&DSPKSPIPlugin::m_DSPKSPI_##a, DSPKSPI_GET_BLOCKING(a, ##__VA_ARGS__, false)} ));
            DSPKSPI_PLUGIN_COMMANDS_CONFIG_TABLE
            #undef  DSPKSPI_PLUGIN_CMD_RECORD
        }

        /**
          * \brief class destructor
        */
        ~DSPKSPIPlugin()
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

            if (true == generic_setparams<DSPKSPIPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
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
            generic_getparams<DSPKSPIPlugin>(this, psGetParams);
        }

        /**
          * \brief dispatch commands
        */
        bool doDispatch( const std::string& strCmd, const std::string& strParams, std::stop_token st = {} ) const
        {
            return generic_dispatch<DSPKSPIPlugin>(this, strCmd, strParams, st);
        }

        /**
          * \brief get a pointer to the plugin map
        */
        const PluginCommandsMap<DSPKSPIPlugin> *getMap(void) const
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
          * \brief clear the result data
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
        */
        bool doEnable(void)
        {
            m_bIsEnabled = true;
            return true;
        }

        /**
          * \brief perform the de-initialization of modules used by the plugin
          * \note public because it needs to be called explicitly before closing/freeing the shared library
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

        // ── SPI-specific setters (used by dspkspi_setup.hpp) ─────────────────

        /**
          * \brief set USB VID (hex string, e.g. "16C0")
        */
        bool setSpiVid (const std::string& strVid) const
        {
            if (false == numeric::str2uint16(strVid, m_u16Vid)) { return false; }
            return true;
        }

        /**
          * \brief set USB PID (hex string, e.g. "05DF")
        */
        bool setSpiPid (const std::string& strPid) const
        {
            if (false == numeric::str2uint16(strPid, m_u16Pid)) { return false; }
            return true;
        }

        /**
          * \brief set SPI mode (0-3)
        */
        bool setSpiMode (const std::string& strMode) const
        {
            uint8_t u8Tmp = 0;
            if (false == numeric::str2uint8(strMode, u8Tmp) || u8Tmp > static_cast<uint8_t>(SPIBridge::SPIMode::Mode_Last)) { return false; }
            m_eSpiMode = static_cast<SPIBridge::SPIMode>(u8Tmp);
            return true;
        }

        /**
          * \brief set SPI clock divider (0=Div2, 1=Div4, 2=Div8, 3=Div16)
        */
        bool setSpiClockDiv (const std::string& strDiv) const
        {
            uint8_t u8Tmp = 0;
            if (false == numeric::str2uint8(strDiv, u8Tmp) || u8Tmp > static_cast<uint8_t>(SPIBridge::SPIClockDiv::Div_Last)) { return false; }
            m_eClockDiv = static_cast<SPIBridge::SPIClockDiv>(u8Tmp);
            return true;
        }

        /**
          * \brief set SPI read timeout [ms]
        */
        bool setSpiReadTimeout (const std::string& strReadTimeout) const
        {
            return numeric::str2uint32(strReadTimeout, m_u32ReadTimeout);
        }

        /**
          * \brief set SPI write timeout [ms]
        */
        bool setSpiWriteTimeout (const std::string& strWriteTimeout) const
        {
            return numeric::str2uint32(strWriteTimeout, m_u32WriteTimeout);
        }

        /**
          * \brief set SPI read buffer size [bytes, <= SPI_MAX_READ_PAYLOAD]
        */
        bool setSpiReadBufferSize (const std::string& strBufSize) const
        {
            return numeric::str2uint32(strBufSize, m_u32ReadBufferSize);
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
        PluginCommandsMap<DSPKSPIPlugin> m_mapCmds;

        /**
          * \brief plugin version
        */
        std::string m_strVersion;


        /**
          * \brief runtime instance identity used for the GUI comm-dump panel
          *        (e.g. "DSPKSPI" or "DSPKSPI:1" -- see
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
          * \brief the artefacts path got from command line
        */
        std::string m_strArtefactsPath;

        // ── SPI / USB configuration ───────────────────────────────────────────

        /** USB Vendor ID */
        mutable uint16_t m_u16Vid;

        /** USB Product ID */
        mutable uint16_t m_u16Pid;

        /** SPI clock mode (CPOL/CPHA) */
        mutable SPIBridge::SPIMode m_eSpiMode;

        /** SPI clock divider */
        mutable SPIBridge::SPIClockDiv m_eClockDiv;

        /** Read timeout [ms] */
        mutable uint32_t m_u32ReadTimeout;

        /** Write timeout [ms] */
        mutable uint32_t m_u32WriteTimeout;

        /** Maximum bytes to read in a single operation */
        mutable uint32_t m_u32ReadBufferSize;

        /**
          * \brief functions associated to the plugin commands
        */
        #define DSPKSPI_PLUGIN_CMD_RECORD(a, ...)  bool m_DSPKSPI_##a ( const std::string& args, std::stop_token st ) const;
        DSPKSPI_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  DSPKSPI_PLUGIN_CMD_RECORD
};

#endif /* DSPKSPI_PLUGIN_HPP */
