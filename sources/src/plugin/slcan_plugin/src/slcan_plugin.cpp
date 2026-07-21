#include "uSharedConfig.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include "slcan_setup.hpp"
#include "slcan_plugin.hpp"

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
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : x:tx_id also becomes the default RX id (the matching std/ext"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         filter slot is set to an exact match, the other slot cleared)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("FILTER : install the adapter's standard/extended acceptance filters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : <id:mask>[,<id:mask>]  (one std + one ext slot max; empty clears both)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : SLCAN.FILTER 0x100:0x7FF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SLCAN.FILTER 0x100:0x7FF,0x18DAF100:0x1FFFFFFF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         SLCAN.FILTER"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : overrides the RX default derived from CONFIG's x:tx_id"));
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
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : a '~ id' xtra_params suffix overrides the tx/rx id for that one"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         CMD only. RX-side matching is done in software: an id not"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         already covered by the active std/ext filter will time out —"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         unlike KVCAN, this adapter's filters can't be changed while the"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         channel is open, so widen FILTER beforehand if needed"));
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
        SLCAN_PLUGIN_NAME,
        m_u32CanReadBufferSize, m_u32ReadTimeout, LT_HDR);
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
        SLCAN_PLUGIN_NAME,
        m_strArtefactsPath, m_u32CanReadBufferSize, m_u32ReadTimeout, LT_HDR);
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

std::shared_ptr<SLCANFrameDriver> SLCANPlugin::m_OpenAndConfigure(void) const
{
    auto shpDriver = std::make_shared<SLCANFrameDriver>(
        m_strDevice, m_u32UartBaud, m_u32CanTxId, m_bFdBrs, m_strDevice);

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

