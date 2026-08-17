#include "uSharedConfig.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include "candlelight_setup.hpp"
#include "candlelight_plugin.hpp"

#include "uPluginSettings.hpp"

#include "uNumeric.hpp"
#include "uFile.hpp"
#include "uString.hpp"
#include "uCommandExec.hpp"


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR     "CANDLELIGHT|"
#define LOG_HDR    LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                 //
///////////////////////////////////////////////////////////////////

#define    ARTEFACTS_PATH         "ARTEFACTS_PATH"
#define    CANDLE_USB_VID         "CANDLE_USB_VID"
#define    CANDLE_USB_PID         "CANDLE_USB_PID"
#define    CANDLE_USB_INDEX       "CANDLE_USB_INDEX"
#define    CANDLE_BITRATE         "CANDLE_BITRATE"
#define    CANDLE_SAMPLE_POINT    "CANDLE_SAMPLE_POINT"
#define    CANDLE_FD_BITRATE      "CANDLE_FD_BITRATE"
#define    CANDLE_FD_SAMPLE_POINT "CANDLE_FD_SAMPLE_POINT"
#define    CANDLE_FD_BRS          "CANDLE_FD_BRS"
#define    CANDLE_MODE_FLAGS      "CANDLE_MODE_FLAGS"
#define    CAN_TX_ID              "CAN_TX_ID"
#define    CAN_RX_ID              "CAN_RX_ID"
#define    CAN_FILTERS            "CAN_FILTERS"
#define    READ_TIMEOUT           "READ_TIMEOUT"
#define    WRITE_TIMEOUT          "WRITE_TIMEOUT"
#define    READ_BUF_SIZE          "READ_BUF_SIZE"
#define    CAN_TP_PROTOCOL        "CAN_TP_PROTOCOL"

// ---- TpConfig tuning parameters (same INI key names as KVCAN/PCAN/SLCAN/UCAN) ----
#define    TP_BLOCK_SIZE           "TP_BLOCK_SIZE"
#define    TP_ST_MIN               "TP_ST_MIN"
#define    TP_PAD_FRAMES           "TP_PAD_FRAMES"
#define    TP_PADDING_BYTE         "TP_PADDING_BYTE"
#define    TP_TIMEOUT_NBS          "TP_TIMEOUT_NBS"
#define    TP_TIMEOUT_NCR          "TP_TIMEOUT_NCR"
#define    TP_MAX_MSG_LEN          "TP_MAX_MSG_LEN"
#define    J1939_USE_BAM           "J1939_USE_BAM"
#define    J1939_MAX_PACKETS       "J1939_MAX_PACKETS"
#define    TP_TIMEOUT_T1           "TP_TIMEOUT_T1"
#define    TP_TIMEOUT_T2           "TP_TIMEOUT_T2"
#define    TP_TIMEOUT_T3           "TP_TIMEOUT_T3"
#define    TP_TIMEOUT_TH           "TP_TIMEOUT_TH"
#define    J1939_MAX_MSG_LEN       "J1939_MAX_MSG_LEN"
#define    CANOPEN_INDEX           "CANOPEN_INDEX"
#define    CANOPEN_SUBINDEX        "CANOPEN_SUBINDEX"
#define    CANOPEN_USE_BLOCK       "CANOPEN_USE_BLOCK"
#define    CANOPEN_BLOCK_SIZE      "CANOPEN_BLOCK_SIZE"
#define    TP_TIMEOUT_SDO          "TP_TIMEOUT_SDO"
#define    CANOPEN_MAX_MSG_LEN     "CANOPEN_MAX_MSG_LEN"
#define    TP_TIMEOUT_FP_INTERFRAME "TP_TIMEOUT_FP_INTERFRAME"
#define    FP_MAX_MSG_LEN          "FP_MAX_MSG_LEN"

///////////////////////////////////////////////////////////////////
//                          PLUGIN ENTRY POINT                   //
///////////////////////////////////////////////////////////////////

