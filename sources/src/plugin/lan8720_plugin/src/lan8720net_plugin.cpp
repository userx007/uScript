#include "uSharedConfig.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include "lan8720net_setup.hpp"
#include "lan8720net_plugin.hpp"
#include "uLan8720Net.hpp"

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
    EXPORTED Lan8720NetPlugin* pluginEntry()
    {
        return new Lan8720NetPlugin();
    }

    EXPORTED void pluginExit( Lan8720NetPlugin *ptrPlugin)
    {
        if (nullptr != ptrPlugin)
        {
            delete ptrPlugin;
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////
// Driver factory                                                              //
/////////////////////////////////////////////////////////////////////////////////

std::shared_ptr<Lan8720Net> Lan8720NetPlugin::m_OpenDriver (void) const
{
    if (m_strServerIp.empty() || m_u16ServerPort == 0U) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Server IP/Port not configured"));
        return nullptr;
    }

    auto shpDriver = std::make_shared<Lan8720Net>();

    if (Lan8720Net::Status::SUCCESS != shpDriver->open(m_strServerIp, m_u16ServerPort)) {
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

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief INFO command implementation; shows details about the plugin and
  *        describes the supported functions with examples of usage.
  *        This command takes no arguments and is executed even if plugin initialization fails.
  *
  * \note Usage example:
  *       LAN8720NET.INFO
  *
  * \param[in] args  empty string (no arguments expected)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
bool Lan8720NetPlugin::m_LAN8720NET_INFO(const std::string& args, std::stop_token st) const
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
    LOG_PRINT(LOG_EMPTY, LOG_STRING(LAN8720NET_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(LAN8720NET_PLUGIN_VERSION));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: communicate via TCP/IP over a LAN8720 Ethernet PHY (client socket)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the server IP, port and transfer parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [i=ip] [p=port] [r=read_tout] [w=write_tout] [s=recv_bufsize]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : LAN8720NET.CONFIG i=192.168.1.10 p=5000 r=2000 w=2000 s=512"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         LAN8720NET.CONFIG i=localhost p=8080"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : any subset of keys may be given; omitted keys retain their current values"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : send commands from a script file"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : scriptpathname [|delay]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : LAN8720NET.SCRIPT script.txt"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : send, receive or both"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : direction message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : LAN8720NET.CMD > Hello | ok"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         LAN8720NET.CMD < \"Please send!\" | Sending..."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : a fresh connection to ip:port is opened for CMD and closed once it completes"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC : send one or more periodic messages"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : time1 val1 [id1], time2 val2 [id2], ... (time_i in ms, val_i hex)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : LAN8720NET.CYCLIC 100 AABBCCDD, 250 06"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         LAN8720NET.CYCLIC 100 AABBCCDD, 250 06 &"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : id has no meaning here (single-peer stream) and is always omitted"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : without '&' sends one full pattern (lcm of the time_i) then returns;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         with '&' repeats forever until the script/thread is stopped"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("INI file parameters (copy/paste into your ini file):"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("[LAN8720NET]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("ARTEFACTS_PATH   =               # directory used by SCRIPT/CMD/wrrdf for reading/writing artefact files"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SERVER_IP        = 192.168.1.10  # IP address of the LAN8720-attached device to connect to"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SERVER_PORT      = 5000          # TCP port of the LAN8720-attached device"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_TIMEOUT     = 2000          # read timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("WRITE_TIMEOUT    = 2000          # write timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_BUFFER_SIZE = 1024          # size in bytes of the local read buffer"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RAW_RESULT       = false         # CMD returns raw bytes instead of a hexlified string when true"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC_CACHED    = true          # true=validate/parse each CYCLIC entry once per session; false=re-resolve every tick (needed for volatile ?= macros)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note: the CONFIG command above can override a subset of these at runtime;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("      any key not accepted by CONFIG must be set via the ini file."));


    return true;
}

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CONFIG command: apply default-peer/timeout/buffer-size settings
  *        at runtime, using the same key=value grammar as the ini-backed
  *        m_LocalSetParams()
  *
*/
/*--------------------------------------------------------------------------------------------------------*/
bool Lan8720NetPlugin::m_LAN8720NET_CONFIG(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();
    return generic_lan8720net_set_params(this, args);
}

/*--------------------------------------------------------------------------------------------------------*/
/* LAN8720NET.CMD                                                                                         */
/*--------------------------------------------------------------------------------------------------------*/
bool Lan8720NetPlugin::m_LAN8720NET_CMD(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    return ucmdexec::generic_cmd(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<Lan8720Net> { return m_OpenDriver(); },
        m_strInstanceName,
        m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, &m_strResultData, m_bRawResult);
}

/*--------------------------------------------------------------------------------------------------------*/
/* LAN8720NET.SCRIPT                                                                                      */
/*--------------------------------------------------------------------------------------------------------*/
bool Lan8720NetPlugin::m_LAN8720NET_SCRIPT(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    return ucmdexec::generic_script(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<Lan8720Net> { return m_OpenDriver(); },
        m_strInstanceName,
        m_strArtefactsPath, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CYCLIC command implementation; send one or more periodic LAN8720NET messages.
  *
  * \note The connection is opened once for the whole CYCLIC session (like SCRIPT) and closed
  *       automatically on return (RAII). LAN8720NET is a single-peer stream with no addressable
  *       channels, so each entry's optional "id" is never sent on the wire — omit it — and
  *       "val" is the payload as a plain hex string (e.g. "AABBCCDD").
  *
  * \note Usage example:
  *       LAN8720NET.CYCLIC 100 AABBCCDD, 250 06
  *       LAN8720NET.CYCLIC 100 AABBCCDD, 250 06 &
  *
  * \param[in] args  "time1 val1 , time2 val2 , ..." (see generic_send_cyclic())
  * \param[in] st    stop_token; forwarded as-is (present/absent '&' selects run-once vs. forever)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
bool Lan8720NetPlugin::m_LAN8720NET_CYCLIC(const std::string& args, std::stop_token st) const
{
    resetData();

    return ucmdexec::generic_send_cyclic(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<Lan8720Net> { return m_OpenDriver(); },
        m_strInstanceName, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, st, m_bCyclicCached);
}
