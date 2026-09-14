#ifndef VECTOR_PLUGIN_HPP
#define VECTOR_PLUGIN_HPP
#include "ICommDriver.hpp"
#include "IPlugin.hpp"
#include "IPluginDataTypes.hpp"
#include "ITransportProtocol.hpp"
#include "PluginExport.hpp"
#include "PluginOperations.hpp"
#include "TpConfig.hpp"
#include "TpFactory.hpp"
#include "uBoolEvaluator.hpp"
#include "uCommScriptClient.hpp"
#include "uCommScriptCommandInterpreter.hpp"
#include "uCommandExec.hpp"
#include "uLogger.hpp"
#include "uNumeric.hpp"
#include "uSharedConfig.hpp"
#include "uVector.hpp"

#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN NAME / VERSION                              //
/////////////////////////////////////////////////////////////////////////////////

#define VECTOR_PLUGIN_VERSION "1.0.0.0"
#define VECTOR_PLUGIN_NAME    "VECTOR"

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN MACROS                                      //
/////////////////////////////////////////////////////////////////////////////////

#ifndef VECTOR_GET_BLOCKING
#define VECTOR_GET_BLOCKING(name, blocking, ...) blocking
#endif

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN COMMANDS                                    //
/////////////////////////////////////////////////////////////////////////////////

#define VECTOR_PLUGIN_COMMANDS_CONFIG_TABLE \
    VECTOR_PLUGIN_CMD_RECORD(INFO)          \
    VECTOR_PLUGIN_CMD_RECORD(CONFIG)        \
    VECTOR_PLUGIN_CMD_RECORD(FILTER)        \
    VECTOR_PLUGIN_CMD_RECORD(CMD)           \
    VECTOR_PLUGIN_CMD_RECORD(SCRIPT)        \
    VECTOR_PLUGIN_CMD_RECORD(CYCLIC)        \
    VECTOR_PLUGIN_CMD_RECORD(DEVICES)

/////////////////////////////////////////////////////////////////////////////////
//                          PLUGIN INTERFACE                                   //
/////////////////////////////////////////////////////////////////////////////////

/**
 * \brief Vector plugin class definition.
 *
 * Wraps the uVector driver (Vector Informatik XL-API, e.g. VN1610/VN16xx)
 * and exposes it through the standard PluginInterface dispatch model.
 * Windows only — see uVector.hpp.
 *
 * The command set (INFO / CONFIG / FILTER / CMD / SCRIPT / CYCLIC) and the
 * CONFIG/FILTER argument grammar deliberately mirror the PCAN and KVCAN
 * plugins so scripts written for those can be reused with minimal changes.
 *
 * Key differences vs PCAN:
 *   - "a=" the application name configured in Vector Hardware Config
 *     (replaces PCAN's "i=" channel-handle key).
 *   - "i=" here is instead the zero-based channel INDEX within that
 *     application's assignment (PCAN's "i=" is a global channel handle;
 *     XL-API has no such thing — see uVector.hpp's "Device selection" note).
 *   - "f=" (CAN FD) is fully implemented (see uVector.hpp); "d="/"iso="/"brs="
 *     tune the data-phase bitrate, ISO vs Bosch/non-ISO framing, and whether
 *     outgoing FD frames use the bitrate switch, respectively.
 *   - FILTER has the same single-active-filter caveat as PCAN.FILTER: only
 *     the first "<id>:<mask>" entry is enforced (uVector's
 *     frameMatchesFilter() tracks one id in software, same as PCAN — there
 *     is no XL-API hardware acceptance filter involved here).
 *
 * TX/RX id defaults, per-call overrides, and multi-frame transport protocol
 * selection (t=/y=, CAN_TP_PROTOCOL/CAN_RX_ID) all work exactly as
 * documented in pcan_plugin.hpp — the underlying can_tp library and its
 * RawIo indirection are shared, unmodified, across every CAN plugin in this
 * codebase.
 */
