#include "uSharedConfig.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include "pcan_setup.hpp"
#include "pcan_plugin.hpp"

#include "uNumeric.hpp"
#include "uFile.hpp"
#include "uString.hpp"
#include "uHexlify.hpp"
#include "uPcan.hpp"


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR     "PCAN        |"
#define LOG_HDR    LOG_STRING(LT_HDR)

///////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                 //
///////////////////////////////////////////////////////////////////

#define    ARTEFACTS_PATH     "ARTEFACTS_PATH"
#define    PCAN_CHANNEL       "PCAN_CHANNEL"
#define    PCAN_BITRATE       "PCAN_BITRATE"
#define    PCAN_EXTENDED      "PCAN_EXTENDED"
#define    PCAN_FD            "PCAN_FD"
#define    PCAN_TX_ID         "CAN_TX_ID"        // same key name as KVCAN / SLCAN for INI compatibility
#define    PCAN_FILTERS       "CAN_FILTERS"      // same key name as KVCAN for INI compatibility
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
    EXPORTED PCANPlugin* pluginEntry()
    {
        return new PCANPlugin();
    }

    EXPORTED void pluginExit( PCANPlugin *ptrPlugin)
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

bool PCANPlugin::doInit(void *pvUserData)
{
    m_bIsInitialized = true;
    return m_bIsInitialized;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief Function where to execute de-initialization of sub-modules
*/
/*--------------------------------------------------------------------------------------------------------*/

void PCANPlugin::doCleanup(void)
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
  *       PCAN.INFO
  *
  * \param[in] args  empty string (no arguments expected)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool PCANPlugin::m_PCAN_INFO (const std::string &args, std::stop_token st) const
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
    LOG_PRINT(LOG_EMPTY, LOG_STRING(PCAN_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: communicate via PEAK-System PCAN-Basic (USB/PCI/PCIe adapters)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the PCAN channel, bitrate and transfer parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [i:channel] [b:bitrate] [x:tx_id] [r:read_tout] [w:write_tout]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [s:recv_bufsize] [e:extended] [f:fd]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : PCAN.CONFIG i:0x51 b:500000 x:0x7FF r:2000 w:2000 s:8"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         PCAN.CONFIG i:0x51 b:500000 x:0x18DAF100 e:0 f:0"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  i  - PCAN channel handle (decimal or 0x-hex): 0x51=PCAN_USBBUS1,"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("       0x52=PCAN_USBBUS2, 0x41=PCAN_ISABUS1, 0x81=PCAN_PCIBUS1, …"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  b  - CAN bitrate in bps: 1000000, 800000, 500000, 250000, 125000,"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("       100000, 95000, 83000, 50000, 47000, 33000, 20000, 10000, 5000"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  x  - TX CAN ID (decimal or 0x-hex); EFF flag auto-set when ID > 0x7FF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  r  - read timeout in ms (default 1000)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  w  - write timeout in ms (default 1000)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  s  - read buffer size in bytes, 1-64 (default 8 for classic CAN)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  e  - force extended (29-bit) frame format: 0=auto, 1=force EFF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  f  - CAN FD mode: 0=classic CAN (default), 1=CAN FD"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("FILTER : install software acceptance filters on the PCAN channel"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : <id:mask>[,<id:mask>…]  (empty string clears all filters)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : PCAN.FILTER 0x100:0x7FF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         PCAN.FILTER 0x100:0x7FF,0x18DAF100:0x1FFFFFFF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         PCAN.FILTER"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : PCAN-Basic has one hardware filter slot; additional entries are"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         matched in software. Syntax identical to KVCAN.FILTER."));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : send commands from a script file"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : scriptpathname [delay_ms]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : PCAN.SCRIPT obd_sequence.txt"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         PCAN.SCRIPT uds_session.txt 10"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : send, receive or both"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : direction message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : PCAN.CMD > H\"AABBCCDD\" | H\"06\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         PCAN.CMD < \"Ready\" | \"Go!\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : payload must be <= 8 bytes (classic CAN) or <= 64 bytes (CAN FD)"));
    LOG_SEP();

    return true;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CONFIG command implementation; overwrite the current PCAN parameters at runtime.
  *
  * \note Any subset of parameters can be specified; omitted keys retain their current values.
  *       The channel is not reopened by CONFIG — changes take effect on the next CMD or SCRIPT call.
  *
  * \note Usage example:
  *       PCAN.CONFIG i:0x51 b:500000 x:0x7FF r:2000 w:2000 s:8
  *       PCAN.CONFIG i:0x51 b:500000 x:0x18DAF100
  *
  * \param[in] args  [i:channel] [b:bitrate] [x:tx_id] [r:read_tout] [w:write_tout]
  *                  [s:recv_bufsize] [e:extended] [f:fd]
  *
  * \return true if parameters were updated successfully, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool PCANPlugin::m_PCAN_CONFIG (const std::string &args, std::stop_token st) const
{
    return generic_can_set_params<PCANPlugin>(this, args);
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief FILTER command implementation; install software acceptance filters.
  *
  * \note Filters are stored in m_vFilters and applied inside the driver's receive loop
  *       on every CMD or SCRIPT call. Calling FILTER with an empty argument clears all
  *       filters (accept everything).  Syntax is identical to KVCAN.FILTER.
  *
  * \note Usage example:
  *       PCAN.FILTER 0x100:0x7FF
  *       PCAN.FILTER 0x100:0x7FF,0x18DAF100:0x1FFFFFFF
  *       PCAN.FILTER
  *
  * \param[in] args  comma-separated list of <id>:<mask> pairs, or empty to clear
  *
  * \return true on success, false on parse error
*/
/*--------------------------------------------------------------------------------------------------------*/

bool PCANPlugin::m_PCAN_FILTER (const std::string &args, std::stop_token st) const
{
    // if plugin is not enabled stop execution here and return true as the argument(s) validation passed
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
  * \brief CMD command implementation; execute a single send/receive operation over PCAN.
  *
  * \note The PCAN channel is opened for the duration of the call and closed automatically on return (RAII).
  *       Software acceptance filters stored in m_vFilters are passed as an RX filter hint
  *       (first filter entry's id as xtra_params) to the driver's tout_read.
  *
  * \note Usage example:
  *       PCAN.CMD > H\"AABBCCDD\" | H\"06\"
  *       PCAN.CMD < \"Ready\" | \"Go!\"
  *
  * \param[in] args  direction and data expression (see CommScriptCommandValidator grammar)
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool PCANPlugin::m_PCAN_CMD (const std::string &args, std::stop_token st) const
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
            auto shpDriver = m_OpenAndConfigure();

            if (shpDriver && shpDriver->is_open()) {

                CommScriptCommandValidator validator;
                CommCommand command;

                if (true == validator.validateCommand(0, args, command)) {
                    CommScriptCommandInterpreter<PCAN> interpreter(
                        shpDriver,
                        m_u32CanReadBufferSize,
                        m_u32ReadTimeout
                    );
                    bRetVal = interpreter.interpretCommand(command, m_bIsEnabled);
                }
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
  * \brief SCRIPT command implementation; execute a multi-command script file over PCAN.
  *
  * \note The PCAN channel is opened once for the lifetime of the script and closed on return.
  *
  * \note Usage example:
  *       PCAN.SCRIPT obd_sequence.txt
  *       PCAN.SCRIPT uds_session.txt 10
  *
  * \param[in] args  filename [delay_ms]
  *
  * \return true on success, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/

bool PCANPlugin::m_PCAN_SCRIPT (const std::string &args, std::stop_token st) const
{
    bool bRetVal = false;

    do {

        // expected to have as parameter the name of the script
        if (true == args.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing arg(s): scriptpathname [|delay]"));
            break;
        }

        std::vector<std::string> vstrArgs;
        ustring::tokenizeSpaceQuotesAware(args, vstrArgs);
        const size_t szNrArgs = vstrArgs.size();

        // args was already verified non-empty, so szNrArgs >= 1 is guaranteed here;
        // only reject unexpected extra tokens beyond the optional delay argument.
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

        try {
            auto shpDriver = m_OpenAndConfigure();

            if (shpDriver && shpDriver->is_open()) {

                CommScriptClient<PCAN> client(
                    strScriptPathName,
                    shpDriver,
                    m_u32CanReadBufferSize,  // szMaxRecvSize
                    m_u32ReadTimeout,        // u32DefaultTimeout
                    szDelay                  // szDelay
                );
                bRetVal = client.execute(m_bIsEnabled);
            }
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
/**
  * \brief Load configuration from the INI file settings map.
  *
  *  INI key           | Description
  *  ------------------|-----------------------------------------
  *  PCAN_CHANNEL      | Channel handle, decimal or 0x-hex string
  *  PCAN_BITRATE      | CAN bitrate in bps
  *  PCAN_EXTENDED     | Force 29-bit EFF: 0=auto, 1=force
  *  PCAN_FD           | CAN FD mode: 0=classic, 1=FD
  *  CAN_TX_ID         | TX CAN ID (same key as KVCAN/SLCAN)
  *  CAN_FILTERS       | Comma-separated <id:mask> filter list (same key as KVCAN)
  *  READ_TIMEOUT      | Read timeout in ms
  *  WRITE_TIMEOUT     | Write timeout in ms
  *  READ_BUF_SIZE     | Read buffer size in bytes
  *  ARTEFACTS_PATH    | Base path for script files
*/
/*--------------------------------------------------------------------------------------------------------*/

bool PCANPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    bool bRetVal = false;

    if (false == psSetParams->mapSettings.empty()) {
        do {

            auto it = psSetParams->mapSettings.find(ARTEFACTS_PATH);
            if (it != psSetParams->mapSettings.end()) {
                m_strArtefactsPath = it->second;
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ArtefactsPath :"); LOG_STRING(m_strArtefactsPath));
            }

            it = psSetParams->mapSettings.find(PCAN_CHANNEL);
            if (it != psSetParams->mapSettings.end()) {
                m_strPcanChannel = it->second;
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Channel :"); LOG_STRING(m_strPcanChannel));
            }

            it = psSetParams->mapSettings.find(PCAN_BITRATE);
            if (it != psSetParams->mapSettings.end()) {
                if (false == setPcanBitrate(it->second)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Bitrate :"); LOG_UINT32(m_u32Bitrate));
            }

            it = psSetParams->mapSettings.find(PCAN_EXTENDED);
            if (it != psSetParams->mapSettings.end()) {
                if (false == setPcanExtended(it->second)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Extended :"); LOG_UINT32(static_cast<uint32_t>(m_bExtended)));
            }

            it = psSetParams->mapSettings.find(PCAN_FD);
            if (it != psSetParams->mapSettings.end()) {
                if (false == setPcanFd(it->second)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("FD :"); LOG_UINT32(static_cast<uint32_t>(m_bFd)));
            }

            it = psSetParams->mapSettings.find(PCAN_TX_ID);
            if (it != psSetParams->mapSettings.end()) {
                // Route through setCanTxId() so the EFF-flag fixup and data-bit
                // clamping are applied whether the ID comes from the INI file or
                // from the CONFIG command.
                if (false == setCanTxId(it->second)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("TxId :"); LOG_HEX32(m_u32CanTxId));
            }

            it = psSetParams->mapSettings.find(PCAN_FILTERS);
            if (it != psSetParams->mapSettings.end() && !it->second.empty()) {
                if (false == m_ParseFilters(it->second, m_vFilters)) {
                    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to parse CAN_FILTERS:"); LOG_STRING(it->second));
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                          LOG_STRING("Filters :"); LOG_UINT32(static_cast<uint32_t>(m_vFilters.size())));
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
  * \brief Parse a comma-separated "<id>:<mask>" filter string into a vector of (can_id, can_mask) pairs.
  *        Both id and mask fields accept decimal or 0x-prefixed hex values.
  *        Example: "0x100:0x7FF,0x18DAF100:0x1FFFFFFF"
  *
  *        CAN_EFF_FLAG auto-correction mirrors the KVCAN plugin's m_ParseFilters exactly.
*/
/*--------------------------------------------------------------------------------------------------------*/

bool PCANPlugin::m_ParseFilters(const std::string& strFilters,
                                std::vector<std::pair<uint32_t,uint32_t>>& vFilters) const
{
    vFilters.clear();

    // SocketCAN-compatible frame-ID flag bits (mirrors linux/can.h)
    static constexpr uint32_t CAN_EFF_FLAG = 0x80000000U;
    static constexpr uint32_t CAN_RTR_FLAG = 0x40000000U;
    static constexpr uint32_t CAN_ERR_FLAG = 0x20000000U;
    static constexpr uint32_t CAN_SFF_MASK = 0x000007FFU;
    static constexpr uint32_t CAN_EFF_MASK = 0x1FFFFFFFU;

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

        // ── EFF / SFF flag fixup (mirrors KVCAN plugin m_ParseFilters) ──────────
        const uint32_t flagsInId = can_id & (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG);
        can_mask |= flagsInId;

        if (can_id & CAN_EFF_FLAG) {
            can_id   &= (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_EFF_MASK);
            can_mask &= (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG | CAN_EFF_MASK);
        } else {
            if ((can_id & CAN_EFF_MASK) > CAN_SFF_MASK) {
                LOG_PRINT(LOG_WARNING, LOG_HDR;
                          LOG_STRING("Filter id > 0x7FF without CAN_EFF_FLAG — setting EFF flag automatically:"); LOG_STRING(strEntry));
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
  * \brief Open the PCAN channel with the current configuration and return a shared_ptr to
  *        the PCAN driver.  If the software filter list is non-empty the first entry's id
  *        is forwarded to the driver's default RX filter so the hardware narrows the
  *        interrupt load; subsequent entries are matched in the driver's software loop.
  *
  *        Returns nullptr if the channel could not be opened (already logged by the driver).
*/
/*--------------------------------------------------------------------------------------------------------*/

std::shared_ptr<PCAN> PCANPlugin::m_OpenAndConfigure (void) const
{
    auto shpDriver = std::make_shared<PCAN>();

    ICommDriver::Status sts = shpDriver->open(
        m_strPcanChannel,
        m_u32Bitrate,
        m_u32CanTxId,
        m_bExtended,
        m_bFd
    );

    if (sts != ICommDriver::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Failed to open PCAN channel:"); LOG_STRING(m_strPcanChannel.c_str());
                  LOG_STRING("bitrate:"); LOG_UINT32(m_u32Bitrate));
        return nullptr;
    }

    // Forward the first software filter entry to the hardware RX filter
    // so the adapter can pre-filter at the interrupt level.
    // The full list is applied in software inside the driver receive loop
    // via the xtra_params mechanism.
    if (!m_vFilters.empty()) {
        shpDriver->setDefaultRxFilterId(m_vFilters.front().first);
    }

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("PCAN channel ready:"); LOG_STRING(m_strPcanChannel.c_str());
              LOG_STRING("TX ID:"); LOG_HEX32(m_u32CanTxId));

    return shpDriver;
}
