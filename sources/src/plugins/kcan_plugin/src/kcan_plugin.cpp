#include "kcan_plugin.hpp"

#include "ICommDriver.hpp"
#include "ICommDumpProtocol.hpp"
#include "ITransportProtocol.hpp"
#include "PluginExport.hpp"
#include "TpFactory.hpp"
#include "kcan_setup.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"
#include "uCommandExec.hpp"
#include "uFile.hpp"
#include "uGuiNotify.hpp"
#include "uHexlify.hpp"
#include "uKCan.hpp"
#include "uKCanLink.hpp"
#include "uLogger.hpp"
#include "uNumeric.hpp"
#include "uPluginSettings.hpp"
#include "uSharedConfig.hpp"
#include "uString.hpp"

#include <cstdio>
#include <memory>
#include <span>
#include <stdint.h>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/////////////////////////////////////////////////////////////////////////////////
//                  PLUGIN ENTRY POINTS                                        //
/////////////////////////////////////////////////////////////////////////////////

/**
 * \brief The plugin's entry points
 */
extern "C" {
    EXPORTED KCANPlugin *pluginEntry()
    {
        return new KCANPlugin();
    }

    EXPORTED void pluginExit(KCANPlugin *pPlugin)
    {
        if (nullptr != pPlugin) {
            delete pPlugin;
        }
    }
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
 *       KCAN.INFO
 *
 * \param[in] args  empty string (no arguments expected)
 *
 * \return true on success, false otherwise
 */
/*--------------------------------------------------------------------------------------------------------*/

bool KCANPlugin::m_KCAN_INFO(const std::string &strArgs, std::stop_token st) const
{
    // expected no arguments
    if (!strArgs.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected no argument(s)"));
        return false;
    }

    // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
    if (!m_bIsEnabled) {
        return true;
    }

    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING(KCAN_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: communicate with REAL CAN adapters via SocketCAN (can0, can1 ... e.g. PEAK PCAN-USB"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("             in SocketCAN mode) incl. bus speed setup; also works on vcan0"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set interface, bus speed, TX/RX ID, transport and transfer parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [i=iface] [b=bitrate] [sp=sample_point] [x=tx_id] [y=rx_id] [r=read_tout]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [w=write_tout] [s=recv_bufsize] [t=tp_protocol]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [db=data_bitrate] [dsp=data_sample_point] [fd=on|off] [lo=on|off] [lb=on|off]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [os=on|off] [ts=on|off] [berr=on|off] [rs=restart_ms] [auto=on|off] [e=err_mask]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : KCAN.CONFIG i=can0 b=500000 x=0x123 r=2000 w=2000 s=8"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         KCAN.CONFIG i=can0 b=250k sp=0.875 x=0x18DAF100"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         KCAN.CONFIG i=can0 b=500k fd=on db=2M      (CAN FD)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         KCAN.CONFIG x=0x7E0 y=0x7E8 t=isotp"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : b=bitrate in bit/s (500000, 500k, 1M); sp=sample point (0.875, 87.5 or 875 = 87.5%)."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         Bitrate / modes are applied to the adapter on first use (auto=on, default) or by"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         UP; needs root or CAP_NET_ADMIN, unless the interface is already up with those settings."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         Without b= the interface is used exactly as the system configured it."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : lo=listen-only (never transmits/ACKs), lb=controller loopback (no bus needed),"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         os=one-shot (no retransmit), ts=triple sampling, berr=report bus errors,"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         rs=bus-off auto-recovery in ms (0 = off), e=CAN_ERR_* mask of error frames to log"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : x=tx_id also becomes the default RX id (an acceptance filter"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         matching exactly tx_id is installed). A per-call xtra_params"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         override applies to that single CMD only; the tx_id/rx_id"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         defaults set here are restored right after it completes."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : y=rx_id sets the id expected for peer responses/handshake"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         frames; only used once t=tp_protocol != none. Omit it when"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         TX and RX share the same id (e.g. loopback / broadcast)."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : t=tp_protocol selects for payloads > single frame one of the following:"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         none | isotp | j1939 | canopen | nmea2000"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         Payloads that already fit one frame are unaffected. Default: none."));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("UP     : apply the configured bus speed / modes to the adapter and bring it up"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [same key=value tokens as CONFIG]  (optional)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : KCAN.UP"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         KCAN.UP i=can0 b=500000"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : briefly takes the link down if it has to change the bit-timing; does nothing"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         when the interface is already up with the requested settings"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("DOWN   : bring the interface down (e.g. before handing the adapter to another tool)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : KCAN.DOWN"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("STATUS : show the interface state; also returned as the command result"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : KCAN.STATUS"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Result : can0: kind=can up=1 carrier=1 mtu=16 state=ERROR-ACTIVE bitrate=500000 ..."));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RESTART: recover a controller from BUS-OFF (needs rs=0 and the interface up)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : KCAN.RESTART"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("FILTER : install hardware acceptance filters on the open socket"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : <id:mask>[,<id:mask>…]  (empty string clears all filters)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : KCAN.FILTER 0x100:0x7FF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         KCAN.FILTER 0x100:0x7FF,0x200:0x7FF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         KCAN.FILTER"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : overrides the RX default derived from CONFIG's x=tx_id"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : send commands from a script file"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : script"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : KCAN.SCRIPT script.txt"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : send, receive or both"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : direction message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : KCAN.CMD > H\"AABBCCDD\" | H\"06\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         KCAN.CMD < \"Ready\" | \"Go!\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : payload must be <= 8 bytes (classic CAN) or <= 64 bytes (CAN FD),"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         unless t=tp_protocol selects a segmented transport (see CONFIG)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC : send one or more periodic messages"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : time1 val1 [id1], time2 val2 [id2], ... (time_i in ms, val_i hex)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : KCAN.CYCLIC 100 AABBCCDD 0x100, 250 1122 0x200"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         KCAN.CYCLIC 100 AABBCCDD 0x100, 250 1122 0x200 &"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : id is optional; when omitted, falls back to the TX id set via CONFIG"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : without '&' sends one full pattern (lcm of the time_i) then returns;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         with '&' repeats forever until the script/thread is stopped"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("INI file parameters (copy/paste into your ini file):"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("[KCAN]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("ARTEFACTS_PATH           =        # directory used by SCRIPT/CMD/wrrdf for reading/writing artefact files"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CAN_IFACE                = can0   # SocketCAN interface name (can0, can1, vcan0 ...)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CAN_BITRATE              =        # bus speed in bit/s (500000, 500k, 1M); empty = leave the interface as configured by the system"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CAN_SAMPLE_POINT         =        # sample point: 0.875 | 87.5 | 875 (= 87.5 %); empty = kernel default"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CAN_DATA_BITRATE         =        # CAN FD data-phase bitrate in bit/s (needs CAN_FD = on)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CAN_DATA_SAMPLE_POINT    =        # CAN FD data-phase sample point"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CAN_FD                   =        # on/off: CAN FD mode"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CAN_LISTEN_ONLY          =        # on/off: listen-only (silent) mode"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CAN_LOOPBACK             =        # on/off: controller internal loopback (frames do not reach the bus)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CAN_ONE_SHOT             =        # on/off: no automatic retransmission"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CAN_TRIPLE_SAMPLING      =        # on/off: sample every bit three times"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CAN_BERR_REPORTING       =        # on/off: report bus errors as error frames"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CAN_RESTART_MS           =        # bus-off auto-recovery delay in ms (0 = off)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CAN_AUTO_LINK            = on     # apply the settings above (bitrate, modes, link up) automatically before first use"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CAN_ERR_MASK             =        # CAN_ERR_* class mask of error frames to log (0x1FFFFFFF = all), empty/0 = none"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CAN_TX_ID                =        # CAN arbitration ID used by CMD/SCRIPT/CYCLIC when sending"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CAN_RX_ID                =        # CAN arbitration ID to filter on when receiving (empty = accept all)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CAN_TP_PROTOCOL          = none   # transport protocol layered on top of raw CAN frames (none/isotp/j1939/canopen/fastpacket)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_BLOCK_SIZE            =        # ISO-TP: block size (BS) sent in our Flow Control frames (0 = no limit)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_ST_MIN                =        # ISO-TP: separation time (STmin) sent in our Flow Control frames, raw encoded byte"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_PAD_FRAMES            =        # ISO-TP: pad SF/CF/FC to 8 bytes (classic CAN convention) when true"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_PADDING_BYTE          =        # ISO-TP: padding fill byte"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_TIMEOUT_NBS           =        # ISO-TP: N_Bs - max wait in ms for Flow Control after our First Frame"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_TIMEOUT_NCR           =        # ISO-TP: N_Cr - max wait in ms for the next Consecutive Frame from the peer"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_MAX_MSG_LEN           =        # ISO-TP: classic 12-bit length field limit"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("J1939_USE_BAM            =        # J1939-21: true = broadcast (BAM), false = peer-to-peer (RTS/CTS)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("J1939_MAX_PACKETS        =        # J1939-21: max packets granted per CTS (RTS/CTS only)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_TIMEOUT_T1            =        # J1939-21: T1 - max wait in ms for CTS after RTS"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_TIMEOUT_T2            =        # J1939-21: T2 - max wait in ms for a data packet after CTS"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_TIMEOUT_T3            =        # J1939-21: T3 - max wait in ms for the next CTS after a burst"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_TIMEOUT_TH            =        # J1939-21: Th (BAM) - max inter-packet gap in ms on the receive side"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("J1939_MAX_MSG_LEN        =        # J1939-21: message size limit"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CANOPEN_INDEX            =        # CANopen SDO: Object Dictionary index of the entry being transferred"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CANOPEN_SUBINDEX         =        # CANopen SDO: Object Dictionary sub-index"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CANOPEN_USE_BLOCK        =        # CANopen SDO: true = block transfer, false = segmented transfer"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CANOPEN_BLOCK_SIZE       =        # CANopen SDO: block transfer segments per block, 1-127"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_TIMEOUT_SDO           =        # CANopen SDO: response timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CANOPEN_MAX_MSG_LEN      =        # CANopen SDO: message size limit"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_TIMEOUT_FP_INTERFRAME =        # Fast Packet: max inter-frame gap in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("FP_MAX_MSG_LEN           =        # Fast Packet: message size limit"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CAN_FILTERS              =        # comma-separated list of hardware CAN ID/mask filter entries"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_TIMEOUT             = 2000   # read timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("WRITE_TIMEOUT            = 2000   # write timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_BUF_SIZE            = 1024   # size in bytes of the local read buffer"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RAW_RESULT               = false  # CMD returns raw bytes instead of a hexlified string when true"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC_CACHED            = true   # true=validate/parse each CYCLIC entry once per session; false=re-resolve every tick (needed for volatile ?= macros)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("(TP_*/J1939_*/CANOPEN_*/FP_* left blank above = keep the transport-protocol"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(" library's own built-in default; they only apply once CAN_TP_PROTOCOL selects"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(" a segmented protocol)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note: the CONFIG command above can override a subset of these at runtime;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("      any key not accepted by CONFIG must be set via the ini file."));

    return true;
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief CONFIG command implementation; overwrite the current KCAN parameters at runtime.
 *
 * \note Any subset of parameters can be specified; omitted keys retain their current values.
 *
 * \note The "x=" key sets both the default TX id (m_u32CanTxId) AND the default
 *       RX id: setCanTxId() replaces m_vFilters with a single acceptance filter
 *       that matches exactly the same CAN id (see setCanTxId() in kcan_plugin.hpp).
 *       These two members are therefore always the "default" Tx/Rx pair applied
 *       to a freshly opened socket by m_KCAN_CMD / m_KCAN_SCRIPT. A per-call
 *       xtra_params override (handled inside the KCAN driver) only affects that
 *       single tout_read()/tout_write() call; the driver restores the previous
 *       filter/TX-id state immediately afterwards, so any following command
 *       issued without xtra_params falls back to these CONFIG-set defaults.
 *       Use the FILTER command afterwards if RX must listen on an id different
 *       from TX.
 *
 * \note Usage example:
 *       KCAN.CONFIG i=vcan0 x=0x123 r=2000 w=2000 s=64
 *       KCAN.CONFIG i=can0 x=0x18DAF100
 *
 * \param[in] args  [i=iface] [x=tx_id] [r=read_tout] [w=write_tout] [s=recv_bufsize]
 *
 * \return true if parameters were updated successfully, false otherwise
 */
/*--------------------------------------------------------------------------------------------------------*/

bool KCANPlugin::m_KCAN_CONFIG(const std::string &strArgs, std::stop_token st) const
{
    return generic_can_set_params<KCANPlugin>(this, strArgs);
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief FILTER command implementation; install CAN hardware acceptance filters.
 *
 * \note Filters are stored in m_vFilters and applied every time a CMD or SCRIPT
 *       opens a new socket.  Calling FILTER with an empty argument clears all
 *       filters (accept everything).
 *
 * \note Usage example:
 *       KCAN.FILTER 0x100:0x7FF
 *       KCAN.FILTER 0x100:0x7FF,0x200:0x7FF
 *       KCAN.FILTER
 *
 * \param[in] args  comma-separated list of <id>:<mask> pairs, or empty to clear
 *
 * \return true on success, false on parse error
 */
/*--------------------------------------------------------------------------------------------------------*/

bool KCANPlugin::m_KCAN_FILTER(const std::string &strArgs, std::stop_token st) const
{
    // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
    if (!m_bIsEnabled) {
        return true;
    }

    std::vector<KCAN::CanFilter> vFilters;

    if (!strArgs.empty()) {
        if (false == m_ParseFilters(strArgs, vFilters)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FILTER: invalid filter string:"); LOG_STRING(strArgs));
            return false;
        }
    }

    m_vFilters = std::move(vFilters);

    LOG_PRINT(LOG_WERBOSE, LOG_HDR;
              LOG_STRING("Filters set, count:"); LOG_UINT32(static_cast<uint32_t>(m_vFilters.size())));

    return true;
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief CMD command implementation; execute a single send/receive operation over KCAN.
 *
 * \note The KCAN socket is opened for the duration of the call and closed automatically on return (RAII).
 *       Filters stored in m_vFilters are applied immediately after open.
 *
 * \note Usage example:
 *       KCAN.CMD > H\"AABBCCDD\" | H\"06\"
 *       KCAN.CMD < \"Ready\" | \"Go!\"
 *
 * \param[in] args  direction and data expression (see CommScriptCommandValidator grammar)
 *
 * \return true on success, false otherwise
 */
/*--------------------------------------------------------------------------------------------------------*/

bool KCANPlugin::m_KCAN_CMD(const std::string &strArgs, std::stop_token st) const
{
    return ucmdexec::generic_cmd(
        strArgs, m_bIsEnabled,
        [this]() -> std::shared_ptr<KCAN> { return m_OpenDriver(); },
        m_strInstanceName,
        m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, &m_strResultData, m_bRawResult,
        // Route every send/receive through m_Send()/m_Receive() instead of the
        // interpreter's default driver->tout_write()/tout_read() — see their
        // doc comments in kcan_plugin.hpp. This is what actually makes
        // CAN_TP_PROTOCOL / "t=" have any effect on a CMD exchange; without
        // it the configured protocol was selected but never consulted.
        [this](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const KCAN> drv, std::string_view x, std::stop_token tok) {
            return m_Send(t, d, drv, x, tok);
        },
        [this](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions &o, std::shared_ptr<const KCAN> drv, std::string_view x, std::stop_token tok) {
            return m_Receive(t, b, o, drv, x, tok);
        },
        st);
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief SCRIPT command implementation; execute a multi-command script file over KCAN.
 *
 * \note The KCAN socket is opened once for the lifetime of the script and closed on return.
 *       Filters stored in m_vFilters are applied immediately after open.
 *
 * \note Usage example:
 *       KCAN.SCRIPT obd_sequence.txt
 *       KCAN.SCRIPT uds_session.txt 10
 *
 * \param[in] args  filename [delay_ms]
 *
 * \return true on success, false otherwise
 */
/*--------------------------------------------------------------------------------------------------------*/

bool KCANPlugin::m_KCAN_SCRIPT(const std::string &strArgs, std::stop_token st) const
{
    return ucmdexec::generic_script(
        strArgs, m_bIsEnabled,
        [this]() -> std::shared_ptr<KCAN> { return m_OpenDriver(); },
        m_strInstanceName,
        m_strArtefactsPath, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR,
        // Same rationale as m_KCAN_CMD() above — a SCRIPT run needs the same
        // TP dispatch as a single CMD, otherwise a SCRIPT-driven send/receive
        // of a message longer than one frame would silently never segment.
        [this](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const KCAN> drv, std::string_view x, std::stop_token tok) {
            return m_Send(t, d, drv, x, tok);
        },
        [this](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions &o, std::shared_ptr<const KCAN> drv, std::string_view x, std::stop_token tok) {
            return m_Receive(t, b, o, drv, x, tok);
        },
        st);
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief CYCLIC command implementation; send one or more periodic KCAN messages.
 *
 * \note The KCAN socket is opened once for the whole CYCLIC session (like SCRIPT) and closed
 *       automatically on return (RAII). Filters stored in m_vFilters are applied immediately
 *       after open. Each entry's optional "id" is the KCAN arbitration id (decimal or 0x-hex,
 *       same syntax KCAN::tout_write()'s xtra_params already accepts — an empty id falls back
 *       to the TX id set via CONFIG/set_tx_id()) and "val" is the payload as a plain hex string
 *       (e.g. "AABBCCDD"), <= 8 bytes classic CAN / <= 64 bytes CAN FD.
 *
 * \note This command bypasses m_Send()/the CAN-TP dispatch on purpose: a cyclic message is by
 *       definition a single, self-contained frame per tick, so the segmented-transport path
 *       (m_eTpProtocol != NONE) used by CMD/SCRIPT for multi-frame payloads does not apply here.
 *
 * \note Usage example:
 *       KCAN.CYCLIC 100 AABBCCDD 0x100, 250 1122 0x200
 *       KCAN.CYCLIC 100 AABBCCDD 0x100, 250 1122 0x200 &
 *
 * \param[in] args  "time1 val1 , time2 val2 , ..." (see generic_send_cyclic())
 * \param[in] st    stop_token; forwarded as-is (present/absent '&' selects run-once vs. forever)
 *
 * \return true on success, false otherwise
 */
/*--------------------------------------------------------------------------------------------------------*/

bool KCANPlugin::m_KCAN_CYCLIC(const std::string &strArgs, std::stop_token st) const
{
    return ucmdexec::generic_send_cyclic(
        strArgs, m_bIsEnabled,
        [this]() -> std::shared_ptr<KCAN> { return m_OpenDriver(); },
        m_strInstanceName, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, st, m_bCyclicCached);
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief UP command implementation; apply the configured bus speed / controller modes to the adapter and
 *        bring the interface up (the `ip link set canX type can bitrate N` + `ip link set canX up` step).
 *
 * \note Optional arguments use the same key=value tokens as CONFIG (i= b= sp= db= dsp= fd= lo= ...); they are
 *       stored first, so "KCAN.UP i=can0 b=500000" is a one-line setup.
 *
 * \note If the interface already is up with the requested settings it is left untouched. Otherwise it is taken
 *       down, programmed and brought up again - this needs root or CAP_NET_ADMIN.
 *
 * \note Usage example:
 *       KCAN.UP
 *       KCAN.UP i=can0 b=500000
 *
 * \param[in] args  optional CONFIG-style key=value tokens
 *
 * \return true on success, false otherwise
 */
/*--------------------------------------------------------------------------------------------------------*/

bool KCANPlugin::m_KCAN_UP(const std::string &strArgs, std::stop_token st) const
{
    if (!strArgs.empty() && false == generic_can_set_params<KCANPlugin>(this, strArgs)) {
        return false;
    }

    // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
    if (!m_bIsEnabled) {
        return true;
    }

    return m_ApplyLink();
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief DOWN command implementation; bring the CAN interface down.
 *
 * \note The next CMD/SCRIPT/CYCLIC brings it up again automatically when a bitrate is configured (auto=on).
 *       Needs root or CAP_NET_ADMIN.
 *
 * \note Usage example:
 *       KCAN.DOWN
 *
 * \param[in] args  empty string (no arguments expected)
 *
 * \return true on success, false otherwise
 */
/*--------------------------------------------------------------------------------------------------------*/

bool KCANPlugin::m_KCAN_DOWN(const std::string &strArgs, std::stop_token st) const
{
    if (!strArgs.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected no argument(s)"));
        return false;
    }

    if (!m_bIsEnabled) {
        return true;
    }

    std::lock_guard<std::mutex> lock(m_mtxLink);

    if (m_strCanIface.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("DOWN: no CAN interface configured (CAN_IFACE / CONFIG i=)"));
        return false;
    }

    if (const int rc = kcan::link_set_up(m_strCanIface, false); rc != 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("DOWN: cannot bring"); LOG_STRING(m_strCanIface); LOG_STRING("down:");
                  LOG_STRING(kcan::describe_errno(rc)));
        return false;
    }

    m_bLinkDirty = true; // whatever comes next has to configure it again
    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING(m_strCanIface); LOG_STRING("is down"));
    return true;
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief STATUS command implementation; report the state of the CAN interface (up/down, bus state,
 *        bitrate, sample point, controller modes, error counters). Needs no privileges.
 *
 * \note The report is logged and also returned as the command result, as space separated key=value pairs.
 *
 * \note Usage example:
 *       KCAN.STATUS
 *
 * \param[in] args  empty string (no arguments expected)
 *
 * \return true on success, false if the interface does not exist or cannot be read
 */
/*--------------------------------------------------------------------------------------------------------*/

bool KCANPlugin::m_KCAN_STATUS(const std::string &strArgs, std::stop_token st) const
{
    if (!strArgs.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected no argument(s)"));
        return false;
    }

    if (!m_bIsEnabled) {
        return true;
    }

    if (m_strCanIface.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("STATUS: no CAN interface configured (CAN_IFACE / CONFIG i=)"));
        return false;
    }

    kcan::LinkStatus sStatus;
    if (const int rc = kcan::link_get_status(m_strCanIface, sStatus); rc != 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("STATUS:"); LOG_STRING(m_strCanIface); LOG_STRING(":"); LOG_STRING(kcan::describe_errno(rc)));
        return false;
    }

    m_strResultData = kcan::format_status(m_strCanIface, sStatus);
    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING(m_strResultData));
    return true;
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief RESTART command implementation; recover the CAN controller from the BUS-OFF state
 *        (`ip link set canX type can restart`).
 *
 * \note Only possible while the interface is up and automatic recovery (rs=) is off. Needs root or CAP_NET_ADMIN.
 *
 * \note Usage example:
 *       KCAN.RESTART
 *
 * \param[in] args  empty string (no arguments expected)
 *
 * \return true on success, false otherwise
 */
/*--------------------------------------------------------------------------------------------------------*/

bool KCANPlugin::m_KCAN_RESTART(const std::string &strArgs, std::stop_token st) const
{
    if (!strArgs.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected no argument(s)"));
        return false;
    }

    if (!m_bIsEnabled) {
        return true;
    }

    if (m_strCanIface.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("RESTART: no CAN interface configured (CAN_IFACE / CONFIG i=)"));
        return false;
    }

    if (const int rc = kcan::link_restart(m_strCanIface); rc != 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("RESTART:"); LOG_STRING(m_strCanIface); LOG_STRING(":"); LOG_STRING(kcan::describe_errno(rc));
                  LOG_STRING("(restart needs the interface up and restart-ms == 0)"));
        return false;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING(m_strCanIface); LOG_STRING("restarted"));
    return true;
}

/////////////////////////////////////////////////////////////////////////////////
//                 PLUGIN PRIVATE INTERFACES IMPLEMENTATION                    //
/////////////////////////////////////////////////////////////////////////////////

// DRIVER DECORATOR

namespace {
    /**
     * \brief Thin ICommDriver decorator that reports every physical tout_write()/
     *        tout_read() call to the GUI comm-dump panel before returning.
     *
     * \note  Why this exists: ITransportProtocol::send()/receive() (see cantp)
     *        turn one logical message into however many physical CAN frames the
     *        segmented protocol needs (SF/FF/CF/FC, ...), calling driver.tout_write()/
     *        tout_read() once per frame. Wrapping the real driver with this
     *        decorator before handing it to send()/receive() means every one of
     *        those physical frames — PCI byte, padding and all — gets its own
     *        accurate comm-dump row, instead of a single row showing the
     *        pre-segmentation logical payload (which is what the generic
     *        CommScriptCommandInterpreter would otherwise produce — see
     *        uCommScriptCommandInterpreter.hpp's pfsend/pfrecv override).
     *
     * \note  Not used on the TpProtocol::NONE path: there, one call already maps
     *        to exactly one physical frame, so KCANPlugin::m_Send()/m_Receive()
     *        dump directly instead of paying for a decorator.
     */
    class DumpingDriver : public ICommDriver {
        public:
            DumpingDriver(std::shared_ptr<const KCAN> shpInner, std::string strPluginName)
                : m_shpInner(std::move(shpInner))
                , m_strPluginName(std::move(strPluginName))
            {
            }

            bool is_open() const override
            {
                return m_shpInner->is_open();
            }

            CommDetails describeConnection(std::string_view xtra_params = {}) const override
            {
                return m_shpInner->describeConnection(xtra_params);
            }

            ReadResult tout_read(uint32_t u32ReadTimeout, std::span<uint8_t> buffer,
                                 const ReadOptions &sOptions, std::string_view xtra_params = {},
                                 std::stop_token stop_tok = {}) const override
            {
                auto result = m_shpInner->tout_read(u32ReadTimeout, buffer, sOptions, xtra_params, stop_tok);
                if (result.status == Status::SUCCESS && result.bytes_read > 0 && gui_mode_active()) {
                    gui_notify_comm_dump(m_strPluginName, m_shpInner->describeRxConnection(),
                                         CommDir::Rx, buffer.data(), static_cast<uint32_t>(result.bytes_read));
                }
                return result;
            }

            WriteResult tout_write(uint32_t u32WriteTimeout, std::span<const uint8_t> buffer,
                                   std::string_view xtra_params = {},
                                   std::stop_token stop_tok     = {}) const override
            {
                auto result = m_shpInner->tout_write(u32WriteTimeout, buffer, xtra_params, stop_tok);
                if (result.status == Status::SUCCESS && result.bytes_written > 0 && gui_mode_active()) {
                    gui_notify_comm_dump(m_strPluginName, m_shpInner->describeConnection(xtra_params),
                                         CommDir::Tx, buffer.data(), static_cast<uint32_t>(result.bytes_written));
                }
                return result;
            }

        private:
            std::shared_ptr<const KCAN> m_shpInner;
            std::string m_strPluginName;
    };

} // anonymous namespace

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Make the CAN interface match the configured link settings (see kcan::link_apply()).
 */
/*--------------------------------------------------------------------------------------------------------*/

bool KCANPlugin::m_ApplyLink() const
{
    std::lock_guard<std::mutex> lock(m_mtxLink);

    if (m_strCanIface.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("No CAN interface configured (CAN_IFACE / CONFIG i=)"));
        return false;
    }

    if ((m_sLinkCfg.ctrlModeMask & kcan::CTRLMODE_FD) && (m_sLinkCfg.ctrlModeFlags & kcan::CTRLMODE_FD) && m_sLinkCfg.dataBitrate == 0U) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("fd=on requires a data bitrate (db= / CAN_DATA_BITRATE), e.g. db=2M"));
        return false;
    }
    if (m_sLinkCfg.dataBitrate != 0U && !((m_sLinkCfg.ctrlModeMask & kcan::CTRLMODE_FD) && (m_sLinkCfg.ctrlModeFlags & kcan::CTRLMODE_FD))) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("data bitrate is set but CAN FD is not enabled (fd=on) - the data bitrate has no effect"));
    }

    bool bChanged = false;
    if (const int rc = kcan::link_apply(m_strCanIface, m_sLinkCfg, bChanged); rc != 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Cannot set up"); LOG_STRING(m_strCanIface); LOG_STRING(":"); LOG_STRING(kcan::describe_errno(rc)));
        return false;
    }

    m_bLinkDirty = false;

    kcan::LinkStatus sStatus;
    if (0 == kcan::link_get_status(m_strCanIface, sStatus)) {
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING(bChanged ? "Interface configured:" : "Interface already as requested:");
                  LOG_STRING(kcan::format_status(m_strCanIface, sStatus)));
    }
    return true;
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Open the CAN socket for one CMD / SCRIPT / CYCLIC run.
 */
