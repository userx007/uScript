#include "uSharedConfig.hpp"

#include "slcan_setup.hpp"
#include "slcan_plugin.hpp"

#include "uNumeric.hpp"
#include "uFile.hpp"
#include "uString.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <thread>


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR     "SLCAN       |"
#define LOG_HDR    LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                 //
///////////////////////////////////////////////////////////////////

#define    ARTEFACTS_PATH     "ARTEFACTS_PATH"
#define    SLCAN_DEVICE       "SLCAN_DEVICE"
#define    SLCAN_UART_BAUD    "SLCAN_UART_BAUD"
#define    SLCAN_BITRATE      "SLCAN_BITRATE"
#define    SLCAN_FD_DATARATE  "SLCAN_FD_DATARATE"
#define    SLCAN_MODE         "SLCAN_MODE"
#define    SLCAN_AUTO_RETX    "SLCAN_AUTO_RETX"
#define    SLCAN_FD_BRS       "SLCAN_FD_BRS"
#define    CAN_TX_ID          "CAN_TX_ID"
#define    CAN_FILTERS        "CAN_FILTERS"
#define    READ_TIMEOUT       "READ_TIMEOUT"
#define    WRITE_TIMEOUT      "WRITE_TIMEOUT"
#define    READ_BUF_SIZE      "READ_BUF_SIZE"

///////////////////////////////////////////////////////////////////
//        DELIBERATE DEVIATION FROM THE KVCAN PLUGIN PATTERN     //
///////////////////////////////////////////////////////////////////
//
// The KVCAN plugin's CMD/SCRIPT handlers hand the open driver straight to
// CommScriptCommandInterpreter<KVCAN> / CommScriptClient<KVCAN>, which call
// the driver only through the generic ICommDriver::tout_write()/tout_read()
// interface. That works for KVCAN because SocketCAN's frame construction
// (stamping the can_id configured via set_tx_id()) happens inside KVCAN's
// own tout_write().
//
// uSlcan.cpp's tout_write()/tout_read() are a verbatim/raw byte passthrough
// (xtra_params is accepted but unused) — they do not build or parse a
// CanFrame the way KVCAN's generic interface evidently does. uSlcan.hpp's
// own class comment says as much: "the richer typed API (send_frame /
// receive_frame) is strongly preferred." So instead of routing SLCAN
// through CommScriptCommandInterpreter<SLCAN>/CommScriptClient<SLCAN>, the
// handlers below call SLCAN::send_frame()/receive_frame() directly via the
// m_Send()/m_Receive() helpers, and a small self-contained interpreter
// (m_ExecuteExpression/m_ParseDatum/m_ExpectReceive, near the bottom of this
// file) implements the same "> data [| expect]" / "< expect [| reply]"
// grammar documented for KVCAN.CMD, so usage from the operator's point of
// view is unchanged. If your CommScriptCommandInterpreter/CommScriptClient
// turn out to only require is_open()/tout_write()/tout_read() from TDriver,
// the cleaner long-term fix is to make SLCAN's generic interface frame-aware
// (using xtra_params for the TX id, as its header already documents) and
// drop this local interpreter in favour of the shared one.
//
///////////////////////////////////////////////////////////////////
//                          PLUGIN ENTRY POINT                   //
///////////////////////////////////////////////////////////////////

/**
  * \brief The plugin's entry points
*/
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


///////////////////////////////////////////////////////////////////
//                          INIT / CLEANUP                       //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Function where to execute initialization of sub-modules
*/
/*--------------------------------------------------------------------------------------------------------*/