class VectorPlugin : public PluginInterface
{
public:
    /**
     * \brief class constructor
     */
    VectorPlugin()
        : m_strVersion(VECTOR_PLUGIN_VERSION)
        , m_strInstanceName(VECTOR_PLUGIN_NAME)
        , m_bIsInitialized(false)
        , m_bIsEnabled(false)
        , m_bIsFaultTolerant(false)
        , m_bIsPrivileged(false)
        , m_strResultData()
        , m_bRawResult(false)
        , m_bCyclicCached(true)
        , m_strAppName("Vector_Plugin") // must match an app configured in Vector Hardware Config
        , m_u32AppChannel(0U)
        , m_strDeviceHw()
        , m_bDeviceHwSet(false)
        , m_u32DeviceHw(0U)
        , m_u32DeviceSerial(0U)
        , m_strDeviceName()
        , m_bDeviceHwIndexSet(false)
        , m_u32DeviceHwIndex(0U)
        , m_bDeviceHwChannelSet(false)
        , m_u32DeviceHwChannel(0U)
        , m_u32Bitrate(500000U)
        , m_bExtended(false)
        , m_bFd(false)
        , m_u32FdDataBitrate(Vector::VECTOR_DEFAULT_FD_DATA_BITRATE)
        , m_bFdIso(true)
        , m_bFdBrs(true)
        , m_u8FdPaddingByte(0x00U)
        , m_u32CanTxId(Vector::VECTOR_DEFAULT_TX_ID)
        , m_bCanRxIdSet(false)
        , m_u32CanRxId(0U)
        , m_eTpProtocol(TpProtocol::NONE)
        , m_sTpConfig()
        , m_u32ReadTimeout(1000U)
        , m_u32WriteTimeout(1000U)
        , m_u32ReadBufferSize(8U)
    {
#define VECTOR_PLUGIN_CMD_RECORD(a, ...) m_mapCmds.insert(std::make_pair(#a, \
                                                                         PluginCommandEntry<VectorPlugin>{&VectorPlugin::m_VECTOR_##a, VECTOR_GET_BLOCKING(a, ##__VA_ARGS__, false)}));
        VECTOR_PLUGIN_COMMANDS_CONFIG_TABLE
#undef VECTOR_PLUGIN_CMD_RECORD
    }

    ~VectorPlugin() = default;

    bool isInitialized(void) const
    {
        return m_bIsInitialized;
    }

    bool isEnabled(void) const
    {
        return m_bIsEnabled;
    }

    bool setParams(const PluginDataSet *psSetParams)
    {
        bool bRetVal = false;

        if (true == generic_setparams<VectorPlugin>(this, psSetParams, &m_bIsFaultTolerant, &m_bIsPrivileged)) {
            if (true == m_LocalSetParams(psSetParams)) {
                bRetVal = true;
            }
        }

        return bRetVal;
    }

    void getParams(PluginDataGet *psGetParams) const
    {
        generic_getparams<VectorPlugin>(this, psGetParams);
    }

    bool doInit(void *pvUserData)
    {
        m_bIsInitialized = true;
        return m_bIsInitialized;
    }

    bool doEnable(void)
    {
        m_bIsEnabled = true;
        return true;
    }

    void doCleanup(void)
    {
        m_bIsInitialized = false;
        m_bIsEnabled     = false;
    }

    bool doDispatch(const std::string &strCmd, const std::string &strParams, std::stop_token st = {}) const
    {
        return generic_dispatch<VectorPlugin>(this, strCmd, strParams, st);
    }

    const PluginCommandsMap<VectorPlugin> *getMap(void) const
    {
        return &m_mapCmds;
    }

    const std::string &getVersion(void) const
    {
        return m_strVersion;
    }

    const std::string &getData(void) const
    {
        return m_strResultData;
    }

    void resetData(void) const
    {
        m_strResultData.clear();
    }

    bool setRawResult(const std::string &strValue) const
    {
        return ucmdexec::parseRawResultFlag(strValue, m_bRawResult);
    }

    bool setCyclicCached(const std::string &strValue) const
    {
        return ucmdexec::parseCyclicCachedFlag(strValue, m_bCyclicCached);
    }

    bool isFaultTolerant(void) const
    {
        return m_bIsFaultTolerant;
    }

    bool isPrivileged(void) const
    {
        return m_bIsPrivileged;
    }

    /**
     * \brief get the Vector Hardware Config application name
     */
    const char *getAppName(void) const
    {
        return m_strAppName.c_str();
    }

    /**
     * \brief set the Vector Hardware Config application name.
     *        Must match an entry created in "Vector Hardware Config"
     *        with a CAN channel assigned to it.
     */
    void setAppName(const std::string &strAppName) const
    {
        m_strAppName.assign(strAppName);
    }

    /**
     * \brief set the zero-based channel index within this application's
     *        assignment (see uVector.hpp's "Device selection" note)
     */
    bool setAppChannel(const std::string &strChannel) const
    {
        return numeric::str2uint32(strChannel, m_u32AppChannel);
    }

    /**
     * \brief select a physical channel directly by device type, bypassing
     *        Vector Hardware Config entirely. Accepts either a known type
     *        name (case-insensitive, e.g. "VN1610") or a raw numeric
     *        XL_HWTYPE_* value - see Vector::hwTypeFromString(). Setting
     *        this (or setDeviceSerial()/setDeviceName()) makes
     *        m_OpenAndConfigure() use Vector::openDirect() instead of the
     *        Vector Hardware Config a=/i= path - see class comment.
     */
    bool setDeviceHw(const std::string &strHw) const
    {
        uint32_t u32HwType = 0U;
        if (false == Vector::hwTypeFromString(strHw, u32HwType)) {
            LOG_PRINT(LOG_ERROR, LOG_STRING("VECTOR |");
                      LOG_STRING("Unknown device type, run VECTOR.DEVICES to see what's connected:");
                      LOG_STRING(strHw.c_str()));
            return false;
        }
        m_strDeviceHw  = strHw;
        m_u32DeviceHw  = u32HwType;
        m_bDeviceHwSet = true;
        return true;
    }

    /**
     * \brief select a physical channel directly by serial number, bypassing
     *        Vector Hardware Config. See setDeviceHw() for the selection-mode note.
     */
    bool setDeviceSerial(const std::string &strSerial) const
    {
        return numeric::str2uint32(strSerial, m_u32DeviceSerial);
    }

    /**
     * \brief select a physical channel directly by its exact XL-API channel
     *        name (as shown by VECTOR.DEVICES), bypassing Vector Hardware
     *        Config. See setDeviceHw() for the selection-mode note.
     */
    bool setDeviceName(const std::string &strName) const
    {
        if (strName.empty()) {
            LOG_PRINT(LOG_ERROR, LOG_STRING("VECTOR |"); LOG_STRING("Device name cannot be empty"));
            return false;
        }
        m_strDeviceName = strName;
        return true;
    }

    /**
     * \brief disambiguate between multiple boards of the same device type
     *        (setDeviceHw()) - the hardware index XL-API reports, not a
     *        global channel handle. Only meaningful together with
     *        setDeviceHw()/setDeviceSerial()/setDeviceName().
     */
    bool setDeviceHwIndex(const std::string &strHwIndex) const
    {
        if (false == numeric::str2uint32(strHwIndex, m_u32DeviceHwIndex)) {
            return false;
        }
        m_bDeviceHwIndexSet = true;
        return true;
    }

    /**
     * \brief disambiguate between multiple connectors on the same board
     *        (setDeviceHw()/setDeviceHwIndex()). Only meaningful together
     *        with at least one of setDeviceHw()/setDeviceSerial()/setDeviceName().
     */
    bool setDeviceHwChannel(const std::string &strHwChannel) const
    {
        if (false == numeric::str2uint32(strHwChannel, m_u32DeviceHwChannel)) {
            return false;
        }
        m_bDeviceHwChannelSet = true;
        return true;
    }

    /**
     * \brief true once any direct-selection key (hw=/serial=/name=) has been
     *        set - m_OpenAndConfigure() checks this to decide between
     *        Vector::openDirect() and the Vector Hardware Config a=/i= path.
     */
    bool isUsingDirectSelection(void) const
    {
        return m_bDeviceHwSet || (m_u32DeviceSerial != 0U) || !m_strDeviceName.empty();
    }

    /**
     * \brief set the CAN bitrate in bps (e.g. "500000" for 500 kbps)
     */
    bool setVectorBitrate(const std::string &strBitrate) const
    {
        return numeric::str2uint32(strBitrate, m_u32Bitrate);
    }

    /**
     * \brief force 29-bit extended frame format for all outgoing frames
     *        "0" = auto-detect from TX ID (default), "1" = force EFF
     */
    bool setVectorExtended(const std::string &strExtended) const
    {
        uint32_t u32Val = 0U;
        if ((false == numeric::str2uint32(strExtended, u32Val)) || (u32Val > 1U)) {
            LOG_PRINT(LOG_ERROR, LOG_STRING("VECTOR |");
                      LOG_STRING("Extended must be 0 (auto) or 1 (force EFF):"); LOG_UINT32(u32Val));
            return false;
        }
        m_bExtended = (1U == u32Val);
        return true;
    }

    /**
     * \brief CAN FD mode: "0" = classic CAN (default), "1" = CAN FD.
     *        When enabled, see setVectorFdDataBitrate()/setVectorFdIso()/
     *        setVectorFdBrs() to tune the data-phase bitrate, ISO vs
     *        Bosch/non-ISO framing, and bitrate-switch behaviour.
     */
    bool setVectorFd(const std::string &strFd) const
    {
        uint32_t u32Val = 0U;
        if ((false == numeric::str2uint32(strFd, u32Val)) || (u32Val > 1U)) {
            LOG_PRINT(LOG_ERROR, LOG_STRING("VECTOR |");
                      LOG_STRING("FD must be 0 (classic) or 1 (FD):"); LOG_UINT32(u32Val));
            return false;
        }
        m_bFd = (1U == u32Val);
        return true;
    }

    /**
     * \brief CAN FD data-phase bitrate in bps (e.g. "2000000" for 2 Mbit/s).
     *        Only meaningful when f=1 (see setVectorFd()); the bitrate set
     *        via CONFIG's b= becomes the arbitration-phase bitrate.
     */
    bool setVectorFdDataBitrate(const std::string &strDataBitrate) const
    {
        return numeric::str2uint32(strDataBitrate, m_u32FdDataBitrate);
    }

    /**
     * \brief CAN FD framing standard: "1" (default) = ISO 11898-1:2015,
     *        "0" = pre-standard Bosch/"non-ISO" CAN FD. Only meaningful
     *        when f=1.
     */
    bool setVectorFdIso(const std::string &strIso) const
    {
        BoolExprEvaluator sEvaluator;
        return sEvaluator.evaluate(strIso, m_bFdIso);
    }

    /**
     * \brief Whether outgoing CAN FD frames use the data-phase bitrate
     *        switch (BRS): "1" (default) = switch to the data bitrate,
     *        "0" = send FD-framed (EDL) messages at the arbitration
     *        bitrate only. Only meaningful when f=1.
     */
    bool setVectorFdBrs(const std::string &strBrs) const
    {
        BoolExprEvaluator sEvaluator;
        return sEvaluator.evaluate(strBrs, m_bFdBrs);
    }

    /**
     * \brief Fill byte used to pad a short CAN FD fragment up to the next
     *        legal CAN-FD DLC length (0-8,12,16,20,24,32,48,64 bytes).
     *        Only meaningful when f=1. Default 0x00.
     */
    bool setVectorFdPaddingByte(const std::string &strByte) const
    {
        return numeric::str2uint8(strByte, m_u8FdPaddingByte);
    }

    /**
     * \brief set the CAN ID stamped on outgoing frames, and mirror it onto the
     *        default RX acceptance filter. Same SocketCAN canid_t convention
     *        and EFF auto-detection/clamping as PCANPlugin::setCanTxId().
     */
    bool setCanTxId(const std::string &strTxId) const
    {
        static constexpr uint32_t CAN_EFF_FLAG = 0x80000000U;
        static constexpr uint32_t CAN_SFF_MASK = 0x000007FFU;
        static constexpr uint32_t CAN_EFF_MASK = 0x1FFFFFFFU;

        uint32_t u32Id                         = 0U;
        if (false == numeric::str2uint32(strTxId, u32Id)) {
            return false;
        }

        if (!(u32Id & CAN_EFF_FLAG) && ((u32Id & CAN_EFF_MASK) > CAN_SFF_MASK)) {
            u32Id |= CAN_EFF_FLAG;
        }

        if (u32Id & CAN_EFF_FLAG) {
            u32Id &= (CAN_EFF_FLAG | CAN_EFF_MASK);
        } else {
            u32Id &= CAN_SFF_MASK;
        }

        m_u32CanTxId           = u32Id;

        const uint32_t u32Mask = (u32Id & CAN_EFF_FLAG) ? (CAN_EFF_FLAG | CAN_EFF_MASK)
                                                        : CAN_SFF_MASK;
        m_vFilters.clear();
        m_vFilters.emplace_back(u32Id, u32Mask);

        return true;
    }

    /**
     * \brief set the CAN id this plugin expects incoming (response)
     *        frames to arrive on, when it differs from the TX id.
     *        Only meaningful once a transport protocol other than
     *        TpProtocol::NONE is selected (see setCanTpProtocol()).
     */
    bool setCanRxId(const std::string &strRxId) const
    {
        static constexpr uint32_t CAN_EFF_FLAG = 0x80000000U;
        static constexpr uint32_t CAN_SFF_MASK = 0x000007FFU;
        static constexpr uint32_t CAN_EFF_MASK = 0x1FFFFFFFU;

        uint32_t u32Id                         = 0U;
        if (false == numeric::str2uint32(strRxId, u32Id)) {
            return false;
        }

        if (!(u32Id & CAN_EFF_FLAG) && ((u32Id & CAN_EFF_MASK) > CAN_SFF_MASK)) {
            u32Id |= CAN_EFF_FLAG;
        }

        if (u32Id & CAN_EFF_FLAG) {
            u32Id &= (CAN_EFF_FLAG | CAN_EFF_MASK);
        } else {
            u32Id &= CAN_SFF_MASK;
        }

        m_u32CanRxId  = u32Id;
        m_bCanRxIdSet = true;

        return true;
    }

    /**
     * \brief select the multi-frame transport protocol used for
     *        payloads that don't fit in a single CAN frame.
     *        Accepted values (case-insensitive): "none" (default),
     *        "isotp", "j1939".
     */
    bool setCanTpProtocol(const std::string &strProtocol) const
    {
        TpProtocol eProto = TpProtocol::NONE;
        if (false == tp_protocol_from_string(strProtocol, eProto)) {
            LOG_PRINT(LOG_ERROR, LOG_STRING("VECTOR |");
                      LOG_STRING("Unknown CAN transport protocol:"); LOG_STRING(strProtocol.c_str()));
            return false;
        }
        m_eTpProtocol = eProto;
        return true;
    }

    // ---- TpConfig tuning parameters -- identical field set/semantics to PCAN/KVCAN ----

    bool setTpBlockSize(const std::string &strVal) const
    {
        return numeric::str2uint8(strVal, m_sTpConfig.blockSize);
    }

    bool setTpStMin(const std::string &strVal) const
    {
        return numeric::str2uint8(strVal, m_sTpConfig.stMin);
    }

    bool setTpPadFrames(const std::string &strVal) const
    {
        BoolExprEvaluator sEvaluator;
        return sEvaluator.evaluate(strVal, m_sTpConfig.padFrames);
    }

    bool setTpPaddingByte(const std::string &strVal) const
    {
        return numeric::str2uint8(strVal, m_sTpConfig.paddingByte);
    }

    bool setTpTimeoutNBs(const std::string &strVal) const
    {
        return numeric::str2uint32(strVal, m_sTpConfig.timeoutNBs_ms);
    }

    bool setTpTimeoutNCr(const std::string &strVal) const
    {
        return numeric::str2uint32(strVal, m_sTpConfig.timeoutNCr_ms);
    }

    bool setTpMaxMessageLen(const std::string &strVal) const
    {
        return numeric::str2sizet(strVal, m_sTpConfig.maxMessageLen);
    }

    bool setJ1939UseBam(const std::string &strVal) const
    {
        BoolExprEvaluator sEvaluator;
        return sEvaluator.evaluate(strVal, m_sTpConfig.j1939UseBam);
    }

    bool setJ1939MaxPackets(const std::string &strVal) const
    {
        return numeric::str2uint8(strVal, m_sTpConfig.j1939MaxPackets);
    }

    bool setTpTimeoutT1(const std::string &strVal) const
    {
        return numeric::str2uint32(strVal, m_sTpConfig.timeoutT1_ms);
    }

    bool setTpTimeoutT2(const std::string &strVal) const
    {
        return numeric::str2uint32(strVal, m_sTpConfig.timeoutT2_ms);
    }

    bool setTpTimeoutT3(const std::string &strVal) const
    {
        return numeric::str2uint32(strVal, m_sTpConfig.timeoutT3_ms);
    }

    bool setTpTimeoutTh(const std::string &strVal) const
    {
        return numeric::str2uint32(strVal, m_sTpConfig.timeoutTh_ms);
    }

    bool setJ1939MaxMessageLen(const std::string &strVal) const
    {
        return numeric::str2sizet(strVal, m_sTpConfig.j1939MaxMessageLen);
    }

    bool setCanOpenIndex(const std::string &strVal) const
    {
        return numeric::str2uint16(strVal, m_sTpConfig.canOpenIndex);
    }

    bool setCanOpenSubIndex(const std::string &strVal) const
    {
        return numeric::str2uint8(strVal, m_sTpConfig.canOpenSubIndex);
    }

    bool setCanOpenUseBlock(const std::string &strVal) const
    {
        BoolExprEvaluator sEvaluator;
        return sEvaluator.evaluate(strVal, m_sTpConfig.canOpenUseBlock);
    }

    bool setCanOpenBlockSize(const std::string &strVal) const
    {
        return numeric::str2uint8(strVal, m_sTpConfig.canOpenBlockSize);
    }

    bool setTpTimeoutSdo(const std::string &strVal) const
    {
        return numeric::str2uint32(strVal, m_sTpConfig.timeoutSdo_ms);
    }

    bool setCanOpenMaxMessageLen(const std::string &strVal) const
    {
        return numeric::str2sizet(strVal, m_sTpConfig.canOpenMaxMessageLen);
    }

    bool setTpTimeoutFpInterFrame(const std::string &strVal) const
    {
        return numeric::str2uint32(strVal, m_sTpConfig.timeoutFpInterFrame_ms);
    }

    bool setFpMaxMessageLen(const std::string &strVal) const
    {
        return numeric::str2sizet(strVal, m_sTpConfig.fastPacketMaxMessageLen);
    }

    bool setCanReadTimeout(const std::string &strReadTimeout) const
    {
        return numeric::str2uint32(strReadTimeout, m_u32ReadTimeout);
    }

    bool setCanWriteTimeout(const std::string &strWriteTimeout) const
    {
        return numeric::str2uint32(strWriteTimeout, m_u32WriteTimeout);
    }

    /**
     * \brief set Vector read buffer size in bytes: 1-8 for classic CAN,
     *        1-64 once f=1 (CAN FD) is selected -- see setVectorFd().
     */
    bool setCanReadBufferSize(const std::string &strReadBufferSize) const
    {
        uint32_t u32Size = 0U;
        if (false == numeric::str2uint32(strReadBufferSize, u32Size)) {
            return false;
        }
        const uint32_t u32Max = m_bFd ? static_cast<uint32_t>(Vector::VECTOR_FD_MAX_PAYLOAD)
                                      : static_cast<uint32_t>(Vector::VECTOR_MAX_PAYLOAD);
        if (u32Size == 0U || u32Size > u32Max) {
            LOG_PRINT(LOG_ERROR, LOG_STRING("VECTOR |");
                      LOG_STRING("ReadBufSize out of range [1-"); LOG_UINT32(u32Max);
                      LOG_STRING("]:"); LOG_UINT32(u32Size));
            return false;
        }
        m_u32ReadBufferSize = u32Size;
        return true;
    }

private:
    bool m_LocalSetParams(const PluginDataSet *psSetParams);

    /**
     * \brief helper: parse a comma-separated filter list string into a list of id:mask pairs.
     *        Identical grammar/semantics to PCANPlugin::m_ParseFilters() /
     *        KVCAN's m_ParseFilters(): only the FIRST parsed entry is
     *        actually enforced by the driver (uVector's single active
     *        RX filter id, checked in software).
     */
    bool m_ParseFilters(const std::string &strFilters,
                        std::vector<std::pair<uint32_t, uint32_t>> &vFilters) const;

    /**
     * \brief Open the Vector channel with the current configuration parameters.
     *        Returns a ready-to-use Vector driver instance, or nullptr if
     *        any step failed (already logged).
     */
    std::shared_ptr<Vector> m_OpenAndConfigure(void) const;

    PluginCommandsMap<VectorPlugin> m_mapCmds;
    std::string m_strVersion;
    std::string m_strInstanceName;
    bool m_bIsInitialized;
    bool m_bIsEnabled;
    bool m_bIsFaultTolerant;
    bool m_bIsPrivileged;
    mutable std::string m_strResultData;
    mutable bool m_bRawResult;
    mutable bool m_bCyclicCached;
    std::string m_strArtefactsPath;

    /** Vector Hardware Config application name (see setAppName()) */
    mutable std::string m_strAppName;

    /** zero-based channel index within that application's assignment */
    mutable uint32_t m_u32AppChannel;

    /** direct-selection alternative to m_strAppName/m_u32AppChannel - see isUsingDirectSelection() */
    mutable std::string m_strDeviceHw;
    mutable bool m_bDeviceHwSet;
    mutable uint32_t m_u32DeviceHw;
    mutable uint32_t m_u32DeviceSerial;
    mutable std::string m_strDeviceName;
    mutable bool m_bDeviceHwIndexSet;
    mutable uint32_t m_u32DeviceHwIndex;
    mutable bool m_bDeviceHwChannelSet;
    mutable uint32_t m_u32DeviceHwChannel;

    mutable uint32_t m_u32Bitrate;
    mutable bool m_bExtended;
    mutable bool m_bFd;
    mutable uint32_t m_u32FdDataBitrate;
    mutable bool m_bFdIso;
    mutable bool m_bFdBrs;
    mutable uint8_t m_u8FdPaddingByte;
    mutable uint32_t m_u32CanTxId;
    mutable bool m_bCanRxIdSet;
    mutable uint32_t m_u32CanRxId;
    mutable TpProtocol m_eTpProtocol;
    mutable TpConfig m_sTpConfig;
    mutable uint32_t m_u32ReadTimeout;
    mutable uint32_t m_u32WriteTimeout;
    mutable uint32_t m_u32ReadBufferSize;
    mutable std::vector<std::pair<uint32_t, uint32_t>> m_vFilters;

#define VECTOR_PLUGIN_CMD_RECORD(a, ...) bool m_VECTOR_##a(const std::string &args, std::stop_token st) const;
    VECTOR_PLUGIN_COMMANDS_CONFIG_TABLE
#undef VECTOR_PLUGIN_CMD_RECORD
};

#endif /* VECTOR_PLUGIN_HPP */