/*--------------------------------------------------------------------------------------------------------*/

std::shared_ptr<KCAN> KCANPlugin::m_OpenDriver() const
{
    // Bus speed first: a real adapter does not carry any frame until it is configured and up.
    if (m_bAutoLink && m_bLinkDirty && !m_sLinkCfg.empty()) {
        if (!m_ApplyLink()) {
            return nullptr;
        }
    }

    // Open the CAN socket (RAII - closed automatically by destructor)
    auto shpDriver = std::make_shared<KCAN>(m_strCanIface, m_strCanIface);

    if (!shpDriver->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Cannot open CAN interface"); LOG_STRING(m_strCanIface);
                  LOG_STRING("- does it exist (ip link show) and is it up? Try KCAN.UP / CONFIG b=<bitrate>"));
        return nullptr;
    }

    // Apply TX ID, acceptance filters and error-frame mask
    shpDriver->set_tx_id(m_u32CanTxId);

    if (!m_vFilters.empty()) {
        shpDriver->set_filters(m_vFilters);
    }

    if (m_u32ErrMask != 0U) {
        shpDriver->set_error_mask(m_u32ErrMask);
    }

    return shpDriver;
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Parse a comma-separated "<id>:<mask>" filter string into a vector of KCAN::CanFilter.
 *        Both id and mask fields accept decimal or 0x-prefixed hex values.
 *        Example: "0x100:0x7FF,0x200:0x7FF"
 */
