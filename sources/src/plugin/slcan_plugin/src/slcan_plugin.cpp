#include "uSharedConfig.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include "slcan_setup.hpp"
#include "slcan_plugin.hpp"

#include "uPluginSettings.hpp"

#include "uNumeric.hpp"
#include "uFile.hpp"
#include "uString.hpp"
#include "uCommandExec.hpp"

/////////////////////////////////////////////////////////////////////////////////
//                  PLUGIN ENTRY POINTS                                        //
/////////////////////////////////////////////////////////////////////////////////

extern "C"
{
    EXPORTED SLCANPlugin* pluginEntry()
    {
        return new SLCANPlugin();
    }

    EXPORTED void pluginExit( SLCANPlugin *ptrPlugin)
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
  * \brief INFO command implementation; shows details about the plugin and
  *        describes the supported functions with examples of usage.
  *        This command takes no arguments and is executed even if plugin initialization fails.
  *
  * \note Usage example:
  *       SLCAN.INFO
  *
  * \param[in] args  empty string (no arguments expected)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool SLCANPlugin::m_SLCAN_INFO (const std::string &args, std::stop_token st) const
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
    LOG_PRINT(LOG_EMPTY, LOG_STRING(SLCAN_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: communicate via an SLCAN ASCII adapter (WeActStudio USB2CANFDV1) over UART"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the UART device, CAN bus parameters and transfer parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [i=device] [p=uart_baud] [b=bitrate] [y=fd_rate] [m=mode]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [a=auto_retx] [z=fd_brs] [x=tx_id] [v=rx_id] [r=read_tout]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [w=write_tout] [s=recv_bufsize] [t=tp_protocol]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : SLCAN.CONFIG i=/dev/ttyACM0 p=115200 b=6 x=0x123 r=2000 w=2000 s=64"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SLCAN.CONFIG i=/dev/ttyACM0 b=4 x=0x18DAF100"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SLCAN.CONFIG x=0x7E0 v=0x7E8 t=isotp"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : b is the S-command preset 0-13 (4=125k, adapter default); y is Y1-Y5 (2=2M, adapter default)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : x=tx_id also becomes the default RX id (the matching std/ext"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         filter slot is set to an exact match, the other slot cleared)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : v=rx_id sets the id expected for peer responses/handshake"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         frames; only used once t=tp_protocol != none. Omit it when TX"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         and RX share the same id. Still needs FILTER coverage — see below."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : t=tp_protocol selects none|isotp|j1939 for payloads that exceed"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         a single frame. Payloads that already fit one frame are"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         unaffected. Default: none (today's behaviour)."));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("FILTER : install the adapter's standard/extended acceptance filters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : <id:mask>[,<id:mask>]  (one std + one ext slot max; empty clears both)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : SLCAN.FILTER 0x100:0x7FF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SLCAN.FILTER 0x100:0x7FF,0x18DAF100:0x1FFFFFFF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SLCAN.FILTER"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : overrides the RX default derived from CONFIG's x=tx_id"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : send commands from a script file"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : scriptpathname [delay_ms]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : SLCAN.SCRIPT script.txt"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : send, receive or both"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : direction message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : SLCAN.CMD > H\"AABBCCDD\" | H\"06\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SLCAN.CMD < \"Ready\" | \"Go!\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : payload must be <= 8 bytes (classic CAN) or <= 64 bytes (CAN FD),"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         unless t=tp_protocol selects a segmented transport (see CONFIG)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : a '~ id' xtra_params suffix overrides the tx/rx id for that one"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         CMD only. RX-side matching is done in software: an id not"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         already covered by the active std/ext filter will time out —"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         unlike KVCAN, this adapter's filters can't be changed while the"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         channel is open, so widen FILTER beforehand if needed"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC : send one or more periodic messages"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : time1 val1 [id1], time2 val2 [id2], ... (time_i in ms, val_i hex)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : SLCAN.CYCLIC 100 AABBCCDD 0x100, 250 1122 0x200"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SLCAN.CYCLIC 100 AABBCCDD 0x100, 250 1122 0x200 &"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : id is optional; when omitted, falls back to the TX id set via CONFIG"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : without '&' sends one full pattern (lcm of the time_i) then returns;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         with '&' repeats forever until the script/thread is stopped"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("INI file parameters (copy/paste into your ini file):"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("[SLCAN]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("ARTEFACTS_PATH           =          # directory used by SCRIPT/CMD/wrrdf for reading/writing artefact files"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SLCAN_DEVICE             = COM2     # serial port the SLCAN adapter enumerates as (e.g. COM3, /dev/ttyUSB0)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SLCAN_UART_BAUD          = 115200   # USB-serial baud rate used to talk to the SLCAN adapter"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SLCAN_BITRATE            = 500000   # classic CAN arbitration bitrate in bit/s"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SLCAN_FD_DATARATE        = 2000000  # CAN FD data-phase bitrate in bit/s"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SLCAN_MODE               = normal   # CAN mode (e.g. normal/listen-only/loopback)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SLCAN_AUTO_RETX          = true     # enable automatic retransmission on arbitration loss/error"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SLCAN_FD_BRS             = false    # enable Bit Rate Switch for CAN FD frames"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CAN_TX_ID                =          # CAN arbitration ID used by CMD/SCRIPT/CYCLIC when sending"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CAN_RX_ID                =          # CAN arbitration ID to filter on when receiving (empty = accept all)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CAN_TP_PROTOCOL          = none     # transport protocol layered on top of raw CAN frames (none/isotp/j1939/canopen/fastpacket)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_BLOCK_SIZE            =          # ISO-TP: block size (BS) sent in our Flow Control frames (0 = no limit)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_ST_MIN                =          # ISO-TP: separation time (STmin) sent in our Flow Control frames, raw encoded byte"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_PAD_FRAMES            =          # ISO-TP: pad SF/CF/FC to 8 bytes (classic CAN convention) when true"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_PADDING_BYTE          =          # ISO-TP: padding fill byte"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_TIMEOUT_NBS           =          # ISO-TP: N_Bs - max wait in ms for Flow Control after our First Frame"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_TIMEOUT_NCR           =          # ISO-TP: N_Cr - max wait in ms for the next Consecutive Frame from the peer"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_MAX_MSG_LEN           =          # ISO-TP: classic 12-bit length field limit"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("J1939_USE_BAM            =          # J1939-21: true = broadcast (BAM), false = peer-to-peer (RTS/CTS)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("J1939_MAX_PACKETS        =          # J1939-21: max packets granted per CTS (RTS/CTS only)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_TIMEOUT_T1            =          # J1939-21: T1 - max wait in ms for CTS after RTS"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_TIMEOUT_T2            =          # J1939-21: T2 - max wait in ms for a data packet after CTS"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_TIMEOUT_T3            =          # J1939-21: T3 - max wait in ms for the next CTS after a burst"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_TIMEOUT_TH            =          # J1939-21: Th (BAM) - max inter-packet gap in ms on the receive side"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("J1939_MAX_MSG_LEN        =          # J1939-21: message size limit"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CANOPEN_INDEX            =          # CANopen SDO: Object Dictionary index of the entry being transferred"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CANOPEN_SUBINDEX         =          # CANopen SDO: Object Dictionary sub-index"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CANOPEN_USE_BLOCK        =          # CANopen SDO: true = block transfer, false = segmented transfer"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CANOPEN_BLOCK_SIZE       =          # CANopen SDO: block transfer segments per block, 1-127"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_TIMEOUT_SDO           =          # CANopen SDO: response timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CANOPEN_MAX_MSG_LEN      =          # CANopen SDO: message size limit"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_TIMEOUT_FP_INTERFRAME =          # Fast Packet: max inter-frame gap in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("FP_MAX_MSG_LEN           =          # Fast Packet: message size limit"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CAN_FILTERS              =          # comma-separated list of hardware CAN ID/mask filter entries"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_TIMEOUT             = 2000     # read timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("WRITE_TIMEOUT            = 2000     # write timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_BUF_SIZE            = 1024     # size in bytes of the local read buffer"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RAW_RESULT               = false    # CMD returns raw bytes instead of a hexlified string when true"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC_CACHED            = true     # true=validate/parse each CYCLIC entry once per session; false=re-resolve every tick (needed for volatile ?= macros)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("(TP_*/J1939_*/CANOPEN_*/FP_* left blank above = keep the transport-protocol"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(" library's own built-in default; they only apply once CAN_TP_PROTOCOL selects"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(" a segmented protocol)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note: the CONFIG command above can override a subset of these at runtime;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("      any key not accepted by CONFIG must be set via the ini file."));


    return true;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CONFIG command implementation; overwrite the current SLCAN parameters at runtime.
  *
  * \note Any subset of parameters can be specified; omitted keys retain their current values.
  *
  * \note Usage example:
  *       SLCAN.CONFIG i=/dev/ttyACM0 p=115200 b=6 x=0x123 r=2000 w=2000 s=64
  *       SLCAN.CONFIG i=/dev/ttyACM0 b=4 x=0x18DAF100
  *
  * \param[in] args  [i=device] [p=uart_baud] [b=bitrate] [y=fd_rate] [m=mode] [a=auto_retx]
  *                  [z=fd_brs] [x=tx_id] [r=read_tout] [w=write_tout] [s=recv_bufsize]
  *
  * \return true if parameters were updated successfully, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool SLCANPlugin::m_SLCAN_CONFIG (const std::string &args, std::stop_token st) const
{
    return generic_can_set_params<SLCANPlugin>(this, args);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief FILTER command implementation; install the adapter's acceptance filters.
  *
  * \note Filters are stored in m_oStdFilter/m_oExtFilter and (re)applied every time a CMD
  *       or SCRIPT opens a new channel — they must be sent while the channel is closed, so
  *       there is no equivalent of KVCAN's "apply to the already-open socket" here. Calling
  *       FILTER with an empty argument clears both slots (accept everything).
  *
  * \note Usage example:
  *       SLCAN.FILTER 0x100:0x7FF
  *       SLCAN.FILTER 0x100:0x7FF,0x18DAF100:0x1FFFFFFF
  *       SLCAN.FILTER
  *
  * \param[in] args  comma-separated list of <id>:<mask> pairs (max one std + one ext), or empty to clear
  *
  * \return true on success, false on parse error
*/
/*--------------------------------------------------------------------------------------------------------*/

bool SLCANPlugin::m_SLCAN_FILTER (const std::string &args, std::stop_token st) const
{
    // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
    if (!m_bIsEnabled)
    {
        return true;
    }

    if (false == m_ParseFilters(args))
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FILTER: invalid filter string:"); LOG_STRING(args));
        return false;
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("Filters set, std:"); LOG_UINT32(m_oStdFilter.has_value() ? 1U : 0U);
              LOG_STRING("ext:"); LOG_UINT32(m_oExtFilter.has_value() ? 1U : 0U));

    return true;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CMD command implementation; execute a single send/receive operation over SLCAN.
  *
  * \note The UART is opened, the bus parameters/filters are pushed and the CAN channel is
  *       opened for the duration of the call; everything is closed automatically on return
  *       (RAII, via SLCAN's destructor — see m_OpenAndConfigure).
  *
  * \note Usage example:
  *       SLCAN.CMD > H\"AABBCCDD\" | H\"06\"
  *       SLCAN.CMD < \"Ready\" | \"Go!\"
  *
  * \param[in] args  direction and data expression (see CommScriptCommandValidator grammar)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool SLCANPlugin::m_SLCAN_CMD (const std::string &args, std::stop_token st) const
{
    (void)st;

    return ucmdexec::generic_cmd(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<SLCANFrameDriver> {
            // Open + configure the SLCAN channel (RAII — closed automatically by destructor)
            return m_OpenAndConfigure();
        },
        m_strInstanceName,
        m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, &m_strResultData, m_bRawResult,
        // Trivial pass-throughs — SLCANFrameDriver::tout_write()/tout_read()
        // already dump every physical frame themselves via dumpFrame() (see
        // slcan_frame_driver.hpp), covering both the raw single-frame path
        // and a segmented transport protocol's SF/FF/CF/FC frames alike.
        // Installing *any* non-empty pfsend/pfrecv here only exists to make
        // the interpreter skip its own generic dump of the pre-segmentation
        // logical payload — see uCommScriptCommandInterpreter.hpp.
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const SLCANFrameDriver> drv, std::string_view x) {
            return drv->tout_write(t, d, x);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const SLCANFrameDriver> drv, std::string_view x) {
            return drv->tout_read(t, b, o, x);
        });
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief SCRIPT command implementation; execute a multi-command script file over SLCAN.
  *
  * \note The SLCAN channel is opened once for the lifetime of the script and closed on return.
  *       Blank lines and lines starting with '#' are skipped. Execution stops at the first
  *       failing line, or immediately if a stop is requested via the stop_token.
  *
  * \note Usage example:
  *       SLCAN.SCRIPT obd_sequence.txt
  *       SLCAN.SCRIPT uds_session.txt 10
  *
  * \param[in] args  filename [delay_ms]
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool SLCANPlugin::m_SLCAN_SCRIPT (const std::string &args, std::stop_token st) const
{
    (void)st;

    return ucmdexec::generic_script(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<SLCANFrameDriver> {
            // Open + configure the SLCAN channel (RAII — closed automatically by destructor)
            return m_OpenAndConfigure();
        },
        m_strInstanceName,
        m_strArtefactsPath, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR,
        // Same rationale as m_SLCAN_CMD() above.
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const SLCANFrameDriver> drv, std::string_view x) {
            return drv->tout_write(t, d, x);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const SLCANFrameDriver> drv, std::string_view x) {
            return drv->tout_read(t, b, o, x);
        });
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CYCLIC command implementation; send one or more periodic SLCAN messages.
  *
  * \note The SLCAN channel is opened once for the whole CYCLIC session (like SCRIPT) and closed
  *       automatically on return (RAII). Each entry's optional "id" is the CAN id (decimal or
  *       0x-hex, same syntax SLCANFrameDriver::tout_write()'s xtra_params already accepts — an
  *       empty id falls back to the TX id set via CONFIG) and "val" is the payload as a plain
  *       hex string (e.g. "AABBCCDD").
  *
  * \note This bypasses TP-segmented transport on purpose — same rationale as KVCAN's CYCLIC: a
  *       cyclic message is by definition a single, self-contained frame per tick.
  *
  * \note Usage example:
  *       SLCAN.CYCLIC 100 AABBCCDD 0x100, 250 1122 0x200
  *       SLCAN.CYCLIC 100 AABBCCDD 0x100, 250 1122 0x200 &
  *
  * \param[in] args  "time1 val1 , time2 val2 , ..." (see generic_send_cyclic())
  * \param[in] st    stop_token; forwarded as-is (present/absent '&' selects run-once vs. forever)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool SLCANPlugin::m_SLCAN_CYCLIC (const std::string &args, std::stop_token st) const
{
    return ucmdexec::generic_send_cyclic(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<SLCANFrameDriver> {
            // Open + configure the SLCAN channel (RAII — closed automatically by destructor)
            return m_OpenAndConfigure();
        },
        m_strInstanceName, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, st, m_bCyclicCached);
}

/////////////////////////////////////////////////////////////////////////////////
//            PRIVATE INTERFACES IMPLEMENTATION                                //
/////////////////////////////////////////////////////////////////////////////////

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Parse a comma-separated "<id>:<mask>" filter string into the adapter's single
  *        standard-filter and single extended-filter slots. Both id and mask fields accept
  *        decimal or 0x-prefixed hex values. Example: "0x100:0x7FF,0x18DAF100:0x1FFFFFFF"
*/
/*--------------------------------------------------------------------------------------------------------*/

bool SLCANPlugin::m_ParseFilters(const std::string& strFilters) const
{
    // SocketCAN-style frame-ID flag bit, reused here only to recognise an
    // explicitly-flagged extended id; the adapter's f/F commands take plain
    // 11-bit / 29-bit values with no flag bits of their own.
    static constexpr uint32_t CAN_EFF_FLAG = 0x80000000U;
    static constexpr uint32_t CAN_SFF_MASK = 0x000007FFU;
    static constexpr uint32_t CAN_EFF_MASK = 0x1FFFFFFFU;

    m_oStdFilter.reset();
    m_oExtFilter.reset();

    // Empty argument is an explicit clear — both slots stay unset (accept all).
    if (true == strFilters.empty()) {
        return true;
    }

    // Split on commas to get individual "<id>:<mask>" tokens
    std::vector<std::string> vstrEntries;
    ustring::tokenize(strFilters, ',', vstrEntries);

    for (const auto& strEntry : vstrEntries)
    {
        // Split each entry on ':' to separate id from mask
        std::vector<std::string> vstrParts;
        ustring::tokenize(strEntry, ':', vstrParts);

        if (vstrParts.size() != 2) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Filter entry malformed (expected id:mask):"); LOG_STRING(strEntry));
            return false;
        }

        uint32_t u32Id = 0U, u32Mask = 0U;

        if (false == numeric::str2uint32(ustring::trim(vstrParts[0]), u32Id)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Filter id parse failed:"); LOG_STRING(vstrParts[0]));
            return false;
        }

        if (false == numeric::str2uint32(ustring::trim(vstrParts[1]), u32Mask)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Filter mask parse failed:"); LOG_STRING(vstrParts[1]));
            return false;
        }

        // ── EFF / SFF classification ─────────────────────────────────────────
        // Mirrors setCanTxId: an id > 0x7FF without the EFF flag set is most
        // likely a forgotten flag, not a deliberate 11-bit id — warn and treat
        // it as extended automatically rather than silently truncating it.
        const bool bExplicitExt = (u32Id & CAN_EFF_FLAG) != 0U;
        const bool bExtended    = bExplicitExt || ((u32Id & CAN_EFF_MASK) > CAN_SFF_MASK);

        if (true == bExtended) {
            if (true == m_oExtFilter.has_value()) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("Only one extended filter slot is supported by the adapter:"); LOG_STRING(strEntry));
                return false;
            }
            if (!bExplicitExt) {
                LOG_PRINT(LOG_WARNING, LOG_HDR;
                          LOG_STRING("Filter id > 0x7FF without CAN_EFF_FLAG — treating as extended:"); LOG_STRING(strEntry));
            }
            m_oExtFilter = std::make_pair(static_cast<uint32_t>(u32Id  & CAN_EFF_MASK),
                                           static_cast<uint32_t>(u32Mask & CAN_EFF_MASK));
        } else {
            if (true == m_oStdFilter.has_value()) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("Only one standard filter slot is supported by the adapter:"); LOG_STRING(strEntry));
                return false;
            }
            m_oStdFilter = std::make_pair(static_cast<uint16_t>(u32Id  & CAN_SFF_MASK),
                                           static_cast<uint16_t>(u32Mask & CAN_SFF_MASK));
        }
    }

    return true;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Open the UART, push bit rate / FD rate / mode / auto-retx / filters while the
  *        channel is closed (the adapter rejects those commands otherwise — see
  *        uSlcan.cpp's set_bitrate()/set_mode()/… INVALID_PARAM checks), then open the
  *        CAN channel itself.
*/
/*--------------------------------------------------------------------------------------------------------*/

std::shared_ptr<SLCANFrameDriver> SLCANPlugin::m_OpenAndConfigure(void) const
{
    auto shpDriver = std::make_shared<SLCANFrameDriver>(
        m_strDevice, m_u32UartBaud, m_u32CanTxId, m_bFdBrs, m_strDevice, m_strInstanceName);

    if (false == shpDriver->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to open UART device:"); LOG_STRING(m_strDevice));
        return nullptr;
    }

    // Transport-protocol selection is orthogonal to the UART/bus config below —
    // push it regardless of whether channel setup succeeds further down, so
    // it's already in place for the driver's tout_write()/tout_read() calls.
    shpDriver->set_tp_protocol(m_eTpProtocol);
    shpDriver->set_tp_config(m_sTpConfig);
    if (true == m_bCanRxIdSet) {
        shpDriver->set_rx_id(m_u32CanRxId);
    }

    if (ICommDriver::Status::SUCCESS != shpDriver->set_bitrate(m_eBitrate, m_u32WriteTimeout)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to set CAN bit rate"));
        return nullptr;
    }

    if (ICommDriver::Status::SUCCESS != shpDriver->set_fd_data_rate(m_eFdDataRate, m_u32WriteTimeout)) {
        // Not fatal: classic CAN frames (the common case) do not need it.
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Failed to set CAN-FD data rate (classic frames still work)"));
    }

    if (ICommDriver::Status::SUCCESS != shpDriver->set_mode(m_eMode, m_u32WriteTimeout)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to set bus mode"));
        return nullptr;
    }

    if (ICommDriver::Status::SUCCESS != shpDriver->set_auto_retx(m_eAutoRetx, m_u32WriteTimeout)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to set auto-retransmission"));
        return nullptr;
    }

    if (true == m_oStdFilter.has_value()) {
        if (ICommDriver::Status::SUCCESS != shpDriver->set_std_filter(m_oStdFilter->first, m_oStdFilter->second, m_u32WriteTimeout)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to set standard filter"));
            return nullptr;
        }
    }

    if (true == m_oExtFilter.has_value()) {
        if (ICommDriver::Status::SUCCESS != shpDriver->set_ext_filter(m_oExtFilter->first, m_oExtFilter->second, m_u32WriteTimeout)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to set extended filter"));
            return nullptr;
        }
    }

    // No filter configured at all: clear the adapter's filters explicitly
    // rather than relying implicitly on close_channel()'s reset-on-close
    // side effect from whatever the previous session left behind.
    if (false == m_oStdFilter.has_value() && false == m_oExtFilter.has_value()) {
        if (ICommDriver::Status::SUCCESS != shpDriver->clear_filters(m_u32WriteTimeout)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to clear filters"));
            return nullptr;
        }
    }

    if (ICommDriver::Status::SUCCESS != shpDriver->open_channel(m_u32WriteTimeout)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to open CAN channel"));
        return nullptr;
    }

    return shpDriver;
}

