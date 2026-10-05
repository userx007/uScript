#ifndef KCAN_SETUP_HPP
#define KCAN_SETUP_HPP
#include "PluginSetup.hpp"
#include "kcan_plugin.hpp"
#include "uCommandExec.hpp"
#include "uPluginSettings.hpp"

#include <string>

/////////////////////////////////////////////////////////////////////////////////
//                            LOG DEFINITIONS                                  //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
#undef LT_HDR
#endif
#ifdef LOG_HDR
#undef LOG_HDR
#endif

#define LT_HDR                   "KCAN_P     |"
#define LOG_HDR                  LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                               //
/////////////////////////////////////////////////////////////////////////////////

#define ARTEFACTS_PATH           "ARTEFACTS_PATH"
#define KCAN_IFACE              "CAN_IFACE"
#define KCAN_TX_ID              "CAN_TX_ID"
#define KCAN_RX_ID              "CAN_RX_ID"
#define KCAN_FILTERS            "CAN_FILTERS"
#define KCAN_BITRATE            "CAN_BITRATE"
#define KCAN_SAMPLE_POINT       "CAN_SAMPLE_POINT"
#define KCAN_DATA_BITRATE       "CAN_DATA_BITRATE"
#define KCAN_DATA_SAMPLE_POINT  "CAN_DATA_SAMPLE_POINT"
#define KCAN_FD                 "CAN_FD"
#define KCAN_LISTEN_ONLY        "CAN_LISTEN_ONLY"
#define KCAN_LOOPBACK           "CAN_LOOPBACK"
#define KCAN_ONE_SHOT           "CAN_ONE_SHOT"
#define KCAN_TRIPLE_SAMPLING    "CAN_TRIPLE_SAMPLING"
#define KCAN_BERR_REPORTING     "CAN_BERR_REPORTING"
#define KCAN_RESTART_MS         "CAN_RESTART_MS"
#define KCAN_AUTO_LINK          "CAN_AUTO_LINK"
#define KCAN_ERR_MASK           "CAN_ERR_MASK"
#define READ_TIMEOUT             "READ_TIMEOUT"
#define WRITE_TIMEOUT            "WRITE_TIMEOUT"
#define READ_BUF_SIZE            "READ_BUF_SIZE"
#define CAN_TP_PROTOCOL          "CAN_TP_PROTOCOL"

// ---- TpConfig tuning parameters (see setCanTpProtocol() family in kcan_plugin.hpp) ----
#define TP_BLOCK_SIZE            "TP_BLOCK_SIZE"
#define TP_ST_MIN                "TP_ST_MIN"
#define TP_PAD_FRAMES            "TP_PAD_FRAMES"
#define TP_PADDING_BYTE          "TP_PADDING_BYTE"
#define TP_TIMEOUT_NBS           "TP_TIMEOUT_NBS"
#define TP_TIMEOUT_NCR           "TP_TIMEOUT_NCR"
#define TP_MAX_MSG_LEN           "TP_MAX_MSG_LEN"
#define J1939_USE_BAM            "J1939_USE_BAM"
#define J1939_MAX_PACKETS        "J1939_MAX_PACKETS"
#define TP_TIMEOUT_T1            "TP_TIMEOUT_T1"
#define TP_TIMEOUT_T2            "TP_TIMEOUT_T2"
#define TP_TIMEOUT_T3            "TP_TIMEOUT_T3"
#define TP_TIMEOUT_TH            "TP_TIMEOUT_TH"
#define J1939_MAX_MSG_LEN        "J1939_MAX_MSG_LEN"
#define CANOPEN_INDEX            "CANOPEN_INDEX"
#define CANOPEN_SUBINDEX         "CANOPEN_SUBINDEX"
#define CANOPEN_USE_BLOCK        "CANOPEN_USE_BLOCK"
#define CANOPEN_BLOCK_SIZE       "CANOPEN_BLOCK_SIZE"
#define TP_TIMEOUT_SDO           "TP_TIMEOUT_SDO"
#define CANOPEN_MAX_MSG_LEN      "CANOPEN_MAX_MSG_LEN"
#define TP_TIMEOUT_FP_INTERFRAME "TP_TIMEOUT_FP_INTERFRAME"
#define FP_MAX_MSG_LEN           "FP_MAX_MSG_LEN"