bool SLCANPlugin::doInit(void *pvUserData)
{
    m_bIsInitialized = true;
    return m_bIsInitialized;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Function where to execute de-initialization of sub-modules
*/
/*--------------------------------------------------------------------------------------------------------*/

void SLCANPlugin::doCleanup(void)
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
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [i:device] [p:uart_baud] [b:bitrate] [y:fd_rate] [m:mode]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [a:auto_retx] [z:fd_brs] [x:tx_id] [r:read_tout] [w:write_tout] [s:recv_bufsize]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : SLCAN.CONFIG i:/dev/ttyACM0 p:115200 b:6 x:0x123 r:2000 w:2000 s:64"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SLCAN.CONFIG i:/dev/ttyACM0 b:4 x:0x18DAF100"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : b is the S-command preset 0-13 (4=125k, adapter default); y is Y1-Y5 (2=2M, adapter default)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("FILTER : install the adapter's standard/extended acceptance filters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : <id:mask>[,<id:mask>]  (one std + one ext slot max; empty clears both)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : SLCAN.FILTER 0x100:0x7FF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SLCAN.FILTER 0x100:0x7FF,0x18DAF100:0x1FFFFFFF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SLCAN.FILTER"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : send commands from a script file"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : scriptpathname [delay_ms]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : SLCAN.SCRIPT script.txt"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : send, receive or both"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : direction message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : SLCAN.CMD > H\"AABBCCDD\" | H\"06\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SLCAN.CMD < \"Ready\" | \"Go!\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : payload must be <= 8 bytes (classic CAN) or <= 64 bytes (CAN FD)"));
    LOG_SEP();

    return true;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CONFIG command implementation; overwrite the current SLCAN parameters at runtime.
  *
  * \note Any subset of parameters can be specified; omitted keys retain their current values.
  *
  * \note Usage example:
  *       SLCAN.CONFIG i:/dev/ttyACM0 p:115200 b:6 x:0x123 r:2000 w:2000 s:64
  *       SLCAN.CONFIG i:/dev/ttyACM0 b:4 x:0x18DAF100
  *
  * \param[in] args  [i:device] [p:uart_baud] [b:bitrate] [y:fd_rate] [m:mode] [a:auto_retx]
  *                  [z:fd_brs] [x:tx_id] [r:read_tout] [w:write_tout] [s:recv_bufsize]
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
  * \param[in] args  direction and data expression (see the grammar documented on m_ExecuteExpression)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool SLCANPlugin::m_SLCAN_CMD (const std::string &args, std::stop_token st) const
{
    bool bRetVal = false;

    do {

        if (true == args.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing command"));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled) {
            bRetVal = true;
            break;
        }

        try {
            // Open + configure the SLCAN channel (RAII — closed automatically by destructor)
            auto shpDriver = m_OpenAndConfigure();

            if (nullptr != shpDriver) {
                bRetVal = m_ExecuteExpression(args, shpDriver);
            }
        } catch (const std::bad_alloc& e) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Memory allocation failed:"); LOG_STRING(e.what()));
        } catch (const std::exception& e) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Execution failed:"); LOG_STRING(e.what()));
        }

    } while(false);

    return bRetVal;
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
    bool bRetVal = false;

    do {

        // expected to have as parameter the name of the script
        if (true == args.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing arg(s): scriptpathname [delay_ms]"));
            break;
        }

        std::vector<std::string> vstrArgs;
        ustring::tokenizeSpaceQuotesAware(args, vstrArgs);
        const size_t szNrArgs = vstrArgs.size();

        if (szNrArgs > 2) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected: scriptpathname [delay_ms]"));
            break;
        }

        size_t szDelay = 0;
        if (2 == szNrArgs) {
            if (false == numeric::str2sizet(vstrArgs[1], szDelay)) {
                break;
            }
        }

        std::string strScriptPathName;
        ufile::buildFilePath(m_strArtefactsPath, vstrArgs[0], strScriptPathName);

        // Check file existence and size
        if (false == ufile::fileExistsAndNotEmpty(strScriptPathName)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Script not found or empty:"); LOG_STRING(strScriptPathName));
            break;
        }

        // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
        if (false == m_bIsEnabled) {
            bRetVal = true;
            break;
        }

        try {
            // Open + configure the SLCAN channel (RAII — closed automatically by destructor)
            auto shpDriver = m_OpenAndConfigure();

            if (nullptr == shpDriver) {
                break;
            }

            std::ifstream scriptFile(strScriptPathName);
            std::string   strLine;
            bool          bAllOk = true;

            while (std::getline(scriptFile, strLine)) {

                if (true == st.stop_requested()) {
                    LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("SCRIPT cancelled by stop request"));
                    bAllOk = false;
                    break;
                }

                const std::string strTrimmed = ustring::trim(strLine);
                if (strTrimmed.empty() || '#' == strTrimmed[0]) {
                    continue; // skip blank lines / comments
                }

                if (false == m_ExecuteExpression(strTrimmed, shpDriver)) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SCRIPT line failed:"); LOG_STRING(strTrimmed));
                    bAllOk = false;
                    break;
                }

                if (szDelay > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(szDelay));
                }
            }

            bRetVal = bAllOk;

        } catch (const std::bad_alloc& e) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Memory allocation failed:"); LOG_STRING(e.what()));
        } catch (const std::exception& e) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Execution failed:"); LOG_STRING(e.what()));
        }

    } while(false);

    return bRetVal;
}