/*--------------------------------------------------------------------------------------------------------*/

bool KCANPlugin::m_ParseFilters(const std::string &strFilters, std::vector<KCAN::CanFilter> &vFilters) const
{
    vFilters.clear();

    // SocketCAN frame-ID flag bits (mirrors linux/can.h — kept local so the
    // plugin does not need a kernel header dependency at this level).
    static constexpr uint32_t CAN_EFF_FLAG = 0x80000000U; // extended (29-bit) frame
    static constexpr uint32_t CAN_RTR_FLAG = 0x40000000U; // remote-transmission request
    static constexpr uint32_t CAN_ERR_FLAG = 0x20000000U; // error frame
    static constexpr uint32_t CAN_SFF_MASK = 0x000007FFU; // 11-bit SFF id mask
    static constexpr uint32_t CAN_EFF_MASK = 0x1FFFFFFFU; // 29-bit EFF id mask

    // Split on commas to get individual "<id>:<mask>" tokens
    std::vector<std::string> vstrEntries;
    ustring::tokenize(strFilters, ',', vstrEntries);

    for (const auto &strEntry : vstrEntries) {
        // Split each entry on ':' to separate id from mask
        std::vector<std::string> vstrParts;
        ustring::tokenize(strEntry, ':', vstrParts);

        if (vstrParts.size() != 2) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Filter entry malformed (expected id:mask):"); LOG_STRING(strEntry));
            return false;
        }

        KCAN::CanFilter filter = {};

        if (false == numeric::str2uint32(ustring::trim(vstrParts[0]), filter.can_id)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Filter id parse failed:"); LOG_STRING(vstrParts[0]));
            return false;
        }

        if (false == numeric::str2uint32(ustring::trim(vstrParts[1]), filter.can_mask)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Filter mask parse failed:"); LOG_STRING(vstrParts[1]));
            return false;
        }

        // ── EFF / SFF flag fixup ─────────────────────────────────────────────
        // SocketCAN's kernel filter comparison is:
        //   (received_id & filter.can_mask) == (filter.can_id & filter.can_mask)
        //
        // The CAN_EFF_FLAG bit (bit 31) is part of the frame ID word that the
        // kernel compares.  If the user wants to match a 29-bit extended frame
        // the flag must be set in BOTH can_id AND can_mask, otherwise:
        //   • can_id has CAN_EFF_FLAG set but can_mask does not → the flag bit
        //     is masked out of both sides and the filter also matches standard
        //     frames whose lower 11 bits happen to equal the EFF id's lower 11
        //     bits — unintended false positives.
        //   • can_id has CAN_EFF_FLAG clear but the target id > 0x7FF → the id
        //     is silently truncated to 11 bits, matching the wrong frames.
        //
        // Likewise, the RTR and ERR flags must be included in the mask if they
        // are set in can_id so the comparison is unambiguous.
        //
        // Auto-correct: propagate every flag bit that is set in can_id into
        // can_mask, and clamp the id's data bits to the legal range for the
        // chosen frame format.
        const uint32_t flagsInId = filter.can_id & (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG);
        filter.can_mask |= flagsInId; // ensure every flag present in id is also masked

        if (filter.can_id & CAN_EFF_FLAG) {
            // 29-bit extended frame: id data bits must fit in CAN_EFF_MASK
            filter.can_id &= (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_EFF_MASK);
            filter.can_mask &= (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG | CAN_EFF_MASK);
        } else {
            // 11-bit standard frame: id data bits must fit in CAN_SFF_MASK.
            // If the user supplied an id > 0x7FF without the EFF flag they most
            // likely forgot it — log a warning and set the flag automatically so
            // the filter targets the intended extended frame rather than silently
            // matching wrong standard frames.
            if ((filter.can_id & CAN_EFF_MASK) > CAN_SFF_MASK) {
                LOG_PRINT(LOG_WARNING, LOG_HDR;
                          LOG_STRING("Filter id > 0x7FF without CAN_EFF_FLAG — setting EFF flag automatically:"); LOG_STRING(strEntry));
                filter.can_id |= CAN_EFF_FLAG;
                filter.can_mask |= CAN_EFF_FLAG;
                filter.can_id &= (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_EFF_MASK);
                filter.can_mask &= (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG | CAN_EFF_MASK);
            } else {
                filter.can_id &= (CAN_RTR_FLAG | CAN_SFF_MASK);
                filter.can_mask &= (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG | CAN_SFF_MASK);
            }
        }

        vFilters.push_back(filter);
    }

    return true;
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief message sender
 */
