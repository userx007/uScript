#include "uSharedConfig.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include "tcpip_setup.hpp"
#include "tcpip_plugin.hpp"
#include "uTcpip.hpp"

#include <memory>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "TCPIP PLUGIN |"
#define LOG_HDR    LOG_STRING(LT_HDR)


// ============================================================================
// LIFECYCLE
// ============================================================================

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief perform the initialization of modules used by the plugin.
  *
  * Unlike KVCAN — where doInit() has nothing to open (the SocketKVCAN
  * interface is opened lazily, per command, against m_strCanIface) — TCPIP
  * follows the same lazy-open convention: doInit() only records the plugin
  * as ready to accept setParams()/dispatch() calls. The actual TCP connect
  * happens per invocation in m_OpenDriver(), called from m_TCPIP_CMD /
  * m_TCPIP_SCRIPT, so a stale or unreachable host configured at load time
  * does not fail plugin initialization itself.
*/
/*--------------------------------------------------------------------------------------------------------*/
bool TCPIPPlugin::doInit(void *pvUserData)
{
    bool bRetVal = false;

    do {
        // pvUserData follows the same convention as the sibling comm
        // plugins (UART/I2C/SPI/KVCAN): it carries shared config such as
        // the artefacts path. Adjust the extraction call below to match
        // whatever uSharedConfig actually exposes in this tree.
        if (nullptr != pvUserData) {
            m_strArtefactsPath = uSharedConfig::getArtefactsPath(pvUserData);
        }

        m_bIsInitialized = true;
        bRetVal          = true;

        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Initialized"));

    } while (false);

    return bRetVal;

} /* doInit() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief perform the de-initialization of modules used by the plugin.
*/
/*--------------------------------------------------------------------------------------------------------*/
void TCPIPPlugin::doCleanup(void)
{
    m_bIsInitialized = false;
    m_bIsEnabled     = false;
    m_strResultData.clear();

    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("Cleanup done"));

} /* doCleanup() */


// ============================================================================
// PARAMETER HANDLING
// ============================================================================

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief processing of the plugin specific settings.
  *
  * Mirrors the KVCAN plugin's handling of the CAN_TX_ID ini entry: pulls the
  * plugin-specific keys out of the ini-backed PluginDataSet and feeds them
  * through the same setter surface the CONFIG command uses, so an ini file
  * and a runtime CONFIG command are always interpreted identically.
  *
  * \note The exact PluginDataSet accessor (getValue() below) is assumed to
  *       match the one used by the KVCAN plugin's m_LocalSetParams(); adjust
  *       the calls if this tree's PluginDataSet exposes a different method
  *       name/signature.
*/
/*--------------------------------------------------------------------------------------------------------*/
bool TCPIPPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    bool bRetVal = true;

    if (nullptr == psSetParams) {
        return false;
    }

    std::string strValue;

    if (true == psSetParams->getValue("TCP_HOST", strValue)) {
        setTcpHost(strValue);
    }

    if (true == psSetParams->getValue("TCP_PORT", strValue)) {
        bRetVal = setTcpPort(strValue) && bRetVal;
    }

    if (true == psSetParams->getValue("TCP_CONNECT_TIMEOUT", strValue)) {
        bRetVal = setConnectTimeout(strValue) && bRetVal;
    }

    if (true == psSetParams->getValue("TCP_READ_TIMEOUT", strValue)) {
        bRetVal = setReadTimeout(strValue) && bRetVal;
    }

    if (true == psSetParams->getValue("TCP_WRITE_TIMEOUT", strValue)) {
        bRetVal = setWriteTimeout(strValue) && bRetVal;
    }

    if (true == psSetParams->getValue("TCP_READ_BUFFER_SIZE", strValue)) {
        bRetVal = setTcpReadBufferSize(strValue) && bRetVal;
    }

    return bRetVal;

} /* m_LocalSetParams() */


// ============================================================================
// DRIVER HELPERS
// ============================================================================

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief open a fresh TCPIP driver instance against the configured
  *        host/port, honouring the configured connect timeout.
  *
  * Opened per-invocation (from m_TCPIP_CMD / m_TCPIP_SCRIPT) rather than
  * held open for the plugin's lifetime, the same pattern the KVCAN plugin
  * uses for its SocketKVCAN handle: this keeps a single command's failure
  * (e.g. an unreachable peer) from poisoning the state of the next one.
*/
/*--------------------------------------------------------------------------------------------------------*/
std::shared_ptr<ICommDriver> TCPIPPlugin::m_OpenDriver(void) const
{
    if (m_strTcpHost.empty() || m_u16TcpPort == 0U) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Host/port not configured"));
        return nullptr;
    }

    auto shpDriver = std::make_shared<TCPIP>();

    if (TCPIP::Status::SUCCESS != shpDriver->open(m_strTcpHost, m_u16TcpPort, m_u32ConnectTimeout)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Connect failed:"); LOG_STRING(m_strTcpHost.c_str());
                  LOG_STRING(":"); LOG_UINT32(m_u16TcpPort));
        return nullptr;
    }

    return shpDriver;

} /* m_OpenDriver() */


// ============================================================================
// SEND / RECEIVE
// ============================================================================

bool TCPIPPlugin::m_Send(std::span<const uint8_t> data, std::shared_ptr<const ICommDriver> shpDriver) const
{
    if (!shpDriver) {
        return false;
    }

    const auto result = shpDriver->tout_write(m_u32WriteTimeout, data);

    return (ICommDriver::Status::SUCCESS == result.status) && (result.bytes_written == data.size());

} /* m_Send() */