///////////////////////////////////////////////////////////////////
//            PRIVATE INTERFACES IMPLEMENTATION                  //
///////////////////////////////////////////////////////////////////


/*--------------------------------------------------------------------------------------------------------*/

bool SLCANPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    bool bRetVal = false;

    if (false == psSetParams->mapSettings.empty()) {
        do {
            // Use find() for each key — single lookup instead of count()+at().

            auto it = psSetParams->mapSettings.find(ARTEFACTS_PATH);
            if (it != psSetParams->mapSettings.end()) {
                m_strArtefactsPath = it->second;
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ArtefactsPath :"); LOG_STRING(m_strArtefactsPath));
            }

            it = psSetParams->mapSettings.find(SLCAN_DEVICE);
            if (it != psSetParams->mapSettings.end()) {
                m_strDevice = it->second;
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Device :"); LOG_STRING(m_strDevice));
            }

            it = psSetParams->mapSettings.find(SLCAN_UART_BAUD);
            if (it != psSetParams->mapSettings.end()) {
                if (false == setUartBaud(it->second)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("UartBaud :"); LOG_UINT32(m_u32UartBaud));
            }

            it = psSetParams->mapSettings.find(SLCAN_BITRATE);
            if (it != psSetParams->mapSettings.end()) {
                if (false == setCanBitrate(it->second)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Bitrate :"); LOG_UINT32(static_cast<uint32_t>(m_eBitrate)));
            }

            it = psSetParams->mapSettings.find(SLCAN_FD_DATARATE);
            if (it != psSetParams->mapSettings.end()) {
                if (false == setCanFdDataRate(it->second)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("FdDataRate :"); LOG_UINT32(static_cast<uint32_t>(m_eFdDataRate)));
            }

            it = psSetParams->mapSettings.find(SLCAN_MODE);
            if (it != psSetParams->mapSettings.end()) {
                if (false == setCanMode(it->second)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Mode :"); LOG_UINT32(static_cast<uint32_t>(m_eMode)));
            }

            it = psSetParams->mapSettings.find(SLCAN_AUTO_RETX);
            if (it != psSetParams->mapSettings.end()) {
                if (false == setCanAutoRetx(it->second)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("AutoRetx :"); LOG_UINT32(static_cast<uint32_t>(m_eAutoRetx)));
            }

            it = psSetParams->mapSettings.find(SLCAN_FD_BRS);
            if (it != psSetParams->mapSettings.end()) {
                if (false == setCanFdBrs(it->second)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("FdBrs :"); LOG_UINT32(m_bFdBrs ? 1U : 0U));
            }

            it = psSetParams->mapSettings.find(CAN_TX_ID);
            if (it != psSetParams->mapSettings.end()) {
                // Route through setCanTxId() so the EFF-flag fixup and data-bit
                // clamping are applied whether the ID comes from the INI file or
                // from the CONFIG command.
                if (false == setCanTxId(it->second)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("TxId :"); LOG_HEX32(m_u32CanTxId));
            }

            it = psSetParams->mapSettings.find(CAN_FILTERS);
            if (it != psSetParams->mapSettings.end() && !it->second.empty()) {
                if (false == m_ParseFilters(it->second)) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to parse CAN_FILTERS:"); LOG_STRING(it->second));
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Filters parsed from ini"));
            }

            it = psSetParams->mapSettings.find(READ_TIMEOUT);
            if (it != psSetParams->mapSettings.end()) {
                if (false == numeric::str2uint32(it->second, m_u32ReadTimeout)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ReadTimeout :"); LOG_UINT32(m_u32ReadTimeout));
            }

            it = psSetParams->mapSettings.find(WRITE_TIMEOUT);
            if (it != psSetParams->mapSettings.end()) {
                if (false == numeric::str2uint32(it->second, m_u32WriteTimeout)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("WriteTimeout :"); LOG_UINT32(m_u32WriteTimeout));
            }

            it = psSetParams->mapSettings.find(READ_BUF_SIZE);
            if (it != psSetParams->mapSettings.end()) {
                // Route through the setter so the [1-64] range check is applied
                // consistently regardless of whether the value came from INI or CONFIG.
                if (false == setCanReadBufferSize(it->second)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ReadBufSize :"); LOG_UINT32(m_u32CanReadBufferSize));
            }

            bRetVal = true;

        } while(false);
    } else {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        bRetVal = true;
    }

    return bRetVal;

} /* m_LocalSetParams() */


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

std::shared_ptr<SLCAN> SLCANPlugin::m_OpenAndConfigure(void) const
{
    auto shpDriver = std::make_shared<SLCAN>(m_strDevice, m_u32UartBaud);

    if (false == shpDriver->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to open UART device:"); LOG_STRING(m_strDevice));
        return nullptr;
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

    if (ICommDriver::Status::SUCCESS != shpDriver->open_channel(m_u32WriteTimeout)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to open CAN channel"));
        return nullptr;
    }

    return shpDriver;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief message sender — builds a CanFrame from the configured TX id and the payload
  *        bytes already extracted from a CMD-grammar token, then calls the driver's typed
  *        send_frame(). See the "DELIBERATE DEVIATION" note near the top of this file for
  *        why this does not go through ICommDriver::tout_write().
*/
/*--------------------------------------------------------------------------------------------------------*/

bool SLCANPlugin::m_Send(std::span<const uint8_t> dataSpan, std::shared_ptr<SLCAN> shpDriver) const
{
    static constexpr uint32_t CAN_EFF_FLAG    = 0x80000000U;
    static constexpr uint32_t CAN_EFF_MASK    = 0x1FFFFFFFU;
    static constexpr uint32_t CAN_SFF_MASK    = 0x000007FFU;
    static constexpr size_t   CLASSIC_MAX_LEN = 8U;
    static constexpr size_t   FD_MAX_LEN      = 64U;

    if (dataSpan.size() > FD_MAX_LEN) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Payload exceeds 64-byte CAN-FD limit:"); LOG_SIZET(dataSpan.size()));
        return false;
    }

    CanFrame frame{};
    frame.is_extended = (m_u32CanTxId & CAN_EFF_FLAG) != 0U;
    frame.is_remote   = false;
    frame.is_canfd    = dataSpan.size() > CLASSIC_MAX_LEN;
    frame.brs         = frame.is_canfd && m_bFdBrs;
    frame.id          = m_u32CanTxId & (frame.is_extended ? CAN_EFF_MASK : CAN_SFF_MASK);
    frame.len         = static_cast<uint8_t>(dataSpan.size());
    std::copy(dataSpan.begin(), dataSpan.end(), frame.data.begin());

    const auto status = shpDriver->send_frame(frame, m_u32WriteTimeout);

    if (ICommDriver::Status::SUCCESS != status) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("send_frame failed, id="); LOG_HEX32(frame.id);
                  LOG_STRING("status:"); LOG_STRING(ICommDriver::to_string(status)));
        return false;
    }

    return true;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief message receiver — calls the driver's typed receive_frame() and copies the
  *        decoded payload into the caller's buffer.
  * \note readType is accepted for interface parity with the KVCAN pattern but unused — see
  *       the doc comment on the declaration in slcan_plugin.hpp.
*/
/*--------------------------------------------------------------------------------------------------------*/

bool SLCANPlugin::m_Receive(std::span<uint8_t> dataSpan, size_t& szSize, CommCommandReadType readType, std::shared_ptr<SLCAN> shpDriver) const
{
    (void)readType;

    CanFrame frame{};
    const auto status = shpDriver->receive_frame(frame, m_u32ReadTimeout);

    if (ICommDriver::Status::SUCCESS != status) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("receive_frame failed, status:"); LOG_STRING(ICommDriver::to_string(status)));
        szSize = 0U;
        return false;
    }

    const size_t szCopyLen = std::min(static_cast<size_t>(frame.len), dataSpan.size());
    std::copy(frame.data.begin(), frame.data.begin() + szCopyLen, dataSpan.begin());
    szSize = szCopyLen;

    if (szCopyLen < static_cast<size_t>(frame.len)) {
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("Received frame truncated, frame_len="); LOG_UINT8(frame.len);
                  LOG_STRING("buffer_len="); LOG_SIZET(dataSpan.size()));
    }

    return true;
}


