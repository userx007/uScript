#include "uSharedConfig.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include "vector_setup.hpp"
#include "vector_plugin.hpp"

#include "uPluginSettings.hpp"

#include "uNumeric.hpp"
#include "uFile.hpp"
#include "uString.hpp"
#include "uHexlify.hpp"
#include "uVector.hpp"
#include "uCommandExec.hpp"


/////////////////////////////////////////////////////////////////////////////////
//                  PLUGIN ENTRY POINTS                                        //
/////////////////////////////////////////////////////////////////////////////////

extern "C"
{
    EXPORTED VectorPlugin* pluginEntry()
    {
        return new VectorPlugin();
    }

    EXPORTED void pluginExit( VectorPlugin *ptrPlugin)
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
  *       VECTOR.INFO
  *
  * \param[in] args  empty string (no arguments expected)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool VectorPlugin::m_VECTOR_INFO (const std::string &args, std::stop_token st) const
{
    if (!args.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected no argument(s)"));
        return false;
    }

    if (!m_bIsEnabled)
    {
        return true;
    }

    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING(VECTOR_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: communicate via Vector Informatik XL-API (VN16xx/VN89xx/VX1xxx), Windows only"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the Vector application/channel, bitrate and transfer parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [a=app_name] [i=app_channel] [hw=device_type] [serial=serial_nr]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [name=channel_name] [hwidx=hw_index] [hwch=hw_channel] [b=bitrate]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [x=tx_id] [y=rx_id] [r=read_tout] [w=write_tout] [s=recv_bufsize]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [e=extended] [t=tp_protocol]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : VECTOR.CONFIG a=Vector_Plugin i=0 b=500000 x=0x7FF r=2000 w=2000 s=8"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         VECTOR.CONFIG hw=VN1610 b=500000 x=0x7FF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         VECTOR.CONFIG serial=12345 b=500000"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         VECTOR.CONFIG a=Vector_Plugin i=0 b=500000 x=0x18DAF100 e=0"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         VECTOR.CONFIG x=0x7E0 y=0x7E8 t=isotp"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  a  - application name as configured in \"Vector Hardware Config\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("       (create it there and assign the VN1610/etc. channel(s) to it first)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  i  - zero-based channel index within that application's assignment"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("       (NOT a global device handle - see uVector.hpp's device-selection note)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  hw - select a device directly by type (\"VN1610\" or a raw XL_HWTYPE_*"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("       number), bypassing Vector Hardware Config entirely - see DEVICES below"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  serial - select a device directly by serial number, same bypass as hw="));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  name   - select a channel directly by its exact XL-API name (see DEVICES)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  hwidx  - disambiguates hw=/serial=/name= when more than one board/channel"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  hwch   - matches (multiple boards of the same type, multiple connectors)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note: hw=/serial=/name= take priority over a=/i= the moment any of them"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("        is set - the two selection modes are not combined"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  b  - CAN bitrate in bps, e.g. 500000 for 500 kbit/s"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  x  - TX CAN ID (decimal or 0x-hex); EFF flag auto-set when ID > 0x7FF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  y  - RX CAN ID for peer responses/handshake frames; only used once"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("       t=tp_protocol != none. Defaults to mirroring x=tx_id."));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  r  - read timeout in ms (default 1000)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  w  - write timeout in ms (default 1000)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  s  - read buffer size in bytes, 1-8 (classic CAN only)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  e  - force extended (29-bit) frame format: 0=auto, 1=force EFF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  f  - CAN FD: accepted for grammar symmetry with PCAN/KVCAN, but 1 is"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("       rejected - CAN FD is not implemented by this plugin/driver"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  t  - transport protocol for payloads over one frame: none (default,"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("       naive fragmentation) | isotp (ISO 15765-2) | j1939 (SAE J1939-21)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : x=tx_id also becomes the default RX filter id (replaces the"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         whole filter list with one entry matching tx_id)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("FILTER : install a software acceptance filter (checked per received frame)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : <id:mask>[,<id:mask>…]  (empty string clears the filter)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : VECTOR.FILTER 0x100:0x7FF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         VECTOR.FILTER 0x100:0x7FF,0x18DAF100:0x1FFFFFFF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         VECTOR.FILTER"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : only the FIRST id:mask entry is actually enforced today - the"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         driver tracks one active RX filter id, same caveat as PCAN.FILTER"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : overrides the RX default derived from CONFIG's x=tx_id"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : send commands from a script file"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : scriptpathname [delay_ms]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : VECTOR.SCRIPT obd_sequence.txt"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         VECTOR.SCRIPT uds_session.txt 10"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : send, receive or both"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : direction message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : VECTOR.CMD > H\"AABBCCDD\" | H\"06\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         VECTOR.CMD < \"Ready\" | \"Go!\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : payload over 8 bytes is fragmented across frames; select"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         t=tp_protocol (see CONFIG) for a real segmented transport"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC : send one or more periodic messages"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : time1 val1 [id1], time2 val2 [id2], ... (time_i in ms, val_i hex)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : VECTOR.CYCLIC 100 AABBCCDD 0x100, 250 1122 0x200"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         VECTOR.CYCLIC 100 AABBCCDD 0x100, 250 1122 0x200 &"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : id is optional; when omitted, falls back to the default TX id"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : without '&' sends one full pattern (lcm of the time_i) then returns;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         with '&' repeats forever until the script/thread is stopped"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("DEVICES: list every channel XL-API currently sees, independent of Vector"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         Hardware Config and of whether anything is open"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : none"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : VECTOR.DEVICES"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : use its output to fill in CONFIG's hw=/serial=/name=/hwidx=/hwch="));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Setup  : before first use, EITHER open \"Vector Hardware Config\" and create"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         an application (matching a=app_name) with your VN1610/etc. channel(s)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         assigned to it, OR skip that step entirely and just run DEVICES then"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         CONFIG hw=/serial=/name= to select a channel directly"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("INI file parameters (copy/paste into your ini file):"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("[VECTOR]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("ARTEFACTS_PATH           =               # directory used by SCRIPT/CMD/wrrdf for reading/writing artefact files"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("VECTOR_APP_NAME          = Vector_Plugin # application name configured in Vector Hardware Config"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("VECTOR_APP_CHANNEL       = 0             # zero-based channel index within that application"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("VECTOR_DEVICE_HW         =               # direct selection: device type (\"VN1610\" or numeric XL_HWTYPE_*), bypasses the two keys above"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("VECTOR_DEVICE_SERIAL     =               # direct selection: serial number, same bypass as VECTOR_DEVICE_HW"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("VECTOR_DEVICE_NAME       =               # direct selection: exact XL-API channel name (see the DEVICES command)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("VECTOR_DEVICE_HWINDEX    =               # disambiguates VECTOR_DEVICE_HW when more than one board of that type is present"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("VECTOR_DEVICE_HWCHANNEL  =               # disambiguates multiple connectors on the same board"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("VECTOR_BITRATE           = 500000        # classic CAN arbitration bitrate in bit/s"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("VECTOR_EXTENDED          = false         # use 29-bit extended CAN identifiers when true"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("VECTOR_FD                = false         # must stay false - CAN FD is not implemented"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CAN_TX_ID                =               # CAN arbitration ID used by CMD/SCRIPT/CYCLIC when sending"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CAN_RX_ID                =               # CAN arbitration ID to filter on when receiving (empty = accept all)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CAN_FILTERS              =               # comma-separated list of software CAN ID/mask filter entries"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CAN_TP_PROTOCOL          = none          # transport protocol layered on top of raw CAN frames (none/isotp/j1939/canopen/fastpacket)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_BLOCK_SIZE            =               # ISO-TP: block size (BS) sent in our Flow Control frames (0 = no limit)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_ST_MIN                =               # ISO-TP: separation time (STmin) sent in our Flow Control frames, raw encoded byte"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_PAD_FRAMES            =               # ISO-TP: pad SF/CF/FC to 8 bytes (classic CAN convention) when true"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_PADDING_BYTE          =               # ISO-TP: padding fill byte"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_TIMEOUT_NBS           =               # ISO-TP: N_Bs - max wait in ms for Flow Control after our First Frame"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_TIMEOUT_NCR           =               # ISO-TP: N_Cr - max wait in ms for the next Consecutive Frame from the peer"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_MAX_MSG_LEN           =               # ISO-TP: classic 12-bit length field limit"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("J1939_USE_BAM            =               # J1939-21: true = broadcast (BAM), false = peer-to-peer (RTS/CTS)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("J1939_MAX_PACKETS        =               # J1939-21: max packets granted per CTS (RTS/CTS only)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_TIMEOUT_T1            =               # J1939-21: T1 - max wait in ms for CTS after RTS"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_TIMEOUT_T2            =               # J1939-21: T2 - max wait in ms for a data packet after CTS"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_TIMEOUT_T3            =               # J1939-21: T3 - max wait in ms for the next CTS after a burst"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_TIMEOUT_TH            =               # J1939-21: Th (BAM) - max inter-packet gap in ms on the receive side"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("J1939_MAX_MSG_LEN        =               # J1939-21: message size limit"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CANOPEN_INDEX            =               # CANopen SDO: Object Dictionary index of the entry being transferred"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CANOPEN_SUBINDEX         =               # CANopen SDO: Object Dictionary sub-index"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CANOPEN_USE_BLOCK        =               # CANopen SDO: true = block transfer, false = segmented transfer"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CANOPEN_BLOCK_SIZE       =               # CANopen SDO: block transfer segments per block, 1-127"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_TIMEOUT_SDO           =               # CANopen SDO: response timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CANOPEN_MAX_MSG_LEN      =               # CANopen SDO: message size limit"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("TP_TIMEOUT_FP_INTERFRAME =               # Fast Packet: max inter-frame gap in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("FP_MAX_MSG_LEN           =               # Fast Packet: message size limit"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_TIMEOUT             = 2000          # read timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("WRITE_TIMEOUT            = 2000          # write timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_BUF_SIZE            = 8             # size in bytes of the local read buffer (max 8, classic CAN only)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RAW_RESULT               = false         # CMD returns raw bytes instead of a hexlified string when true"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC_CACHED            = true          # true=validate/parse each CYCLIC entry once per session; false=re-resolve every tick"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("(TP_*/J1939_*/CANOPEN_*/FP_* left blank above = keep the transport-protocol"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(" library's own built-in default; they only apply once CAN_TP_PROTOCOL selects"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING(" a segmented protocol)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note: the CONFIG command above can override a subset of these at runtime;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("      any key not accepted by CONFIG must be set via the ini file."));

    return true;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CONFIG command implementation; overwrite the current Vector parameters at runtime.
  *
  * \note Any subset of parameters can be specified; omitted keys retain their current values.
  *       The channel is not reopened by CONFIG - changes take effect on the next CMD or SCRIPT call.
  *
  * \note Usage example:
  *       VECTOR.CONFIG a=Vector_Plugin i=0 b=500000 x=0x7FF r=2000 w=2000 s=8
  *
  * \param[in] args  [a=app_name] [i=app_channel] [b=bitrate] [x=tx_id] [r=read_tout] [w=write_tout]
  *                  [s=recv_bufsize] [e=extended]
  *
  * \return true if parameters were updated successfully, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool VectorPlugin::m_VECTOR_CONFIG (const std::string &args, std::stop_token st) const
{
    return generic_can_set_params<VectorPlugin>(this, args);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief FILTER command implementation; install software acceptance filters.
  *        Identical grammar/semantics to PCAN.FILTER / KVCAN.FILTER.
  *
  * \note Usage example:
  *       VECTOR.FILTER 0x100:0x7FF
  *       VECTOR.FILTER 0x100:0x7FF,0x18DAF100:0x1FFFFFFF
  *       VECTOR.FILTER
  *
  * \param[in] args  comma-separated list of <id>:<mask> pairs, or empty to clear
  *
  * \return true on success, false on parse error
*/
/*--------------------------------------------------------------------------------------------------------*/

bool VectorPlugin::m_VECTOR_FILTER (const std::string &args, std::stop_token st) const
{
    if (!m_bIsEnabled)
    {
        return true;
    }

    std::vector<std::pair<uint32_t,uint32_t>> vFilters;

    if (!args.empty())
    {
        if (false == m_ParseFilters(args, vFilters))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FILTER: invalid filter string:"); LOG_STRING(args));
            return false;
        }
    }

    m_vFilters = std::move(vFilters);

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("Filters set, count:"); LOG_UINT32(static_cast<uint32_t>(m_vFilters.size())));

    return true;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CMD command implementation; execute a single send/receive operation over Vector.
  *
  * \note The Vector channel is opened for the duration of the call and closed automatically on
  *       return (RAII). Software acceptance filters stored in m_vFilters are passed as an RX
  *       filter hint (first filter entry's id as xtra_params) to the driver's tout_read.
  *
  * \note Usage example:
  *       VECTOR.CMD > H\"AABBCCDD\" | H\"06\"
  *       VECTOR.CMD < \"Ready\" | \"Go!\"
  *
  * \param[in] args  direction and data expression (see CommScriptCommandValidator grammar)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool VectorPlugin::m_VECTOR_CMD (const std::string &args, std::stop_token st) const
{
    return ucmdexec::generic_cmd(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<Vector> {
            auto shpDriver = m_OpenAndConfigure();
            return (shpDriver && shpDriver->is_open()) ? shpDriver : nullptr;
        },
        m_strInstanceName,
        m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, &m_strResultData, m_bRawResult,
        // Trivial pass-throughs, same rationale as PCANPlugin::m_PCAN_CMD(): Vector::tout_write()/
        // tout_read() already dump every physical frame themselves (see Vector::dumpFrame()).
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const Vector> drv, std::string_view x, std::stop_token tok) {
            return drv->tout_write(t, d, x, tok);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const Vector> drv, std::string_view x, std::stop_token tok) {
            return drv->tout_read(t, b, o, x, tok);
        }, st);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief SCRIPT command implementation; execute a multi-command script file over Vector.
  *
  * \note The Vector channel is opened once for the lifetime of the script and closed on return.
  *
  * \note Usage example:
  *       VECTOR.SCRIPT obd_sequence.txt
  *       VECTOR.SCRIPT uds_session.txt 10
  *
  * \param[in] args  filename [delay_ms]
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool VectorPlugin::m_VECTOR_SCRIPT (const std::string &args, std::stop_token st) const
{
    return ucmdexec::generic_script(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<Vector> {
            auto shpDriver = m_OpenAndConfigure();
            return (shpDriver && shpDriver->is_open()) ? shpDriver : nullptr;
        },
        m_strInstanceName,
        m_strArtefactsPath, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR,
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const Vector> drv, std::string_view x, std::stop_token tok) {
            return drv->tout_write(t, d, x, tok);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const Vector> drv, std::string_view x, std::stop_token tok) {
            return drv->tout_read(t, b, o, x, tok);
        }, st);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CYCLIC command implementation; send one or more periodic Vector messages.
  *
  * \note The Vector channel is opened once for the whole CYCLIC session (like SCRIPT) and closed
  *       automatically on return (RAII). Same argument grammar as PCAN.CYCLIC/KVCAN.CYCLIC.
  *
  * \note Usage example:
  *       VECTOR.CYCLIC 100 AABBCCDD 0x100, 250 1122 0x200
  *       VECTOR.CYCLIC 100 AABBCCDD 0x100, 250 1122 0x200 &
  *
  * \param[in] args  "time1 val1 , time2 val2 , ..." (see generic_send_cyclic())
  * \param[in] st    stop_token; forwarded as-is (present/absent '&' selects run-once vs. forever)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool VectorPlugin::m_VECTOR_CYCLIC (const std::string &args, std::stop_token st) const
{
    return ucmdexec::generic_send_cyclic(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<Vector> {
            auto shpDriver = m_OpenAndConfigure();
            return (shpDriver && shpDriver->is_open()) ? shpDriver : nullptr;
        },
        m_strInstanceName, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, st, m_bCyclicCached);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief DEVICES command implementation; list every channel currently visible to XL-API,
  *        independent of Vector Hardware Config and of whether anything is currently open.
  *
  * \note This takes no arguments. Useful for finding the exact hw=/serial=/name=/hwidx=/hwch=
  *       values to feed CONFIG for direct device selection - see setDeviceHw() and friends.
  *
  * \note Usage example:
  *       VECTOR.DEVICES
  *
  * \param[in] args  empty string (no arguments expected)
  *
  * \return true on success (even if zero channels are found), false on a malformed call
*/
/*--------------------------------------------------------------------------------------------------------*/

bool VectorPlugin::m_VECTOR_DEVICES (const std::string &args, std::stop_token st) const
{
    (void)st;

    if (!args.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected no argument(s)"));
        return false;
    }

    if (!m_bIsEnabled)
    {
        return true;
    }

    const auto vChannels = Vector::enumerateChannels();

    if (vChannels.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("No channels reported by XL-API - is a Vector device connected "
                             "and its driver installed?"));
        return true;
    }

    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("idx  name                            hwType      hwIdx hwCh  serial     onBus  CAN"));
    LOG_SEP();

    uint32_t idx = 0;
    for (const auto& ch : vChannels)
    {
        char line[160];
        std::snprintf(line, sizeof(line), "%-4u %-31s %-11s %-5u %-5u %-10u %-6s %s",
                      idx++,
                      ch.strName.c_str(),
                      ch.strHwType.c_str(),
                      ch.u32HwIndex,
                      ch.u32HwChannel,
                      ch.u32SerialNumber,
                      ch.bIsOnBus ? "yes" : "no",
                      ch.bSupportsCan ? "yes" : "no");
        LOG_PRINT(LOG_EMPTY, LOG_STRING(line));
    }

    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Select one directly with CONFIG's hw=/serial=/name= (add hwidx=/hwch="));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("if hw= alone is ambiguous), e.g.:"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  VECTOR.CONFIG hw=VN1610 b=500000"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  VECTOR.CONFIG serial=12345 b=500000"));
    LOG_SEP();

    return true;
}

/////////////////////////////////////////////////////////////////////////////////
//            PRIVATE INTERFACES IMPLEMENTATION                                //
/////////////////////////////////////////////////////////////////////////////////

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Parse a comma-separated "<id>:<mask>" filter string into a vector of (can_id, can_mask) pairs.
  *        Identical to PCANPlugin::m_ParseFilters() - see that function's doc comment for the full
  *        EFF/SFF flag fixup rationale.
*/
/*--------------------------------------------------------------------------------------------------------*/

bool VectorPlugin::m_ParseFilters(const std::string& strFilters,
                                  std::vector<std::pair<uint32_t,uint32_t>>& vFilters) const
{
    vFilters.clear();

    static constexpr uint32_t CAN_EFF_FLAG = 0x80000000U;
    static constexpr uint32_t CAN_RTR_FLAG = 0x40000000U;
    static constexpr uint32_t CAN_ERR_FLAG = 0x20000000U;
    static constexpr uint32_t CAN_SFF_MASK = 0x000007FFU;
    static constexpr uint32_t CAN_EFF_MASK = 0x1FFFFFFFU;

    std::vector<std::string> vstrEntries;
    ustring::tokenize(strFilters, ',', vstrEntries);

    for (const auto& strEntry : vstrEntries)
    {
        std::vector<std::string> vstrParts;
        ustring::tokenize(strEntry, ':', vstrParts);

        if (vstrParts.size() != 2) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Filter entry malformed (expected id:mask):"); LOG_STRING(strEntry));
            return false;
        }

        uint32_t can_id   = 0U;
        uint32_t can_mask = 0U;

        if (false == numeric::str2uint32(ustring::trim(vstrParts[0]), can_id)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Filter id parse failed:"); LOG_STRING(vstrParts[0]));
            return false;
        }

        if (false == numeric::str2uint32(ustring::trim(vstrParts[1]), can_mask)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Filter mask parse failed:"); LOG_STRING(vstrParts[1]));
            return false;
        }

        const uint32_t flagsInId = can_id & (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG);
        can_mask |= flagsInId;

        if (can_id & CAN_EFF_FLAG) {
            can_id   &= (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_EFF_MASK);
            can_mask &= (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG | CAN_EFF_MASK);
        } else {
            if ((can_id & CAN_EFF_MASK) > CAN_SFF_MASK) {
                LOG_PRINT(LOG_WARNING, LOG_HDR;
                          LOG_STRING("Filter id > 0x7FF without CAN_EFF_FLAG - setting EFF flag automatically:"); LOG_STRING(strEntry));
                can_id   |= CAN_EFF_FLAG;
                can_mask |= CAN_EFF_FLAG;
                can_id   &= (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_EFF_MASK);
                can_mask &= (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG | CAN_EFF_MASK);
            } else {
                can_id   &= (CAN_RTR_FLAG | CAN_SFF_MASK);
                can_mask &= (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG | CAN_SFF_MASK);
            }
        }

        vFilters.emplace_back(can_id, can_mask);
    }

    return true;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Open the Vector channel with the current configuration and return a shared_ptr to
  *        the Vector driver.
  *
  * \note If m_vFilters is non-empty, only its FIRST entry's id is forwarded to the driver
  *       (Vector::setDefaultRxFilterId()) - see the note on m_ParseFilters(). setCanTxId()
  *       keeps this in sync automatically: every CONFIG "x=" (or CAN_TX_ID ini entry) replaces
  *       m_vFilters with one entry matching the new TX id.
  *
  *        Returns nullptr if the channel could not be opened (already logged by the driver).
*/
/*--------------------------------------------------------------------------------------------------------*/

std::shared_ptr<Vector> VectorPlugin::m_OpenAndConfigure (void) const
{
    std::shared_ptr<Vector> shpDriver;

    if (isUsingDirectSelection()) {

        Vector::DeviceSelector sel;
        sel.i32HwType      = m_bDeviceHwSet ? static_cast<int32_t>(m_u32DeviceHw) : -1;
        sel.u32SerialNumber = m_u32DeviceSerial;
        sel.strChannelName = m_strDeviceName;
        sel.i32HwIndex     = m_bDeviceHwIndexSet ? static_cast<int32_t>(m_u32DeviceHwIndex) : -1;
        sel.i32HwChannel   = m_bDeviceHwChannelSet ? static_cast<int32_t>(m_u32DeviceHwChannel) : -1;

        shpDriver = std::make_shared<Vector>(
            sel, m_u32Bitrate, m_u32CanTxId, m_bExtended, m_bFd,
            m_strDeviceHw.empty() ? m_strDeviceName : m_strDeviceHw,
            m_strInstanceName
        );

        if (!shpDriver->is_open()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Failed to open Vector channel (direct selection) - "
                                 "run VECTOR.DEVICES to see what's currently connected"));
            return nullptr;
        }

    } else {

        shpDriver = std::make_shared<Vector>(
            m_strAppName,
            m_u32AppChannel,
            m_u32Bitrate,
            m_u32CanTxId,
            m_bExtended,
            m_bFd,
            m_strAppName,
            m_strInstanceName
        );

        if (!shpDriver->is_open()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Failed to open Vector channel, app:"); LOG_STRING(m_strAppName.c_str());
                      LOG_STRING("index:"); LOG_UINT32(m_u32AppChannel);
                      LOG_STRING("bitrate:"); LOG_UINT32(m_u32Bitrate));
            return nullptr;
        }
    }

    shpDriver->setTpProtocol(m_eTpProtocol);
    shpDriver->setTpConfig(m_sTpConfig);
    if (true == m_bCanRxIdSet) {
        shpDriver->setTpRxId(m_u32CanRxId);
    }

    if (!m_vFilters.empty()) {
        shpDriver->setDefaultRxFilterId(m_vFilters.front().first);
    }

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("Vector channel ready, selection:"); LOG_STRING(isUsingDirectSelection() ? "direct" : "app-based");
              LOG_STRING("TX ID:"); LOG_HEX32(m_u32CanTxId));

    return shpDriver;
}
