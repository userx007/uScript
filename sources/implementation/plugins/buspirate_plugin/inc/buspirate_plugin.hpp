#ifndef BUSPIRATE_PLUGIN_HPP
#define BUSPIRATE_PLUGIN_HPP

#include "CommonSettings.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "PluginOperations.hpp"
#include "PluginExport.hpp"

#include "buspirate_generic.hpp"
#include "spi_config.hpp"
#include "i2c_config.hpp"
#include "uart_config.hpp"
#include "onewire_config.hpp"
#include "rawwire_config.hpp"
#include "mode_config.hpp"

#include "uUart.hpp"

#include <span>

///////////////////////////////////////////////////////////////////
//                          PLUGIN VERSION                       //
///////////////////////////////////////////////////////////////////

#define BUSPIRATE_PLUGIN_VERSION "1.8.0.0"

///////////////////////////////////////////////////////////////////
//                          PLUGIN COMMANDS                      //
///////////////////////////////////////////////////////////////////

#define BUSPIRATE_PLUGIN_COMMANDS_CONFIG_TABLE_STD     \
BUSPIRATE_PLUGIN_CMD_RECORD( INFO                    ) \
BUSPIRATE_PLUGIN_CMD_RECORD( MODE                    ) \

#define BUSPIRATE_PLUGIN_COMMANDS_CONFIG_TABLE_CMDS    \
BUSPIRATE_PLUGIN_CMD_RECORD( ONEWIRE                 ) \
BUSPIRATE_PLUGIN_CMD_RECORD( SPI                     ) \
BUSPIRATE_PLUGIN_CMD_RECORD( I2C                     ) \
BUSPIRATE_PLUGIN_CMD_RECORD( UART                    ) \
BUSPIRATE_PLUGIN_CMD_RECORD( RAWWIRE                 ) \


///////////////////////////////////////////////////////////////////
//                          PLUGIN INTERFACE                     //
///////////////////////////////////////////////////////////////////

/**
  * \brief Buspirate plugin class definition
*/
class BuspiratePlugin: public PluginInterface
{
    public:

        /**
          * \brief class constructor
        */
        BuspiratePlugin() : m_strPluginVersion(BUSPIRATE_PLUGIN_VERSION)
                          , m_bIsInitialized(false)
                          , m_bIsEnabled(false)
                          , m_bIsFaultTolerant(false)
                          , m_bIsPrivileged(false)
                          , m_strResultData("")
                          , m_strArtefactsPath("")
                          , m_strUartPort("")
                          , m_u32UartBaudrate(0)
                          , m_u32ReadTimeout(0)
                          , m_u32WriteTimeout(0)
                          , m_u32UartReadBufferSize(0)
        {

// PLUGIN COMMANDS
            #define BUSPIRATE_PLUGIN_CMD_RECORD(a) m_mapCmds.insert( std::make_pair( #a, &BuspiratePlugin::m_Buspirate_##a ));
            BUSPIRATE_PLUGIN_COMMANDS_CONFIG_TABLE_STD
            #undef BUSPIRATE_PLUGIN_CMD_RECORD

            #define BUSPIRATE_PLUGIN_CMD_RECORD(a) m_mapCmds.insert( std::make_pair( #a, &BuspiratePlugin::m_Buspirate_##a ));
            BUSPIRATE_PLUGIN_COMMANDS_CONFIG_TABLE_CMDS
            #undef BUSPIRATE_PLUGIN_CMD_RECORD

// MODES
            #define MODE_CMD_RECORD(a,b,c,d) { mode_s sTmp = {b, c, #d}; m_mapModes.insert(std::make_pair(#a, sTmp)); }
            MODE_COMMANDS_CONFIG_TABLE
            #undef MODE_CMD_RECORD

// SPI CONFIGURATION
            #define SPI_CMD_RECORD(a) m_mapCmds_SPI.insert( std::make_pair( #a, &BuspiratePlugin::m_handle_spi_##a ));
            SPI_COMMANDS_CONFIG_TABLE
            #undef SPI_CMD_RECORD

            #define SPI_SPEED_RECORD(a,b) m_mapSpeed_SPI.insert( std::make_pair( a, b ));
            SPI_SPEED_CONFIG_TABLE
            #undef SPI_SPEED_RECORD

// I2C CONFIGURATION
            #define I2C_CMD_RECORD(a) m_mapCmds_I2C.insert( std::make_pair( #a, &BuspiratePlugin::m_handle_i2c_##a ));
            I2C_COMMANDS_CONFIG_TABLE
            #undef I2C_CMD_RECORD

            #define I2C_SPEED_RECORD(a,b) m_mapSpeed_I2C.insert( std::make_pair( a, b ));
            I2C_SPEED_CONFIG_TABLE
            #undef I2C_SPEED_RECORD

// UART CONFIGURATION
            #define UART_CMD_RECORD(a) m_mapCmds_UART.insert( std::make_pair( #a, &BuspiratePlugin::m_handle_uart_##a ));
            UART_COMMANDS_CONFIG_TABLE
            #undef UART_CMD_RECORD

            #define UART_SPEED_RECORD(a,b) m_mapSpeed_UART.insert( std::make_pair( a, b ));
            UART_SPEED_CONFIG_TABLE
            #undef UART_SPEED_RECORD

// RAWWIRE CONFIGURATION
            #define RAWWIRE_CMD_RECORD(a) m_mapCmds_RAWWIRE.insert( std::make_pair( #a, &BuspiratePlugin::m_handle_rawwire_##a ));
            RAWWIRE_COMMANDS_CONFIG_TABLE
            #undef RAWWIRE_CMD_RECORD

            #define RAWWIRE_SPEED_RECORD(a,b) m_mapSpeed_RAWWIRE.insert( std::make_pair( a, b ));
            RAWWIRE_SPEED_CONFIG_TABLE
            #undef RAWWIRE_SPEED_RECORD

// ONEWIRE CONFIGURATION
            #define ONEWIRE_CMD_RECORD(a) m_mapCmds_ONEWIRE.insert( std::make_pair( #a, &BuspiratePlugin::m_handle_onewire_##a ));
            ONEWIRE_COMMANDS_CONFIG_TABLE
            #undef ONEWIRE_CMD_RECORD

// SPEED MAP OF MAPS
            #define BUSPIRATE_PLUGIN_CMD_RECORD(a) m_mapSpeedsMaps.insert( std::make_pair(  std::string(#a), &m_mapSpeed_##a ));
            BUSPIRATE_PLUGIN_COMMANDS_CONFIG_TABLE_CMDS
            #undef BUSPIRATE_PLUGIN_CMD_RECORD

// COMMAND MAP OF MAPS
            #define BUSPIRATE_PLUGIN_CMD_RECORD(a) m_mapCommandsMaps.insert( std::make_pair( #a, &m_mapCmds_##a ));
            BUSPIRATE_PLUGIN_COMMANDS_CONFIG_TABLE_CMDS
            #undef BUSPIRATE_PLUGIN_CMD_RECORD

        }

