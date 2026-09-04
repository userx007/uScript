#include "uSharedConfig.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"
#include "uGuiNotify.hpp"

#include "systec_setup.hpp"
#include "systec_plugin.hpp"

#include "uPluginSettings.hpp"

#include "uNumeric.hpp"
#include "uFile.hpp"
#include "uString.hpp"
#include "uHexlify.hpp"
#include "uSystecCan.hpp"
#include "uCommandExec.hpp"

/////////////////////////////////////////////////////////////////////////////////
//                  PLUGIN ENTRY POINTS                                        //
/////////////////////////////////////////////////////////////////////////////////

/**
  * \brief The plugin's entry points
*/
extern "C"
{
    EXPORTED SYSTECPlugin* pluginEntry()
    {
        return new SYSTECPlugin();
    }

    EXPORTED void pluginExit( SYSTECPlugin *ptrPlugin)
    {
        if (nullptr != ptrPlugin)
        {
            delete ptrPlugin;
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////
//                  DRIVER DECORATOR                                           //
/////////////////////////////////////////////////////////////////////////////////

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
 *        to exactly one physical frame, so SYSTECPlugin::m_Send()/m_Receive()
 *        dump directly instead of paying for a decorator.
*/
class DumpingDriver : public ICommDriver
{
    public:

        DumpingDriver(std::shared_ptr<const ICommDriver> shpInner, std::string strPluginName)
            : m_shpInner(std::move(shpInner))
            , m_strPluginName(std::move(strPluginName))
        {}

        bool is_open() const override
        {
            return m_shpInner->is_open();
        }

        CommDetails describeConnection(std::string_view xtra_params = {}) const override
        {
            return m_shpInner->describeConnection(xtra_params);
        }

        ReadResult tout_read(uint32_t u32ReadTimeout, std::span<uint8_t> buffer,
                              const ReadOptions& options, std::string_view xtra_params = {}) const override
        {
            auto result = m_shpInner->tout_read(u32ReadTimeout, buffer, options, xtra_params);
            if (result.status == Status::SUCCESS && result.bytes_read > 0 && gui_mode_active()) {
                gui_notify_comm_dump(m_strPluginName, m_shpInner->describeConnection(xtra_params),
                                      CommDir::Rx, buffer.data(), static_cast<uint32_t>(result.bytes_read));
            }
            return result;
        }

        WriteResult tout_write(uint32_t u32WriteTimeout, std::span<const uint8_t> buffer,
                                std::string_view xtra_params = {}) const override
        {
            auto result = m_shpInner->tout_write(u32WriteTimeout, buffer, xtra_params);
            if (result.status == Status::SUCCESS && result.bytes_written > 0 && gui_mode_active()) {
                gui_notify_comm_dump(m_strPluginName, m_shpInner->describeConnection(xtra_params),
                                      CommDir::Tx, buffer.data(), static_cast<uint32_t>(result.bytes_written));
            }
            return result;
        }

    private:

        std::shared_ptr<const ICommDriver> m_shpInner;
        std::string m_strPluginName;
};

} // anonymous namespace


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
  *       SYSTEC.INFO
  *
  * \param[in] args  empty string (no arguments expected)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool SYSTECPlugin::m_SYSTEC_INFO (const std::string &args, std::stop_token st) const
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
    LOG_PRINT(LOG_EMPTY, LOG_STRING(SYSTEC_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: communicate via SocketCAN (can0, can1 …) exposed by"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("             SYS TEC electronic USB-CANmodul devices (systec_can.ko)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the SYSTECCAN interface, TX/RX ID, transport and transfer parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [i=iface] [x=tx_id] [y=rx_id] [r=read_tout] [w=write_tout]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [s=recv_bufsize] [t=tp_protocol]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : SYSTEC.CONFIG i=vcan0 x=0x123 r=2000 w=2000 s=64"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SYSTEC.CONFIG i=can0 x=0x18DAF100"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SYSTEC.CONFIG x=0x7E0 y=0x7E8 t=isotp"));
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
    LOG_PRINT(LOG_EMPTY, LOG_STRING("FILTER : install hardware acceptance filters on the open socket"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : <id:mask>[,<id:mask>…]  (empty string clears all filters)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : SYSTEC.FILTER 0x100:0x7FF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SYSTEC.FILTER 0x100:0x7FF,0x200:0x7FF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SYSTEC.FILTER"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : overrides the RX default derived from CONFIG's x=tx_id"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("HWCTRL : get/set SYS TEC device- and channel-specific hardware controls"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : <key>[=<value>]  (omit '=value' to read, supply it to write)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : SYSTEC.HWCTRL devicenr             (read)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SYSTEC.HWCTRL devicenr=5           (write, 0-254)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SYSTEC.HWCTRL reset                (trigger USBCAN_CMD_RESET_HW)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SYSTEC.HWCTRL dual_channel         (read-only)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SYSTEC.HWCTRL status_timeout       (read)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SYSTEC.HWCTRL status_timeout=1500  (write, ms)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SYSTEC.HWCTRL high_performance     (read)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SYSTEC.HWCTRL high_performance=1   (write, 0/1)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SYSTEC.HWCTRL channel              (read-only, this netdev's channel index)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SYSTEC.HWCTRL tx_timeout_ms        (read)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SYSTEC.HWCTRL tx_timeout_ms=50     (write, dual-channel units only)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : reads store the value in the result data (see CONFIG's raw= flag);"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         devicenr/reset/status_timeout/high_performance are device-scoped"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         (shared by both channels on a dual-channel unit); channel/tx_timeout_ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         are scoped to i=iface's own netdev. Uses plain sysfs I/O — CONFIG's"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         i=iface must be set first and no socket needs to be open."));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : send commands from a script file"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : script"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : SYSTEC.SCRIPT script.txt"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : send, receive or both"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : direction message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : SYSTEC.CMD > H\"AABBCCDD\" | H\"06\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SYSTEC.CMD < \"Ready\" | \"Go!\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : payload must be <= 8 bytes (systec_can.ko is classic CAN only, no CAN FD),"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         unless t=tp_protocol selects a segmented transport (see CONFIG)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC : send one or more periodic messages"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : time1 val1 [id1], time2 val2 [id2], ... (time_i in ms, val_i hex)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : SYSTEC.CYCLIC 100 AABBCCDD 0x100, 250 1122 0x200"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SYSTEC.CYCLIC 100 AABBCCDD 0x100, 250 1122 0x200 &"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : id is optional; when omitted, falls back to the TX id set via CONFIG"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : without '&' sends one full pattern (lcm of the time_i) then returns;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         with '&' repeats forever until the script/thread is stopped"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("INI file parameters (copy/paste into your ini file):"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("[SYSTEC]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("ARTEFACTS_PATH           =        # directory used by SCRIPT/CMD/wrrdf for reading/writing artefact files"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CAN_IFACE                = can0   # SYS TEC CAN channel/interface name (as registered by systec_can.ko)"));
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
  * \brief CONFIG command implementation; overwrite the current SYSTECCAN parameters at runtime.
  *
  * \note Any subset of parameters can be specified; omitted keys retain their current values.
  *
  * \note The "x=" key sets both the default TX id (m_u32CanTxId) AND the default
  *       RX id: setCanTxId() replaces m_vFilters with a single acceptance filter
  *       that matches exactly the same CAN id (see setCanTxId() in systec_plugin.hpp).
  *       These two members are therefore always the "default" Tx/Rx pair applied
  *       to a freshly opened socket by m_SYSTEC_CMD / m_SYSTEC_SCRIPT. A per-call
  *       xtra_params override (handled inside the SYSTECCAN driver) only affects that
  *       single tout_read()/tout_write() call; the driver restores the previous
  *       filter/TX-id state immediately afterwards, so any following command
  *       issued without xtra_params falls back to these CONFIG-set defaults.
  *       Use the FILTER command afterwards if RX must listen on an id different
  *       from TX.
  *
  * \note Usage example:
  *       SYSTEC.CONFIG i=vcan0 x=0x123 r=2000 w=2000 s=64
  *       SYSTEC.CONFIG i=can0 x=0x18DAF100
  *
  * \param[in] args  [i=iface] [x=tx_id] [r=read_tout] [w=write_tout] [s=recv_bufsize]
  *
  * \return true if parameters were updated successfully, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool SYSTECPlugin::m_SYSTEC_CONFIG (const std::string &args, std::stop_token st) const
{
    return generic_can_set_params<SYSTECPlugin>(this, args);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief FILTER command implementation; install SYSTECCAN hardware acceptance filters.
  *
  * \note Filters are stored in m_vFilters and applied every time a CMD or SCRIPT
  *       opens a new socket.  Calling FILTER with an empty argument clears all
  *       filters (accept everything).
  *
  * \note Usage example:
  *       SYSTEC.FILTER 0x100:0x7FF
  *       SYSTEC.FILTER 0x100:0x7FF,0x200:0x7FF
  *       SYSTEC.FILTER
  *
  * \param[in] args  comma-separated list of <id>:<mask> pairs, or empty to clear
  *
  * \return true on success, false on parse error
*/
/*--------------------------------------------------------------------------------------------------------*/

bool SYSTECPlugin::m_SYSTEC_FILTER (const std::string &args, std::stop_token st) const
{
    // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
    if (!m_bIsEnabled)
    {
        return true;
    }

    std::vector<SYSTECCAN::CanFilter> vFilters;

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
  * \brief HWCTRL command implementation; get/set SYS TEC device- and channel-specific
  *        hardware controls exposed by systec_can.ko via sysfs (see uSystecCan.hpp
  *        hw_get_/hw_set_ methods — no SocketCAN-generic equivalent exists for these).
  *
  * \note Pure sysfs file I/O keyed off m_strCanIface (set via CONFIG's i=iface);
  *       does NOT require an open CAN_RAW socket, unlike CMD/SCRIPT/CYCLIC.
  *
  * \note "devicenr", "reset", "dual_channel", "status_timeout" and
  *       "high_performance" are device-scoped: on a dual-channel USB-CANmodul
  *       they read/affect the whole unit, shared by both channels' netdevs.
  *       "channel" and "tx_timeout_ms" are scoped to m_strCanIface's own netdev.
  *
  * \note Usage example:
  *       SYSTEC.HWCTRL devicenr
  *       SYSTEC.HWCTRL devicenr=5
  *       SYSTEC.HWCTRL reset
  *       SYSTEC.HWCTRL dual_channel
  *       SYSTEC.HWCTRL status_timeout=1500
  *       SYSTEC.HWCTRL high_performance=1
  *       SYSTEC.HWCTRL channel
  *       SYSTEC.HWCTRL tx_timeout_ms=50
  *
  * \param[in] args  a single "<key>" (read) or "<key>=<value>" (write) token
  *
  * \return true on success, false on parse error / out-of-range value / sysfs failure
*/
/*--------------------------------------------------------------------------------------------------------*/

bool SYSTECPlugin::m_SYSTEC_HWCTRL (const std::string &args, std::stop_token st) const
{
    (void)st;

    const std::string strArg = ustring::trim(args);

    if (strArg.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("HWCTRL: missing <key>[=<value>] argument"));
        return false;
    }

    // Split "<key>" or "<key>=<value>" — at most one '=' expected.
    const std::string::size_type posEq = strArg.find('=');
    const bool        bHasValue = (posEq != std::string::npos);
    const std::string strKey    = ustring::trim(bHasValue ? strArg.substr(0, posEq) : strArg);
    const std::string strValue  = bHasValue ? ustring::trim(strArg.substr(posEq + 1)) : std::string();

    if (strKey.empty() || (bHasValue && strValue.empty()))
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("HWCTRL: malformed argument:"); LOG_STRING(args));
        return false;
    }

    // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
    // (key recognition above still ran, matching the CMD/FILTER validation-only convention)
    static const bool bKnownKey =
        (strKey == "devicenr" || strKey == "reset" || strKey == "dual_channel" ||
         strKey == "status_timeout" || strKey == "high_performance" ||
         strKey == "channel" || strKey == "tx_timeout_ms");

    if (!bKnownKey)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("HWCTRL: unknown key:"); LOG_STRING(strKey));
        return false;
    }

    if (!m_bIsEnabled)
    {
        return true;
    }

    if (m_strCanIface.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("HWCTRL: no interface set — run CONFIG i=<iface> first"));
        return false;
    }

    resetData();
    SYSTECCAN::Status eStatus = SYSTECCAN::Status::INVALID_PARAM;
    std::ostringstream oss;

    if (strKey == "devicenr")
    {
        if (!bHasValue)
        {
            uint32_t u32Val = 0;
            eStatus = SYSTECCAN::hw_get_devicenr(m_strCanIface, u32Val);
            if (eStatus == SYSTECCAN::Status::SUCCESS) { oss << u32Val; }
        }
        else
        {
            uint32_t u32Val = 0;
            if (!numeric::str2uint32(strValue, u32Val)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("HWCTRL: devicenr= not a valid integer:"); LOG_STRING(strValue));
                return false;
            }
            eStatus = SYSTECCAN::hw_set_devicenr(m_strCanIface, u32Val);
            if (eStatus == SYSTECCAN::Status::SUCCESS) { oss << u32Val; }
        }
    }
    else if (strKey == "reset")
    {
        if (bHasValue)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("HWCTRL: reset takes no value (write-only trigger)"));
            return false;
        }
        eStatus = SYSTECCAN::hw_reset(m_strCanIface);
        if (eStatus == SYSTECCAN::Status::SUCCESS) { oss << "OK"; }
    }
    else if (strKey == "dual_channel")
    {
        if (bHasValue)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("HWCTRL: dual_channel is read-only"));
            return false;
        }
        bool bVal = false;
        eStatus = SYSTECCAN::hw_get_dual_channel(m_strCanIface, bVal);
        if (eStatus == SYSTECCAN::Status::SUCCESS) { oss << (bVal ? 1 : 0); }
    }
    else if (strKey == "status_timeout")
    {
        if (!bHasValue)
        {
            uint32_t u32Val = 0;
            eStatus = SYSTECCAN::hw_get_status_timeout(m_strCanIface, u32Val);
            if (eStatus == SYSTECCAN::Status::SUCCESS) { oss << u32Val; }
        }
        else
        {
            uint32_t u32Val = 0;
            if (!numeric::str2uint32(strValue, u32Val)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("HWCTRL: status_timeout= not a valid integer:"); LOG_STRING(strValue));
                return false;
            }
            eStatus = SYSTECCAN::hw_set_status_timeout(m_strCanIface, u32Val);
            if (eStatus == SYSTECCAN::Status::SUCCESS) { oss << u32Val; }
        }
    }
    else if (strKey == "high_performance")
    {
        if (!bHasValue)
        {
            bool bVal = false;
            eStatus = SYSTECCAN::hw_get_high_performance(m_strCanIface, bVal);
            if (eStatus == SYSTECCAN::Status::SUCCESS) { oss << (bVal ? 1 : 0); }
        }
        else
        {
            BoolExprEvaluator sEvaluator;
            bool bVal = false;
            if (!sEvaluator.evaluate(strValue, bVal)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("HWCTRL: high_performance= not a valid boolean:"); LOG_STRING(strValue));
                return false;
            }
            eStatus = SYSTECCAN::hw_set_high_performance(m_strCanIface, bVal);
            if (eStatus == SYSTECCAN::Status::SUCCESS) { oss << (bVal ? 1 : 0); }
        }
    }
    else if (strKey == "channel")
    {
        if (bHasValue)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("HWCTRL: channel is read-only"));
            return false;
        }
        uint32_t u32Val = 0;
        eStatus = SYSTECCAN::hw_get_channel(m_strCanIface, u32Val);
        if (eStatus == SYSTECCAN::Status::SUCCESS) { oss << u32Val; }
    }
    else /* tx_timeout_ms */
    {
        if (!bHasValue)
        {
            uint32_t u32Val = 0;
            eStatus = SYSTECCAN::hw_get_tx_timeout_ms(m_strCanIface, u32Val);
            if (eStatus == SYSTECCAN::Status::SUCCESS) { oss << u32Val; }
        }
        else
        {
            uint32_t u32Val = 0;
            if (!numeric::str2uint32(strValue, u32Val)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("HWCTRL: tx_timeout_ms= not a valid integer:"); LOG_STRING(strValue));
                return false;
            }
            eStatus = SYSTECCAN::hw_set_tx_timeout_ms(m_strCanIface, u32Val);
            if (eStatus == SYSTECCAN::Status::SUCCESS) { oss << u32Val; }
        }
    }

    if (eStatus != SYSTECCAN::Status::SUCCESS)
    {
        // Most likely causes: sysfs node missing (wrong iface / driver not
        // systec_can.ko), permission denied (device-scoped nodes typically
        // need root or a udev rule), or -ENOSYS surfaced as a read/write
        // failure (e.g. tx_timeout_ms on a single-channel unit).
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("HWCTRL: sysfs operation failed for"); LOG_STRING(strKey.c_str());
                  LOG_STRING("on"); LOG_STRING(m_strCanIface.c_str()));
        return false;
    }

    m_strResultData = oss.str();

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("HWCTRL"); LOG_STRING(strKey.c_str());
              LOG_STRING(bHasValue ? "set to" : "="); LOG_STRING(m_strResultData.c_str()));

    return true;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CMD command implementation; execute a single send/receive operation over SYSTEC.
  *
  * \note The SYSTECCAN socket is opened for the duration of the call and closed automatically on return (RAII).
  *       Filters stored in m_vFilters are applied immediately after open.
  *
  * \note Usage example:
  *       SYSTEC.CMD > H\"AABBCCDD\" | H\"06\"
  *       SYSTEC.CMD < \"Ready\" | \"Go!\"
  *
  * \param[in] args  direction and data expression (see CommScriptCommandValidator grammar)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool SYSTECPlugin::m_SYSTEC_CMD (const std::string &args, std::stop_token st) const
{
    (void)st;

    return ucmdexec::generic_cmd(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<SYSTECCAN> {
            // Open the SYSTECCAN socket (RAII — closed automatically by destructor)
            auto shpDriver = std::make_shared<SYSTECCAN>(m_strCanIface, m_strCanIface);

            if (!shpDriver->is_open()) {
                return nullptr;
            }

            // Apply TX ID and acceptance filters
            shpDriver->set_tx_id(m_u32CanTxId);

            if (!m_vFilters.empty()) {
                shpDriver->set_filters(m_vFilters);
            }

            return shpDriver;
        },
        m_strInstanceName,
        m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, &m_strResultData, m_bRawResult,
        // Route every send/receive through m_Send()/m_Receive() instead of the
        // interpreter's default driver->tout_write()/tout_read() — see their
        // doc comments in systec_plugin.hpp. This is what actually makes
        // CAN_TP_PROTOCOL / "t=" have any effect on a CMD exchange; without
        // it the configured protocol was selected but never consulted.
        [this](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const SYSTECCAN> drv, std::string_view x) {
            return m_Send(t, d, drv, x);
        },
        [this](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const SYSTECCAN> drv, std::string_view x) {
            return m_Receive(t, b, o, drv, x);
        });
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief SCRIPT command implementation; execute a multi-command script file over SYSTEC.
  *
  * \note The SYSTECCAN socket is opened once for the lifetime of the script and closed on return.
  *       Filters stored in m_vFilters are applied immediately after open.
  *
  * \note Usage example:
  *       SYSTEC.SCRIPT obd_sequence.txt
  *       SYSTEC.SCRIPT uds_session.txt 10
  *
  * \param[in] args  filename [delay_ms]
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool SYSTECPlugin::m_SYSTEC_SCRIPT (const std::string &args, std::stop_token st) const
{
    (void)st;

    return ucmdexec::generic_script(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<SYSTECCAN> {
            // Open the SYSTECCAN socket (RAII — closed automatically by destructor)
            auto shpDriver = std::make_shared<SYSTECCAN>(m_strCanIface, m_strCanIface);

            if (!shpDriver->is_open()) {
                return nullptr;
            }

            // Apply TX ID and acceptance filters
            shpDriver->set_tx_id(m_u32CanTxId);

            if (!m_vFilters.empty()) {
                shpDriver->set_filters(m_vFilters);
            }

            return shpDriver;
        },
        m_strInstanceName,
        m_strArtefactsPath, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR,
        // Same rationale as m_SYSTEC_CMD() above — a SCRIPT run needs the same
        // TP dispatch as a single CMD, otherwise a SCRIPT-driven send/receive
        // of a message longer than one frame would silently never segment.
        [this](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const SYSTECCAN> drv, std::string_view x) {
            return m_Send(t, d, drv, x);
        },
        [this](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const SYSTECCAN> drv, std::string_view x) {
            return m_Receive(t, b, o, drv, x);
        });
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CYCLIC command implementation; send one or more periodic SYSTEC CAN messages.
  *
  * \note The SYSTECCAN socket is opened once for the whole CYCLIC session (like SCRIPT) and closed
  *       automatically on return (RAII). Filters stored in m_vFilters are applied immediately
  *       after open. Each entry's optional "id" is the SYSTECCAN arbitration id (decimal or 0x-hex,
  *       same syntax SYSTECCAN::tout_write()'s xtra_params already accepts — an empty id falls back
  *       to the TX id set via CONFIG/set_tx_id()) and "val" is the payload as a plain hex string
  *       (e.g. "AABBCCDD"), <= 8 bytes (systec_can.ko is classic CAN only, no CAN FD).
  *
  * \note This command bypasses m_Send()/the CAN-TP dispatch on purpose: a cyclic message is by
  *       definition a single, self-contained frame per tick, so the segmented-transport path
  *       (m_eTpProtocol != NONE) used by CMD/SCRIPT for multi-frame payloads does not apply here.
  *
  * \note Usage example:
  *       SYSTEC.CYCLIC 100 AABBCCDD 0x100, 250 1122 0x200
  *       SYSTEC.CYCLIC 100 AABBCCDD 0x100, 250 1122 0x200 &
  *
  * \param[in] args  "time1 val1 , time2 val2 , ..." (see generic_send_cyclic())
  * \param[in] st    stop_token; forwarded as-is (present/absent '&' selects run-once vs. forever)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool SYSTECPlugin::m_SYSTEC_CYCLIC (const std::string &args, std::stop_token st) const
{
    return ucmdexec::generic_send_cyclic(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<SYSTECCAN> {
            // Open the SYSTECCAN socket (RAII — closed automatically by destructor)
            auto shpDriver = std::make_shared<SYSTECCAN>(m_strCanIface, m_strCanIface);

            if (!shpDriver->is_open()) {
                return nullptr;
            }

            // Apply TX ID and acceptance filters
            shpDriver->set_tx_id(m_u32CanTxId);

            if (!m_vFilters.empty()) {
                shpDriver->set_filters(m_vFilters);
            }

            return shpDriver;
        },
        m_strInstanceName, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, st, m_bCyclicCached);
}


/////////////////////////////////////////////////////////////////////////////////
//                 PLUGIN PRIVATE INTERFACES IMPLEMENTATION                    //
/////////////////////////////////////////////////////////////////////////////////

/*--------------------------------------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Parse a comma-separated "<id>:<mask>" filter string into a vector of SYSTECCAN::CanFilter.
  *        Both id and mask fields accept decimal or 0x-prefixed hex values.
  *        Example: "0x100:0x7FF,0x200:0x7FF"
*/
/*--------------------------------------------------------------------------------------------------------*/

bool SYSTECPlugin::m_ParseFilters(const std::string& strFilters, std::vector<SYSTECCAN::CanFilter>& vFilters) const
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

        SYSTECCAN::CanFilter filter = {};

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
        filter.can_mask |= flagsInId;   // ensure every flag present in id is also masked

        if (filter.can_id & CAN_EFF_FLAG) {
            // 29-bit extended frame: id data bits must fit in CAN_EFF_MASK
            filter.can_id  &= (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_EFF_MASK);
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
                filter.can_id  |= CAN_EFF_FLAG;
                filter.can_mask |= CAN_EFF_FLAG;
                filter.can_id  &= (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_EFF_MASK);
                filter.can_mask &= (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG | CAN_EFF_MASK);
            } else {
                filter.can_id  &= (CAN_RTR_FLAG | CAN_SFF_MASK);
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

ICommDriver::WriteResult SYSTECPlugin::m_Send(uint32_t u32WriteTimeout, std::span<const uint8_t> dataSpan,
                                              std::shared_ptr<const SYSTECCAN> shpDriver, std::string_view xtra_params) const
{
    ICommDriver::WriteResult result;

    if (m_eTpProtocol == TpProtocol::NONE)
    {
        if (dataSpan.size() > 8) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid length for a single CAN frame:"); LOG_SIZET(dataSpan.size()); LOG_STRING("(no TP protocol was set)"));
            result.status = ICommDriver::Status::INVALID_PARAM;
            return result;
        }
        result = shpDriver->tout_write(u32WriteTimeout, dataSpan, xtra_params);

        if (result.status == ICommDriver::Status::SUCCESS && result.bytes_written > 0 && gui_mode_active()) {
            gui_notify_comm_dump(m_strInstanceName, shpDriver->describeConnection(xtra_params),
                                  CommDir::Tx, dataSpan.data(), static_cast<uint32_t>(result.bytes_written));
        }
    }
    else
    {
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

ICommDriver::ReadResult SYSTECPlugin::m_Receive(uint32_t u32ReadTimeout, std::span<uint8_t> dataSpan,
                                                const ICommDriver::ReadOptions& options,
                                                std::shared_ptr<const SYSTECCAN> shpDriver, std::string_view xtra_params) const
{
    ICommDriver::ReadResult result;

    // Delimiter/token reads are an ASCII-stream concept (line or token
    // search across raw frame payloads); segmented binary transports don't
    // have a notion of either, so those two modes always use the driver's
    // legacy framing regardless of m_eTpProtocol. Only the default
    // "exact/raw" read benefits from — and requires — TP reassembly.
    const bool bWantsRawExact = (options.mode == ICommDriver::ReadMode::Exact);

    if (m_eTpProtocol != TpProtocol::NONE && bWantsRawExact)
    {
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
        result = upTp->receive(sDumpingDriver, u32ReadTimeout, dataSpan, szRxId, szTxId);
    }
    else
    {
        // Raw single-frame path (TpProtocol::NONE, or a LINE/TOKEN read type
        // that always bypasses TP) — one call maps to one physical read,
        // exactly as before this feature existed; xtra_params still overrides
        // the RX filter for this single call.
        result = shpDriver->tout_read(u32ReadTimeout, dataSpan, options, xtra_params);

        // ReadMode::UntilToken leaves bytes_read == 0 by design (the matched
        // bytes are consumed internally and never copied into the caller's
        // buffer — see uCommScriptCommandInterpreter.hpp's receiveUntilToken()),
        // so there is nothing meaningful to dump for that mode; the bytes_read
        // > 0 guard below already skips it.
        if (result.status == ICommDriver::Status::SUCCESS && result.bytes_read > 0 && gui_mode_active()) {
            gui_notify_comm_dump(m_strInstanceName, shpDriver->describeConnection(xtra_params),
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
