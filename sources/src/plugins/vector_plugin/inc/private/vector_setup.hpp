#ifndef VECTOR_SETUP_HPP
#define VECTOR_SETUP_HPP
#include "PluginSetup.hpp"
#include "vector_plugin.hpp"
#include "uPluginSettings.hpp"
#include "uCommandExec.hpp"

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

#define LT_HDR   "VECTOR_CAN_P|"
#define LOG_HDR  LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                               //
/////////////////////////////////////////////////////////////////////////////////

#define    ARTEFACTS_PATH          "ARTEFACTS_PATH"
#define    VECTOR_APP_NAME         "VECTOR_APP_NAME"
#define    VECTOR_APP_CHANNEL      "VECTOR_APP_CHANNEL"
#define    VECTOR_DEVICE_HW        "VECTOR_DEVICE_HW"
#define    VECTOR_DEVICE_SERIAL    "VECTOR_DEVICE_SERIAL"
#define    VECTOR_DEVICE_NAME      "VECTOR_DEVICE_NAME"
#define    VECTOR_DEVICE_HWINDEX   "VECTOR_DEVICE_HWINDEX"
#define    VECTOR_DEVICE_HWCHANNEL "VECTOR_DEVICE_HWCHANNEL"
#define    VECTOR_BITRATE          "VECTOR_BITRATE"
#define    VECTOR_EXTENDED         "VECTOR_EXTENDED"
#define    VECTOR_FD               "VECTOR_FD"
#define    VECTOR_FD_DATA_BITRATE  "VECTOR_FD_DATA_BITRATE"
#define    VECTOR_FD_ISO           "VECTOR_FD_ISO"
#define    VECTOR_FD_BRS           "VECTOR_FD_BRS"
#define    VECTOR_FD_PADDING_BYTE  "VECTOR_FD_PADDING_BYTE"
#define    VECTOR_TX_ID            "CAN_TX_ID"
#define    VECTOR_RX_ID            "CAN_RX_ID"
#define    VECTOR_FILTERS          "CAN_FILTERS"
#define    READ_TIMEOUT            "READ_TIMEOUT"
#define    WRITE_TIMEOUT           "WRITE_TIMEOUT"
#define    READ_BUF_SIZE           "READ_BUF_SIZE"
#define    VECTOR_TP_PROTOCOL      "CAN_TP_PROTOCOL"

// ---- TpConfig tuning parameters (same INI key names as PCAN/KVCAN, for compatibility) ----
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


/////////////////////////////////////////////////////////////////////////////////
//                  CONFIGURATION INTERFACES                                   //
/////////////////////////////////////////////////////////////////////////////////

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief processing of the plugin specific settings.
  *
  * Pulls the plugin-specific keys out of the ini-backed PluginDataSet and feeds them through the
  * same setter surface the CONFIG command uses so an ini file and a runtime CONFIG command are
  * always interpreted identically.
