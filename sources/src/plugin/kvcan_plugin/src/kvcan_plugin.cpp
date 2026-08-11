#include "uSharedConfig.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"
#include "uGuiNotify.hpp"

#include "kvcan_setup.hpp"
#include "kvcan_plugin.hpp"

#include "uPluginSettings.hpp"

#include "uNumeric.hpp"
#include "uFile.hpp"
#include "uString.hpp"
#include "uHexlify.hpp"
#include "uKVCan.hpp"
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
#define LT_HDR     "KVCAN       |"
#define LOG_HDR    LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                 //
///////////////////////////////////////////////////////////////////

#define    ARTEFACTS_PATH     "ARTEFACTS_PATH"
#define    KVCAN_IFACE        "CAN_IFACE"
#define    KVCAN_TX_ID        "CAN_TX_ID"
#define    KVCAN_RX_ID        "CAN_RX_ID"
#define    KVCAN_FILTERS      "CAN_FILTERS"
#define    READ_TIMEOUT       "READ_TIMEOUT"
#define    WRITE_TIMEOUT      "WRITE_TIMEOUT"
#define    READ_BUF_SIZE      "READ_BUF_SIZE"
#define    CAN_TP_PROTOCOL    "CAN_TP_PROTOCOL"

// ---- TpConfig tuning parameters (see setCanTpProtocol() family in kvcan_plugin.hpp) ----
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
//               COMM-DUMP DECORATOR FOR TP TRAFFIC              //
///////////////////////////////////////////////////////////////////

