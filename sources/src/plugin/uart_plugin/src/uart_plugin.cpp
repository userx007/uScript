#include "uSharedConfig.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include "uart_setup.hpp"
#include "uart_plugin.hpp"

#include "uPluginSettings.hpp"

#include "uNumeric.hpp"
#include "uFile.hpp"
#include "uString.hpp"
#include "uHexlify.hpp"
#include "uUart.hpp"
#include "uCommandExec.hpp"


/////////////////////////////////////////////////////////////////////////////////
//                  PLUGIN ENTRY POINTS                                        //
/////////////////////////////////////////////////////////////////////////////////

extern "C"
{
    EXPORTED UARTPlugin* pluginEntry()
    {
        return new UARTPlugin();
    }

    EXPORTED void pluginExit( UARTPlugin *ptrPlugin)
    {
        if (nullptr != ptrPlugin)
        {
            delete ptrPlugin;
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////
//                 PLUGIN TOP LEVEL COMMANDS                                   //
/////////////////////////////////////////////////////////////////////////////////

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief INFO command implementation; shows details about plugin and
  *        describe the supported functions with examples of usage.
  *        This command takes no arguments and is executed even if the plugin initialization fails
  *
  * \note Usage example: <br>
  *       UART.INFO
  *
  * \param[in] pstrArgs NULL (NULL means that no arguments are provided to this function)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/


bool UARTPlugin::m_UART_INFO (const std::string &args, std::stop_token st ) const
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
    LOG_PRINT(LOG_EMPTY, LOG_STRING(UART_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: communicate with other apps/devices via UART"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : overwrite the default UART port"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args : [p=port] [b=baudrate] [r=read_tout] [w=write_tout] [s=recv_bufsize]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage: UART.CONFIG p=COM2 b=115200 r=2000 w=2000 s=1024"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("       UART.CONFIG p=/dev/ttyUSB0 b=115200 s=2048"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : send commands from a file"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args : script"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage: UART.SCRIPT script.txt"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD  : send, receive or both"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args : direction message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage: UART.CMD > H\"AABBCCDD\" | ok"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("       UART.CMD < \"Please send!\" | F\"data.bin, 1024\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note : can be both sent/received: (un)quoted strings, hex. lines"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note : can be only sent: files, only received: tokens"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC : send one or more periodic messages"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args : time1 val1 [id1], time2 val2 [id2], ... (time_i in ms, val_i hex)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage: UART.CYCLIC 100 AABBCCDD, 250 06"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("       UART.CYCLIC 100 AABBCCDD, 250 06 &"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note : id has no meaning here (byte-stream) and is always omitted"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note : without '&' sends one full pattern (lcm of the time_i) then returns;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("       with '&' repeats forever until the script/thread is stopped"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("INI file parameters (copy/paste into your ini file):"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("[UART]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("ARTEFACTS_PATH   =         # directory used by SCRIPT/CMD/wrrdf for reading/writing artefact files"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("UART_PORT        = COM2    # serial port to open (e.g. COM3 on Windows, /dev/ttyUSB0 on Linux)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("BAUDRATE         = 115200  # UART baud rate"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_TIMEOUT     = 2000    # read timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("WRITE_TIMEOUT    = 2000    # write timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_BUF_SIZE    = 1024    # size in bytes of the local read buffer"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_BUF_TIMEOUT = 2000    # timeout in ms while draining/emptying the read buffer"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RAW_RESULT       = false   # CMD returns raw bytes instead of a hexlified string when true"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC_CACHED    = true    # true=validate/parse each CYCLIC entry once per session; false=re-resolve every tick (needed for volatile ?= macros)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note: the CONFIG command above can override a subset of these at runtime;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("      any key not accepted by CONFIG must be set via the ini file."));

    return true;

}

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CONFIG command implementation; overwrite the current UART port (m_strUartPort)
  *
  * \note If an empty string is provided then the command doesn't change anything
  *
  * \note Is intended to change the port when a virtual UART over USB is used
  *
  * \note Usage example: <br>
  *       UART.CONFIG p=COM2 b=115200 r=2000 w=2000 s=1024
  *       UART.CONFIG p=/dev/ttyUSB0 b=115200 r=2000 w=2000 s=1024
  *
  * \param[in] p=port b=baudrate r=readtout w=writetout s=readbuffersize
  *
  * \return true if reading succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/


bool UARTPlugin::m_UART_CONFIG ( const std::string &args, std::stop_token st ) const
{
    return generic_uart_set_params<UARTPlugin>(this, args);
}

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief m_UART_CMD command implementation;
  *
  * \note Usage example: <br>
  *       UART.CMD
  *       UART.CMD > Hello | ok                   // send "Hello" and expect to read back "ok"
  *       UART.CMD < "Please send!" | Sending...  // wait to receive "Please send!" and send back "Sending..."
  *
  * \param[in] pstrArgs - optional timeout
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool UARTPlugin::m_UART_CMD ( const std::string &args, std::stop_token st ) const
{
    (void)st;

    return ucmdexec::generic_cmd(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<UART> {
            // open the UART port (RAII implementation, the close is done by destructor)
            auto shpDriver = std::make_shared<UART>(m_strUartPort, m_u32UartBaudrate, m_strUartPort);
            return shpDriver->is_open() ? shpDriver : nullptr;
        },
        m_strInstanceName,
        m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, &m_strResultData, m_bRawResult);
}

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief SCRIPT command implementation;
  *
  * \note Usage example: <br>
  *       UART.SCRIPT scriptname [|delay]
  *
  * \param[in] filename<string>
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool UARTPlugin::m_UART_SCRIPT ( const std::string &args, std::stop_token st ) const
{
    (void)st;

    return ucmdexec::generic_script(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<UART> {
            // open the UART port (RAII implementation, the close is done by destructor)
            auto shpDriver = std::make_shared<UART>(m_strUartPort, m_u32UartBaudrate, m_strUartPort);
            return shpDriver->is_open() ? shpDriver : nullptr;
        },
        m_strInstanceName,
        m_strArtefactsPath, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CYCLIC command implementation; send one or more periodic UART messages.
  *
  * \note The UART port is opened once for the whole CYCLIC session (like SCRIPT) and closed
  *       automatically on return (RAII). UART is a byte-stream with no addressing concept, so
  *       each entry's optional "id" is never sent on the wire — omit it — and "val" is the
  *       payload as a plain hex string (e.g. "AABBCCDD").
  *
  * \note Usage example:
  *       UART.CYCLIC 100 AABBCCDD, 250 06
  *       UART.CYCLIC 100 AABBCCDD, 250 06 &
  *
  * \param[in] args  "time1 val1 , time2 val2 , ..." (see generic_send_cyclic())
  * \param[in] st    stop_token; forwarded as-is (present/absent '&' selects run-once vs. forever)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool UARTPlugin::m_UART_CYCLIC ( const std::string &args, std::stop_token st ) const
{
    return ucmdexec::generic_send_cyclic(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<UART> {
            // open the UART port (RAII implementation, the close is done by destructor)
            auto shpDriver = std::make_shared<UART>(m_strUartPort, m_u32UartBaudrate, m_strUartPort);
            return shpDriver->is_open() ? shpDriver : nullptr;
        },
        m_strInstanceName, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, st, m_bCyclicCached);
}

