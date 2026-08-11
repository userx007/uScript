#include "uSharedConfig.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include "enc28j60net_setup.hpp"
#include "enc28j60net_plugin.hpp"

#include "uPluginSettings.hpp"
#include "uEnc28J60Net.hpp"

#include "uNumeric.hpp"
#include "uFile.hpp"
#include "uString.hpp"
#include "uCommandExec.hpp"

#include <memory>

#ifdef LT_HDR
    #undef LT_HDR
#endif
#define LT_HDR "ENC28J60NET PLUGIN |"
#define LOG_HDR  LOG_STRING(LT_HDR)

#define ARTEFACTS_PATH              "ARTEFACTS_PATH"
#define SERVER_IP                   "SERVER_IP"
#define SERVER_PORT                 "SERVER_PORT"
#define READ_TIMEOUT                "READ_TIMEOUT"
#define WRITE_TIMEOUT               "WRITE_TIMEOUT"
#define READ_BUFFER_SIZE            "READ_BUFFER_SIZE"

extern "C"
{
    EXPORTED Enc28J60NetPlugin* pluginEntry()
    {
        return new Enc28J60NetPlugin();
    }

    EXPORTED void pluginExit( Enc28J60NetPlugin *ptrPlugin)
    {
        if (nullptr != ptrPlugin)
        {
            delete ptrPlugin;
        }
    }
}

bool Enc28J60NetPlugin::doInit(void *pvUserData)
{
    m_bIsInitialized = true;
    return m_bIsInitialized;
}

void Enc28J60NetPlugin::doCleanup(void)
{
    m_bIsInitialized = false;
    m_bIsEnabled     = false;
    m_strResultData.clear();
    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Cleanup done"));
}

bool Enc28J60NetPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    // Runtime instance identity for the GUI comm-dump panel (e.g. "ENC28J60NET:1"); falls back to the fixed plugin name if the
    // interpreter didn't supply one. Done before the "nothing loaded from ini"
    // early-return below so it's always captured.
    m_strInstanceName = psSetParams->strInstanceName.empty() ? ENC28J60NET_PLUGIN_NAME : psSetParams->strInstanceName;

    if (true == psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        return true;
    }

    PluginSettingsBinder sSettings;
    sSettings.Bind(ARTEFACTS_PATH,   m_strArtefactsPath);
    sSettings.Bind(SERVER_IP,        [this](const std::string& v) { setServerIp(v); return true; });
    sSettings.Bind(SERVER_PORT,      [this](const std::string& v) { return setServerPort(v); });
    sSettings.Bind(READ_TIMEOUT,     [this](const std::string& v) { return setReadTimeout(v); });
    sSettings.Bind(WRITE_TIMEOUT,    [this](const std::string& v) { return setWriteTimeout(v); });
    sSettings.Bind(READ_BUFFER_SIZE, [this](const std::string& v) { return setReadBufferSize(v); });

    return sSettings.Apply(psSetParams->mapSettings,
        [](const std::string& strKey, const std::string& strRawValue) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strKey); LOG_STRING(":"); LOG_STRING(strRawValue));
        });
}

bool Enc28J60NetPlugin::setServerPort (const std::string& strServerPort) const
{
    static constexpr uint32_t TCP_PORT_MAX = 65535U;
    uint32_t u32Port = 0U;
    if (false == numeric::str2uint32(strServerPort, u32Port)) {
        return false;
    }
    if (u32Port == 0U || u32Port > TCP_PORT_MAX) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Port out of range [1-65535]:"); LOG_UINT32(u32Port));
        return false;
    }
    m_u16ServerPort = static_cast<uint16_t>(u32Port);
    return true;
}

bool Enc28J60NetPlugin::setReadTimeout (const std::string& strReadTimeout) const
{
    return numeric::str2uint32(strReadTimeout, m_u32ReadTimeout);
}

bool Enc28J60NetPlugin::setWriteTimeout (const std::string& strWriteTimeout) const
{
    return numeric::str2uint32(strWriteTimeout, m_u32WriteTimeout);
}