*/
/*--------------------------------------------------------------------------------------------------------*/
bool VectorPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    m_strInstanceName = psSetParams->strInstanceName.empty() ? VECTOR_PLUGIN_NAME : psSetParams->strInstanceName;

    if (true == psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        return true;
    }

    PluginSettingsBinder sSettings;
    sSettings.Bind(ARTEFACTS_PATH,     m_strArtefactsPath);
    sSettings.Bind(VECTOR_APP_NAME,    m_strAppName);
    sSettings.Bind(VECTOR_APP_CHANNEL, [this](const std::string& v) { return setAppChannel(v); });
    sSettings.Bind(VECTOR_DEVICE_HW, [this](const std::string& v) {
        if (v.empty()) { return true; }
        return setDeviceHw(v);
    });
    sSettings.Bind(VECTOR_DEVICE_SERIAL, [this](const std::string& v) {
        if (v.empty()) { return true; }
        return setDeviceSerial(v);
    });
    sSettings.Bind(VECTOR_DEVICE_NAME, [this](const std::string& v) {
        if (v.empty()) { return true; }
        return setDeviceName(v);
    });
    sSettings.Bind(VECTOR_DEVICE_HWINDEX, [this](const std::string& v) {
        if (v.empty()) { return true; }
        return setDeviceHwIndex(v);
    });
    sSettings.Bind(VECTOR_DEVICE_HWCHANNEL, [this](const std::string& v) {
        if (v.empty()) { return true; }
        return setDeviceHwChannel(v);
    });
    sSettings.Bind(VECTOR_BITRATE,     [this](const std::string& v) { return setVectorBitrate(v); });
    sSettings.Bind(VECTOR_EXTENDED,    [this](const std::string& v) { return setVectorExtended(v); });
    sSettings.Bind(VECTOR_FD,          [this](const std::string& v) { return setVectorFd(v); });
    sSettings.Bind(VECTOR_FD_DATA_BITRATE, [this](const std::string& v) {
        if (v.empty()) { return true; }
        return setVectorFdDataBitrate(v);
    });
    sSettings.Bind(VECTOR_FD_ISO, [this](const std::string& v) {
        if (v.empty()) { return true; }
        return setVectorFdIso(v);
    });
    sSettings.Bind(VECTOR_FD_BRS, [this](const std::string& v) {
        if (v.empty()) { return true; }
        return setVectorFdBrs(v);
    });
    sSettings.Bind(VECTOR_FD_PADDING_BYTE, [this](const std::string& v) {
        if (v.empty()) { return true; }
        return setVectorFdPaddingByte(v);
    });
    sSettings.Bind(VECTOR_TX_ID,       [this](const std::string& v) { return setCanTxId(v); });
    sSettings.Bind(VECTOR_RX_ID,       [this](const std::string& v) {
        if (v.empty()) {
            return true;
        }
        return setCanRxId(v);
    });
    sSettings.Bind(VECTOR_TP_PROTOCOL, [this](const std::string& v) {
        if (v.empty()) {
            return true;
        }
        return setCanTpProtocol(v);
    });
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
    sSettings.Bind(VECTOR_FILTERS, [this](const std::string& v) {
        if (v.empty()) {
            return true;
        }
        if (false == m_ParseFilters(v, m_vFilters)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to parse CAN_FILTERS:"); LOG_STRING(v));
            return false;
        }
        return true;
    });
    sSettings.Bind(READ_TIMEOUT,   m_u32ReadTimeout);
    sSettings.Bind(WRITE_TIMEOUT,  m_u32WriteTimeout);
    sSettings.Bind(READ_BUF_SIZE,  [this](const std::string& v) { return setCanReadBufferSize(v); });
    sSettings.Bind(ucmdexec::RAW_RESULT_INI_KEY, m_bRawResult);
    sSettings.Bind(ucmdexec::CYCLIC_CACHED_INI_KEY, m_bCyclicCached);

    return sSettings.Apply(psSetParams->mapSettings,
        [](const std::string& strKey, const std::string& strRawValue) {
            LOG_PRINT(LOG_WERBOSE, LOG_HDR; LOG_STRING(strKey); LOG_STRING(":"); LOG_STRING(strRawValue));
        });

} /* m_LocalSetParams() */