        /**
          * \brief class destructor
        */
        ~BuspiratePlugin()
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
        bool isEnabled ( void ) const
        {
            return m_bIsEnabled;
        }

        /**
          * \brief Import external settings into the plugin
        */
        bool setParams( const PluginDataSet *psSetParams )
        {
            bool bRetVal = false;

            if (true == generic_setparams<BuspiratePlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
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
            generic_getparams<BuspiratePlugin>(this, psGetParams);
        }

        /**
          * \brief dispatch commands
        */
        bool doDispatch( const std::string& strCmd, const std::string& strParams ) const
        {
            return generic_dispatch<BuspiratePlugin>(this, strCmd, strParams);
        }

        /**
          * \brief get a pointer to the plugin map
        */
        const PluginCommandsMap<BuspiratePlugin> *getMap(void) const
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
        bool isFaultTolerant ( void ) const
        {
            return m_bIsFaultTolerant;
        }

        /**
          * \brief get the privileged status
        */
        bool isPrivileged ( void ) const
        {
            return false;
        }

        ModuleCommandsMap<BuspiratePlugin> *getModuleCmdsMap ( const std::string& strModule ) const;
        ModuleSpeedMap *getModuleSpeedsMap ( const std::string& strModule ) const;
        bool generic_uart_send_receive(std::span<uint8_t> request, std::span<const uint8_t> expect = {}) const;

        static constexpr std::array<uint8_t, 1> positive_val{ 0x01 };
        static constexpr std::span<const uint8_t> g_positive_answer{ positive_val };

    private:

        struct mode_s
        {
            int  iRequest;
            int  iRepetition;
            const std::string& strAnswer;
        };

        using ModesMap = std::map<std::string, mode_s>;

        const size_t m_CS_ENABLE = 0;
        const size_t m_CS_DISABLE = 1;
        const uint8_t m_CMD_SPI_WRRD = 0x04;
        const uint8_t m_CMD_I2C_WRRD = 0x08;

        /**
          * \brief map with association between the command string and the execution function
        */
        PluginCommandsMap<BuspiratePlugin> m_mapCmds;

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
          * \brief plugin privileged mode
        */
        bool m_bIsPrivileged;

        /**
          * \brief the artefacts path
        */
        std::string m_strArtefactsPath;

        /**
          * \brief the UART port
        */
        std::string m_strUartPort;

        /**
          * \brief the UART baudrate in used intialized from u32UartBaudrateStart
        */
        uint32_t m_u32UartBaudrate;

        /**
          * \brief the UART read timeout
        */
        uint32_t m_u32ReadTimeout;

        /**
          * \brief the UART write timeout
        */
        uint32_t m_u32WriteTimeout;

        /**
          * \brief the UART buffer size
        */
        uint32_t m_u32UartReadBufferSize;


        UART drvUart;

// MODE SPECIFIC
        ModesMap m_mapModes;

// COMMON MODULE SPECIFIC
        SpeedsMapsMap m_mapSpeedsMaps;
        CommandsMapsMap<BuspiratePlugin> m_mapCommandsMaps;


// SPI MODULE SPECIFIC

        /**
          * \brief map with association between the command string and the execution function
        */
        ModuleCommandsMap<BuspiratePlugin> m_mapCmds_SPI;

        /**
          * \brief map with association speed descriptor and speed value
        */
        ModuleSpeedMap m_mapSpeed_SPI;


// I2C MODULE SPECIFIC

        /**
          * \brief map with association between the command string and the execution function
        */
        ModuleCommandsMap<BuspiratePlugin> m_mapCmds_I2C;

        /**
          * \brief map with association speed descriptor and speed value
        */
        ModuleSpeedMap m_mapSpeed_I2C;

// UART MODULE SPECIFIC

        /**
          * \brief map with association between the command string and the execution function
        */
        ModuleCommandsMap<BuspiratePlugin> m_mapCmds_UART;

        /**
          * \brief map with association speed descriptor and speed value
        */
        ModuleSpeedMap m_mapSpeed_UART;


// RAWWIRE MODULE SPECIFIC

        /**
          * \brief map with association between the command string and the execution function
        */
        ModuleCommandsMap<BuspiratePlugin> m_mapCmds_RAWWIRE;

        /**
          * \brief map with association speed descriptor and speed value
        */
        ModuleSpeedMap m_mapSpeed_RAWWIRE;


// ONEWIRE MODULE SPECIFIC

        /**
          * \brief map with association between the command string and the execution function
        */
        ModuleCommandsMap<BuspiratePlugin> m_mapCmds_ONEWIRE;

        /**
          * \brief map with association speed descriptor and speed value
        */
        ModuleSpeedMap m_mapSpeed_ONEWIRE;


// PLUGIN COMMANDS DECLARATION

        /**
          * \brief functions associated to the plugin commands
        */
        #define BUSPIRATE_PLUGIN_CMD_RECORD(a)         bool m_Buspirate_##a (const std::string &args) const;
        BUSPIRATE_PLUGIN_COMMANDS_CONFIG_TABLE_STD
        #undef  BUSPIRATE_PLUGIN_CMD_RECORD

        #define BUSPIRATE_PLUGIN_CMD_RECORD(a)         bool m_Buspirate_##a (const std::string &args) const { return generic_module_dispatch<BuspiratePlugin>(this, #a, args); }
        BUSPIRATE_PLUGIN_COMMANDS_CONFIG_TABLE_CMDS
        #undef  BUSPIRATE_PLUGIN_CMD_RECORD

// SPI MODULE COMMANDS DECLARATION

        #define SPI_CMD_RECORD(a)                      bool m_handle_spi_##a (const std::string &args) const;
        SPI_COMMANDS_CONFIG_TABLE
        #undef  SPI_CMD_RECORD

// I2C MODULE COMMANDS DECLARATION

        #define I2C_CMD_RECORD(a)                      bool m_handle_i2c_##a (const std::string &args) const;
        I2C_COMMANDS_CONFIG_TABLE
        #undef  I2C_CMD_RECORD

// UART MODULE COMMANDS DECLARATION

        #define UART_CMD_RECORD(a)                     bool m_handle_uart_##a (const std::string &args) const;
        UART_COMMANDS_CONFIG_TABLE
        #undef  UART_CMD_RECORD

// RAWWIRE MODULE COMMANDS DECLARATION

        #define RAWWIRE_CMD_RECORD(a)                  bool m_handle_rawwire_##a (const std::string &args) const;
        RAWWIRE_COMMANDS_CONFIG_TABLE
        #undef  RAWWIRE_CMD_RECORD

// ONEWIRE MODULE COMMANDS DECLARATION

        #define ONEWIRE_CMD_RECORD(a)                  bool m_handle_onewire_##a (const std::string &args) const;
        ONEWIRE_COMMANDS_CONFIG_TABLE
        #undef  ONEWIRE_CMD_RECORD


        bool m_LocalSetParams( const PluginDataSet *psSetParams);

        bool m_handle_mode(const std::string &args) const;
        bool m_spi_cs_enable ( const size_t iEnable  ) const;
        bool m_i2c_bulk_write ( const uint8_t *pu8Data, const size_t szLen ) const;
        bool m_spi_bulk_write ( const uint8_t *pu8Data, const size_t szLen ) const;
        bool m_handle_wrrd(const std::string &args) const;

        bool generic_write_read_file( const uint8_t u8Cmd, const std::string &args ) const;
        bool generic_write_read_data( const uint8_t u8Cmd, const std::string &args ) const;
        bool generic_set_peripheral(const std::string &args) const;
        bool generic_internal_write_read_data( const uint8_t u8Cmd, const size_t szWriteSize, const size_t szReadSize, std::vector<uint8_t>& data ) const;
        bool generic_internal_write_read_file( const uint8_t u8Cmd, const std::string& strFileName, const size_t iWriteChunkSize, const size_t iReadChunkSize ) const;
        bool generic_wire_write_data( const uint8_t *pu8Data, const size_t szLen ) const;
};

#endif // BUSPIRATE_PLUGIN_HPP