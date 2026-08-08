#include "modbus_plugin.hpp"
#include "uCommandExec.hpp"
#include "uPluginSettings.hpp"

#include <sstream>

#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LOG_HDR "MODBUS PLUGIN |"

// INI Keys
#define K_HOST           "HOST"
#define K_PORT           "PORT"
#define K_ARTEFACTS      "ARTEFACTS_PATH"
#define K_READ_TIMEOUT   "READ_TIMEOUT"
#define K_READ_BUFSIZE   "READ_BUFFER_SIZE"

// Config Command Short Keys
#define SK_HOST  "h"
#define SK_PORT  "p"
#define SK_RTOUT "rt"
#define SK_RBUF  "rb"

extern "C"
{
    EXPORTED ModbusPlugin* pluginEntry() { return new ModbusPlugin(); }
    EXPORTED void pluginExit(ModbusPlugin *ptrPlugin) { delete ptrPlugin; }
}

bool ModbusPlugin::doInit(void *pvUserData)
{
    (void)pvUserData;
    m_bIsInitialized = true;
    return true;
}

void ModbusPlugin::doCleanup(void)
{
    m_bIsInitialized = false;
    m_bIsEnabled = false;
    m_strResultData.clear();
    m_pDriver.reset();
    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Cleanup done"));
}

bool ModbusPlugin::setParams(const PluginDataSet *psSetParams)
{
    bool bRetVal = false;
    if (generic_setparams<ModbusPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
        if (m_LocalSetParams(psSetParams)) {
            bRetVal = true;
        }
    }
    return bRetVal;
}

void ModbusPlugin::getParams(PluginDataGet *psGetParams) const
{
    generic_getparams<ModbusPlugin>(this, psGetParams);
}

bool ModbusPlugin::doDispatch(const std::string& strCmd, const std::string& strParams, std::stop_token st) const
{
    return generic_dispatch<ModbusPlugin>(this, strCmd, strParams, st);
}

// --- Setters requiring validation ---

bool ModbusPlugin::setPort(const std::string& portStr) const
{
    uint32_t port = 0;
    if (!numeric::str2uint32(portStr, port)) return false;
    if (port > 65535) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid port:"); LOG_UINT32(port));
        return false;
    }
    m_u16Port = static_cast<uint16_t>(port);
    return true;
}

bool ModbusPlugin::setReadTimeout(const std::string& timeoutStr) const
{
    return numeric::str2uint32(timeoutStr, m_u32ReadTimeout);
}

bool ModbusPlugin::setReadBufferSize(const std::string& bufSizeStr) const
{
    uint32_t sz = 0;
    if (!numeric::str2uint32(bufSizeStr, sz)) return false;
    if (sz == 0) return false;
    m_u32ReadBufferSize = sz;
    return true;
}

// --- Local Params ---

bool ModbusPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    if (psSetParams->mapSettings.empty()) return true;

    PluginSettingsBinder sSettings;
    sSettings.Bind(K_ARTEFACTS,    m_strArtefactsPath);
    sSettings.Bind(K_HOST,         m_strHost);
    sSettings.Bind(K_PORT,         [this](const std::string& v) { return setPort(v); });
    sSettings.Bind(K_READ_TIMEOUT, [this](const std::string& v) { return setReadTimeout(v); });
    sSettings.Bind(K_READ_BUFSIZE, [this](const std::string& v) { return setReadBufferSize(v); });

    sSettings.Apply(psSetParams->mapSettings, nullptr, /*bStopOnFirstError=*/false);

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Config updated. Host:") LOG_STRING(m_strHost));
    return true;
}

// -----------------------------------------------------------------------
// Driver factory
// -----------------------------------------------------------------------

std::shared_ptr<ModbusDriver> ModbusPlugin::m_OpenDriver(void) const
{
    if (m_pDriver && m_pDriver->is_open()) {
        return m_pDriver;
    }

    if (m_strHost.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Host not configured — MODBUS.CONFIG h=<host> first"));
        return nullptr;
    }

    ModbusDriver::Config cfg;
    cfg.host              = m_strHost;
    cfg.port              = m_u16Port;
    cfg.connectTimeoutMs  = 5000;
    cfg.responseTimeoutMs = m_u32ReadTimeout;

    auto driver = std::make_shared<ModbusDriver>(cfg);
    if (!driver->open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("ModbusDriver open failed"));
        return nullptr;
    }

    m_pDriver = driver;
    return m_pDriver;
}

// -----------------------------------------------------------------------
// Top-level commands
// -----------------------------------------------------------------------

