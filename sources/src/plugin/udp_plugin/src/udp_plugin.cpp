#include "uSharedConfig.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include "udp_setup.hpp"
#include "udp_plugin.hpp"

#include "uUdp.hpp"

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

#define LT_HDR     "UDP PLUGIN |"
#define LOG_HDR    LOG_STRING(LT_HDR)


// ============================================================================
// LIFECYCLE
// ============================================================================

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief perform the initialization of modules used by the plugin.
  *
  * Same lazy-open convention as the KVCAN/TCPIP plugins: doInit() only
  * records the plugin as ready to accept setParams()/dispatch() calls. The
  * UDP socket itself is opened per invocation in m_OpenDriver(), called from
  * m_UDP_CMD / m_UDP_SCRIPT, so a stale or unreachable default peer
  * configured at load time does not fail plugin initialization itself.
*/
/*--------------------------------------------------------------------------------------------------------*/
bool UDPPlugin::doInit(void *pvUserData)
{
    bool bRetVal = false;

    do {
        // pvUserData follows the same convention as the sibling comm
        // plugins (UART/I2C/SPI/KVCAN/TCPIP): it carries shared config such
        // as the artefacts path. Adjust the extraction call below to match
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
void UDPPlugin::doCleanup(void)
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
  * Mirrors the KVCAN plugin's handling of the CAN_TX_ID ini entry and the
  * TCPIP plugin's TCP_* keys: pulls the plugin-specific keys out of the
  * ini-backed PluginDataSet and feeds them through the same setter surface
  * the CONFIG command uses, so an ini file and a runtime CONFIG command are
  * always interpreted identically.
  *
  * \note The exact PluginDataSet accessor (getValue() below) is assumed to
  *       match the one used by the KVCAN/TCPIP plugins' m_LocalSetParams();
  *       adjust the calls if this tree's PluginDataSet exposes a different
  *       method name/signature.
*/
/*--------------------------------------------------------------------------------------------------------*/
bool UDPPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    bool bRetVal = true;

    if (nullptr == psSetParams) {
        return false;
    }

    std::string strValue;

    if (true == psSetParams->getValue("UDP_HOST", strValue)) {
        setUdpHost(strValue);
    }

    if (true == psSetParams->getValue("UDP_PORT", strValue)) {
        bRetVal = setUdpPort(strValue) && bRetVal;
    }

    if (true == psSetParams->getValue("UDP_CONNECT_TIMEOUT", strValue)) {
        bRetVal = setConnectTimeout(strValue) && bRetVal;
    }

    if (true == psSetParams->getValue("UDP_READ_TIMEOUT", strValue)) {
        bRetVal = setReadTimeout(strValue) && bRetVal;
    }

    if (true == psSetParams->getValue("UDP_WRITE_TIMEOUT", strValue)) {
        bRetVal = setWriteTimeout(strValue) && bRetVal;
    }

    if (true == psSetParams->getValue("UDP_READ_BUFFER_SIZE", strValue)) {
        bRetVal = setUdpReadBufferSize(strValue) && bRetVal;
    }

    return bRetVal;

} /* m_LocalSetParams() */


// ============================================================================
// DRIVER HELPERS
// ============================================================================

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief open a fresh UDP driver instance against the configured default
  *        peer host/port.
  *
  * Opened per-invocation (from m_UDP_CMD / m_UDP_SCRIPT) rather than held
  * open for the plugin's lifetime — the same pattern the KVCAN plugin uses
  * for its SocketKVCAN handle and the TCPIP plugin uses for its TCP socket.
  * connect()ing a UDP socket does not handshake, so unlike TCPIP this call
  * essentially never blocks on the network; it can still fail synchronously
  * (e.g. invalid address family, resolution failure).
*/
/*--------------------------------------------------------------------------------------------------------*/
std::shared_ptr<ICommDriver> UDPPlugin::m_OpenDriver(void) const
{
    if (m_strUdpHost.empty() || m_u16UdpPort == 0U) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Host/port not configured"));
        return nullptr;
    }

    auto shpDriver = std::make_shared<UDP>();

    if (UDP::Status::SUCCESS != shpDriver->open(m_strUdpHost, m_u16UdpPort, m_u32ConnectTimeout)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Open failed:"); LOG_STRING(m_strUdpHost.c_str());
                  LOG_STRING(":"); LOG_UINT32(m_u16UdpPort));
        return nullptr;
    }

    return shpDriver;

} /* m_OpenDriver() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief split a "d:host:port <payload>" argument string into its optional
  *        destination-override token and the remaining payload.
  *
  * Only a single leading "d:" token is recognised (mirrors CONFIG's "h:"/"p:"
  * etc. token grammar, but this one is read at CMD/SCRIPT dispatch time, not
  * stored). Everything after the first space following it is the payload
  * verbatim, so payloads may contain arbitrary bytes/spaces of their own.
*/
/*--------------------------------------------------------------------------------------------------------*/
void UDPPlugin::m_SplitDestOverride(const std::string& args, std::string& strDest, std::string& strData) const
{
    strDest.clear();
    strData = args;

    if (args.rfind("d:", 0) != 0) {
        // No override token — whole string is payload.
        return;
    }

    const auto szSpacePos = args.find(' ');
    if (szSpacePos == std::string::npos) {
        // "d:host:port" with no payload after it.
        strDest = args.substr(2);
        strData.clear();
        return;
    }

    strDest = args.substr(2, szSpacePos - 2);
    strData = args.substr(szSpacePos + 1);

} /* m_SplitDestOverride() */


