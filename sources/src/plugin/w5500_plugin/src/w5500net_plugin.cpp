#include "uSharedConfig.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include "w5500net_setup.hpp"
#include "w5500net_plugin.hpp"
#include "uW5500Net.hpp"

#include "uPluginSettings.hpp"

#include "uNumeric.hpp"
#include "uFile.hpp"
#include "uString.hpp"
#include "uCommandExec.hpp"

#include <memory>

/////////////////////////////////////////////////////////////////////////////////
//                  PLUGIN ENTRY POINTS                                        //
/////////////////////////////////////////////////////////////////////////////////

extern "C"
{
    EXPORTED W5500NetPlugin* pluginEntry()
    {
        return new W5500NetPlugin();
    }

    EXPORTED void pluginExit( W5500NetPlugin *ptrPlugin)
    {
        if (nullptr != ptrPlugin)
        {
            delete ptrPlugin;
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////
// Driver factory
/////////////////////////////////////////////////////////////////////////////////

std::shared_ptr<W5500Net> W5500NetPlugin::m_OpenDriver (void) const
{
    if (m_strServerIp.empty() || m_u16ServerPort == 0U) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Server IP/Port not configured"));
        return nullptr;
    }

    auto shpDriver = std::make_shared<W5500Net>();

    if (W5500Net::Status::SUCCESS != shpDriver->open(m_strServerIp, m_u16ServerPort)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Connect failed:"); LOG_STRING(m_strServerIp.c_str());
                  LOG_STRING(":"); LOG_UINT32(m_u16ServerPort));
        return nullptr;
    }

    return shpDriver;
}

/////////////////////////////////////////////////////////////////////////////////
//                 PLUGIN TOP LEVEL COMMANDS                                   //
/////////////////////////////////////////////////////////////////////////////////

bool W5500NetPlugin::m_W5500NET_INFO(const std::string& args, std::stop_token st) const
{
    (void)st;

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
    LOG_PRINT(LOG_EMPTY, LOG_STRING(W5500NET_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(W5500NET_PLUGIN_VERSION));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: communicate via TCP/IP over a W5500 Ethernet controller (client socket)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the server IP, port and transfer parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [i=ip] [p=port] [r=read_tout] [w=write_tout] [s=recv_bufsize]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : W5500NET.CONFIG i=192.168.1.10 p=5000 r=2000 w=2000 s=512"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         W5500NET.CONFIG i=localhost p=8080"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : any subset of keys may be given; omitted keys retain their current values"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : send commands from a script file"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : scriptpathname [|delay]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : W5500NET.SCRIPT script.txt"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : send, receive or both"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : direction message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : W5500NET.CMD > Hello | ok"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         W5500NET.CMD < \"Please send!\" | Sending..."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : a fresh connection to ip:port is opened for CMD and closed once it completes"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC : send one or more periodic messages"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : time1 val1 [id1], time2 val2 [id2], ... (time_i in ms, val_i hex)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : W5500NET.CYCLIC 100 AABBCCDD, 250 06"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         W5500NET.CYCLIC 100 AABBCCDD, 250 06 &"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : id has no meaning here (single-peer stream) and is always omitted"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : without '&' sends one full pattern (lcm of the time_i) then returns;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         with '&' repeats forever until the script/thread is stopped"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("INI file parameters (copy/paste into your ini file):"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("[W5500NET]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("ARTEFACTS_PATH   =               # directory used by SCRIPT/CMD/wrrdf for reading/writing artefact files"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SERVER_IP        = 192.168.1.10  # IP address of the W5500-attached device to connect to"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SERVER_PORT      = 5000          # TCP port of the W5500-attached device"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_TIMEOUT     = 2000          # read timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("WRITE_TIMEOUT    = 2000          # write timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_BUFFER_SIZE = 1024          # size in bytes of the local read buffer"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RAW_RESULT       = false         # CMD returns raw bytes instead of a hexlified string when true"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC_CACHED    = true          # true=validate/parse each CYCLIC entry once per session; false=re-resolve every tick (needed for volatile ?= macros)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note: the CONFIG command above can override a subset of these at runtime;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("      any key not accepted by CONFIG must be set via the ini file."));


    return true;
}

// -----------------------------------------------------------------------
// W5500NET.CONFIG
// -----------------------------------------------------------------------
bool W5500NetPlugin::m_W5500NET_CONFIG(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();
    return generic_w5500net_set_params(this, args);
}

// -----------------------------------------------------------------------
// W5500NET.CMD
// -----------------------------------------------------------------------
bool W5500NetPlugin::m_W5500NET_CMD(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    return ucmdexec::generic_cmd(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<W5500Net> { return m_OpenDriver(); },
        m_strInstanceName,
        m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, &m_strResultData, m_bRawResult);
}

// -----------------------------------------------------------------------
// W5500NET.SCRIPT
// -----------------------------------------------------------------------
bool W5500NetPlugin::m_W5500NET_SCRIPT(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    return ucmdexec::generic_script(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<W5500Net> { return m_OpenDriver(); },
        m_strInstanceName,
        m_strArtefactsPath, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR);
}

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CYCLIC command implementation; send one or more periodic W5500NET messages.
  *
  * \note The connection is opened once for the whole CYCLIC session (like SCRIPT) and closed
  *       automatically on return (RAII). W5500NET is a single-peer stream with no addressable
  *       channels, so each entry's optional "id" is never sent on the wire — omit it — and
  *       "val" is the payload as a plain hex string (e.g. "AABBCCDD").
  *
  * \note Usage example:
  *       W5500NET.CYCLIC 100 AABBCCDD, 250 06
  *       W5500NET.CYCLIC 100 AABBCCDD, 250 06 &
  *
  * \param[in] args  "time1 val1 , time2 val2 , ..." (see generic_send_cyclic())
  * \param[in] st    stop_token; forwarded as-is (present/absent '&' selects run-once vs. forever)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
bool W5500NetPlugin::m_W5500NET_CYCLIC(const std::string& args, std::stop_token st) const
{
    resetData();

    return ucmdexec::generic_send_cyclic(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<W5500Net> { return m_OpenDriver(); },
        m_strInstanceName, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, st, m_bCyclicCached);
}