bool TCPIPPlugin::m_Receive(std::span<uint8_t> data, size_t& szSize, CommCommandReadType readType, std::shared_ptr<const ICommDriver> shpDriver) const
{
    if (!shpDriver) {
        return false;
    }

    // NOTE: CommCommandReadType is the same generic read-mode selector shared
    // across the UART/I2C/SPI/KVCAN/TCPIP plugins. The mapping below assumes
    // it carries the same three modes uTcpip.hpp's ReadMode does (Exact /
    // UntilDelimiter / UntilToken); adjust the case labels to match whatever
    // enumerators this tree's PluginOperations.hpp actually defines. Only
    // Exact is unambiguous with the signature available here (no delimiter/
    // token payload is passed in) — the other two are wired up assuming a
    // sensible default delimiter/token is supplied elsewhere (e.g. a member
    // set via CONFIG), which this skeleton does not yet do.
    ICommDriver::ReadOptions options{};

    switch (readType) {
        case CommCommandReadType::EXACT:
            options.mode = ICommDriver::ReadMode::Exact;
            break;
        case CommCommandReadType::UNTIL_DELIMITER:
            options.mode = ICommDriver::ReadMode::UntilDelimiter;
            break;
        case CommCommandReadType::UNTIL_TOKEN:
            options.mode = ICommDriver::ReadMode::UntilToken;
            break;
        default:
            options.mode = ICommDriver::ReadMode::Exact;
            break;
    }

    const auto result = shpDriver->tout_read(m_u32ReadTimeout, data, options);

    szSize = result.bytes_read;

    return (ICommDriver::Status::SUCCESS == result.status);

} /* m_Receive() */


// ============================================================================
// COMMAND HANDLERS
// ============================================================================

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief INFO command: report plugin version and current configuration.
*/
/*--------------------------------------------------------------------------------------------------------*/
bool TCPIPPlugin::m_TCPIP_INFO(const std::string& args, std::stop_token st) const
{
    (void)args;
    (void)st;

    resetData();

    m_strResultData = TCPIP_PLUGIN_NAME " v" TCPIP_PLUGIN_VERSION
                     + std::string(" host=") + m_strTcpHost
                     + std::string(" port=") + std::to_string(m_u16TcpPort);

    return true;

} /* m_TCPIP_INFO() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CONFIG command: apply host/port/timeout/buffer-size settings at
  *        runtime, using the same key:value grammar as the ini-backed
  *        m_LocalSetParams() (see tcpip_setup.hpp).
  *
  *        Recognised keys: h:host  p:port  c:connect_tout  r:read_tout
  *        w:write_tout  s:recv_bufsize
*/
/*--------------------------------------------------------------------------------------------------------*/
bool TCPIPPlugin::m_TCPIP_CONFIG(const std::string& args, std::stop_token st) const
{
    (void)st;

    resetData();

    return generic_tcp_set_params(this, args);

} /* m_TCPIP_CONFIG() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CMD command: open a connection to the configured host:port, send
  *        the given payload, read back a response, and report it.
  *
  *        Mirrors m_KVCAN_CMD's per-call open/use/close lifecycle: the
  *        driver only lives for the duration of this single dispatch.
*/
/*--------------------------------------------------------------------------------------------------------*/
bool TCPIPPlugin::m_TCPIP_CMD(const std::string& args, std::stop_token st) const
{
    (void)st;

    bool bRetVal = false;

    resetData();

    do {
        if (!m_bIsEnabled) {
            // Un-enabled plugins validate arguments only; no real I/O.
            bRetVal = !args.empty();
            break;
        }

        auto shpDriver = m_OpenDriver();
        if (!shpDriver) {
            break;
        }

        const std::span<const uint8_t> sendData(
            reinterpret_cast<const uint8_t*>(args.data()), args.size());

        if (false == m_Send(sendData, shpDriver)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Send failed"));
            break;
        }

        std::vector<uint8_t> vRecvBuf(m_u32TcpReadBufferSize);
        size_t szRecvSize = 0U;

        if (false == m_Receive(vRecvBuf, szRecvSize, CommCommandReadType::EXACT, shpDriver)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Receive failed"));
            break;
        }

        m_strResultData.assign(reinterpret_cast<const char*>(vRecvBuf.data()), szRecvSize);
        bRetVal = true;

    } while (false);

    return bRetVal;

} /* m_TCPIP_CMD() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief SCRIPT command: run a scripted sequence of sends/receives over a
  *        single connection, the TCPIP analogue of m_KVCAN_SCRIPT.
  *
  * \note Actual script-line parsing depends on uScriptReader /
  *       uCommScriptCommandInterpreter, neither of which was provided
  *       alongside the KVCAN reference files, so this handler only sets up
  *       and tears down the connection around a single script invocation.
  *       Fill in the interpreter call once its exact API is available.
*/
/*--------------------------------------------------------------------------------------------------------*/
bool TCPIPPlugin::m_TCPIP_SCRIPT(const std::string& args, std::stop_token st) const
{
    bool bRetVal = false;

    resetData();

    do {
        if (!m_bIsEnabled) {
            bRetVal = !args.empty();
            break;
        }

        auto shpDriver = m_OpenDriver();
        if (!shpDriver) {
            break;
        }

        // TODO: hand off (args, st, shpDriver, m_u32ReadTimeout,
        // m_u32WriteTimeout, m_u32TcpReadBufferSize) to the script
        // interpreter, the same way m_KVCAN_SCRIPT does for its socket.
        (void)st;

        bRetVal = true;

    } while (false);

    return bRetVal;

} /* m_TCPIP_SCRIPT() */
