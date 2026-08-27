#include "uSharedConfig.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include "ki2c_setup.hpp"
#include "ki2c_plugin.hpp"

#include "uPluginSettings.hpp"

#include "uNumeric.hpp"
#include "uFile.hpp"
#include "uString.hpp"
#include "uHexlify.hpp"
#include "uKI2C.hpp"
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
#define LT_HDR     "KKI2C        |"
#define LOG_HDR    LOG_STRING(LT_HDR)

/**
  * \brief The plugin's entry points
*/
extern "C"
{
    EXPORTED KI2CPlugin* pluginEntry()
    {
        return new KI2CPlugin();
    }

    EXPORTED void pluginExit( KI2CPlugin *ptrPlugin)
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

bool KI2CPlugin::doInit(void *pvUserData)
{
    m_bIsInitialized = true;
    return m_bIsInitialized;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Function where to execute de-initialization of sub-modules
*/
/*--------------------------------------------------------------------------------------------------------*/

void KI2CPlugin::doCleanup(void)
{
    m_bIsInitialized = false;
    m_bIsEnabled     = false;
}

///////////////////////////////////////////////////////////////////
//                          COMMAND HANDLERS                     //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief INFO command implementation; shows details about the plugin and
  *        describes the supported functions with examples of usage.
  *        This command takes no arguments and is executed even if plugin initialization fails.
  *
  * \note Usage example:
  *       KI2C.INFO
  *
  * \param[in] args  empty string (no arguments expected)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool KI2CPlugin::m_KI2C_INFO (const std::string &args, std::stop_token st) const
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
    LOG_PRINT(LOG_EMPTY, LOG_STRING(KI2C_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: communicate with devices via KI2C (/dev/i2c-N)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the KI2C device, slave address and transfer parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [d=device] [a=address] [r=read_tout] [w=write_tout] [s=recv_bufsize]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : KI2C.CONFIG d=/dev/i2c-1 a=0x48 r=2000 w=2000 s=256"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         KI2C.CONFIG d=/dev/i2c-0 a=72"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : send commands from a script file"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : script"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : KI2C.SCRIPT script.txt"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : send, receive or both"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : direction message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : KI2C.CMD > H\"AABBCCDD\" | ok"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         KI2C.CMD < \"Please send!\" | F\"data.bin, 256\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : can be both sent/received: (un)quoted strings, hex lines"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : can be only sent: files, only received: tokens"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC : send one or more periodic messages"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : time1 val1 [id1], time2 val2 [id2], ... (time_i in ms, val_i hex)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : KI2C.CYCLIC 100 AABBCCDD 0x50, 250 1122 0x51"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         KI2C.CYCLIC 100 AABBCCDD 0x50, 250 1122 0x51 &"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : id is optional; when omitted, falls back to the address set via CONFIG"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : without '&' sends one full pattern (lcm of the time_i) then returns;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         with '&' repeats forever until the script/thread is stopped"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("INI file parameters (copy/paste into your ini file):"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("[KI2C]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("ARTEFACTS_PATH =             # directory used by SCRIPT/CMD/wrrdf for reading/writing artefact files"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("I2C_DEVICE     = /dev/i2c-1  # Linux i2c-dev device node"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("I2C_ADDRESS    = 0x50        # 7-bit I2C slave address"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_TIMEOUT   = 2000        # read timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("WRITE_TIMEOUT  = 2000        # write timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_BUF_SIZE  = 1024        # size in bytes of the local read buffer"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RAW_RESULT     = false       # CMD returns raw bytes instead of a hexlified string when true"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC_CACHED  = true        # true=validate/parse each CYCLIC entry once per session; false=re-resolve every tick (needed for volatile ?= macros)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note: the CONFIG command above can override a subset of these at runtime;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("      any key not accepted by CONFIG must be set via the ini file."));


    return true;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CONFIG command implementation; overwrite the current KI2C parameters at runtime.
  *
  * \note Any subset of parameters can be specified; omitted keys retain their current values.
  *
  * \note Usage example:
  *       KI2C.CONFIG d=/dev/i2c-1 a=0x48 r=2000 w=2000 s=256
  *       KI2C.CONFIG d=/dev/i2c-0 a=72 r=1000
  *
  * \param[in] args  [d=device] [a=address] [r=read_tout] [w=write_tout] [s=recv_bufsize]
  *
  * \return true if parameters were updated successfully, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool KI2CPlugin::m_KI2C_CONFIG (const std::string &args, std::stop_token st) const
{
    return generic_i2c_set_params<KI2CPlugin>(this, args);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CMD command implementation; execute a single send/receive operation over KI2C.
  *
  * \note The KI2C device is opened for the duration of the call and closed automatically on return (RAII).
  *
  * \note Usage example:
  *       KI2C.CMD > H\"AABBCCDD\" | H\"06\"
  *       KI2C.CMD < \"Ready\" | \"Go!\"
  *
  * \param[in] args  direction and data expression (see CommScriptCommandValidator grammar)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool KI2CPlugin::m_KI2C_CMD (const std::string &args, std::stop_token st) const
{
    (void)st;

    return ucmdexec::generic_cmd(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<KI2C> {
            // Open the KI2C device (RAII — closed automatically by destructor)
            auto shpDriver = std::make_shared<KI2C>(m_strKI2CDevice, m_u8KI2CAddress, m_strKI2CDevice);
            return shpDriver->is_open() ? shpDriver : nullptr;
        },
        m_strInstanceName,
        m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, &m_strResultData, m_bRawResult);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief SCRIPT command implementation; execute a multi-command script file over KI2C.
  *
  * \note Usage example:
  *       KI2C.SCRIPT init_sequence.txt
  *       KI2C.SCRIPT sensor_poll.txt 100
  *
  * \param[in] args  filename [delay_ms]
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool KI2CPlugin::m_KI2C_SCRIPT (const std::string &args, std::stop_token st) const
{
    (void)st;

    return ucmdexec::generic_script(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<KI2C> {
            // Open the KI2C device (RAII — closed automatically by destructor)
            auto shpDriver = std::make_shared<KI2C>(m_strKI2CDevice, m_u8KI2CAddress, m_strKI2CDevice);
            return shpDriver->is_open() ? shpDriver : nullptr;
        },
        m_strInstanceName,
        m_strArtefactsPath, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CYCLIC command implementation; send one or more periodic KI2C messages.
  *
  * \note The KI2C device is opened once for the whole CYCLIC session (like SCRIPT) and closed
  *       automatically on return (RAII). Each entry's optional "id" is the I2C slave address
  *       (decimal or 0x-hex, same syntax KI2C::tout_write()'s xtra_params already accepts — an
  *       empty id falls back to the address set via CONFIG) and "val" is the payload as a plain
  *       hex string (e.g. "AABBCCDD").
  *
  * \note Usage example:
  *       KI2C.CYCLIC 100 AABBCCDD 0x50, 250 1122 0x51
  *       KI2C.CYCLIC 100 AABBCCDD 0x50, 250 1122 0x51 &
  *
  * \param[in] args  "time1 val1 , time2 val2 , ..." (see generic_send_cyclic())
  * \param[in] st    stop_token; forwarded as-is (present/absent '&' selects run-once vs. forever)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool KI2CPlugin::m_KI2C_CYCLIC (const std::string &args, std::stop_token st) const
{
    return ucmdexec::generic_send_cyclic(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<KI2C> {
            // Open the KI2C device (RAII — closed automatically by destructor)
            auto shpDriver = std::make_shared<KI2C>(m_strKI2CDevice, m_u8KI2CAddress, m_strKI2CDevice);
            return shpDriver->is_open() ? shpDriver : nullptr;
        },
        m_strInstanceName, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, st, m_bCyclicCached);
}


///////////////////////////////////////////////////////////////////
//            PRIVATE INTERFACES IMPLEMENTATION                  //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief message sender
*/
/*--------------------------------------------------------------------------------------------------------*/

bool KI2CPlugin::m_Send(std::span<const uint8_t> dataSpan, std::shared_ptr<const ICommDriver> shpDriver) const
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
  * \brief message receiver
*/
/*--------------------------------------------------------------------------------------------------------*/

bool KI2CPlugin::m_Receive(std::span<uint8_t> dataSpan, size_t& szSize, CommCommandReadType readType, std::shared_ptr<const ICommDriver> shpDriver) const
{
    bool bRetVal = false;
    ICommDriver::ReadOptions options;

    switch(readType)
    {
        case CommCommandReadType::LINE:
            options.mode      = ICommDriver::ReadMode::UntilDelimiter;
            options.delimiter = '\n';
            break;

        case CommCommandReadType::TOKEN_STRING:
            [[fallthrough]];
        case CommCommandReadType::TOKEN_HEXSTREAM:
            options.mode       = ICommDriver::ReadMode::UntilToken;
            options.token      = dataSpan;
            options.use_buffer = true;
            break;

        default:
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