///////////////////////////////////////////////////////////////////
//      SELF-CONTAINED CMD-GRAMMAR INTERPRETER (see note above)  //
///////////////////////////////////////////////////////////////////

namespace {

/** Strip ASCII spaces/tabs from both ends. */
std::string trimLocal(const std::string& s)
{
    const size_t szStart = s.find_first_not_of(" \t");
    if (szStart == std::string::npos) { return ""; }
    const size_t szEnd = s.find_last_not_of(" \t");
    return s.substr(szStart, szEnd - szStart + 1);
}

int hexNibble(char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'A' && c <= 'F') { return 10 + (c - 'A'); }
    if (c >= 'a' && c <= 'f') { return 10 + (c - 'a'); }
    return -1;
}

bool hexToBytes(const std::string& strHex, std::vector<uint8_t>& vOut)
{
    if (strHex.size() % 2 != 0) { return false; }
    vOut.clear();
    vOut.reserve(strHex.size() / 2);
    for (size_t i = 0; i < strHex.size(); i += 2) {
        const int hi = hexNibble(strHex[i]);
        const int lo = hexNibble(strHex[i + 1]);
        if (hi < 0 || lo < 0) { return false; }
        vOut.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return true;
}

/** Expand the small set of C-style escapes used inside quoted tokens. */
std::string unescapeQuoted(const std::string& strIn)
{
    std::string strOut;
    strOut.reserve(strIn.size());
    for (size_t i = 0; i < strIn.size(); ++i) {
        if (strIn[i] == '\\' && i + 1 < strIn.size()) {
            switch (strIn[i + 1]) {
                case 'r':  strOut.push_back('\r'); ++i; break;
                case 'n':  strOut.push_back('\n'); ++i; break;
                case 't':  strOut.push_back('\t'); ++i; break;
                case '\\': strOut.push_back('\\'); ++i; break;
                case '"':  strOut.push_back('"');  ++i; break;
                default:   strOut.push_back(strIn[i]);  break;
            }
        } else {
            strOut.push_back(strIn[i]);
        }
    }
    return strOut;
}

/**
 * Splits "> A | B" / "< A | B" into a direction char and one or two datum
 * strings, honouring quotes so a '|' inside H"..", ".." or F".." is never
 * mistaken for the composite separator.
 */
bool splitExpression(const std::string& strExpr, char& chDir, std::string& strPrimary,
                      std::string& strSecondary, bool& bHasSecondary)
{
    const std::string strTrim = trimLocal(strExpr);
    if (strTrim.empty()) { return false; }

    chDir = strTrim[0];
    if (chDir != '>' && chDir != '<') { return false; }

    const std::string strRest = trimLocal(strTrim.substr(1));

    bool   bInQuotes = false;
    size_t szPipe    = std::string::npos;
    for (size_t i = 0; i < strRest.size(); ++i) {
        if (strRest[i] == '"') {
            bInQuotes = !bInQuotes;
        } else if (strRest[i] == '|' && !bInQuotes) {
            szPipe = i;
            break;
        }
    }

    if (szPipe == std::string::npos) {
        strPrimary    = strRest;
        bHasSecondary = false;
    } else {
        strPrimary    = trimLocal(strRest.substr(0, szPipe));
        strSecondary  = trimLocal(strRest.substr(szPipe + 1));
        bHasSecondary = true;
    }

    return !strPrimary.empty();
}

} // namespace


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Parse a single CMD-grammar datum token into raw bytes.
  *
  *   H"AABBCCDD"   — hex stream, decoded byte-for-byte
  *   "Hello\r\n"   — quoted string, \r \n \t \\ \" escapes expanded, then taken as ASCII
  *   F"name"       — binary file read from ARTEFACTS_PATH/name (any trailing ",size" is
  *                   ignored on the send side — the file's own length is the payload size)
  *   Hello         — bare token, taken literally as ASCII
*/
/*--------------------------------------------------------------------------------------------------------*/

bool SLCANPlugin::m_ParseDatum(const std::string& strToken, std::vector<uint8_t>& vData) const
{
    vData.clear();

    if (strToken.size() >= 3 && (strToken[0] == 'H' || strToken[0] == 'h') &&
        strToken[1] == '"' && strToken.back() == '"') {
        const std::string strHex = strToken.substr(2, strToken.size() - 3);
        if (false == hexToBytes(strHex, vData)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Malformed hex token:"); LOG_STRING(strToken));
            return false;
        }
        return true;
    }

    if (strToken.size() >= 3 && (strToken[0] == 'F' || strToken[0] == 'f') &&
        strToken[1] == '"' && strToken.back() == '"') {
        const std::string strInner    = strToken.substr(2, strToken.size() - 3);
        const auto         szComma    = strInner.find(',');
        const std::string  strFileName = trimLocal(szComma == std::string::npos ? strInner : strInner.substr(0, szComma));

        std::string strFullPath;
        ufile::buildFilePath(m_strArtefactsPath, strFileName, strFullPath);

        if (false == ufile::fileExistsAndNotEmpty(strFullPath)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("File not found or empty:"); LOG_STRING(strFullPath));
            return false;
        }

        std::ifstream file(strFullPath, std::ios::binary);
        vData.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return true;
    }

    if (strToken.size() >= 2 && strToken.front() == '"' && strToken.back() == '"') {
        const std::string strUnescaped = unescapeQuoted(strToken.substr(1, strToken.size() - 2));
        vData.assign(strUnescaped.begin(), strUnescaped.end());
        return true;
    }

    // Bare/plain string token
    vData.assign(strToken.begin(), strToken.end());
    return true;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Perform one receive step implied by an expression token.
  *
  *   H"..", ".." and bare tokens — their parsed byte length sets the receive size, and the
  *   bytes actually received are compared against that template; a mismatch fails the call.
  *   This is what makes a sequence like SLCAN.CMD < H"6727" | H"272800000000" a useful UDS
  *   seed/key handshake check rather than a no-op buffer-size hint.
  *
  *   F"name,size" — size sets the receive length; whatever is actually received is written
  *   to ARTEFACTS_PATH/name instead of being compared (useful for capturing dumps/firmware
  *   reads where the content is not known ahead of time).
*/
/*--------------------------------------------------------------------------------------------------------*/

bool SLCANPlugin::m_ExpectReceive(const std::string& strToken, std::shared_ptr<SLCAN> shpDriver) const
{
    if (strToken.size() >= 3 && (strToken[0] == 'F' || strToken[0] == 'f') &&
        strToken[1] == '"' && strToken.back() == '"') {

        const std::string strInner = strToken.substr(2, strToken.size() - 3);
        const auto szComma = strInner.find(',');
        if (szComma == std::string::npos) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("F\"name,size\" requires a size for receive:"); LOG_STRING(strToken));
            return false;
        }

        const std::string strFileName = trimLocal(strInner.substr(0, szComma));
        size_t szLen = 0;
        if ((false == numeric::str2sizet(trimLocal(strInner.substr(szComma + 1)), szLen)) ||
            (szLen == 0) || (szLen > 64)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Bad receive size in:"); LOG_STRING(strToken));
            return false;
        }

        std::vector<uint8_t> vBuf(szLen);
        size_t szGot = 0;
        if (false == m_Receive(std::span<uint8_t>(vBuf), szGot, CommCommandReadType{}, shpDriver)) {
            return false;
        }
        vBuf.resize(szGot);

        std::string strFullPath;
        ufile::buildFilePath(m_strArtefactsPath, strFileName, strFullPath);
        std::ofstream outFile(strFullPath, std::ios::binary);
        outFile.write(reinterpret_cast<const char*>(vBuf.data()), static_cast<std::streamsize>(vBuf.size()));

        m_strResultData.assign(vBuf.begin(), vBuf.end());
        return true;
    }

    std::vector<uint8_t> vExpected;
    if (false == m_ParseDatum(strToken, vExpected)) {
        return false;
    }
    if (vExpected.empty() || vExpected.size() > 64) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Receive token must imply 1-64 bytes:"); LOG_STRING(strToken));
        return false;
    }

    std::vector<uint8_t> vActual(vExpected.size());
    size_t szGot = 0;
    if (false == m_Receive(std::span<uint8_t>(vActual), szGot, CommCommandReadType{}, shpDriver)) {
        return false;
    }
    vActual.resize(szGot);

    m_strResultData.assign(vActual.begin(), vActual.end());

    if (vActual != vExpected) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Receive content mismatch for:"); LOG_STRING(strToken));
        return false;
    }

    return true;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Execute one CMD-grammar line against an already-open, already-configured driver.
  *
  *   "> data"            — send data
  *   "> data | expect"   — send data, then expect a response matching expect
  *   "< expect"          — receive-only, wait for a frame matching expect
  *   "< expect | reply"  — wait for a frame matching expect, then send reply
  *
  * data/reply use m_ParseDatum (H"..", "..", F"name", bare word); expect uses
  * m_ExpectReceive (same token forms, plus content comparison / file capture — see there).
