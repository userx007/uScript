#include "uSharedConfig.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include "dspkspi_setup.hpp"
#include "dspkspi_plugin.hpp"

#include "uPluginSettings.hpp"

#include "uNumeric.hpp"
#include "uFile.hpp"
#include "uString.hpp"
#include "uHexlify.hpp"
#include "uDigisparkSPI.hpp"
#include "uCommandExec.hpp"


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR     "DSPKSPI     |"
#define LOG_HDR    LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                 //
///////////////////////////////////////////////////////////////////

#define    ARTEFACTS_PATH     "ARTEFACTS_PATH"
#define    SPI_VID            "SPI_VID"
#define    SPI_PID            "SPI_PID"
#define    SPI_MODE           "SPI_MODE"
#define    SPI_CLOCK_DIV      "SPI_CLOCK_DIV"
#define    READ_TIMEOUT       "READ_TIMEOUT"
#define    WRITE_TIMEOUT      "WRITE_TIMEOUT"
#define    READ_BUF_SIZE      "READ_BUF_SIZE"

///////////////////////////////////////////////////////////////////
//                          PLUGIN ENTRY POINT                   //
///////////////////////////////////////////////////////////////////


/**
  * \brief The plugin's entry points
*/
extern "C"
{
    EXPORTED DSPKSPIPlugin* pluginEntry()
    {
        return new DSPKSPIPlugin();
    }

    EXPORTED void pluginExit( DSPKSPIPlugin *ptrPlugin)
    {
        if (nullptr != ptrPlugin)
        {
            delete ptrPlugin;
        }
    }
}


///////////////////////////////////////////////////////////////////
//                          INIT / CLEANUP                       //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Function where to execute initialization of sub-modules
*/
/*--------------------------------------------------------------------------------------------------------*/

