#include "vector_eth_plugin.hpp"

#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"
#include "uCommandExec.hpp"
#include "uFile.hpp"
#include "uHexlify.hpp"
#include "uNumeric.hpp"
#include "uPluginSettings.hpp"
#include "uSharedConfig.hpp"
#include "uString.hpp"
#include "uVectorEth.hpp"
#include "vector_eth_setup.hpp"

#include <algorithm>
#include <cctype>

/////////////////////////////////////////////////////////////////////////////////
//                  PLUGIN ENTRY POINTS                                        //
/////////////////////////////////////////////////////////////////////////////////

extern "C" {
EXPORTED VectorEthPlugin *pluginEntry()
{
    return new VectorEthPlugin();
}

EXPORTED void pluginExit(VectorEthPlugin *ptrPlugin)
{
    if (nullptr != ptrPlugin) {
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
 *       VECTOR_ETH.INFO
 *
 * \param[in] args  empty string (no arguments expected)
 *
 * \return true on success, false otherwise
 */
/*--------------------------------------------------------------------------------------------------------*/

bool VectorEthPlugin::m_VECTOR_ETH_INFO(const std::string &args, std::stop_token st) const
{
    if (!args.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected no argument(s)"));
        return false;
    }

    if (!m_bIsEnabled) {
        return true;
    }

    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING(VECTOR_ETH_PLUGIN_NAME); LOG_STRING("Vers:"); LOG_STRING(m_strVersion));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Build:"); LOG_STRING(__DATE__); LOG_STRING(__TIME__));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Description: communicate via Vector Informatik XL-API's direct Ethernet interface"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("             (VN5610(A)/VN7610/VN7570/VX1135/...), Windows only"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("             for CAN/CAN-FD see the separate VECTOR plugin"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CONFIG : set the Vector application/channel, addressing and PHY parameters"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [a=app_name] [i=app_channel] [hw=device_type] [serial=serial_nr]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [name=channel_name] [hwidx=hw_index] [hwch=hw_channel]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [dst=dest_mac] [type=ethertype] [speed=] [duplex=] [connector=] [phy=]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         [r=read_tout] [w=write_tout] [s=recv_bufsize]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : VECTOR_ETH.CONFIG a=VectorEth_Plugin i=0 dst=AA:BB:CC:DD:EE:FF type=0x0800"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         VECTOR_ETH.CONFIG hw=VN5610A dst=broadcast type=0x88B5"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         VECTOR_ETH.CONFIG serial=12345 speed=auto1000 duplex=full"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  a  - application name as configured in \"Vector Hardware Config\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("       (create it there and assign the VN5610/etc. Ethernet channel to it first)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  i  - zero-based channel index within that application's assignment"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  hw - select a device directly by type (\"VN5610A\" or a raw XL_HWTYPE_*"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("       number), bypassing Vector Hardware Config entirely - see DEVICES below"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  serial - select a device directly by serial number, same bypass as hw="));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  name   - select a channel directly by its exact XL-API name (see DEVICES)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  hwidx  - disambiguates hw=/serial=/name= when more than one board/channel"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  hwch   - matches (multiple boards of the same type, multiple connectors)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  Note: hw=/serial=/name= take priority over a=/i= the moment any of them"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("        is set - the two selection modes are not combined"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  dst    - default destination MAC (\"AA:BB:CC:DD:EE:FF\" or \"broadcast\", default"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("           broadcast); overridable per CMD/SCRIPT/CYCLIC call, see CMD below"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  type   - default EtherType, decimal or 0x-hex (default 0x88B5); overridable per call"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  speed  - PHY link speed: auto/auto100/auto1000/10/100/1000 (default auto)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  duplex - PHY duplex: auto/half/full (default auto)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  connector - physical connector on multi-connector boards: dontcare/rj45/dsub"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  phy    - PHY layer: dontcare/8023/broadr (BroadR-Reach)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  r  - read timeout in ms (default 1000)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  w  - write timeout in ms (default 1000)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  s  - read buffer size in bytes, 1-1500 (Ethernet MTU)"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("FILTER : install a software RX acceptance filter (source MAC and/or EtherType)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : [src=<mac>] [type=<ethertype>]  (empty string clears both filters)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : VECTOR_ETH.FILTER src=AA:BB:CC:DD:EE:FF"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         VECTOR_ETH.FILTER type=0x0800"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         VECTOR_ETH.FILTER src=AA:BB:CC:DD:EE:FF type=0x0800"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         VECTOR_ETH.FILTER src= type="));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : both filters are enforced together (AND); a key with an empty value"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         clears that one filter, an omitted key leaves it unchanged"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("SCRIPT : send commands from a script file"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : scriptpathname [delay_ms]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : VECTOR_ETH.SCRIPT eth_sequence.txt"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CMD    : send, receive or both"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : direction message"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : VECTOR_ETH.CMD > H\"AABBCCDD\" | H\"06\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         VECTOR_ETH.CMD < \"Ready\" | \"Go!\""));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : payload over the 1500-byte MTU is fragmented across frames, one"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         frame per up-to-MTU chunk, with no reassembly framing of its own"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         (see uVectorEth.hpp) - pair with a higher-level protocol if needed"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC : send one or more periodic messages"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : time1 val1 [dst1], time2 val2 [dst2], ... (time_i in ms, val_i hex)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : VECTOR_ETH.CYCLIC 100 AABBCCDD AA:BB:CC:DD:EE:FF, 250 1122"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         VECTOR_ETH.CYCLIC 100 AABBCCDD AA:BB:CC:DD:EE:FF/0x0800 &"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : dst is optional; when omitted, falls back to CONFIG's dst=/type="));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : without '&' sends one full pattern (lcm of the time_i) then returns;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         with '&' repeats forever until the script/thread is stopped"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("DEVICES: list every channel XL-API currently sees, independent of Vector"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         Hardware Config and of whether anything is open"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Args   : none"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Usage  : VECTOR_ETH.DEVICES"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note   : use its output to fill in CONFIG's hw=/serial=/name=/hwidx=/hwch="));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Setup  : before first use, EITHER open \"Vector Hardware Config\" and create"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         an application (matching a=app_name) with your VN5610/etc. Ethernet"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         channel assigned to it, OR skip that step entirely and just run"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("         DEVICES then CONFIG hw=/serial=/name= to select a channel directly"));
    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("INI file parameters (copy/paste into your ini file):"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("[VECTOR_ETH]"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("ARTEFACTS_PATH           =               # directory used by SCRIPT/CMD/wrrdf for reading/writing artefact files"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("VECTOR_ETH_APP_NAME      = VectorEth_Plugin # application name configured in Vector Hardware Config"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("VECTOR_ETH_APP_CHANNEL   = 0             # zero-based channel index within that application"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("VECTOR_ETH_DEVICE_HW     =               # direct selection: device type, bypasses the two keys above"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("VECTOR_ETH_DEVICE_SERIAL =               # direct selection: serial number"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("VECTOR_ETH_DEVICE_NAME   =               # direct selection: exact XL-API channel name"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("VECTOR_ETH_DEVICE_HWINDEX  =             # disambiguates VECTOR_ETH_DEVICE_HW"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("VECTOR_ETH_DEVICE_HWCHANNEL =            # disambiguates multiple connectors on the same board"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("VECTOR_ETH_DEST_MAC      = broadcast     # default destination MAC for outgoing frames"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("VECTOR_ETH_ETHERTYPE     = 0x88B5        # default EtherType for outgoing frames"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("VECTOR_ETH_FILTER_SRC_MAC =              # RX filter: only accept this source MAC (empty = any)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("VECTOR_ETH_FILTER_TYPE   =               # RX filter: only accept this EtherType (empty = any)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("VECTOR_ETH_SPEED         = auto          # PHY link speed"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("VECTOR_ETH_DUPLEX        = auto          # PHY duplex"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("VECTOR_ETH_CONNECTOR     = dontcare      # physical connector (multi-connector boards only)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("VECTOR_ETH_PHY           = dontcare      # PHY layer"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_TIMEOUT             = 1000          # read timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("WRITE_TIMEOUT            = 1000          # write timeout in ms"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("READ_BUF_SIZE            = 1500          # size in bytes of the local read buffer (max 1500)"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("RAW_RESULT               = false         # CMD returns raw bytes instead of a hexlified string when true"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("CYCLIC_CACHED            = true          # true=validate/parse each CYCLIC entry once per session; false=re-resolve every tick"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Note: the CONFIG command above can override a subset of these at runtime;"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("      any key not accepted by CONFIG must be set via the ini file."));

    return true;
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief CONFIG command implementation; overwrite the current VectorEth parameters at runtime.
 *
 * \note Any subset of parameters can be specified; omitted keys retain their current values.
 *       The channel is not reopened by CONFIG - changes take effect on the next CMD or SCRIPT call.
 *
 * \param[in] args  see INFO's CONFIG section for the full key list
 *
 * \return true if parameters were updated successfully, false otherwise
 */
/*--------------------------------------------------------------------------------------------------------*/

bool VectorEthPlugin::m_VECTOR_ETH_CONFIG(const std::string &args, std::stop_token st) const
{
    return generic_eth_set_params<VectorEthPlugin>(this, args);
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief FILTER command implementation; install a software RX acceptance filter
 *        (source MAC and/or EtherType, enforced together).
 *
 * \note Usage example:
 *       VECTOR_ETH.FILTER src=AA:BB:CC:DD:EE:FF
 *       VECTOR_ETH.FILTER type=0x0800
 *       VECTOR_ETH.FILTER src= type=
 *
 * \param[in] args  "[src=<mac>] [type=<ethertype>]", space- or comma-separated
 *
 * \return true on success, false on parse error
 */
/*--------------------------------------------------------------------------------------------------------*/

bool VectorEthPlugin::m_VECTOR_ETH_FILTER(const std::string &args, std::stop_token st) const
{
    if (!m_bIsEnabled) {
        return true;
    }

    if (args.empty()) {
        m_rxFilterSrcMac.reset();
        m_rxFilterEtherType.reset();
        LOG_PRINT(LOG_WERBOSE, LOG_HDR; LOG_STRING("Filters cleared"));
        return true;
    }

    if (false == m_ParseFilter(args, m_rxFilterSrcMac, m_rxFilterEtherType)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FILTER: invalid filter string:"); LOG_STRING(args));
        return false;
    }

    LOG_PRINT(LOG_WERBOSE, LOG_HDR;
              LOG_STRING("Filters set - src:"); LOG_STRING(m_rxFilterSrcMac ? VectorEth::formatMac(*m_rxFilterSrcMac).c_str() : "any");
              LOG_STRING("type:"); LOG_HEX32(m_rxFilterEtherType ? *m_rxFilterEtherType : 0U));

    return true;
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief CMD command implementation; execute a single send/receive operation over VectorEth.
 *
 * \note The VectorEth channel is opened for the duration of the call and closed automatically on
 *       return (RAII).
 *
 * \param[in] args  direction and data expression (see CommScriptCommandValidator grammar)
 *
 * \return true on success, false otherwise
 */
/*--------------------------------------------------------------------------------------------------------*/

bool VectorEthPlugin::m_VECTOR_ETH_CMD(const std::string &args, std::stop_token st) const
{
    return ucmdexec::generic_cmd(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<VectorEth> {
            auto shpDriver = m_OpenAndConfigure();
            return (shpDriver && shpDriver->is_open()) ? shpDriver : nullptr;
        },
        m_strInstanceName,
        m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR, &m_strResultData, m_bRawResult,
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const VectorEth> drv, std::string_view x, std::stop_token tok) {
            return drv->tout_write(t, d, x, tok);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions &o, std::shared_ptr<const VectorEth> drv, std::string_view x, std::stop_token tok) {
            return drv->tout_read(t, b, o, x, tok);
        },
        st);
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief SCRIPT command implementation; execute a multi-command script file over VectorEth.
 *
 * \param[in] args  filename [delay_ms]
 *
 * \return true on success, false otherwise
 */
/*--------------------------------------------------------------------------------------------------------*/

bool VectorEthPlugin::m_VECTOR_ETH_SCRIPT(const std::string &args, std::stop_token st) const
{
    return ucmdexec::generic_script(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<VectorEth> {
            auto shpDriver = m_OpenAndConfigure();
            return (shpDriver && shpDriver->is_open()) ? shpDriver : nullptr;
        },
        m_strInstanceName,
        m_strArtefactsPath, m_u32ReadBufferSize, m_u32ReadTimeout, LT_HDR,
        [](uint32_t t, std::span<const uint8_t> d, std::shared_ptr<const VectorEth> drv, std::string_view x, std::stop_token tok) {
            return drv->tout_write(t, d, x, tok);
        },
        [](uint32_t t, std::span<uint8_t> b, const ICommDriver::ReadOptions &o, std::shared_ptr<const VectorEth> drv, std::string_view x, std::stop_token tok) {
            return drv->tout_read(t, b, o, x, tok);
        },
        st);
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief CYCLIC command implementation; send one or more periodic VectorEth messages.
 *
 * \param[in] args  "time1 val1 [dst1], time2 val2 [dst2], ..." (see generic_send_cyclic())
 * \param[in] st    stop_token; forwarded as-is (present/absent '&' selects run-once vs. forever)
 *
 * \return true on success, false otherwise
 */
/*--------------------------------------------------------------------------------------------------------*/

bool VectorEthPlugin::m_VECTOR_ETH_CYCLIC(const std::string &args, std::stop_token st) const
{
    return ucmdexec::generic_send_cyclic(
        args, m_bIsEnabled,
        [this]() -> std::shared_ptr<VectorEth> {
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
 * \param[in] args  empty string (no arguments expected)
 *
 * \return true on success (even if zero channels are found), false on a malformed call
 */
/*--------------------------------------------------------------------------------------------------------*/

bool VectorEthPlugin::m_VECTOR_ETH_DEVICES(const std::string &args, std::stop_token st) const
{
    (void)st;

    if (!args.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Expected no argument(s)"));
        return false;
    }

    if (!m_bIsEnabled) {
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
    LOG_PRINT(LOG_EMPTY, LOG_STRING("idx  name                            hwType      hwIdx hwCh  serial     onBus  ETH"));
    LOG_SEP();

    uint32_t idx = 0;
    for (const auto &ch : vChannels) {
        char line[160];
        std::snprintf(line, sizeof(line), "%-4u %-31s %-11s %-5u %-5u %-10u %-6s %s",
                      idx++,
                      ch.strName.c_str(),
                      ch.strHwType.c_str(),
                      ch.u32HwIndex,
                      ch.u32HwChannel,
                      ch.u32SerialNumber,
                      ch.bIsOnBus ? "yes" : "no",
                      ch.bSupportsEthernet ? "yes" : "no");
        LOG_PRINT(LOG_EMPTY, LOG_STRING(line));
    }

    LOG_SEP();
    LOG_PRINT(LOG_EMPTY, LOG_STRING("Select one directly with CONFIG's hw=/serial=/name= (add hwidx=/hwch="));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("if hw= alone is ambiguous), e.g.:"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  VECTOR_ETH.CONFIG hw=VN5610A dst=broadcast"));
    LOG_PRINT(LOG_EMPTY, LOG_STRING("  VECTOR_ETH.CONFIG serial=12345"));
    LOG_SEP();

    return true;
}

/////////////////////////////////////////////////////////////////////////////////
//            PRIVATE INTERFACES IMPLEMENTATION                                //
/////////////////////////////////////////////////////////////////////////////////

int VectorEthPlugin::ustring_icompare(const std::string &a, const char *b)
{
    std::string strUpperA(a);
    std::string strUpperB(b);
    std::transform(strUpperA.begin(), strUpperA.end(), strUpperA.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    std::transform(strUpperB.begin(), strUpperB.end(), strUpperB.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return strUpperA.compare(strUpperB);
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Parse FILTER's "[src=<mac>] [type=<ethertype>]" argument string - see
 *        VectorEthPlugin::m_ParseFilter()'s doc comment on vector_eth_plugin.hpp for the full grammar.
 */
/*--------------------------------------------------------------------------------------------------------*/

bool VectorEthPlugin::m_ParseFilter(const std::string &strFilter,
                                    std::optional<VectorEth::MacAddress> &outSrc,
                                    std::optional<uint16_t> &outType) const
{
    std::vector<std::string> vstrTokens;
    ustring::tokenize(strFilter, ' ', vstrTokens);

    // Also accept comma separation for symmetry with VECTOR.FILTER's grammar.
    if (vstrTokens.size() == 1 && vstrTokens[0].find(',') != std::string::npos) {
        vstrTokens.clear();
        ustring::tokenize(strFilter, ',', vstrTokens);
    }

    for (const auto &strToken : vstrTokens) {
        if (strToken.empty()) {
            continue;
        }

        const auto eqPos = strToken.find('=');
        if (eqPos == std::string::npos) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FILTER: expected key=value, got:"); LOG_STRING(strToken));
            return false;
        }

        const std::string strKey   = ustring::trim(strToken.substr(0, eqPos));
        const std::string strValue = ustring::trim(strToken.substr(eqPos + 1));

        if (strKey == "src") {
            if (strValue.empty()) {
                outSrc.reset();
                continue;
            }
            VectorEth::MacAddress mac;
            if (false == VectorEth::parseMac(strValue, mac)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FILTER: malformed src MAC:"); LOG_STRING(strValue));
                return false;
            }
            outSrc = mac;
        } else if (strKey == "type") {
            if (strValue.empty()) {
                outType.reset();
                continue;
            }
            uint32_t u32Val = 0U;
            if ((false == numeric::str2uint32(strValue, u32Val)) || (u32Val > 0xFFFFU)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FILTER: malformed type:"); LOG_STRING(strValue));
                return false;
            }
            outType = static_cast<uint16_t>(u32Val);
        } else {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FILTER: unrecognized key:"); LOG_STRING(strKey));
            return false;
        }
    }

    return true;
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Open the VectorEth channel with the current configuration parameters.
 *
 *        Configuration that must be known at open time (PHY config - see
 *        VectorEth::PhyConfig) is applied via setters BEFORE open()/openDirect()
 *        is called, since VectorEth (like Vector) reads its m_phyConfig at
 *        channel-activation time - see uVectorEth.hpp's m_OpenWithMask_locked().
 *        Addressing (dst MAC/EtherType/RX filters) has no such ordering
 *        constraint but is applied the same way for consistency.
 *
 *        Returns nullptr if the channel could not be opened (already logged by the driver).
 */
/*--------------------------------------------------------------------------------------------------------*/

std::shared_ptr<VectorEth> VectorEthPlugin::m_OpenAndConfigure(void) const
{
    auto shpDriver = std::make_shared<VectorEth>();

    shpDriver->setIdentityLabel(isUsingDirectSelection() ? (m_strDeviceHw.empty() ? m_strDeviceName : m_strDeviceHw)
                                                         : m_strAppName);
    shpDriver->setInstanceName(m_strInstanceName);
    shpDriver->setDefaultDestMac(m_destMac);
    shpDriver->setDefaultEtherType(m_u16EtherType);
    shpDriver->setRxFilterSrcMac(m_rxFilterSrcMac);
    shpDriver->setRxFilterEtherType(m_rxFilterEtherType);
    shpDriver->setPhyConfig(m_phyConfig);

    ICommDriver::Status sts;

    if (isUsingDirectSelection()) {

        Vector::DeviceSelector sel;
        sel.i32HwType       = m_bDeviceHwSet ? static_cast<int32_t>(m_u32DeviceHw) : -1;
        sel.u32SerialNumber = m_u32DeviceSerial;
        sel.strChannelName  = m_strDeviceName;
        sel.i32HwIndex      = m_bDeviceHwIndexSet ? static_cast<int32_t>(m_u32DeviceHwIndex) : -1;
        sel.i32HwChannel    = m_bDeviceHwChannelSet ? static_cast<int32_t>(m_u32DeviceHwChannel) : -1;

        sts                 = shpDriver->openDirect(sel);

        if (sts != ICommDriver::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Failed to open VectorEth channel (direct selection) - "
                                 "run VECTOR_ETH.DEVICES to see what's currently connected"));
            return nullptr;
        }

    } else {

        sts = shpDriver->open(m_strAppName, m_u32AppChannel);

        if (sts != ICommDriver::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Failed to open VectorEth channel, app:"); LOG_STRING(m_strAppName.c_str());
                      LOG_STRING("index:"); LOG_UINT32(m_u32AppChannel));
            return nullptr;
        }
    }

    return shpDriver;
}