*/
/*--------------------------------------------------------------------------------------------------------*/

bool SLCANPlugin::m_ExecuteExpression(const std::string& strExpr, std::shared_ptr<SLCAN> shpDriver) const
{
    char        chDir = 0;
    std::string strPrimary;
    std::string strSecondary;
    bool        bHasSecondary = false;

    if (false == splitExpression(strExpr, chDir, strPrimary, strSecondary, bHasSecondary)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Malformed CMD expression:"); LOG_STRING(strExpr));
        return false;
    }

    bool bRetVal = false;

    if ('>' == chDir) {

        std::vector<uint8_t> vSendData;
        if (false == m_ParseDatum(strPrimary, vSendData)) {
            return false;
        }
        if (false == m_Send(std::span<const uint8_t>(vSendData), shpDriver)) {
            return false;
        }

        bRetVal = (true == bHasSecondary) ? m_ExpectReceive(strSecondary, shpDriver) : true;

    } else {

        bRetVal = m_ExpectReceive(strPrimary, shpDriver);

        if (true == bRetVal && true == bHasSecondary) {
            std::vector<uint8_t> vReplyData;
            if (false == m_ParseDatum(strSecondary, vReplyData)) {
                return false;
            }
            bRetVal = m_Send(std::span<const uint8_t>(vReplyData), shpDriver);
        }
    }

    return bRetVal;
}