bool ModbusPlugin::m_MODBUS_INFO(const std::string& args, std::stop_token st) const
{
    (void)args; (void)st;
    resetData();
    std::ostringstream oss;
    oss << MODBUS_PLUGIN_NAME " v" << m_strVersion
        << " host=" << m_strHost
        << " port=" << m_u16Port;
    m_strResultData = oss.str();

    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING(MODBUS_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: Modbus TCP master (client) — read/write coils and registers on a slave/server"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Architecture: ModbusProtocol (protocol) / TCPIP (real driver, undecorated) / ModbusDriver (protocol+driver glue, ICommDriver) / this plugin (CONFIG + wiring only)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the slave/server host, port and timeouts"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [h=host] [p=port] [rt=read_tout] [rb=read_bufsize]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : MODBUS.CONFIG h=plc.local p=502"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : one Modbus request/response, on the plugin's single persistent connection (opened on first use)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : > <FUNCTION> <unit_id> <address> <args...> [| expected]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : MODBUS.CMD > READ_HOLDING_REGISTERS 1 100 4 | 12,34,56,78"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         MODBUS.CMD > READ_COILS 1 0 8 | 1,0,1,1,0,0,0,1"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         MODBUS.CMD > WRITE_SINGLE_COIL 1 5 1 | OK"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         MODBUS.CMD > WRITE_SINGLE_REGISTER 1 10 1234 | OK"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         MODBUS.CMD > WRITE_MULTIPLE_COILS 1 0 1 0 1 1 | OK"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         MODBUS.CMD > WRITE_MULTIPLE_REGISTERS 1 0 100 200 300 | OK"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : functions: READ_COILS, READ_DISCRETE_INPUTS, READ_HOLDING_REGISTERS, READ_INPUT_REGISTERS,"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         WRITE_SINGLE_COIL, WRITE_SINGLE_REGISTER, WRITE_MULTIPLE_COILS, WRITE_MULTIPLE_REGISTERS."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         unit_id is always required (no default) to keep argument parsing unambiguous."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         Read responses are comma-separated (bits as 0/1, registers as decimal); writes confirm with \"OK\"."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         A Modbus exception response fails the command with \"EXCEPTION:<code>\"."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         Unlike MQTT there is no standalone '<' receive — every request's response is read on its own line's '|'."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         The GUI comm-dump panel shows the real bytes exchanged with the slave (one row per ADU)."));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : run several MODBUS.CMD-style lines from a file over the same connection"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : scriptpathname [|delay]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : MODBUS.SCRIPT script.txt"));
    LOG_SEP();

    return true;
}

bool ModbusPlugin::m_MODBUS_CONFIG(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();
    if (args.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing config args"));
        return false;
    }

    std::istringstream stream(args);
    std::string token;
    bool bRetVal = true;

    while (stream >> token) {
        auto eqPos = token.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = token.substr(0, eqPos);
        std::string val = token.substr(eqPos + 1);

        if (!val.empty() && val[0] == '$') {
            // Unexpanded macro reference during script VALIDATION (dry run) —
            // real execution always resolves $macros before the plugin sees
            // the string; defer the actual value check to then.
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Deferring '"); LOG_STRING(key);
                      LOG_STRING("=" ); LOG_STRING(val);
                      LOG_STRING("' - value is a macro, resolved at execution time"));
            continue;
        }

        if (key == SK_HOST) setHost(val);
        else if (key == SK_PORT)  { if (!setPort(val)) bRetVal = false; }
        else if (key == SK_RTOUT) { if (!setReadTimeout(val)) bRetVal = false; }
        else if (key == SK_RBUF)  { if (!setReadBufferSize(val)) bRetVal = false; }
    }
    return bRetVal;
}

// -----------------------------------------------------------------------
// MODBUS.CMD / MODBUS.SCRIPT — see class doc comment (modbus_plugin.hpp)
// -----------------------------------------------------------------------

bool ModbusPlugin::m_MODBUS_CMD(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    return ucmdexec::generic_cmd(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<ModbusDriver> { return m_OpenDriver(); },
        MODBUS_PLUGIN_NAME,
        m_u32ReadBufferSize, m_u32ReadTimeout, LOG_HDR, &m_strResultData,
        // Non-capturing: ModbusDriver::send()/receive() are handed
        // everything they need through the driver parameter itself — see
        // modbus_driver.hpp's class doc comment.
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const ModbusDriver> drv, std::string_view x) {
            return drv->send(t, d, x);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const ModbusDriver> drv, std::string_view x) {
            return drv->receive(t, b, o, x);
        });
}

bool ModbusPlugin::m_MODBUS_SCRIPT(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    return ucmdexec::generic_script(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<ModbusDriver> { return m_OpenDriver(); },
        MODBUS_PLUGIN_NAME,
        m_strArtefactsPath, m_u32ReadBufferSize, m_u32ReadTimeout, LOG_HDR,
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const ModbusDriver> drv, std::string_view x) {
            return drv->send(t, d, x);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const ModbusDriver> drv, std::string_view x) {
            return drv->receive(t, b, o, x);
        });
}