/**
  * \brief The plugin's entry points
*/
extern "C"
{
    EXPORTED CandlelightPlugin* pluginEntry()
    {
        return new CandlelightPlugin();
    }

    EXPORTED void pluginExit( CandlelightPlugin *ptrPlugin)
    {
        if (nullptr != ptrPlugin)
        {
            delete ptrPlugin;
        }
    }
}


///////////////////////////////////////////////////////////////////
//                          INIT / CLEANUP                       //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Function where to execute initialization of sub-modules
*/
/*--------------------------------------------------------------------------------------------------------*/

bool CandlelightPlugin::doInit(void *pvUserData)
{
    m_bIsInitialized = true;
    return m_bIsInitialized;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Function where to execute de-initialization of sub-modules
*/
/*--------------------------------------------------------------------------------------------------------*/

void CandlelightPlugin::doCleanup(void)
{
    m_bIsInitialized = false;
    m_bIsEnabled     = false;
}

///////////////////////////////////////////////////////////////////
//                          COMMAND HANDLERS                     //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief INFO command implementation; shows details about the plugin and
  *        describes the supported functions with examples of usage.
  *        This command takes no arguments and is executed even if plugin initialization fails.
  *
  * \note Usage example:
  *       CANDLELIGHT.INFO
  *
  * \param[in] args  empty string (no arguments expected)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool CandlelightPlugin::m_CANDLELIGHT_INFO (const std::string &args, std::stop_token st) const
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
    LOG_PRINT(LOG_EMPTY, LOG_STRING(CANDLELIGHT_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: communicate via a Candlelight (gs_usb) native-USB CAN/CAN-FD adapter"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the USB device selection, CAN bus timing and transfer parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [vid=usb_vid] [pid=usb_pid] [idx=dev_index] [b=bitrate] [sp=sample_point]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [fb=fd_bitrate] [fp=fd_sample_point] [z=fd_brs] [m=mode_flags]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [x=tx_id] [v=rx_id] [r=read_tout] [w=write_tout] [s=recv_bufsize] [t=tp_protocol]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : CANDLELIGHT.CONFIG vid=0x1209 pid=0x2323 b=500000 x=0x123 r=2000 w=2000 s=8"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         CANDLELIGHT.CONFIG b=1000000 sp=0.8 fb=4000000 fp=0.7 z=1 m=256 x=0x18DAF100"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         CANDLELIGHT.CONFIG x=0x7E0 v=0x7E8 t=isotp"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : b/sp (and fb/fp for CAN-FD) are the target bit rate in bit/s and sample"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         point 0-1; the driver derives prop_seg/phase_seg1/phase_seg2/sjw/brp from"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         this adapter's queried clock/limits (see Candlelight::set_bitrate())."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         Power users can bypass that calculator with raw ps=/p1=/p2=/sw=/bp="));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         (dps=/dp1=/dp2=/dsw=/dbp= for CAN-FD data phase) register values instead."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : m=mode_flags is the raw GS_CAN_MODE_* bitmask (see uCandlelight.hpp):"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         1=listen-only 2=loopback 4=triple-sample 8=one-shot 128=pad-to-max"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         256=FD 4096=berr-reporting. Combine by adding, e.g. m=257 = listen-only+FD."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : x=tx_id also becomes the default RX filter entry (see FILTER)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : v=rx_id sets the id expected for peer responses/handshake"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         frames; only used once t=tp_protocol != none. Omit it when TX"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         and RX share the same id."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : t=tp_protocol selects none|isotp|j1939 for payloads that exceed"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         a single frame. Payloads that already fit one frame are"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         unaffected. Default: none (today's behaviour)."));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("FILTER : install a software acceptance-filter list (checked after every"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         received frame — the adapter itself has no filtering hardware)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : <id:mask>[,<id:mask>...]  (any number of entries; empty clears all)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : CANDLELIGHT.FILTER 0x100:0x7FF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         CANDLELIGHT.FILTER 0x100:0x7FF,0x200:0x7FF,0x18DAF100:0x1FFFFFFF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         CANDLELIGHT.FILTER"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : overrides the RX default derived from CONFIG's x=tx_id; unlike"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SLCAN/UCAN this is unlimited (no one-slot-per-kind hardware limit)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : send commands from a script file"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : scriptpathname [delay_ms]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : CANDLELIGHT.SCRIPT script.txt"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : send, receive or both"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : direction message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : CANDLELIGHT.CMD > H\"AABBCCDD\" | H\"06\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         CANDLELIGHT.CMD < \"Ready\" | \"Go!\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : payload must be <= 8 bytes (classic CAN) or <= 64 bytes (CAN FD),"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         unless t=tp_protocol selects a segmented transport (see CONFIG)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : a '~ id' xtra_params suffix overrides the tx/rx id for that one"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         CMD only. Since filtering is entirely software-side here, an"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         override id always works regardless of the current FILTER list."));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC : send one or more periodic messages"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : time1 val1 [id1], time2 val2 [id2], ... (time_i in ms, val_i hex)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : CANDLELIGHT.CYCLIC 100 AABBCCDD 0x100, 250 1122 0x200"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         CANDLELIGHT.CYCLIC 100 AABBCCDD 0x100, 250 1122 0x200 &"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : id is optional; when omitted, falls back to the TX id set via CONFIG"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : without '&' sends one full pattern (lcm of the time_i) then returns;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         with '&' repeats forever until the script/thread is stopped"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("INI file parameters (copy/paste into your ini file):"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("[CANDLELIGHT]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("ARTEFACTS_PATH           =          # directory used by SCRIPT/CMD/wrrdf for reading/writing artefact files"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CANDLE_USB_VID           =          # USB VID of the candleLight/gs_usb CAN adapter"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CANDLE_USB_PID           =          # USB PID of the candleLight/gs_usb CAN adapter"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CANDLE_USB_INDEX         = 0        # index of the candleLight device to open, when more than one is attached"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CANDLE_BITRATE           = 500000   # classic CAN arbitration bitrate in bit/s"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CANDLE_SAMPLE_POINT      =          # classic CAN sample point (0.0-1.0), overrides auto bit-timing calc"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CANDLE_FD_BITRATE        = 2000000  # CAN FD data-phase bitrate in bit/s"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CANDLE_FD_SAMPLE_POINT   =          # CAN FD data-phase sample point (0.0-1.0)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CANDLE_FD_BRS            = false    # enable Bit Rate Switch for CAN FD frames"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CANDLE_MODE_FLAGS        = 0        # raw gs_usb device mode flags (listen-only, loopback, etc.)"));
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
  * \brief CONFIG command implementation; overwrite the current Candlelight parameters at runtime.
  *
  * \note Any subset of parameters can be specified; omitted keys retain their current values.
  *
  * \note Usage example:
  *       CANDLELIGHT.CONFIG vid=0x1209 pid=0x2323 b=500000 x=0x123 r=2000 w=2000 s=8
  *
  * \param[in] args  see m_CANDLELIGHT_INFO()'s CONFIG section for the full key list
  *
  * \return true if parameters were updated successfully, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool CandlelightPlugin::m_CANDLELIGHT_CONFIG (const std::string &args, std::stop_token st) const
{
    return generic_can_set_params<CandlelightPlugin>(this, args);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief FILTER command implementation; install the software acceptance-filter list.
  *
  * \note Filters are stored in m_vecFilters and applied to every frame received by
  *       CandlelightFrameDriver::tout_read() — see this file's class doc comment for why this is
  *       purely software, unlike SLCAN/UCAN's hardware filter slots. Calling FILTER with an empty
  *       argument clears the list (accept everything).
  *
  * \note Usage example:
  *       CANDLELIGHT.FILTER 0x100:0x7FF
  *       CANDLELIGHT.FILTER 0x100:0x7FF,0x200:0x7FF,0x18DAF100:0x1FFFFFFF
  *       CANDLELIGHT.FILTER
  *
  * \param[in] args  comma-separated list of <id>:<mask> pairs (any number), or empty to clear
  *
  * \return true on success, false on parse error
*/
/*--------------------------------------------------------------------------------------------------------*/

bool CandlelightPlugin::m_CANDLELIGHT_FILTER (const std::string &args, std::stop_token st) const
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
              LOG_STRING("Filters set, count:"); LOG_UINT32(static_cast<uint32_t>(m_vecFilters.size())));

    return true;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CMD command implementation; execute a single send/receive operation over Candlelight.
  *
  * \note The USB device is opened, bit timing/mode/filters are pushed and the CAN channel is
  *       opened for the duration of the call; everything is closed automatically on return
  *       (RAII, via Candlelight's destructor — see m_OpenAndConfigure).
  *
  * \note Usage example:
  *       CANDLELIGHT.CMD > H\"AABBCCDD\" | H\"06\"
  *       CANDLELIGHT.CMD < \"Ready\" | \"Go!\"
  *
  * \param[in] args  direction and data expression (see CommScriptCommandValidator grammar)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool CandlelightPlugin::m_CANDLELIGHT_CMD (const std::string &args, std::stop_token st) const
{
    (void)st;

    return ucmdexec::generic_cmd(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<CandlelightFrameDriver> {
            // Open + configure the Candlelight channel (RAII — closed automatically by destructor)
            return m_OpenAndConfigure();
        },
        m_strInstanceName,
        m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, &m_strResultData, m_bRawResult,
        // Trivial pass-throughs — CandlelightFrameDriver::tout_write()/tout_read()
        // already dump every physical frame themselves via dumpFrame() (see
        // candlelight_frame_driver.hpp), covering both the raw single-frame path
        // and a segmented transport protocol's SF/FF/CF/FC frames alike.
        // Installing *any* non-empty pfsend/pfrecv here only exists to make
        // the interpreter skip its own generic dump of the pre-segmentation
        // logical payload — see uCommScriptCommandInterpreter.hpp.
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const CandlelightFrameDriver> drv, std::string_view x) {
            return drv->tout_write(t, d, x);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const CandlelightFrameDriver> drv, std::string_view x) {
            return drv->tout_read(t, b, o, x);
        });
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief SCRIPT command implementation; execute a multi-command script file over Candlelight.
  *
  * \note The Candlelight channel is opened once for the lifetime of the script and closed on return.
  *       Blank lines and lines starting with '#' are skipped. Execution stops at the first
  *       failing line, or immediately if a stop is requested via the stop_token.
  *
  * \note Usage example:
  *       CANDLELIGHT.SCRIPT obd_sequence.txt
  *       CANDLELIGHT.SCRIPT uds_session.txt 10
  *
  * \param[in] args  filename [delay_ms]
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool CandlelightPlugin::m_CANDLELIGHT_SCRIPT (const std::string &args, std::stop_token st) const
{
    (void)st;

    return ucmdexec::generic_script(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<CandlelightFrameDriver> {
            // Open + configure the Candlelight channel (RAII — closed automatically by destructor)
            return m_OpenAndConfigure();
        },
        m_strInstanceName,
        m_strArtefactsPath, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR,
        // Same rationale as m_CANDLELIGHT_CMD() above.
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const CandlelightFrameDriver> drv, std::string_view x) {
            return drv->tout_write(t, d, x);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const CandlelightFrameDriver> drv, std::string_view x) {
            return drv->tout_read(t, b, o, x);
        });
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CYCLIC command implementation; send one or more periodic Candlelight messages.
  *
  * \note The Candlelight channel is opened once for the whole CYCLIC session (like SCRIPT) and closed
  *       automatically on return (RAII). Each entry's optional "id" is the CAN id (decimal or
  *       0x-hex, same syntax CandlelightFrameDriver::tout_write()'s xtra_params already accepts — an
  *       empty id falls back to the TX id set via CONFIG) and "val" is the payload as a plain
  *       hex string (e.g. "AABBCCDD").
  *
  * \note This bypasses TP-segmented transport on purpose — same rationale as KVCAN's CYCLIC: a
  *       cyclic message is by definition a single, self-contained frame per tick.
  *
  * \note Usage example:
  *       CANDLELIGHT.CYCLIC 100 AABBCCDD 0x100, 250 1122 0x200
  *       CANDLELIGHT.CYCLIC 100 AABBCCDD 0x100, 250 1122 0x200 &
  *
  * \param[in] args  "time1 val1 , time2 val2 , ..." (see generic_send_cyclic())
  * \param[in] st    stop_token; forwarded as-is (present/absent '&' selects run-once vs. forever)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool CandlelightPlugin::m_CANDLELIGHT_CYCLIC (const std::string &args, std::stop_token st) const
{
    return ucmdexec::generic_send_cyclic(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<CandlelightFrameDriver> {
            // Open + configure the Candlelight channel (RAII — closed automatically by destructor)
            return m_OpenAndConfigure();
        },
        m_strInstanceName, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, st, m_bCyclicCached);
}


///////////////////////////////////////////////////////////////////
//            PRIVATE INTERFACES IMPLEMENTATION                  //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/

bool CandlelightPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    // Runtime instance identity for the GUI comm-dump panel (e.g. "CANDLELIGHT:1"); falls back
    // to plain "CANDLELIGHT" if the interpreter didn't supply one. Done before the "nothing
    // loaded from ini" early-return below so it's always captured.
    m_strInstanceName = psSetParams->strInstanceName.empty() ? "CANDLELIGHT" : psSetParams->strInstanceName;

    if (true == psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        return true;
    }

    PluginSettingsBinder sSettings;
    sSettings.Bind(ARTEFACTS_PATH,         m_strArtefactsPath);
    sSettings.Bind(CANDLE_USB_VID,         [this](const std::string& v) { return setUsbVid(v); });
    sSettings.Bind(CANDLE_USB_PID,         [this](const std::string& v) { return setUsbPid(v); });
    sSettings.Bind(CANDLE_USB_INDEX,       [this](const std::string& v) { return setUsbDeviceIndex(v); });
    sSettings.Bind(CANDLE_BITRATE,         [this](const std::string& v) { return setCanBitrate(v); });
    sSettings.Bind(CANDLE_SAMPLE_POINT,    [this](const std::string& v) { return setCanSamplePoint(v); });
    sSettings.Bind(CANDLE_FD_BITRATE,      [this](const std::string& v) { return setCanFdBitrate(v); });
    sSettings.Bind(CANDLE_FD_SAMPLE_POINT, [this](const std::string& v) { return setCanFdSamplePoint(v); });
    sSettings.Bind(CANDLE_FD_BRS,          [this](const std::string& v) { return setCanFdBrs(v); });
    sSettings.Bind(CANDLE_MODE_FLAGS,      [this](const std::string& v) { return setCanModeFlags(v); });
    // Route through setCanTxId() so the EFF-flag fixup and data-bit clamping
    // are applied whether the ID comes from the INI file or the CONFIG command.
    sSettings.Bind(CAN_TX_ID,              [this](const std::string& v) { return setCanTxId(v); });
    // Optional: only meaningful once CAN_TP_PROTOCOL selects a segmented
    // transport; empty means "mirror CAN_TX_ID" (today's behaviour).
    sSettings.Bind(CAN_RX_ID,              [this](const std::string& v) {
        if (v.empty()) {
            return true;
        }
        return setCanRxId(v);
    });
    // Empty/omitted means TpProtocol::NONE (today's single-frame-only behaviour).
    sSettings.Bind(CAN_TP_PROTOCOL,        [this](const std::string& v) {
        if (v.empty()) {
            return true;
        }
        return setCanTpProtocol(v);
    });
    // TpConfig tuning parameters -- all optional, each keeps TpConfig's own
    // in-struct default until explicitly overridden; same keys as KVCAN/PCAN/SLCAN/UCAN.
    sSettings.Bind(TP_BLOCK_SIZE,            m_sTpConfig.blockSize);
    sSettings.Bind(TP_ST_MIN,                m_sTpConfig.stMin);
    sSettings.Bind(TP_PAD_FRAMES,            m_sTpConfig.padFrames);
    sSettings.Bind(TP_PADDING_BYTE,          m_sTpConfig.paddingByte);
    sSettings.Bind(TP_TIMEOUT_NBS,           m_sTpConfig.timeoutNBs_ms);
    sSettings.Bind(TP_TIMEOUT_NCR,           m_sTpConfig.timeoutNCr_ms);
    sSettings.Bind(TP_MAX_MSG_LEN,           m_sTpConfig.maxMessageLen);
    sSettings.Bind(J1939_USE_BAM,            m_sTpConfig.j1939UseBam);
    sSettings.Bind(J1939_MAX_PACKETS,        m_sTpConfig.j1939MaxPackets);
    sSettings.Bind(TP_TIMEOUT_T1,            m_sTpConfig.timeoutT1_ms);
    sSettings.Bind(TP_TIMEOUT_T2,            m_sTpConfig.timeoutT2_ms);
    sSettings.Bind(TP_TIMEOUT_T3,            m_sTpConfig.timeoutT3_ms);
    sSettings.Bind(TP_TIMEOUT_TH,            m_sTpConfig.timeoutTh_ms);
    sSettings.Bind(J1939_MAX_MSG_LEN,        m_sTpConfig.j1939MaxMessageLen);
    sSettings.Bind(CANOPEN_INDEX,            m_sTpConfig.canOpenIndex);
    sSettings.Bind(CANOPEN_SUBINDEX,         m_sTpConfig.canOpenSubIndex);
    sSettings.Bind(CANOPEN_USE_BLOCK,        m_sTpConfig.canOpenUseBlock);
    sSettings.Bind(CANOPEN_BLOCK_SIZE,       m_sTpConfig.canOpenBlockSize);
    sSettings.Bind(TP_TIMEOUT_SDO,           m_sTpConfig.timeoutSdo_ms);
    sSettings.Bind(CANOPEN_MAX_MSG_LEN,      m_sTpConfig.canOpenMaxMessageLen);
    sSettings.Bind(TP_TIMEOUT_FP_INTERFRAME, m_sTpConfig.timeoutFpInterFrame_ms);
    sSettings.Bind(FP_MAX_MSG_LEN,           m_sTpConfig.fastPacketMaxMessageLen);
    // Empty string means "no filters configured" -- not an error, so treat as a no-op.
    sSettings.Bind(CAN_FILTERS,       [this](const std::string& v) {
        if (v.empty()) {
            return true;
        }
        if (false == m_ParseFilters(v)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to parse CAN_FILTERS:"); LOG_STRING(v));
            return false;
        }
        return true;
    });
    sSettings.Bind(READ_TIMEOUT,      m_u32ReadTimeout);
    sSettings.Bind(WRITE_TIMEOUT,     m_u32WriteTimeout);
    // Route through the setter so the [1-64] range check is applied consistently
    // regardless of whether the value came from INI or CONFIG.
    sSettings.Bind(READ_BUF_SIZE,     [this](const std::string& v) { return setCanReadBufferSize(v); });
    sSettings.Bind(ucmdexec::RAW_RESULT_INI_KEY, m_bRawResult);
    sSettings.Bind(ucmdexec::CYCLIC_CACHED_INI_KEY, m_bCyclicCached);

    return sSettings.Apply(psSetParams->mapSettings,
        [](const std::string& strKey, const std::string& strRawValue) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strKey); LOG_STRING(":"); LOG_STRING(strRawValue));
        });

} /* m_LocalSetParams() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Parse a comma-separated "<id>:<mask>" filter string into the software filter list.
  *        Both id and mask fields accept decimal or 0x-prefixed hex values.
  *        Example: "0x100:0x7FF,0x200:0x7FF,0x18DAF100:0x1FFFFFFF"
*/
/*--------------------------------------------------------------------------------------------------------*/

