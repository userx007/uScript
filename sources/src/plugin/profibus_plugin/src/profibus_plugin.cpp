#include "profibus_plugin.hpp"
#include "private/profibus_setup.hpp"
#include "uCommandExec.hpp"

#include <sstream>

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR  "PROFIBUS PLUGIN |"
#define LOG_HDR LOG_STRING(LT_HDR)

extern "C"
{
    EXPORTED ProfibusPlugin* pluginEntry() { return new ProfibusPlugin(); }
    EXPORTED void pluginExit(ProfibusPlugin *ptrPlugin) { delete ptrPlugin; }
}

bool ProfibusPlugin::doInit(void *pvUserData)
{
    (void)pvUserData;
    m_bIsInitialized = true;
    return true;
}

void ProfibusPlugin::doCleanup(void)
{
    m_bIsInitialized = false;
    m_bIsEnabled = false;
    m_strResultData.clear();
    m_pDriver.reset(); // ~ProfibusDriver() closes the serial port
    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Cleanup done"));
}

bool ProfibusPlugin::setParams(const PluginDataSet *psSetParams)
{
    bool bRetVal = false;
    if (generic_setparams<ProfibusPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
        if (m_LocalSetParams(psSetParams)) {
            bRetVal = true;
        }
    }
    return bRetVal;
}

void ProfibusPlugin::getParams(PluginDataGet *psGetParams) const
{
    generic_getparams<ProfibusPlugin>(this, psGetParams);
}

bool ProfibusPlugin::doDispatch(const std::string& strCmd, const std::string& strParams, std::stop_token st) const
{
    return generic_dispatch<ProfibusPlugin>(this, strCmd, strParams, st);
}

// -----------------------------------------------------------------------
// Driver factory
// -----------------------------------------------------------------------

std::shared_ptr<ProfibusDriver> ProfibusPlugin::m_OpenDriver(void) const
{
    if (m_pDriver && m_pDriver->is_open()) {
        return m_pDriver;
    }

    if (m_strDevice.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Device not configured — PROFIBUS.CONFIG d=<device> first"));
        return nullptr;
    }

    ProfibusDriver::Config cfg;
    cfg.device             = m_strDevice;
    cfg.baud                = m_u32Baud;
    cfg.ownAddress          = m_u8OwnAddress;
    cfg.responseTimeoutMs   = m_u32ResponseTimeout;
    cfg.defaultHighPriority = m_bDefaultHighPriority;
    cfg.strInstanceName     = m_strInstanceName;

    auto driver = std::make_shared<ProfibusDriver>(cfg);
    if (!driver->open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("ProfibusDriver open failed"));
        return nullptr;
    }

    m_pDriver = driver;
    return m_pDriver;
}

// -----------------------------------------------------------------------
// Top-level commands
// -----------------------------------------------------------------------