/*--------------------------------------------------------------------------------------------------------*/

ICommDriver::WriteResult KCANPlugin::m_Send(uint32_t u32WriteTimeout, std::span<const uint8_t> dataSpan,
                                            std::shared_ptr<const KCAN> shpDriver, std::string_view xtra_params,
                                            std::stop_token stop_tok) const
{
    ICommDriver::WriteResult result;

    if (m_eTpProtocol == TpProtocol::NONE) {
        if (dataSpan.size() > 8) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid length for a single CAN frame:"); LOG_SIZET(dataSpan.size()); LOG_STRING("(no TP protocol was set)"));
            result.status = ICommDriver::Status::INVALID_PARAM;
            return result;
        }
        result = shpDriver->tout_write(u32WriteTimeout, dataSpan, xtra_params, stop_tok);

        if (result.status == ICommDriver::Status::SUCCESS && result.bytes_written > 0 && gui_mode_active()) {
            gui_notify_comm_dump(m_strInstanceName, shpDriver->describeConnection(xtra_params),
                                 CommDir::Tx, dataSpan.data(), static_cast<uint32_t>(result.bytes_written));
        }
    } else {
        // Segmented transport: payloads that still fit in a single frame take
        // the same one-frame path internally (see e.g. IsoTpProtocol::send()),
        // so enabling a protocol never changes behaviour for short payloads.
        //
        // xtra_params is intentionally not applied here (same as before this
        // fix): a segmented exchange needs a *paired* TX/RX id, which a single
        // per-call override string can't express — see setCanRxId()'s docs.
        auto upTp = make_transport_protocol(m_eTpProtocol, m_sTpConfig);
        if (!upTp) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to instantiate transport protocol"));
            result.status = ICommDriver::Status::OPERATION_FAILED;
            return result;
        }

        char szTxId[16];
        char szRxId[16];
        std::snprintf(szTxId, sizeof(szTxId), "0x%X", m_u32CanTxId);
        std::snprintf(szRxId, sizeof(szRxId), "0x%X", m_bCanRxIdSet ? m_u32CanRxId : m_u32CanTxId);

        // Every physical frame send() emits (SF/FF/CF, PCI byte and padding
        // included) is reported to the GUI comm-dump panel by the decorator —
        // see DumpingDriver above.
        DumpingDriver sDumpingDriver(shpDriver, m_strInstanceName);
        // TODO(stop-token): upTp->send() doesn't accept stop_tok yet, so a
        // segmented (ISO-TP) send is not cancellable via the STOP button.
        result = upTp->send(sDumpingDriver, u32WriteTimeout, dataSpan, szTxId, szRxId);
    }

    if (result.status != ICommDriver::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Write failed:");
                  LOG_STRING(ICommDriver::to_string(result.status));
                  LOG_STRING("Bytes written:"); LOG_SIZET(result.bytes_written));
    }

    return result;
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief message receiver
 */