bool DSPKSPIPlugin::doInit(void *pvUserData)
{
    m_bIsInitialized = true;
    return m_bIsInitialized;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Function where to execute de-initialization of sub-modules
*/
/*--------------------------------------------------------------------------------------------------------*/

void DSPKSPIPlugin::doCleanup(void)
{
    m_bIsInitialized = false;
    m_bIsEnabled     = false;
}

///////////////////////////////////////////////////////////////////
//                          COMMAND HANDLERS                     //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief INFO command implementation; shows details about plugin and
  *        describes the supported functions with examples of usage.
  *        This command takes no arguments and is executed even if the plugin initialization fails.
  *
  * \note Usage example:
  *       DSPKSPI.INFO
  *
  * \param[in] args  empty string expected
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool DSPKSPIPlugin::m_DSPKSPI_INFO (const std::string &args, std::stop_token st) const
{
    // expected no arguments
    if (!args.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected no argument(s)"));
        return false;
    }

    // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
    if (!m_bIsEnabled)
    {
        return true;
    }

    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING(DSPKSPI_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: communicate with devices via SPI through a Digispark ATtiny85 USB bridge"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : configure USB VID/PID and SPI parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args : [vid=<hex>] [pid=<hex>] [m=<0-3>] [d=<0-3>] [r=<ms>] [w=<ms>] [s=<bytes>]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         vid – USB Vendor ID  (hex, default 16C0)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         pid – USB Product ID (hex, default 05DF)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         m   – SPI mode 0-3 (CPOL/CPHA, default 0)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         d   – Clock divider: 0=Div2 1=Div4 2=Div8 3=Div16 (default 1)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         r   – Read  timeout [ms] (default 2000)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         w   – Write timeout [ms] (default 2000)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         s   – Read buffer size [bytes] (default 6)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage: DSPKSPI.CONFIG m=0 d=1 r=2000 w=2000 s=6"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("       DSPKSPI.CONFIG vid=16C0 pid=05DF m=1 d=2"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : send commands from a file"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args : script"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage: DSPKSPI.SCRIPT script.txt"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD  : send, receive or both (full-duplex SPI)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args : direction message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage: DSPKSPI.CMD > H\"AABBCCDD\" | ok"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("       DSPKSPI.CMD < \"Please send!\" | F\"data.bin, 6\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note : ReadMode::UntilToken triggers full-duplex transfer (MOSI=token bytes)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note : ReadMode::Exact clocks dummy 0x00 bytes on MOSI and captures MISO"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note : ReadMode::UntilDelimiter is not supported on SPI"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC : send one or more periodic messages"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args : time1 val1 [id1], time2 val2 [id2], ... (time_i in ms, val_i hex)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage: DSPKSPI.CYCLIC 100 AABBCCDD, 250 06"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("       DSPKSPI.CYCLIC 100 AABBCCDD, 250 06 &"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note : id has no meaning here (point-to-point bus) and is always omitted"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note : without '&' sends one full pattern (lcm of the time_i) then returns;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("       with '&' repeats forever until the script/thread is stopped"));
    LOG_SEP();

    return true;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CONFIG command implementation; overwrite the current SPI/USB configuration.
  *
  * \note If an empty string is provided then the command doesn't change anything.
  *
  * \note Usage example:
  *       DSPKSPI.CONFIG m=0 d=1 r=2000 w=2000 s=6
  *       DSPKSPI.CONFIG vid=16C0 pid=05DF m=1 d=2
  *
  * \param[in] args  [vid=<hex>] [pid=<hex>] [m=<0-3>] [d=<0-3>] [r=<ms>] [w=<ms>] [s=<bytes>]
  * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool DSPKSPIPlugin::m_DSPKSPI_CONFIG (const std::string &args, std::stop_token st) const
{
    return generic_spi_set_params<DSPKSPIPlugin>(this, args);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CMD command implementation.
  *
  * Opens an SPIBridge instance (RAII), applies the current mode/divider
  * configuration, then dispatches the command through
  * CommScriptCommandInterpreter – identical pattern to the UART plugin.
  *
  * Because SPIBridge::tout_read maps ReadMode::UntilToken to a
  * full-duplex CMD_SPI_TRANSFER and ReadMode::Exact to CMD_SPI_READ,
  * the generic interpreter works without modification.
  * ReadMode::UntilDelimiter is rejected by SPIBridge with
  * Status::INVALID_PARAM.
  *
  * \note Usage example:
  *       DSPKSPI.CMD > H"AABBCCDD" | ok
  *       DSPKSPI.CMD < "Please send!" | F"data.bin, 6"
  *
  * \param[in] args  direction + message tokens
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool DSPKSPIPlugin::m_DSPKSPI_CMD (const std::string &args, std::stop_token st) const
{
    (void)st;

    return ucmdexec::generic_cmd(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<SPIBridge> {
            // open the SPI bridge (RAII – close is done by destructor)
            auto shpDriver = std::make_shared<SPIBridge>(m_u16Vid, m_u16Pid);

            if (!shpDriver->is_open()) {
                return nullptr;
            }

            // apply SPI clock configuration before the first transfer
            ICommDriver::Status cfgStatus = shpDriver->configure(m_eSpiMode, m_eClockDiv);
            if (cfgStatus != ICommDriver::Status::SUCCESS) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI configure failed:"); LOG_STRING(ICommDriver::to_string(cfgStatus)));
                return nullptr;
            }

            return shpDriver;
        },
        DSPKSPI_PLUGIN_NAME,
        m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, &m_strResultData);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief SCRIPT command implementation.
  *
  * \note Usage example:
  *       DSPKSPI.SCRIPT scriptname [|delay]
  *
  * \param[in] args  filename<string> [delay<size_t>]
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool DSPKSPIPlugin::m_DSPKSPI_SCRIPT (const std::string &args, std::stop_token st) const
{
    (void)st;

    return ucmdexec::generic_script(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<SPIBridge> {
            // open the SPI bridge (RAII – close is done by destructor)
            auto shpDriver = std::make_shared<SPIBridge>(m_u16Vid, m_u16Pid);

            if (!shpDriver->is_open()) {
                return nullptr;
            }

            // apply SPI clock configuration before the first transfer
            ICommDriver::Status cfgStatus = shpDriver->configure(m_eSpiMode, m_eClockDiv);
            if (cfgStatus != ICommDriver::Status::SUCCESS) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI configure failed:"); LOG_STRING(ICommDriver::to_string(cfgStatus)));
                return nullptr;
            }

            return shpDriver;
        },
        DSPKSPI_PLUGIN_NAME,
        m_strArtefactsPath, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CYCLIC command implementation; send one or more periodic DSPKSPI messages.
  *
  * \note The SPI bridge is opened once for the whole CYCLIC session (like SCRIPT) and closed
  *       automatically on return (RAII). DSPKSPI is a point-to-point bus with no addressable
  *       channels, so each entry's optional "id" is never sent on the wire — omit it — and
  *       "val" is the payload as a plain hex string (e.g. "AABBCCDD").
  *
  * \note Usage example:
  *       DSPKSPI.CYCLIC 100 AABBCCDD, 250 06
  *       DSPKSPI.CYCLIC 100 AABBCCDD, 250 06 &
  *
  * \param[in] args  "time1 val1 , time2 val2 , ..." (see generic_send_cyclic())
  * \param[in] st    stop_token; forwarded as-is (present/absent '&' selects run-once vs. forever)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool DSPKSPIPlugin::m_DSPKSPI_CYCLIC (const std::string &args, std::stop_token st) const
{
    return ucmdexec::generic_send_cyclic(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<SPIBridge> {
            // open the SPI bridge (RAII – close is done by destructor)
            auto shpDriver = std::make_shared<SPIBridge>(m_u16Vid, m_u16Pid);

            if (!shpDriver->is_open()) {
                return nullptr;
            }

            // apply SPI clock configuration before the first transfer
            ICommDriver::Status cfgStatus = shpDriver->configure(m_eSpiMode, m_eClockDiv);
            if (cfgStatus != ICommDriver::Status::SUCCESS) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SPI configure failed:"); LOG_STRING(ICommDriver::to_string(cfgStatus)));
                return nullptr;
            }

            return shpDriver;
        },
        DSPKSPI_PLUGIN_NAME, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, st);
}


///////////////////////////////////////////////////////////////////
//            PRIVATE INTERFACES IMPLEMENTATION                  //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Load plugin-specific settings from the INI file map.
*/
/*--------------------------------------------------------------------------------------------------------*/

bool DSPKSPIPlugin::m_LocalSetParams( const PluginDataSet *psSetParams)
{
    if (true == psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        return true;
    }

    PluginSettingsBinder sSettings;
    sSettings.Bind(ARTEFACTS_PATH, m_strArtefactsPath);
    sSettings.Bind(SPI_VID,        [this](const std::string& v) { return setSpiVid(v); });
    sSettings.Bind(SPI_PID,        [this](const std::string& v) { return setSpiPid(v); });
    sSettings.Bind(SPI_MODE,       [this](const std::string& v) { return setSpiMode(v); });
    sSettings.Bind(SPI_CLOCK_DIV,  [this](const std::string& v) { return setSpiClockDiv(v); });
    sSettings.Bind(READ_TIMEOUT,   m_u32ReadTimeout);
    sSettings.Bind(WRITE_TIMEOUT,  m_u32WriteTimeout);
    sSettings.Bind(READ_BUF_SIZE,  m_u32ReadBufferSize);

    return sSettings.Apply(psSetParams->mapSettings,
        [](const std::string& strKey, const std::string& strRawValue) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strKey); LOG_STRING(":"); LOG_STRING(strRawValue));
        });

} /* m_LocalSetParams() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief message sender – thin wrapper around ICommDriver::tout_write
*/
/*--------------------------------------------------------------------------------------------------------*/

bool DSPKSPIPlugin::m_Send( std::span<const uint8_t> dataSpan, std::shared_ptr<const ICommDriver> shpDriver ) const
{
    auto result = shpDriver->tout_write(m_u32WriteTimeout, dataSpan);

    if (result.status != ICommDriver::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Write failed:");
                  LOG_STRING(ICommDriver::to_string(result.status));
                  LOG_STRING("Bytes written:"); LOG_SIZET(result.bytes_written));
        return false;
    }

    return true;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief message receiver – maps CommCommandReadType to ICommDriver::ReadOptions.
  *
  * SPI note: ReadMode::UntilDelimiter is not meaningful for SPI and SPIBridge
  * returns Status::INVALID_PARAM.  If the script/command uses LINE mode the
  * error will be reported through the standard log path below.
*/
/*--------------------------------------------------------------------------------------------------------*/

bool DSPKSPIPlugin::m_Receive( std::span<uint8_t> dataSpan, size_t& szSize, CommCommandReadType readType, std::shared_ptr<const ICommDriver> shpDriver ) const
{
    bool bRetVal = false;
    ICommDriver::ReadOptions options;

    switch(readType)
    {
        case CommCommandReadType::LINE:
            // Not supported on SPI – SPIBridge will return INVALID_PARAM.
            // Mapped here for structural symmetry with the UART plugin;
            // the error is surfaced through the result.status check below.
            options.mode = ICommDriver::ReadMode::UntilDelimiter;
            options.delimiter = '\n';
            break;

        case CommCommandReadType::TOKEN_STRING:
            [[fallthrough]];
        case CommCommandReadType::TOKEN_HEXSTREAM:
            // Full-duplex: token bytes are clocked on MOSI, MISO captured in buffer.
            options.mode = ICommDriver::ReadMode::UntilToken;
            options.token = dataSpan;
            options.use_buffer = true;
            break;

        default:
            // Exact: dummy 0x00 bytes on MOSI, capture MISO.
            options.mode = ICommDriver::ReadMode::Exact;
            break;
    }

    auto result = shpDriver->tout_read(m_u32ReadTimeout, dataSpan, options);

    if (result.status == ICommDriver::Status::SUCCESS) {
        szSize   = result.bytes_read;
        bRetVal  = true;
    } else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Read failed:");
                  LOG_STRING(ICommDriver::to_string(result.status));
                  LOG_STRING("Bytes read:"); LOG_SIZET(result.bytes_read));
        szSize  = result.bytes_read;
        bRetVal = false;
    }

    return bRetVal;
}
