#ifndef SPI_PLUGIN_HPP
#define SPI_PLUGIN_HPP

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

#define SPI_PLUGIN_VERSION    "1.0.0.0"
#define SPI_PLUGIN_NAME       "SPI"


///////////////////////////////////////////////////////////////////
//                          PLUGIN COMMANDS                      //
///////////////////////////////////////////////////////////////////

// SPI_GET_BLOCKING: picks the blocking flag when provided,
// defaults to false so non-blocking commands need no annotation.
#ifndef SPI_GET_BLOCKING
#define SPI_GET_BLOCKING(name, blocking, ...) blocking
#endif

#define SPI_PLUGIN_COMMANDS_CONFIG_TABLE    \
SPI_PLUGIN_CMD_RECORD( INFO               ) \
SPI_PLUGIN_CMD_RECORD( CONFIG             ) \
SPI_PLUGIN_CMD_RECORD( CMD                ) \
SPI_PLUGIN_CMD_RECORD( SCRIPT             ) \


///////////////////////////////////////////////////////////////////
//                          PLUGIN INTERFACE                     //
///////////////////////////////////////////////////////////////////

/**
  * \brief SPI plugin class definition
*/
class SPIPlugin: public PluginInterface
{
    public:

        /**
          * \brief class constructor
        */
        SPIPlugin() : m_strVersion(SPI_PLUGIN_VERSION)
                    , m_bIsInitialized(false)
                    , m_bIsEnabled(false)
                    , m_bIsFaultTolerant(false)
                    , m_bIsPrivileged(false)
                    , m_strResultData("")
        {
            #define SPI_PLUGIN_CMD_RECORD(a, ...) m_mapCmds.insert( std::make_pair( #a, \
            PluginCommandEntry<SPIPlugin>{&SPIPlugin::m_SPI_##a, SPI_GET_BLOCKING(a, ##__VA_ARGS__, false)} ));
            SPI_PLUGIN_COMMANDS_CONFIG_TABLE
            #undef  SPI_PLUGIN_CMD_RECORD
        }

        /**
          * \brief class destructor
        */
        ~SPIPlugin()
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

            if (true == generic_setparams<SPIPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
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
            generic_getparams<SPIPlugin>(this, psGetParams);
        }

        /**
          * \brief dispatch commands
        */
        bool doDispatch( const std::string& strCmd, const std::string& strParams, std::stop_token st = {} ) const
        {
            return generic_dispatch<SPIPlugin>(this, strCmd, strParams, st);
        }

        /**
          * \brief get a pointer to the plugin map
        */
        const PluginCommandsMap<SPIPlugin> *getMap(void) const
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
          * \brief get SPI device path
        */
        const char *getSpiDevice (void) const
        {
            return m_strSpiDevice.c_str();
        }

        /**
          * \brief set SPI device path
        */
        void setSpiDevice (const std::string& strSpiDevice) const
        {
            m_strSpiDevice.assign(strSpiDevice);
        }

        /**
          * \brief set SPI mode (0–3, encoding CPOL/CPHA)
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
          * \brief set SPI bus speed in Hz
        */
        bool setSpiSpeedHz (const std::string& strSpeedHz) const
        {
            return numeric::str2uint32(strSpeedHz, m_u32SpiSpeedHz);
        }

        /**
          * \brief set SPI bits per word
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
          * \brief set SPI read timeout
        */
        bool setSpiReadTimeout (const std::string& strReadTimeout) const
        {
            return numeric::str2uint32(strReadTimeout, m_u32ReadTimeout);
        }

        /**
          * \brief set SPI write timeout
        */
        bool setSpiWriteTimeout (const std::string& strWriteTimeout) const
        {
            return numeric::str2uint32(strWriteTimeout, m_u32WriteTimeout);
        }

        /**
          * \brief set SPI read buffer size
        */
        bool setSpiReadBufferSize (const std::string& strReadBufferSize) const
        {
            return numeric::str2uint32(strReadBufferSize, m_u32SpiReadBufferSize);
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
        PluginCommandsMap<SPIPlugin> m_mapCmds;

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
          * \brief the SPI device node (e.g. /dev/spidev0.0)
        */
        mutable std::string m_strSpiDevice;

        /**
          * \brief SPI mode: 0–3 (CPOL/CPHA)
        */
        mutable uint8_t m_u8SpiMode;

        /**
          * \brief SPI bus clock speed in Hz (e.g. 1000000 for 1 MHz)
        */
        mutable uint32_t m_u32SpiSpeedHz;

        /**
          * \brief SPI bits per word (typically 8)
        */
        mutable uint8_t m_u8SpiBitsPerWord;

        /**
          * \brief SPI read timeout in milliseconds
        */
        mutable uint32_t m_u32ReadTimeout;

        /**
          * \brief SPI write timeout in milliseconds
        */
        mutable uint32_t m_u32WriteTimeout;

        /**
          * \brief size of the buffer used for SPI read operations
        */
        mutable uint32_t m_u32SpiReadBufferSize;

        /**
          * \brief functions associated to the plugin commands
        */
        #define SPI_PLUGIN_CMD_RECORD(a, ...)  bool m_SPI_##a ( const std::string& args, std::stop_token st ) const;
        SPI_PLUGIN_COMMANDS_CONFIG_TABLE
        #undef  SPI_PLUGIN_CMD_RECORD
};

#endif /* SPI_PLUGIN_HPP */