bool CandlelightPlugin::m_ParseFilters(const std::string& strFilters) const
{
    // SocketCAN-style frame-ID flag bit, reused here only to recognise an
    // explicitly-flagged extended id — Candlelight's software filter entries
    // themselves carry an explicit is_extended field (see FilterEntry), not
    // a flag bit embedded in the id.
    static constexpr uint32_t CAN_EFF_FLAG = 0x80000000U;
    static constexpr uint32_t CAN_SFF_MASK = 0x000007FFU;
    static constexpr uint32_t CAN_EFF_MASK = 0x1FFFFFFFU;

    m_vecFilters.clear();

    // Empty argument is an explicit clear — accept everything.
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

        if (!bExplicitExt && bExtended) {
            LOG_PRINT(LOG_WARNING, LOG_HDR;
                      LOG_STRING("Filter id > 0x7FF without CAN_EFF_FLAG — treating as extended:"); LOG_STRING(strEntry));
        }

        CandlelightFrameDriver::FilterEntry entry{};
        entry.is_extended = bExtended;
        entry.id           = u32Id   & (bExtended ? CAN_EFF_MASK : CAN_SFF_MASK);
        entry.mask         = u32Mask & (bExtended ? CAN_EFF_MASK : CAN_SFF_MASK);
        m_vecFilters.push_back(entry);
    }

    return true;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Open the USB device, push bit timing / mode while the channel is closed (the
  *        adapter rejects those commands otherwise — see uCandlelight.cpp's
  *        set_bittiming()/set_data_bittiming() INVALID_PARAM checks), install the software
  *        filter list, then open the CAN channel itself.