/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of Vector parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (a=app_name  i=app_channel_index  b=bitrate  x=tx_id  y=rx_id
 *                     r=read_tout  w=write_tout  s=recv_bufsize  e=extended  f=fd
 *                     d=fd_data_bitrate  iso=fd_iso  brs=fd_brs  padb=fd_padding_byte
 *                     t=tp_protocol, plus TpConfig tuning keys - same set as
 *                     pcan_setup.hpp/kvcan_setup.hpp: bs, stmin, pad, padb, nbs, ncr,
 *                     maxlen, bam, maxpkt, t1, t2, t3, th, jmaxlen, coidx, cosub, coblk,
 *                     coblksz, sdotout, comaxlen, fpinter, fpmaxlen)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_can_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "a",        .voidSetter = &T::setAppName                 },
        { .key = "i",        .boolSetter = &T::setAppChannel              },
        { .key = "hw",       .boolSetter = &T::setDeviceHw                },
        { .key = "serial",   .boolSetter = &T::setDeviceSerial            },
        { .key = "name",     .boolSetter = &T::setDeviceName              },
        { .key = "hwidx",    .boolSetter = &T::setDeviceHwIndex           },
        { .key = "hwch",     .boolSetter = &T::setDeviceHwChannel         },
        { .key = "b",        .boolSetter = &T::setVectorBitrate           },
        { .key = "x",        .boolSetter = &T::setCanTxId                 },
        { .key = "y",        .boolSetter = &T::setCanRxId                 },
        { .key = "r",        .boolSetter = &T::setCanReadTimeout          },
        { .key = "w",        .boolSetter = &T::setCanWriteTimeout         },
        { .key = "s",        .boolSetter = &T::setCanReadBufferSize       },
        { .key = "e",        .boolSetter = &T::setVectorExtended          },
        { .key = "f",        .boolSetter = &T::setVectorFd                },
        { .key = "d",        .boolSetter = &T::setVectorFdDataBitrate     },
        { .key = "iso",      .boolSetter = &T::setVectorFdIso             },
        { .key = "brs",      .boolSetter = &T::setVectorFdBrs             },
        { .key = "padb",     .boolSetter = &T::setVectorFdPaddingByte     },
        { .key = "t",        .boolSetter = &T::setCanTpProtocol           },
        // TpConfig tuning parameters
        { .key = "bs",       .boolSetter = &T::setTpBlockSize             },
        { .key = "stmin",    .boolSetter = &T::setTpStMin                 },
        { .key = "pad",      .boolSetter = &T::setTpPadFrames             },
        { .key = "padb",     .boolSetter = &T::setTpPaddingByte           },
        { .key = "nbs",      .boolSetter = &T::setTpTimeoutNBs            },
        { .key = "ncr",      .boolSetter = &T::setTpTimeoutNCr            },
        { .key = "maxlen",   .boolSetter = &T::setTpMaxMessageLen         },
        { .key = "bam",      .boolSetter = &T::setJ1939UseBam             },
        { .key = "maxpkt",   .boolSetter = &T::setJ1939MaxPackets         },
        { .key = "t1",       .boolSetter = &T::setTpTimeoutT1             },
        { .key = "t2",       .boolSetter = &T::setTpTimeoutT2             },
        { .key = "t3",       .boolSetter = &T::setTpTimeoutT3             },
        { .key = "th",       .boolSetter = &T::setTpTimeoutTh             },
        { .key = "jmaxlen",  .boolSetter = &T::setJ1939MaxMessageLen      },
        { .key = "coidx",    .boolSetter = &T::setCanOpenIndex            },
        { .key = "cosub",    .boolSetter = &T::setCanOpenSubIndex         },
        { .key = "coblk",    .boolSetter = &T::setCanOpenUseBlock         },
        { .key = "coblksz",  .boolSetter = &T::setCanOpenBlockSize        },
        { .key = "sdotout",  .boolSetter = &T::setTpTimeoutSdo            },
        { .key = "comaxlen", .boolSetter = &T::setCanOpenMaxMessageLen    },
        { .key = "fpinter",  .boolSetter = &T::setTpTimeoutFpInterFrame   },
        { .key = "fpmaxlen", .boolSetter = &T::setFpMaxMessageLen         },
        { .key = "raw",      .boolSetter = &T::setRawResult               },
        { .key = "cached",   .boolSetter = &T::setCyclicCached            },
    };

    return generic_setup_params(pOwner, args, table, LT_HDR);
}

#endif // VECTOR_SETUP_HPP