/*--------------------------------------------------------------------------------------------------------*/

ICommDriver::ReadResult KCANPlugin::m_Receive(uint32_t u32ReadTimeout, std::span<uint8_t> dataSpan,
                                              const ICommDriver::ReadOptions &options,
                                              std::shared_ptr<const KCAN> shpDriver, std::string_view xtra_params,
                                              std::stop_token stop_tok) const
{
    ICommDriver::ReadResult result;

    // Delimiter/token reads are an ASCII-stream concept (line or token
    // search across raw frame payloads); segmented binary transports don't
    // have a notion of either, so those two modes always use the driver's
    // legacy framing regardless of m_eTpProtocol. Only the default
    // "exact/raw" read benefits from — and requires — TP reassembly.
    const bool bWantsRawExact = (options.mode == ICommDriver::ReadMode::Exact);

    if (m_eTpProtocol != TpProtocol::NONE && bWantsRawExact) {
        auto upTp = make_transport_protocol(m_eTpProtocol, m_sTpConfig);
        if (!upTp) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to instantiate transport protocol"));
            result.status = ICommDriver::Status::OPERATION_FAILED;
            return result;
        }

        char szTxId[16];
        char szRxId[16];
        std::snprintf(szTxId, sizeof(szTxId), "0x%X", m_u32CanTxId);
        std::snprintf(szRxId, sizeof(szRxId), "0x%X", m_bCanRxIdSet ? m_u32CanRxId : m_u32CanTxId);

        // Every physical frame receive() consumes (SF/FF/CF, FC we send back,
        // PCI byte and padding included) is reported to the GUI comm-dump
        // panel by the decorator — see DumpingDriver above.
        DumpingDriver sDumpingDriver(shpDriver, m_strInstanceName);
        // TODO(stop-token): upTp->receive() doesn't accept stop_tok yet, so a
        // segmented (ISO-TP) receive is not cancellable via the STOP button.
        result = upTp->receive(sDumpingDriver, u32ReadTimeout, dataSpan, szRxId, szTxId);
    } else {
        // Raw single-frame path (TpProtocol::NONE, or a LINE/TOKEN read type
        // that always bypasses TP) — one call maps to one physical read,
        // exactly as before this feature existed; xtra_params still overrides
        // the RX filter for this single call.
        result = shpDriver->tout_read(u32ReadTimeout, dataSpan, options, xtra_params, stop_tok);

        // ReadMode::UntilToken leaves bytes_read == 0 by design (the matched
        // bytes are consumed internally and never copied into the caller's
        // buffer — see uCommScriptCommandInterpreter.hpp's receiveUntilToken()),
        // so there is nothing meaningful to dump for that mode; the bytes_read
        // > 0 guard below already skips it.
        if (result.status == ICommDriver::Status::SUCCESS && result.bytes_read > 0 && gui_mode_active()) {
            gui_notify_comm_dump(m_strInstanceName, shpDriver->describeRxConnection(),
                                 CommDir::Rx, dataSpan.data(), static_cast<uint32_t>(result.bytes_read));
        }
    }

    if (result.status != ICommDriver::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Read failed:");
                  LOG_STRING(ICommDriver::to_string(result.status));
                  LOG_STRING("Bytes read:"); LOG_SIZET(result.bytes_read));
    }

    return result;
}
