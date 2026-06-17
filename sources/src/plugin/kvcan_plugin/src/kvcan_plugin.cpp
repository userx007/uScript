#include "uSharedConfig.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"

#include "kvcan_setup.hpp"
#include "kvcan_plugin.hpp"

#include "uNumeric.hpp"
#include "uFile.hpp"
#include "uString.hpp"
#include "uHexlify.hpp"
#include "uKVCan.hpp"


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
#define    KVCAN_FILTERS      "CAN_FILTERS"
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
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the KVCAN interface, TX ID and transfer parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [i:iface] [x:tx_id] [r:read_tout] [w:write_tout] [s:recv_bufsize]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : KVCAN.CONFIG i:vcan0 x:0x123 r:2000 w:2000 s:64"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         KVCAN.CONFIG i:can0 x:0x18DAF100"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("FILTER : install hardware acceptance filters on the open socket"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : <id:mask>[,<id:mask>…]  (empty string clears all filters)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : KVCAN.FILTER 0x100:0x7FF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         KVCAN.FILTER 0x100:0x7FF,0x200:0x7FF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         KVCAN.FILTER"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : send commands from a script file"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : script"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : KVCAN.SCRIPT script.txt"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : send, receive or both"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : direction message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : KVCAN.CMD > H\"AABBCCDD\" | H\"06\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         KVCAN.CMD < \"Ready\" | \"Go!\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : payload must be <= 8 bytes (classic KVCAN) or <= 64 bytes (KVCAN FD)"));
    LOG_SEP();

    return true;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief CONFIG command implementation; overwrite the current KVCAN parameters at runtime.
  *
  * \note Any subset of parameters can be specified; omitted keys retain their current values.
  *
  * \note Usage example:
  *       KVCAN.CONFIG i:vcan0 x:0x123 r:2000 w:2000 s:64
  *       KVCAN.CONFIG i:can0 x:0x18DAF100
  *
  * \param[in] args  [i:iface] [x:tx_id] [r:read_tout] [w:write_tout] [s:recv_bufsize]
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

    m_vFilters = vFilters;

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
            // Open the KVCAN socket (RAII — closed automatically by destructor)
            auto shpDriver = std::make_shared<KVCAN>(m_strCanIface);

            if (shpDriver->is_open()) {

                // Apply TX ID and acceptance filters
                shpDriver->set_tx_id(m_u32CanTxId);

                if (!m_vFilters.empty()) {
                    shpDriver->set_filters(m_vFilters);
                }

                CommScriptCommandValidator validator;
                CommCommand command;

                if (true == validator.validateCommand(0, args, command)) {
                    CommScriptCommandInterpreter<KVCAN> interpreter(
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
    bool bRetVal = false;

    do {

        // expected to have as parameter the name of the script
        if (true == args.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Missing arg(s): scriptpathname [|delay]"));
            break;
        }

        std::vector<std::string> vstrArgs;
        ustring::tokenizeSpaceQuotesAware(args, vstrArgs);
        size_t szNrArgs = vstrArgs.size();

        if ((szNrArgs < 1) || (szNrArgs > 2)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected: scriptpathname [|delay] "));
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
            // Open the KVCAN socket (RAII — closed automatically by destructor)
            auto shpDriver = std::make_shared<KVCAN>(m_strCanIface);

            if (shpDriver->is_open()) {

                // Apply TX ID and acceptance filters
                shpDriver->set_tx_id(m_u32CanTxId);

                if (!m_vFilters.empty()) {
                    shpDriver->set_filters(m_vFilters);
                }

                CommScriptClient<KVCAN> client(
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

/*--------------------------------------------------------------------------------------------------------*/

bool KVCANPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    bool bRetVal = false;

    if (false == psSetParams->mapSettings.empty()) {
        do {
            if (psSetParams->mapSettings.count(ARTEFACTS_PATH) > 0) {
                m_strArtefactsPath = psSetParams->mapSettings.at(ARTEFACTS_PATH);
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ArtefactsPath :"); LOG_STRING(m_strArtefactsPath));
            }

            if (psSetParams->mapSettings.count(KVCAN_IFACE) > 0) {
                m_strCanIface = psSetParams->mapSettings.at(KVCAN_IFACE);
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Interface :"); LOG_STRING(m_strCanIface));
            }

            if (psSetParams->mapSettings.count(KVCAN_TX_ID) > 0) {
                // Route through setCanTxId() so the auto-EFF-flag fixup is applied
                // consistently whether the id comes from the INI file or CONFIG command.
                if (false == setCanTxId(psSetParams->mapSettings.at(KVCAN_TX_ID))) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("TxId :"); LOG_HEX32(m_u32CanTxId));
            }

            if (psSetParams->mapSettings.count(KVCAN_FILTERS) > 0) {
                const std::string& strFilters = psSetParams->mapSettings.at(KVCAN_FILTERS);
                if (!strFilters.empty()) {
                    if (false == m_ParseFilters(strFilters, m_vFilters)) {
                        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to parse KVCAN_FILTERS:"); LOG_STRING(strFilters));
                        break;
                    }
                    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
                              LOG_STRING("Filters :"); LOG_UINT32(static_cast<uint32_t>(m_vFilters.size())));
                }
            }

            if (psSetParams->mapSettings.count(READ_TIMEOUT) > 0) {
                if (false == numeric::str2uint32(psSetParams->mapSettings.at(READ_TIMEOUT), m_u32ReadTimeout)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("ReadTimeout :"); LOG_UINT32(m_u32ReadTimeout));
            }

            if (psSetParams->mapSettings.count(WRITE_TIMEOUT) > 0) {
                if (false == numeric::str2uint32(psSetParams->mapSettings.at(WRITE_TIMEOUT), m_u32WriteTimeout)) {
                    break;
                }
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("WriteTimeout :"); LOG_UINT32(m_u32WriteTimeout));
            }

            if (psSetParams->mapSettings.count(READ_BUF_SIZE) > 0) {
                if (false == numeric::str2uint32(psSetParams->mapSettings.at(READ_BUF_SIZE), m_u32CanReadBufferSize)) {
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

bool KVCANPlugin::m_Send(std::span<const uint8_t> dataSpan, std::shared_ptr<const ICommDriver> shpDriver) const
{
    auto result = shpDriver->tout_write(m_u32WriteTimeout, dataSpan);

    if (result.status != ICommDriver::Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Write failed:");
                  LOG_STRING(ICommDriver::to_string(result.status));
                  LOG_STRING("Bytes written:"); LOG_SIZET(result.bytes_written));
        return false;
    }

    return true;
}


/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief message receiver
*/
/*--------------------------------------------------------------------------------------------------------*/

bool KVCANPlugin::m_Receive(std::span<uint8_t> dataSpan, size_t& szSize, CommCommandReadType readType, std::shared_ptr<const ICommDriver> shpDriver) const
{
    bool bRetVal = false;
    ICommDriver::ReadOptions options;

    switch(readType)
    {
        case CommCommandReadType::LINE:
            options.mode      = ICommDriver::ReadMode::UntilDelimiter;
            options.delimiter = '\n';
            break;

        case CommCommandReadType::TOKEN_STRING:
            [[fallthrough]];
        case CommCommandReadType::TOKEN_HEXSTREAM:
            options.mode       = ICommDriver::ReadMode::UntilToken;
            options.token      = dataSpan;
            options.use_buffer = true;
            break;

        default:
            options.mode = ICommDriver::ReadMode::Exact;
            break;
    }

    auto result = shpDriver->tout_read(m_u32ReadTimeout, dataSpan, options);

    if (result.status == ICommDriver::Status::SUCCESS) {
        szSize  = result.bytes_read;
        bRetVal = true;
    } else {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Read failed:");
                  LOG_STRING(ICommDriver::to_string(result.status));
                  LOG_STRING("Bytes read:"); LOG_SIZET(result.bytes_read));
        szSize  = result.bytes_read;
        bRetVal = false;
    }

    return bRetVal;
}