bool Enc28J60NetPlugin::setReadBufferSize (const std::string& strReadBufferSize) const
{
    static constexpr uint32_t MAX_BUF = 1460U;
    uint32_t u32Size = 0U;
    if (false == numeric::str2uint32(strReadBufferSize, u32Size)) {
        return false;
    }
    if (u32Size == 0U || u32Size > MAX_BUF) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("ReadBufSize out of range [1-"); LOG_UINT32(MAX_BUF); LOG_STRING("]:"); LOG_UINT32(u32Size));
        return false;
    }
    m_u32ReadBufferSize = u32Size;
    return true;
}

std::shared_ptr<Enc28J60Net> Enc28J60NetPlugin::m_OpenDriver (void) const
{
    if (m_strServerIp.empty() || m_u16ServerPort == 0U) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Server IP/Port not configured"));
        return nullptr;
    }

    auto shpDriver = std::make_shared<Enc28J60Net>();

    if (Enc28J60Net::Status::SUCCESS != shpDriver->open(m_strServerIp, m_u16ServerPort)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Connect failed:"); LOG_STRING(m_strServerIp.c_str());
                  LOG_STRING(":"); LOG_UINT32(m_u16ServerPort));
        return nullptr;
    }

    return shpDriver;
}

bool Enc28J60NetPlugin::m_ENC28J60NET_INFO(const std::string& args, std::stop_token st) const
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
    LOG_PRINT(LOG_EMPTY, LOG_STRING(ENC28J60NET_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(ENC28J60NET_PLUGIN_VERSION));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: communicate via TCP/IP over an ENC28J60 Ethernet controller (client socket)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the server IP, port and transfer parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [i=ip] [p=port] [r=read_tout] [w=write_tout] [s=recv_bufsize]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : ENC28J60NET.CONFIG i=192.168.1.10 p=5000 r=2000 w=2000 s=512"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         ENC28J60NET.CONFIG i=localhost p=8080"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : any subset of keys may be given; omitted keys retain their current values"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : send commands from a script file"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : scriptpathname [|delay]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : ENC28J60NET.SCRIPT script.txt"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : send, receive or both"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : direction message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : ENC28J60NET.CMD > Hello | ok"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         ENC28J60NET.CMD < \"Please send!\" | Sending..."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : a fresh connection to ip:port is opened for CMD and closed once it completes"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC : send one or more periodic messages"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : time1 val1 [id1], time2 val2 [id2], ... (time_i in ms, val_i hex)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : ENC28J60NET.CYCLIC 100 AABBCCDD, 250 06"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         ENC28J60NET.CYCLIC 100 AABBCCDD, 250 06 &"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : id has no meaning here (single-peer stream) and is always omitted"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : without '&' sends one full pattern (lcm of the time_i) then returns;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         with '&' repeats forever until the script/thread is stopped"));
    LOG_SEP();

    return true;
}

bool Enc28J60NetPlugin::m_ENC28J60NET_CONFIG(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();
    return generic_enc28j60net_set_params(this, args);
}

bool Enc28J60NetPlugin::m_ENC28J60NET_CMD(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    return ucmdexec::generic_cmd(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<Enc28J60Net> { return m_OpenDriver(); },
        m_strInstanceName,
        m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, &m_strResultData);
}

bool Enc28J60NetPlugin::m_ENC28J60NET_SCRIPT(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    return ucmdexec::generic_script(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<Enc28J60Net> { return m_OpenDriver(); },
        m_strInstanceName,
        m_strArtefactsPath, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CYCLIC command implementation; send one or more periodic ENC28J60NET messages.
  *
  * \note The connection is opened once for the whole CYCLIC session (like SCRIPT) and closed
  *       automatically on return (RAII). ENC28J60NET is a single-peer stream with no
  *       addressable channels, so each entry's optional "id" is never sent on the wire — omit
  *       it — and "val" is the payload as a plain hex string (e.g. "AABBCCDD").
  *
  * \note Usage example:
  *       ENC28J60NET.CYCLIC 100 AABBCCDD, 250 06
  *       ENC28J60NET.CYCLIC 100 AABBCCDD, 250 06 &
  *
  * \param[in] args  "time1 val1 , time2 val2 , ..." (see generic_send_cyclic())
  * \param[in] st    stop_token; forwarded as-is (present/absent '&' selects run-once vs. forever)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
bool Enc28J60NetPlugin::m_ENC28J60NET_CYCLIC(const std::string& args, std::stop_token st) const
{
    resetData();

    return ucmdexec::generic_send_cyclic(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<Enc28J60Net> { return m_OpenDriver(); },
        m_strInstanceName, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, st);
}