namespace
{

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
 *        to exactly one physical frame, so KVCANPlugin::m_Send()/m_Receive()
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


///////////////////////////////////////////////////////////////////
//                          PLUGIN ENTRY POINT                   //
///////////////////////////////////////////////////////////////////

/**
  * \brief The plugin's entry points
*/
extern "C"
{
    EXPORTED KVCANPlugin* pluginEntry()
    {
        return new KVCANPlugin();
    }

    EXPORTED void pluginExit( KVCANPlugin *ptrPlugin)
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

bool KVCANPlugin::doInit(void *pvUserData)
{
    m_bIsInitialized = true;
    return m_bIsInitialized;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Function where to execute de-initialization of sub-modules
*/
/*--------------------------------------------------------------------------------------------------------*/

void KVCANPlugin::doCleanup(void)
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
  *       KVCAN.INFO
  *
  * \param[in] args  empty string (no arguments expected)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool KVCANPlugin::m_KVCAN_INFO (const std::string &args, std::stop_token st) const
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
    LOG_PRINT(LOG_EMPTY, LOG_STRING(KVCAN_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: communicate via SocketKVCAN (vcan0, can0 …)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the KVCAN interface, TX/RX ID, transport and transfer parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [i=iface] [x=tx_id] [y=rx_id] [r=read_tout] [w=write_tout]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [s=recv_bufsize] [t=tp_protocol]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : KVCAN.CONFIG i=vcan0 x=0x123 r=2000 w=2000 s=64"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         KVCAN.CONFIG i=can0 x=0x18DAF100"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         KVCAN.CONFIG x=0x7E0 y=0x7E8 t=isotp"));
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
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : KVCAN.FILTER 0x100:0x7FF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         KVCAN.FILTER 0x100:0x7FF,0x200:0x7FF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         KVCAN.FILTER"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : overrides the RX default derived from CONFIG's x=tx_id"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : send commands from a script file"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : script"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : KVCAN.SCRIPT script.txt"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : send, receive or both"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : direction message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : KVCAN.CMD > H\"AABBCCDD\" | H\"06\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         KVCAN.CMD < \"Ready\" | \"Go!\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : payload must be <= 8 bytes (classic KVCAN) or <= 64 bytes (KVCAN FD),"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         unless t=tp_protocol selects a segmented transport (see CONFIG)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC : send one or more periodic messages"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : time1 val1 [id1], time2 val2 [id2], ... (time_i in ms, val_i hex)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : KVCAN.CYCLIC 100 AABBCCDD 0x100, 250 1122 0x200"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         KVCAN.CYCLIC 100 AABBCCDD 0x100, 250 1122 0x200 &"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : id is optional; when omitted, falls back to the TX id set via CONFIG"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : without '&' sends one full pattern (lcm of the time_i) then returns;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         with '&' repeats forever until the script/thread is stopped"));
    LOG_SEP();

    return true;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CONFIG command implementation; overwrite the current KVCAN parameters at runtime.
  *
  * \note Any subset of parameters can be specified; omitted keys retain their current values.
  *
  * \note The "x=" key sets both the default TX id (m_u32CanTxId) AND the default
  *       RX id: setCanTxId() replaces m_vFilters with a single acceptance filter
  *       that matches exactly the same CAN id (see setCanTxId() in kvcan_plugin.hpp).
  *       These two members are therefore always the "default" Tx/Rx pair applied
  *       to a freshly opened socket by m_KVCAN_CMD / m_KVCAN_SCRIPT. A per-call
  *       xtra_params override (handled inside the KVCAN driver) only affects that
  *       single tout_read()/tout_write() call; the driver restores the previous
  *       filter/TX-id state immediately afterwards, so any following command
  *       issued without xtra_params falls back to these CONFIG-set defaults.
  *       Use the FILTER command afterwards if RX must listen on an id different
  *       from TX.
  *
  * \note Usage example:
  *       KVCAN.CONFIG i=vcan0 x=0x123 r=2000 w=2000 s=64
  *       KVCAN.CONFIG i=can0 x=0x18DAF100
  *
  * \param[in] args  [i=iface] [x=tx_id] [r=read_tout] [w=write_tout] [s=recv_bufsize]
  *
  * \return true if parameters were updated successfully, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool KVCANPlugin::m_KVCAN_CONFIG (const std::string &args, std::stop_token st) const
{
    return generic_can_set_params<KVCANPlugin>(this, args);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief FILTER command implementation; install KVCAN hardware acceptance filters.
  *
  * \note Filters are stored in m_vFilters and applied every time a CMD or SCRIPT
  *       opens a new socket.  Calling FILTER with an empty argument clears all
  *       filters (accept everything).
  *
  * \note Usage example:
  *       KVCAN.FILTER 0x100:0x7FF
  *       KVCAN.FILTER 0x100:0x7FF,0x200:0x7FF
  *       KVCAN.FILTER
  *
  * \param[in] args  comma-separated list of <id>:<mask> pairs, or empty to clear
  *
  * \return true on success, false on parse error
*/
/*--------------------------------------------------------------------------------------------------------*/

bool KVCANPlugin::m_KVCAN_FILTER (const std::string &args, std::stop_token st) const
{
    // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
    if (!m_bIsEnabled)
    {
        return true;
    }

    std::vector<KVCAN::CanFilter> vFilters;

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
  * \brief CMD command implementation; execute a single send/receive operation over KVCAN.
  *
  * \note The KVCAN socket is opened for the duration of the call and closed automatically on return (RAII).
  *       Filters stored in m_vFilters are applied immediately after open.
  *
  * \note Usage example:
  *       KVCAN.CMD > H\"AABBCCDD\" | H\"06\"
  *       KVCAN.CMD < \"Ready\" | \"Go!\"
  *
  * \param[in] args  direction and data expression (see CommScriptCommandValidator grammar)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool KVCANPlugin::m_KVCAN_CMD (const std::string &args, std::stop_token st) const
{
    (void)st;

    return ucmdexec::generic_cmd(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<KVCAN> {
            // Open the KVCAN socket (RAII — closed automatically by destructor)
            auto shpDriver = std::make_shared<KVCAN>(m_strCanIface, m_strCanIface);

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
        m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, &m_strResultData,
        // Route every send/receive through m_Send()/m_Receive() instead of the
        // interpreter's default driver->tout_write()/tout_read() — see their
        // doc comments in kvcan_plugin.hpp. This is what actually makes
        // CAN_TP_PROTOCOL / "t=" have any effect on a CMD exchange; without
        // it the configured protocol was selected but never consulted.
        [this](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const KVCAN> drv, std::string_view x) {
            return m_Send(t, d, drv, x);
        },
        [this](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const KVCAN> drv, std::string_view x) {
            return m_Receive(t, b, o, drv, x);
        });
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief SCRIPT command implementation; execute a multi-command script file over KVCAN.
  *
  * \note The KVCAN socket is opened once for the lifetime of the script and closed on return.
  *       Filters stored in m_vFilters are applied immediately after open.
  *
  * \note Usage example:
  *       KVCAN.SCRIPT obd_sequence.txt
  *       KVCAN.SCRIPT uds_session.txt 10
  *
  * \param[in] args  filename [delay_ms]
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool KVCANPlugin::m_KVCAN_SCRIPT (const std::string &args, std::stop_token st) const
{
    (void)st;

    return ucmdexec::generic_script(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<KVCAN> {
            // Open the KVCAN socket (RAII — closed automatically by destructor)
            auto shpDriver = std::make_shared<KVCAN>(m_strCanIface, m_strCanIface);

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
        // Same rationale as m_KVCAN_CMD() above — a SCRIPT run needs the same
        // TP dispatch as a single CMD, otherwise a SCRIPT-driven send/receive
        // of a message longer than one frame would silently never segment.
        [this](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const KVCAN> drv, std::string_view x) {
            return m_Send(t, d, drv, x);
        },
        [this](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions& o, std::shared_ptr<const KVCAN> drv, std::string_view x) {
            return m_Receive(t, b, o, drv, x);
        });
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CYCLIC command implementation; send one or more periodic KVCAN messages.
  *
  * \note The KVCAN socket is opened once for the whole CYCLIC session (like SCRIPT) and closed
  *       automatically on return (RAII). Filters stored in m_vFilters are applied immediately
  *       after open. Each entry's optional "id" is the KVCAN arbitration id (decimal or 0x-hex,
  *       same syntax KVCAN::tout_write()'s xtra_params already accepts — an empty id falls back
  *       to the TX id set via CONFIG/set_tx_id()) and "val" is the payload as a plain hex string
  *       (e.g. "AABBCCDD"), <= 8 bytes classic KVCAN / <= 64 bytes KVCAN FD.
  *
  * \note This command bypasses m_Send()/the CAN-TP dispatch on purpose: a cyclic message is by
  *       definition a single, self-contained frame per tick, so the segmented-transport path
  *       (m_eTpProtocol != NONE) used by CMD/SCRIPT for multi-frame payloads does not apply here.
  *
  * \note Usage example:
  *       KVCAN.CYCLIC 100 AABBCCDD 0x100, 250 1122 0x200
  *       KVCAN.CYCLIC 100 AABBCCDD 0x100, 250 1122 0x200 &
  *
  * \param[in] args  "time1 val1 , time2 val2 , ..." (see generic_send_cyclic())
  * \param[in] st    stop_token; forwarded as-is (present/absent '&' selects run-once vs. forever)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool KVCANPlugin::m_KVCAN_CYCLIC (const std::string &args, std::stop_token st) const
{
    return ucmdexec::generic_send_cyclic(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<KVCAN> {
            // Open the KVCAN socket (RAII — closed automatically by destructor)
            auto shpDriver = std::make_shared<KVCAN>(m_strCanIface, m_strCanIface);

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
        m_strInstanceName, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, st);
}


///////////////////////////////////////////////////////////////////
//            PRIVATE INTERFACES IMPLEMENTATION                  //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------------------------------------*/

bool KVCANPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    // Runtime instance identity for the GUI comm-dump panel (e.g. "KVCAN:1"); falls back
    // to the fixed plugin name if the interpreter didn't supply one. Done before the
    // "nothing loaded from ini" early-return below so it's always captured.
    m_strInstanceName = psSetParams->strInstanceName.empty() ? KVCAN_PLUGIN_NAME : psSetParams->strInstanceName;

    if (true == psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        return true;
    }

    PluginSettingsBinder sSettings;
    sSettings.Bind(ARTEFACTS_PATH, m_strArtefactsPath);
    sSettings.Bind(KVCAN_IFACE,    m_strCanIface);
    // Route through setCanTxId() so the EFF-flag fixup and data-bit clamping
    // are applied whether the ID comes from the INI file or the CONFIG command.
    sSettings.Bind(KVCAN_TX_ID,    [this](const std::string& v) { return setCanTxId(v); });
    // Optional: only meaningful once CAN_TP_PROTOCOL selects a segmented
    // transport; empty means "mirror CAN_TX_ID" (today's behaviour).
    sSettings.Bind(KVCAN_RX_ID,    [this](const std::string& v) {
        if (v.empty()) {
            return true;
        }
        return setCanRxId(v);
    });
    // Empty/omitted means TpProtocol::NONE (today's single-frame-only behaviour).
    sSettings.Bind(CAN_TP_PROTOCOL, [this](const std::string& v) {
        if (v.empty()) {
            return true;
        }
        return setCanTpProtocol(v);
    });
    // TpConfig tuning parameters -- all optional, each keeps TpConfig's own
    // in-struct default (see TpConfig.hpp) until explicitly overridden here.
    // Bound directly to the m_sTpConfig members (PluginSettingsBinder converts
    // the ini string straight to the member's type); use the matching CONFIG
    // key (see kvcan_setup.hpp) for the same knobs at runtime.
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
    sSettings.Bind(KVCAN_FILTERS,  [this](const std::string& v) {
        if (v.empty()) {
            return true;
        }
        if (false == m_ParseFilters(v, m_vFilters)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to parse KVCAN_FILTERS:"); LOG_STRING(v));
            return false;
        }
        return true;
    });
    sSettings.Bind(READ_TIMEOUT,   m_u32ReadTimeout);
    sSettings.Bind(WRITE_TIMEOUT,  m_u32WriteTimeout);
    // Route through the setter so the [1-64] range check is applied consistently
    // regardless of whether the value came from INI or CONFIG.
    sSettings.Bind(READ_BUF_SIZE,  [this](const std::string& v) { return setCanReadBufferSize(v); });

    return sSettings.Apply(psSetParams->mapSettings,
        [](const std::string& strKey, const std::string& strRawValue) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strKey); LOG_STRING(":"); LOG_STRING(strRawValue));
        });

} /* m_LocalSetParams() */


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Parse a comma-separated "<id>:<mask>" filter string into a vector of KVCAN::CanFilter.
  *        Both id and mask fields accept decimal or 0x-prefixed hex values.
  *        Example: "0x100:0x7FF,0x200:0x7FF"
*/
/*--------------------------------------------------------------------------------------------------------*/

bool KVCANPlugin::m_ParseFilters(const std::string& strFilters, std::vector<KVCAN::CanFilter>& vFilters) const
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

        KVCAN::CanFilter filter = {};

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

ICommDriver::WriteResult KVCANPlugin::m_Send(uint32_t u32WriteTimeout, std::span<const uint8_t> dataSpan,
                                              std::shared_ptr<const KVCAN> shpDriver, std::string_view xtra_params) const
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

ICommDriver::ReadResult KVCANPlugin::m_Receive(uint32_t u32ReadTimeout, std::span<uint8_t> dataSpan,
                                                const ICommDriver::ReadOptions& options,
                                                std::shared_ptr<const KVCAN> shpDriver, std::string_view xtra_params) const
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
