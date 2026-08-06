#ifndef BUSPIRATE_PLUGIN_HPP
#define BUSPIRATE_PLUGIN_HPP

#include "uSharedConfig.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "ICommDriver.hpp"
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
#include <array>
#include <cstdint>  // for uint8_t


// ----- to remove ---
#include "uLogger.hpp"

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR     "BPIRATE     |"
#define LOG_HDR    LOG_STRING(LT_HDR)
// ----- to remove ---


///////////////////////////////////////////////////////////////////
//                          PLUGIN VERSION                       //
///////////////////////////////////////////////////////////////////

#define BUSPIRATE_PLUGIN_VERSION "1.0.0.0"
#define BUSPIRATE_PLUGIN_NAME    "BUSPIRATE"

///////////////////////////////////////////////////////////////////
//                          PLUGIN COMMANDS                      //
///////////////////////////////////////////////////////////////////

// BUSPIRATE_GET_BLOCKING: picks blocking flag when provided, defaults to false.
#ifndef BUSPIRATE_GET_BLOCKING
#define BUSPIRATE_GET_BLOCKING(name, blocking, ...) blocking
#endif

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
        BuspiratePlugin() : m_strVersion
(BUSPIRATE_PLUGIN_VERSION)
                          , m_bIsInitialized(false)
                          , m_bIsEnabled(false)
                          , m_bIsFaultTolerant(false)
                          , m_bIsPrivileged(false)
                          , m_strResultData("")
        {

// PLUGIN COMMANDS
            #define BUSPIRATE_PLUGIN_CMD_RECORD(a, ...) m_mapCmds.insert( std::make_pair(std::string(#a), \
            PluginCommandEntry<BuspiratePlugin>{&BuspiratePlugin::m_Buspirate_##a, BUSPIRATE_GET_BLOCKING(a, ##__VA_ARGS__, false)} ));
            BUSPIRATE_PLUGIN_COMMANDS_CONFIG_TABLE_STD
            #undef BUSPIRATE_PLUGIN_CMD_RECORD

            #define BUSPIRATE_PLUGIN_CMD_RECORD(a, ...) m_mapCmds.insert( std::make_pair(std::string(#a), \
            PluginCommandEntry<BuspiratePlugin>{&BuspiratePlugin::m_Buspirate_##a, BUSPIRATE_GET_BLOCKING(a, ##__VA_ARGS__, false)} ));
            BUSPIRATE_PLUGIN_COMMANDS_CONFIG_TABLE_CMDS
            #undef BUSPIRATE_PLUGIN_CMD_RECORD

// MODES
            #define MODE_CMD_RECORD(a,b,c,d) { mode_s sTmp = {b, c, std::string(#d)}; m_mapModes.insert(std::make_pair(#a, sTmp)); }
            MODE_COMMANDS_CONFIG_TABLE
            #undef MODE_CMD_RECORD

// SPI CONFIGURATION
            #define SPI_CMD_RECORD(a) m_mapCmds_SPI.insert( std::make_pair(std::string(#a), &BuspiratePlugin::m_handle_spi_##a ));
            SPI_COMMANDS_CONFIG_TABLE
            #undef SPI_CMD_RECORD

            #define SPI_SPEED_RECORD(a,b) m_mapSpeed_SPI.insert( std::make_pair(a, b));
            SPI_SPEED_CONFIG_TABLE
            #undef SPI_SPEED_RECORD

// I2C CONFIGURATION
            #define I2C_CMD_RECORD(a) m_mapCmds_I2C.insert( std::make_pair(std::string(#a), &BuspiratePlugin::m_handle_i2c_##a ));
            I2C_COMMANDS_CONFIG_TABLE
            #undef I2C_CMD_RECORD

            #define I2C_SPEED_RECORD(a,b) m_mapSpeed_I2C.insert( std::make_pair(a, b));
            I2C_SPEED_CONFIG_TABLE
            #undef I2C_SPEED_RECORD

// UART CONFIGURATION
            #define UART_CMD_RECORD(a) m_mapCmds_UART.insert( std::make_pair(std::string(#a), &BuspiratePlugin::m_handle_uart_##a ));
            UART_COMMANDS_CONFIG_TABLE
            #undef UART_CMD_RECORD

            #define UART_SPEED_RECORD(a,b) m_mapSpeed_UART.insert( std::make_pair(a, b));
            UART_SPEED_CONFIG_TABLE
            #undef UART_SPEED_RECORD

// RAWWIRE CONFIGURATION
            #define RAWWIRE_CMD_RECORD(a) m_mapCmds_RAWWIRE.insert( std::make_pair(std::string(#a), &BuspiratePlugin::m_handle_rawwire_##a ));
            RAWWIRE_COMMANDS_CONFIG_TABLE
            #undef RAWWIRE_CMD_RECORD

            #define RAWWIRE_SPEED_RECORD(a,b) m_mapSpeed_RAWWIRE.insert( std::make_pair(a, b));
            RAWWIRE_SPEED_CONFIG_TABLE
            #undef RAWWIRE_SPEED_RECORD

// ONEWIRE CONFIGURATION
            #define ONEWIRE_CMD_RECORD(a) m_mapCmds_ONEWIRE.insert( std::make_pair(std::string(#a), &BuspiratePlugin::m_handle_onewire_##a ));
            ONEWIRE_COMMANDS_CONFIG_TABLE
            #undef ONEWIRE_CMD_RECORD

// SPEED MAP OF MAPS
            #define BUSPIRATE_PLUGIN_CMD_RECORD(a) m_mapSpeedsMaps.insert( std::make_pair( std::string(#a), &m_mapSpeed_##a ));
            BUSPIRATE_PLUGIN_COMMANDS_CONFIG_TABLE_CMDS
            #undef BUSPIRATE_PLUGIN_CMD_RECORD

// COMMAND MAP OF MAPS
            #define BUSPIRATE_PLUGIN_CMD_RECORD(a) m_mapCommandsMaps.insert( std::make_pair(std::string(#a), &m_mapCmds_##a ));
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
        bool doDispatch( const std::string& strCmd, const std::string& strParams,
                         std::stop_token st = {} ) const
        {
            return generic_dispatch<BuspiratePlugin>(this, strCmd, strParams, st);
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
            return m_strVersion
;
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
        bool doEnable(void);

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

        ModuleCommandsMap<BuspiratePlugin> *getModuleCmdsMap (const std::string& strModule) const;
        ModuleSpeedMap *getModuleSpeedsMap (const std::string& strModule) const;
        bool generic_uart_send_receive (std::span<const uint8_t> request, std::span<uint8_t> response = std::span<uint8_t>{}, std::span<const uint8_t> expected = std::span<const uint8_t>{}, bool strictCompare = true) const;

        static constexpr uint8_t m_positive_response[] = {0x01};
        mutable uint8_t m_scratch_response[sizeof(m_positive_response)] = {};

        struct sIniValues; // Forward declaration

    private:

        struct IniValues {
            std::string strArtefactsPath{""};
            std::string strUartPort{""};
            uint32_t    u32UartBaudrate{0};
            uint32_t    u32ReadTimeout{0};
            uint32_t    u32WriteTimeout{0};
            uint32_t    u32ReadBufferSize{0};
            uint32_t    u32ScriptDelay{0};
        }m_sIniValues;

        struct mode_s
        {
            const uint8_t iRequest;
            const uint8_t iRepetition;
            const std::string strAnswer;
        };

        using ModesMap = std::map<const std::string, mode_s>;

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
          * \brief plugin privileged mode
        */
        bool m_bIsPrivileged;

        /**
          * \brief UART driver used to communicate with Bus Pirate
        */
        UART m_drvUart;

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
        #define BUSPIRATE_PLUGIN_CMD_RECORD(a, ...)    bool m_Buspirate_##a ( const std::string& args, std::stop_token st ) const;
        BUSPIRATE_PLUGIN_COMMANDS_CONFIG_TABLE_STD
        #undef  BUSPIRATE_PLUGIN_CMD_RECORD

        #define BUSPIRATE_PLUGIN_CMD_RECORD(a)         bool m_Buspirate_##a (const std::string &args, std::stop_token st) const { return generic_module_dispatch<BuspiratePlugin>(this,std::string(#a), args); }
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
        bool m_handle_mode (const std::string &args) const;

        bool m_i2c_read (std::span<uint8_t> response) const;
        bool m_i2c_bulk_write (std::span<const uint8_t> request) const;
        bool m_i2c_probe_address (const uint8_t addr7bit, bool &bAcked) const;
        bool m_i2c_send_bit(uint8_t bit) const;
        bool m_i2c_write_transaction(std::span<const uint8_t> payload) const;
        void m_i2c_flush_rx() const;

        bool m_spi_read (std::span<uint8_t> response) const;
        bool m_spi_bulk_write (std::span<const uint8_t> request) const;
        bool m_spi_cs_enable (bool bEnable) const;

        // Span-based bulk read/write, used by ONEWIRE_CommDriver/RAWWIRE_CommDriver's
        // tout_read()/tout_write() (see class definitions below) — same role as
        // m_spi_read()/m_spi_bulk_write() above, built on the same primitives already
        // used by the CMD handlers in buspirate_onewire.cpp / buspirate_rawwire.cpp.
        bool m_onewire_read (std::span<uint8_t> response) const;
        bool m_onewire_bulk_write (std::span<const uint8_t> request) const;

        bool m_rawwire_read (std::span<uint8_t> response) const;
        bool m_rawwire_bulk_write (std::span<const uint8_t> request) const;

        // UART binary mode has no read command (see buspirate_uart.cpp): once "echo
        // start" is enabled, received bytes stream back with no command framing, so
        // UART_CommDriver::tout_read() reads m_drvUart directly instead of going
        // through one of these — only the bulk-write side needs a helper here.
        bool m_uart_bulk_write (std::span<const uint8_t> request) const;

        bool generic_write_read_file( const uint8_t u8Cmd, const std::string &args ) const;
        bool generic_write_read_data( const uint8_t u8Cmd, const std::string &args ) const;
        bool generic_set_peripheral(const std::string &args) const;
        bool generic_internal_write_read_data(const uint8_t u8Cmd, std::span<const uint8_t> request, std::span<uint8_t> response, bool strictCompare = false) const;
        bool generic_internal_write_read_file( const uint8_t u8Cmd, const std::string& strFileName, const size_t szWriteChunkSize, const size_t szReadChunkSize ) const;
        bool generic_wire_write_data(std::span<const uint8_t> data) const;

        friend const IniValues* getAccessIniValues(const BuspiratePlugin& obj);
        friend bool getEnabledStatus(const BuspiratePlugin& obj);

    public:

        class I2C_CommDriver : public ICommDriver
        {
            public:
                explicit I2C_CommDriver(const BuspiratePlugin& outer)
                    : m_Buspirate(outer) {}

                bool is_open() const override {
                    return m_Buspirate.m_drvUart.is_open();
                }

                ReadResult tout_read([[maybe_unused]] uint32_t u32ReadTimeout,
                                     std::span<uint8_t> buffer,
                                     [[maybe_unused]] const ReadOptions& options,
                                     [[maybe_unused]] std::string_view xtra_params = {}) const override
                {
                    const bool bOk = m_Buspirate.m_i2c_read(buffer);
                    return ReadResult {
                        .status     = bOk ? ICommDriver::Status::SUCCESS : ICommDriver::Status::READ_ERROR,
                        .bytes_read = bOk ? buffer.size() : 0u
                    };
                }

                WriteResult tout_write([[maybe_unused]] uint32_t u32WriteTimeout,
                                       std::span<const uint8_t> buffer,
                                       [[maybe_unused]] std::string_view xtra_params = {}) const override
                {
                    const bool bOk = m_Buspirate.m_i2c_write_transaction(buffer);
                    return WriteResult {
                        .status        = bOk ? ICommDriver::Status::SUCCESS : ICommDriver::Status::WRITE_ERROR,
                        .bytes_written = bOk ? buffer.size() : 0u
                    };
                }

                /**
                 * @brief Describe this connection for the GUI comm-dump panel.
                 * The Buspirate binary-mode I2C protocol carries the slave
                 * address inside the transaction buffer itself, not through
                 * xtra_params, so xtra_params is accepted but ignored — the
                 * label reflects the underlying UART link to the Buspirate.
                 */
                CommDetails describeConnection(std::string_view /*xtra_params*/ = {}) const override
                {
                    return commdump_details(CommFamily::I2C,
                                             "Buspirate I2C " + m_Buspirate.m_sIniValues.strUartPort);
                }

            private:
                const BuspiratePlugin& m_Buspirate;
        };


        // wrapper driver to be used with the script interpreter
        class SPI_CommDriver : ICommDriver 
        {
            public:
                explicit SPI_CommDriver(const BuspiratePlugin& outer)
                    : m_Buspirate(outer) {

                }

                bool is_open() const override {
                    return m_Buspirate.m_drvUart.is_open();
                }

                ReadResult tout_read([[maybe_unused]] uint32_t u32ReadTimeout, std::span<uint8_t> buffer, [[maybe_unused]] const ReadOptions& options, [[maybe_unused]] std::string_view xtra_params = {}) const override {
                    const bool bOk = m_Buspirate.m_spi_read(buffer);
                    return ReadResult {
                        .status     = bOk ? ICommDriver::Status::SUCCESS : ICommDriver::Status::READ_ERROR,
                        .bytes_read = bOk ? buffer.size() : 0u
                    };
                }
                
                WriteResult tout_write([[maybe_unused]] uint32_t u32WriteTimeout, std::span<const uint8_t> buffer, [[maybe_unused]] std::string_view xtra_params = {}) const override {
                    const bool bOk = m_Buspirate.m_spi_bulk_write(buffer);
                    return WriteResult {
                        .status        = bOk ? ICommDriver::Status::SUCCESS : ICommDriver::Status::WRITE_ERROR,
                        .bytes_written = bOk ? buffer.size() : 0u
                    };
                }

                /** @brief Describe this connection for the GUI comm-dump panel (see I2C_CommDriver's note on xtra_params). */
                CommDetails describeConnection(std::string_view /*xtra_params*/ = {}) const override
                {
                    return commdump_details(CommFamily::SPI,
                                             "Buspirate SPI " + m_Buspirate.m_sIniValues.strUartPort);
                }

            private:
                const BuspiratePlugin& m_Buspirate; // reference back to enclosing BuspiratePlugin
        };

        // wrapper driver to be used with the script interpreter
        class ONEWIRE_CommDriver : ICommDriver 
        {
            public:
                explicit ONEWIRE_CommDriver(const BuspiratePlugin& outer)
                    : m_Buspirate(outer) {

                }

                bool is_open() const override {
                    return m_Buspirate.m_drvUart.is_open();
                }

                ReadResult tout_read([[maybe_unused]] uint32_t u32ReadTimeout, std::span<uint8_t> buffer, [[maybe_unused]] const ReadOptions& options, [[maybe_unused]] std::string_view xtra_params = {}) const override {
                    const bool bOk = m_Buspirate.m_onewire_read(buffer);
                    return ReadResult {
                        .status     = bOk ? ICommDriver::Status::SUCCESS : ICommDriver::Status::READ_ERROR,
                        .bytes_read = bOk ? buffer.size() : 0u
                    };
                }
                
                WriteResult tout_write([[maybe_unused]] uint32_t u32WriteTimeout, std::span<const uint8_t> buffer, [[maybe_unused]] std::string_view xtra_params = {}) const override {
                    const bool bOk = m_Buspirate.m_onewire_bulk_write(buffer);
                    return WriteResult {
                        .status        = bOk ? ICommDriver::Status::SUCCESS : ICommDriver::Status::WRITE_ERROR,
                        .bytes_written = bOk ? buffer.size() : 0u
                    };
                }

                /**
                 * @brief Describe this connection for the GUI comm-dump panel.
                 * OneWire doesn't fit UART/I2C/SPI/CAN/NET — classified OTHER.
                 */
                CommDetails describeConnection(std::string_view /*xtra_params*/ = {}) const override
                {
                    return commdump_details(CommFamily::OTHER,
                                             "Buspirate OneWire " + m_Buspirate.m_sIniValues.strUartPort);
                }

            private:
                const BuspiratePlugin& m_Buspirate; // reference back to enclosing BuspiratePlugin
        };

        // wrapper driver to be used with the script interpreter
        class RAWWIRE_CommDriver : ICommDriver 
        {
            public:
                explicit RAWWIRE_CommDriver(const BuspiratePlugin& outer)
                    : m_Buspirate(outer) {

                }

                bool is_open() const override {
                    return m_Buspirate.m_drvUart.is_open();
                }

                ReadResult tout_read([[maybe_unused]] uint32_t u32ReadTimeout, std::span<uint8_t> buffer, [[maybe_unused]] const ReadOptions& options, [[maybe_unused]] std::string_view xtra_params = {}) const override {
                    const bool bOk = m_Buspirate.m_rawwire_read(buffer);
                    return ReadResult {
                        .status     = bOk ? ICommDriver::Status::SUCCESS : ICommDriver::Status::READ_ERROR,
                        .bytes_read = bOk ? buffer.size() : 0u
                    };
                }
                
                WriteResult tout_write([[maybe_unused]] uint32_t u32WriteTimeout, std::span<const uint8_t> buffer, [[maybe_unused]] std::string_view xtra_params = {}) const override {
                    const bool bOk = m_Buspirate.m_rawwire_bulk_write(buffer);
                    return WriteResult {
                        .status        = bOk ? ICommDriver::Status::SUCCESS : ICommDriver::Status::WRITE_ERROR,
                        .bytes_written = bOk ? buffer.size() : 0u
                    };
                }

                /**
                 * @brief Describe this connection for the GUI comm-dump panel.
                 * Raw bit-banged wire doesn't fit UART/I2C/SPI/CAN/NET — classified OTHER.
                 */
                CommDetails describeConnection(std::string_view /*xtra_params*/ = {}) const override
                {
                    return commdump_details(CommFamily::OTHER,
                                             "Buspirate RawWire " + m_Buspirate.m_sIniValues.strUartPort);
                }

            private:
                const BuspiratePlugin& m_Buspirate; // reference back to enclosing BuspiratePlugin
        };

        // wrapper driver to be used with the script interpreter
        class UART_CommDriver : ICommDriver 
        {
            public:
                explicit UART_CommDriver(const BuspiratePlugin& outer)
                    : m_Buspirate(outer) {

                }

                bool is_open() const override {
                    return m_Buspirate.m_drvUart.is_open();
                }

                /**
                 * @brief Raw passthrough read, NOT routed through generic_uart_send_receive().
                 *
                 * Unlike I2C/SPI/1-Wire/RawWire, the Bus Pirate UART binary mode has
                 * no read command byte to frame a request/response around (see
                 * buspirate_uart.cpp / m_handle_uart_mode's "bridge" doc comment):
                 * once UART RX echo is enabled ("UART.CMD echo start"), bytes received
                 * on the target UART are streamed back over the same link with zero
                 * framing, indistinguishable from any other byte on that link. So this
                 * reads m_drvUart directly rather than sending a command byte first.
                 *
                 * PRECONDITION: "UART.CMD echo start" must have been issued before a
                 * script relying on tout_read() runs — this driver has no way to
                 * enable it itself without also affecting the CMD-mode interactive use
                 * of the same connection.
                 */
                ReadResult tout_read([[maybe_unused]] uint32_t u32ReadTimeout, std::span<uint8_t> buffer, const ReadOptions& options, [[maybe_unused]] std::string_view xtra_params = {}) const override {
                    return m_Buspirate.m_drvUart.tout_read(u32ReadTimeout, buffer, options);
                }
                
                WriteResult tout_write([[maybe_unused]] uint32_t u32WriteTimeout, std::span<const uint8_t> buffer, [[maybe_unused]] std::string_view xtra_params = {}) const override {
                    const bool bOk = m_Buspirate.m_uart_bulk_write(buffer);
                    return WriteResult {
                        .status        = bOk ? ICommDriver::Status::SUCCESS : ICommDriver::Status::WRITE_ERROR,
                        .bytes_written = bOk ? buffer.size() : 0u
                    };
                }

                /**
                 * @brief Describe this connection for the GUI comm-dump panel.
                 * Buspirate's UART bridge mode is, itself, a serial passthrough.
                 */
                CommDetails describeConnection(std::string_view /*xtra_params*/ = {}) const override
                {
                    return commdump_details(CommFamily::SERIAL,
                                             "Buspirate UART " + m_Buspirate.m_sIniValues.strUartPort);
                }

            private:
                const BuspiratePlugin& m_Buspirate; // reference back to enclosing BuspiratePlugin
        };
};

#endif // BUSPIRATE_PLUGIN_HPP