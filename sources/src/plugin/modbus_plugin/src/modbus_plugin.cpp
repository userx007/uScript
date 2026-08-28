#include "modbus_plugin.hpp"
#include "private/modbus_setup.hpp"
#include "uCommandExec.hpp"

#include <sstream>

/////////////////////////////////////////////////////////////////////////////////
//                  PLUGIN ENTRY POINTS                                        //
/////////////////////////////////////////////////////////////////////////////////

extern "C"
{
    EXPORTED ModbusPlugin* pluginEntry()
    {
        return new ModbusPlugin();
    }

    EXPORTED void pluginExit(ModbusPlugin *ptrPlugin)
    {
        if(nullptr != ptrPlugin)
        {
            delete ptrPlugin;
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////
//                 PLUGIN INIT / CLEANUP                                       //
/////////////////////////////////////////////////////////////////////////////////

bool ModbusPlugin::doInit(void *pvUserData)
{
    (void)pvUserData;
    m_bIsInitialized = true;
    return true;
}

void ModbusPlugin::doCleanup(void)
{
    m_strResultData.clear();
    m_pDriver.reset();
    m_bIsInitialized = false;
    m_bIsEnabled = false;
}

/////////////////////////////////////////////////////////////////////////////////
//                 PLUGIN SET PARAMS / GET PARAMS / DISPATCH COMMANDS          //
/////////////////////////////////////////////////////////////////////////////////

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

/////////////////////////////////////////////////////////////////////////////////
//                 Driver factory                                              //
/////////////////////////////////////////////////////////////////////////////////

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
    cfg.strInstanceName   = m_strInstanceName;

    auto driver = std::make_shared<ModbusDriver>(cfg);
    if (!driver->open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("ModbusDriver open failed"));
        return nullptr;
    }

    m_pDriver = driver;
    return m_pDriver;
}

/////////////////////////////////////////////////////////////////////////////////
//                 PLUGIN TOP LEVEL COMMANDS                                   //
/////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------
// MODBUS.INFO
// ------------------------------------------------------------------------------

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
    LOG_PRINT(LOG_EMPTY, LOG_STRING("INI file parameters (copy/paste into your ini file):"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("[MODBUS]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("ARTEFACTS_PATH   =            # directory used by SCRIPT/CMD/wrrdf for reading/writing artefact files"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("HOST             = 127.0.0.1  # Modbus TCP server host to connect to"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("PORT             = 502        # Modbus TCP server port to connect to"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_TIMEOUT     = 2000       # read timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_BUFFER_SIZE = 1024       # size in bytes of the local read buffer"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RAW_RESULT       = false      # CMD returns raw bytes instead of a hexlified string when true"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC_CACHED    = true       # true=validate/parse each CYCLIC entry once per session; false=re-resolve every tick (needed for volatile ?= macros)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note: the CONFIG command above can override a subset of these at runtime;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("      any key not accepted by CONFIG must be set via the ini file."));


    return true;
}

// ------------------------------------------------------------------------------
// MODBUS.CONFIG
// ------------------------------------------------------------------------------

bool ModbusPlugin::m_MODBUS_CONFIG(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    return generic_modbus_set_params(this, args);
}

// ------------------------------------------------------------------------------
// MODBUS.CMD
// ------------------------------------------------------------------------------

bool ModbusPlugin::m_MODBUS_CMD(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    return ucmdexec::generic_cmd(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<ModbusDriver> { return m_OpenDriver(); },
        m_strInstanceName,
        m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, &m_strResultData, m_bRawResult,
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

// ------------------------------------------------------------------------------
// MODBUS.SCRIPT
// ------------------------------------------------------------------------------

bool ModbusPlugin::m_MODBUS_SCRIPT(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    return ucmdexec::generic_script(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<ModbusDriver> { return m_OpenDriver(); },
        m_strInstanceName,
        m_strArtefactsPath, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR,
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const ModbusDriver> drv, std::string_view x) {
            return drv->send(t, d, x);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const ModbusDriver> drv, std::string_view x) {
            return drv->receive(t, b, o, x);
        });
}

// ------------------------------------------------------------------------------
// MODBUS.CYCLIC
// ------------------------------------------------------------------------------

bool ModbusPlugin::m_MODBUS_CYCLIC(const std::string& args, std::stop_token st) const
{
    resetData();

    return ucmdexec::generic_send_cyclic(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<ModbusDriver> { return m_OpenDriver(); },
        m_strInstanceName, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, st, m_bCyclicCached,
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