*/
/*--------------------------------------------------------------------------------------------------------*/

std::shared_ptr<CandlelightFrameDriver> CandlelightPlugin::m_OpenAndConfigure(void) const
{
    auto shpDriver = std::make_shared<CandlelightFrameDriver>(
        m_u16UsbVid, m_u16UsbPid, m_u32UsbDeviceIndex, m_u32CanTxId, m_bFdBrs,
        "Candlelight", m_strInstanceName);

    if (false == shpDriver->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to open USB device vid/pid:");
                  LOG_UINT32(m_u16UsbVid); LOG_STRING("/"); LOG_UINT32(m_u16UsbPid));
        return nullptr;
    }

    // Transport-protocol selection is orthogonal to the bus config below —
    // push it regardless of whether channel setup succeeds further down, so
    // it's already in place for the driver's tout_write()/tout_read() calls.
    shpDriver->set_tp_protocol(m_eTpProtocol);
    shpDriver->set_tp_config(m_sTpConfig);
    if (true == m_bCanRxIdSet) {
        shpDriver->set_rx_id(m_u32CanRxId);
    }

    // Nominal bit timing: raw register override takes priority over the
    // bitrate/sample-point calculator — see setCanPropSeg() etc.'s doc comment.
    if (true == m_bRawTimingSet) {
        if (ICommDriver::Status::SUCCESS != shpDriver->set_bittiming(
                m_u32PropSeg, m_u32PhaseSeg1, m_u32PhaseSeg2, m_u32Sjw, m_u32Brp, m_u32WriteTimeout)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to set raw CAN bit timing"));
            return nullptr;
        }
    } else {
        if (ICommDriver::Status::SUCCESS != shpDriver->set_bitrate(m_u32Bitrate, m_dSamplePoint, m_u32WriteTimeout)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to compute/set CAN bit rate"));
            return nullptr;
        }
    }

    // CAN-FD data-phase timing: only meaningful (and only attempted) when the
    // FD mode flag is set and the adapter actually supports FD — see
    // uCandlelight.hpp's is_fd_supported(). Not fatal on failure: classic
    // CAN frames (the common case) do not need it.
    if ((m_u32ModeFlags & GS_CAN_MODE_FD) != 0U && shpDriver->is_fd_supported()) {
        ICommDriver::Status fdStatus;
        if (true == m_bFdRawTimingSet) {
            fdStatus = shpDriver->set_fd_data_bittiming(
                m_u32FdPropSeg, m_u32FdPhaseSeg1, m_u32FdPhaseSeg2, m_u32FdSjw, m_u32FdBrp, m_u32WriteTimeout);
        } else {
            fdStatus = shpDriver->set_fd_data_bitrate(m_u32FdBitrate, m_dFdSamplePoint, m_u32WriteTimeout);
        }
        if (ICommDriver::Status::SUCCESS != fdStatus) {
            LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Failed to compute/set CAN-FD data-phase bit timing"));
        }
    }

    shpDriver->set_filters(m_vecFilters);

    if (ICommDriver::Status::SUCCESS != shpDriver->open_channel(m_u32ModeFlags, m_u32WriteTimeout)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to open CAN channel"));
        return nullptr;
    }

    return shpDriver;
}