/////////////////////////////////////////////////////////////////////////////////
//                  CONFIGURATION INTERFACES                                   //
/////////////////////////////////////////////////////////////////////////////////

bool KCANPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    m_strInstanceName = psSetParams->strInstanceName.empty() ? KCAN_PLUGIN_NAME : psSetParams->strInstanceName;

    if (psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing found in the ini file"));
        return true;
    }

    PluginSettingsBinder sSettings;

    // clang-format off
    sSettings.Bind(KCAN_TX_ID, [this](const std::string &v) { return setCanTxId(v); });

    sSettings.Bind(KCAN_RX_ID, [this](const std::string &v) {
        if (v.empty()) {
            return true;
        }
        return setCanRxId(v); });

    sSettings.Bind(CAN_TP_PROTOCOL, [this](const std::string &v) {
        if (v.empty()) {
            return true;
        }
        return setCanTpProtocol(v); });

    sSettings.Bind(KCAN_FILTERS, [this](const std::string &v) {
        if (v.empty()) {
            return true;
        }
        if (false == m_ParseFilters(v, m_vFilters)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to parse KCAN_FILTERS:"); LOG_STRING(v));
            return false;
        }
        return true; });

    // Link (bus speed) keys: a blank value means "not configured" - the interface is left as the system set it up.
    sSettings.Bind(KCAN_BITRATE,                    [this](const std::string &v) { return v.empty() ? true : setCanBitrate(v); });
    sSettings.Bind(KCAN_SAMPLE_POINT,               [this](const std::string &v) { return v.empty() ? true : setCanSamplePoint(v); });
    sSettings.Bind(KCAN_DATA_BITRATE,               [this](const std::string &v) { return v.empty() ? true : setCanDataBitrate(v); });
    sSettings.Bind(KCAN_DATA_SAMPLE_POINT,          [this](const std::string &v) { return v.empty() ? true : setCanDataSamplePoint(v); });
    sSettings.Bind(KCAN_FD,                         [this](const std::string &v) { return v.empty() ? true : setCanFd(v); });
    sSettings.Bind(KCAN_LISTEN_ONLY,                [this](const std::string &v) { return v.empty() ? true : setCanListenOnly(v); });
    sSettings.Bind(KCAN_LOOPBACK,                   [this](const std::string &v) { return v.empty() ? true : setCanLoopback(v); });
    sSettings.Bind(KCAN_ONE_SHOT,                   [this](const std::string &v) { return v.empty() ? true : setCanOneShot(v); });
    sSettings.Bind(KCAN_TRIPLE_SAMPLING,            [this](const std::string &v) { return v.empty() ? true : setCanTripleSampling(v); });
    sSettings.Bind(KCAN_BERR_REPORTING,             [this](const std::string &v) { return v.empty() ? true : setCanBerrReporting(v); });
    sSettings.Bind(KCAN_RESTART_MS,                 [this](const std::string &v) { return v.empty() ? true : setCanRestartMs(v); });
    sSettings.Bind(KCAN_AUTO_LINK,                  [this](const std::string &v) { return v.empty() ? true : setCanAutoLink(v); });
    sSettings.Bind(KCAN_ERR_MASK,                   [this](const std::string &v) { return v.empty() ? true : setCanErrMask(v); });
    sSettings.Bind(READ_BUF_SIZE,                   [this](const std::string &v) { return setCanReadBufferSize(v); });
    sSettings.Bind(ARTEFACTS_PATH,                  m_strArtefactsPath);
    sSettings.Bind(KCAN_IFACE,                      [this](const std::string &v) { setCanIface(v); return true; });
    sSettings.Bind(TP_BLOCK_SIZE,                   m_sTpConfig.blockSize);
    sSettings.Bind(TP_ST_MIN,                       m_sTpConfig.stMin);
    sSettings.Bind(TP_PAD_FRAMES,                   m_sTpConfig.padFrames);
    sSettings.Bind(TP_PADDING_BYTE,                 m_sTpConfig.paddingByte);
    sSettings.Bind(TP_TIMEOUT_NBS,                  m_sTpConfig.timeoutNBs_ms);
    sSettings.Bind(TP_TIMEOUT_NCR,                  m_sTpConfig.timeoutNCr_ms);
    sSettings.Bind(TP_MAX_MSG_LEN,                  m_sTpConfig.maxMessageLen);
    sSettings.Bind(J1939_USE_BAM,                   m_sTpConfig.j1939UseBam);
    sSettings.Bind(J1939_MAX_PACKETS,               m_sTpConfig.j1939MaxPackets);
    sSettings.Bind(TP_TIMEOUT_T1,                   m_sTpConfig.timeoutT1_ms);
    sSettings.Bind(TP_TIMEOUT_T2,                   m_sTpConfig.timeoutT2_ms);
    sSettings.Bind(TP_TIMEOUT_T3,                   m_sTpConfig.timeoutT3_ms);
    sSettings.Bind(TP_TIMEOUT_TH,                   m_sTpConfig.timeoutTh_ms);
    sSettings.Bind(J1939_MAX_MSG_LEN,               m_sTpConfig.j1939MaxMessageLen);
    sSettings.Bind(CANOPEN_INDEX,                   m_sTpConfig.canOpenIndex);
    sSettings.Bind(CANOPEN_SUBINDEX,                m_sTpConfig.canOpenSubIndex);
    sSettings.Bind(CANOPEN_USE_BLOCK,               m_sTpConfig.canOpenUseBlock);
    sSettings.Bind(CANOPEN_BLOCK_SIZE,              m_sTpConfig.canOpenBlockSize);
    sSettings.Bind(TP_TIMEOUT_SDO,                  m_sTpConfig.timeoutSdo_ms);
    sSettings.Bind(CANOPEN_MAX_MSG_LEN,             m_sTpConfig.canOpenMaxMessageLen);
    sSettings.Bind(TP_TIMEOUT_FP_INTERFRAME,        m_sTpConfig.timeoutFpInterFrame_ms);
    sSettings.Bind(FP_MAX_MSG_LEN,                  m_sTpConfig.fastPacketMaxMessageLen);
    sSettings.Bind(READ_TIMEOUT,                    m_u32ReadTimeout);
    sSettings.Bind(WRITE_TIMEOUT,                   m_u32WriteTimeout);
    sSettings.Bind(ucmdexec::RAW_RESULT_INI_KEY,    m_bRawResult);
    sSettings.Bind(ucmdexec::CYCLIC_CACHED_INI_KEY, m_bCyclicCached);
    // clang-format on

    return sSettings.Apply(psSetParams->mapSettings,
                           [](const std::string &strKey, const std::string &strRawValue) {
                               LOG_PRINT(LOG_WERBOSE, LOG_HDR; LOG_STRING(strKey); LOG_STRING(":"); LOG_STRING(strRawValue));
                           });

} /* m_LocalSetParams() */

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of CAN parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (i=iface  x=tx_id  y=rx_id  r=read_tout  w=write_tout
 *                     s=recv_bufsize  t=tp_protocol,
 *                     link/bus speed: b=bitrate  sp=sample_point  db=data_bitrate  dsp=data_sample_point
 *                     fd=on|off  lo=on|off (listen-only)  lb=on|off (loopback)  os=on|off (one-shot)
 *                     ts=on|off (triple sampling)  berr=on|off  rs=restart_ms  auto=on|off  e=err_mask,
 *                     plus TpConfig tuning keys -
 *                     see TpConfig.hpp for units/defaults: bs, stmin, pad, padb,
 *                     nbs, ncr, maxlen, bam, maxpkt, t1, t2, t3, th, jmaxlen,
 *                     coidx, cosub, coblk, coblksz, sdotout, comaxlen, fpinter, fpmaxlen)
 * \return true if processing succeeded, false otherwise
 */
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_can_set_params(const T *pOwner, const std::string &strArgs)
{
    // clang-format off
    static constexpr KVSetterEntry<T> table[] = {
        {.key = "i",        .voidSetter = &T::setCanIface},
        {.key = "x",        .boolSetter = &T::setCanTxId},
        {.key = "y",        .boolSetter = &T::setCanRxId},
        {.key = "b",        .boolSetter = &T::setCanBitrate},
        {.key = "sp",       .boolSetter = &T::setCanSamplePoint},
        {.key = "db",       .boolSetter = &T::setCanDataBitrate},
        {.key = "dsp",      .boolSetter = &T::setCanDataSamplePoint},
        {.key = "fd",       .boolSetter = &T::setCanFd},
        {.key = "lo",       .boolSetter = &T::setCanListenOnly},
        {.key = "lb",       .boolSetter = &T::setCanLoopback},
        {.key = "os",       .boolSetter = &T::setCanOneShot},
        {.key = "ts",       .boolSetter = &T::setCanTripleSampling},
        {.key = "berr",     .boolSetter = &T::setCanBerrReporting},
        {.key = "rs",       .boolSetter = &T::setCanRestartMs},
        {.key = "auto",     .boolSetter = &T::setCanAutoLink},
        {.key = "e",        .boolSetter = &T::setCanErrMask},
        {.key = "r",        .boolSetter = &T::setCanReadTimeout},
        {.key = "w",        .boolSetter = &T::setCanWriteTimeout},
        {.key = "s",        .boolSetter = &T::setCanReadBufferSize},
        {.key = "t",        .boolSetter = &T::setCanTpProtocol},
        {.key = "bs",       .boolSetter = &T::setTpBlockSize},
        {.key = "stmin",    .boolSetter = &T::setTpStMin},
        {.key = "pad",      .boolSetter = &T::setTpPadFrames},
        {.key = "padb",     .boolSetter = &T::setTpPaddingByte},
        {.key = "nbs",      .boolSetter = &T::setTpTimeoutNBs},
        {.key = "ncr",      .boolSetter = &T::setTpTimeoutNCr},
        {.key = "maxlen",   .boolSetter = &T::setTpMaxMessageLen},
        {.key = "bam",      .boolSetter = &T::setJ1939UseBam},
        {.key = "maxpkt",   .boolSetter = &T::setJ1939MaxPackets},
        {.key = "t1",       .boolSetter = &T::setTpTimeoutT1},
        {.key = "t2",       .boolSetter = &T::setTpTimeoutT2},
        {.key = "t3",       .boolSetter = &T::setTpTimeoutT3},
        {.key = "th",       .boolSetter = &T::setTpTimeoutTh},
        {.key = "jmaxlen",  .boolSetter = &T::setJ1939MaxMessageLen},
        {.key = "coidx",    .boolSetter = &T::setCanOpenIndex},
        {.key = "cosub",    .boolSetter = &T::setCanOpenSubIndex},
        {.key = "coblk",    .boolSetter = &T::setCanOpenUseBlock},
        {.key = "coblksz",  .boolSetter = &T::setCanOpenBlockSize},
        {.key = "sdotout",  .boolSetter = &T::setTpTimeoutSdo},
        {.key = "comaxlen", .boolSetter = &T::setCanOpenMaxMessageLen},
        {.key = "fpinter",  .boolSetter = &T::setTpTimeoutFpInterFrame},
        {.key = "fpmaxlen", .boolSetter = &T::setFpMaxMessageLen},
        {.key = "raw",      .boolSetter = &T::setRawResult},
        {.key = "cached",   .boolSetter = &T::setCyclicCached},
    };
    // clang-format on

    return generic_setup_params(pOwner, strArgs, table, LT_HDR);
}

#endif // KCAN_SETUP_HPP