bool ProfibusPlugin::m_PROFIBUS_INFO(const std::string& args, std::stop_token st) const
{
    (void)args; (void)st;
    resetData();
    std::ostringstream oss;
    oss << PROFIBUS_PLUGIN_NAME " v" << m_strVersion
        << " device=" << m_strDevice
        << " baud=" << m_u32Baud
        << " ownAddress=" << (int)m_u8OwnAddress
        << " responseTimeout=" << m_u32ResponseTimeout << "ms"
        << " defaultPriority=" << (m_bDefaultHighPriority ? "high" : "low");
    m_strResultData = oss.str();

    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING(PROFIBUS_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: PROFIBUS FDL (layer 2) master + passive bus monitor over RS-485/UART"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Architecture: ProfibusProtocol (protocol) / UART (real driver, undecorated) / ProfibusDriver (protocol+driver glue, ICommDriver) / this plugin (CONFIG + wiring only)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note: FDL layer 2 only (SDN/SDA/SRD/FDL-Status) — no DP application layer (Slave_Diag/Set_Prm/Data_Exchange SAPs), no token-ring participation."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note: UART is opened genuinely 8E1 (even parity) per the PROFIBUS spec — see profibus_driver.hpp for the remaining timing limitations."));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the serial device, baud, own station address and timing parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [d=device] [b=baud] [a=own_address] [rt=response_tout] [hp=high_priority] [rb=read_bufsize]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : PROFIBUS.CONFIG d=/dev/ttyUSB0 b=19200 a=2 rt=200"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : b= only accepts rates reachable through the underlying UART driver: 9600, 19200, 500000,"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         and (platform-dependent) 1500000, 3000000 — see INFO's Note above for why the rest aren't."));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : one FDL exchange, on the plugin's single persistent session (opened on first use)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : > <SDN|SDA|SRD> station [hexdata]   |   > STATUS station   |   <"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : PROFIBUS.CMD > SDN 127 0400          // broadcast Global_Control-style telegram, no ack"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         PROFIBUS.CMD > SDA 5                 // send-with-ack, no data | expect ACK"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         PROFIBUS.CMD > SRD 5 0102 | 0304     // send+request data — assert the reply hex"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         PROFIBUS.CMD > STATUS 5              // Request FDL Status | expect e.g. SLAVE:OK"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         PROFIBUS.CMD <                       // one blocking passive-bus-monitor read"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         line ?= PROFIBUS.CMD < &            // background thread; $line tracks the latest telegram seen"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : hexdata is plain hex, e.g. \"0A1B\" — no separators, even digit count."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         Station addresses are 0-127 (127=broadcast, SDN only; 126=reserved for commissioning)."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SRD's reply text is the response payload in hex, or a status word (e.g. NO_RESPONSE_DATA) when the reply carries no data."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         Always pair a '>' line that expects a response (SDA/SRD/STATUS) with its '| expected' — an omitted response is instead read (and mismatched) by the next PROFIBUS.CMD <."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         The GUI comm-dump panel shows the real telegram bytes exchanged (one row per complete telegram, including malformed ones)."));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : run several PROFIBUS.CMD-style lines from a file over the same session"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : scriptpathname [|delay]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : PROFIBUS.SCRIPT script.txt"));
    LOG_SEP();

    return true;
}

// -----------------------------------------------------------------------
// PROFIBUS.CMD / PROFIBUS.SCRIPT — see class doc comment (profibus_plugin.hpp)
// -----------------------------------------------------------------------

bool ProfibusPlugin::m_PROFIBUS_CMD(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    return ucmdexec::generic_cmd(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<ProfibusDriver> { return m_OpenDriver(); },
        m_strInstanceName,
        m_u32ReadBufferSize, m_u32ResponseTimeout, LT_HDR, &m_strResultData, m_bRawResult,
        // Non-capturing: ProfibusDriver::send()/receive() are handed
        // everything they need through the driver parameter itself — see
        // profibus_driver.hpp's class doc comment.
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const ProfibusDriver> drv, std::string_view x) {
            return drv->send(t, d, x);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const ProfibusDriver> drv, std::string_view x) {
            return drv->receive(t, b, o, x);
        });
}

bool ProfibusPlugin::m_PROFIBUS_SCRIPT(const std::string& args, std::stop_token st) const
{
    (void)st;
    resetData();

    return ucmdexec::generic_script(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<ProfibusDriver> { return m_OpenDriver(); },
        m_strInstanceName,
        m_strArtefactsPath, m_u32ReadBufferSize, m_u32ResponseTimeout, LT_HDR,
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const ProfibusDriver> drv, std::string_view x) {
            return drv->send(t, d, x);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const ProfibusDriver> drv, std::string_view x) {
            return drv->receive(t, b, o, x);
        });
}

// -----------------------------------------------------------------------
// PROFIBUS.CYCLIC — see class doc comment (profibus_plugin.hpp)
// -----------------------------------------------------------------------

bool ProfibusPlugin::m_PROFIBUS_CYCLIC(const std::string& args, std::stop_token st) const
{
    resetData();

    return ucmdexec::generic_send_cyclic(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<ProfibusDriver> { return m_OpenDriver(); },
        m_strInstanceName, m_u32ReadBufferSize, m_u32ResponseTimeout, LT_HDR, st, m_bCyclicCached,
        // Non-capturing: ProfibusDriver::send()/receive() are handed
        // everything they need through the driver parameter itself — see
        // profibus_driver.hpp's class doc comment.
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const ProfibusDriver> drv, std::string_view x) {
            return drv->send(t, d, x);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const ProfibusDriver> drv, std::string_view x) {
            return drv->receive(t, b, o, x);
        });
}