// ============================================================================
// SEND / RECEIVE
// ============================================================================

bool UDPPlugin::m_Send(std::span<const uint8_t> data, std::string_view strDestOverride, std::shared_ptr<const ICommDriver> shpDriver) const
{
    if (!shpDriver) {
        return false;
    }

    if (data.size() > UDP::UDP_MAX_DGRAM_LEN) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Payload exceeds UDP_MAX_DGRAM_LEN"));
        return false;
    }

    const auto result = shpDriver->tout_write(m_u32WriteTimeout, data, strDestOverride);

    return (ICommDriver::Status::SUCCESS == result.status) && (result.bytes_written == data.size());

} /* m_Send() */


bool UDPPlugin::m_Receive(std::span<uint8_t> data, size_t& szSize, CommCommandReadType readType, std::shared_ptr<const ICommDriver> shpDriver) const
{
    if (!shpDriver) {
        return false;
    }

    // NOTE: CommCommandReadType is the same generic read-mode selector shared
    // across the UART/I2C/SPI/KVCAN/TCPIP/UDP plugins. The mapping below
    // assumes it carries the same three modes uUdp.hpp's ReadMode does
    // (Exact / UntilDelimiter / UntilToken); adjust the case labels to match
    // whatever enumerators this tree's PluginOperations.hpp actually
    // defines.
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

    // xtra_params is a no-op on the UDP read path (see uUdp.hpp) — the
    // kernel already filters incoming datagrams to the connect()ed default
    // peer — so it is intentionally left empty here.
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
bool UDPPlugin::m_UDP_INFO(const std::string& args, std::stop_token st) const
{
    (void)args;
    (void)st;

    resetData();

    m_strResultData = UDP_PLUGIN_NAME " v" UDP_PLUGIN_VERSION
                     + std::string(" host=") + m_strUdpHost
                     + std::string(" port=") + std::to_string(m_u16UdpPort);

    return true;

} /* m_UDP_INFO() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CONFIG command: apply default-peer/timeout/buffer-size settings
  *        at runtime, using the same key:value grammar as the ini-backed
  *        m_LocalSetParams() (see udp_setup.hpp).
  *
  *        Recognised keys: h:host  p:port  c:connect_tout  r:read_tout
  *        w:write_tout  s:recv_bufsize
*/
/*--------------------------------------------------------------------------------------------------------*/
bool UDPPlugin::m_UDP_CONFIG(const std::string& args, std::stop_token st) const
{
    (void)st;

    resetData();

    return generic_udp_set_params(this, args);

} /* m_UDP_CONFIG() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CMD command: open a socket against the configured default peer,
  *        send the given payload (optionally to a one-off "d:host:port"
  *        override — see m_SplitDestOverride), read back a response, and
  *        report it.
  *
  *        Mirrors m_KVCAN_CMD's / m_TCPIP_CMD's per-call open/use/close
  *        lifecycle: the driver only lives for the duration of this single
  *        dispatch.
*/
/*--------------------------------------------------------------------------------------------------------*/
bool UDPPlugin::m_UDP_CMD(const std::string& args, std::stop_token st) const
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

        std::string strDest;
        std::string strPayload;
        m_SplitDestOverride(args, strDest, strPayload);

        auto shpDriver = m_OpenDriver();
        if (!shpDriver) {
            break;
        }

        const std::span<const uint8_t> sendData(
            reinterpret_cast<const uint8_t*>(strPayload.data()), strPayload.size());

        if (false == m_Send(sendData, strDest, shpDriver)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Send failed"));
            break;
        }

        std::vector<uint8_t> vRecvBuf(m_u32UdpReadBufferSize);
        size_t szRecvSize = 0U;

        if (false == m_Receive(vRecvBuf, szRecvSize, CommCommandReadType::EXACT, shpDriver)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Receive failed"));
            break;
        }

        m_strResultData.assign(reinterpret_cast<const char*>(vRecvBuf.data()), szRecvSize);
        bRetVal = true;

    } while (false);

    return bRetVal;

} /* m_UDP_CMD() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief SCRIPT command: run a scripted sequence of sends/receives over a
  *        single default-peer socket, the UDP analogue of m_KVCAN_SCRIPT /
  *        m_TCPIP_SCRIPT.
  *
  * \note Actual script-line parsing depends on uScriptReader /
  *       uCommScriptCommandInterpreter, neither of which was provided
  *       alongside the KVCAN reference files, so this handler only sets up
  *       and tears down the connection around a single script invocation.
  *       Fill in the interpreter call once its exact API is available.
*/
/*--------------------------------------------------------------------------------------------------------*/
bool UDPPlugin::m_UDP_SCRIPT(const std::string& args, std::stop_token st) const
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
        // m_u32WriteTimeout, m_u32UdpReadBufferSize) to the script
        // interpreter, the same way m_KVCAN_SCRIPT/m_TCPIP_SCRIPT do for
        // their respective sockets.
        (void)st;

        bRetVal = true;

    } while (false);

    return bRetVal;

} /* m_UDP_SCRIPT() */
