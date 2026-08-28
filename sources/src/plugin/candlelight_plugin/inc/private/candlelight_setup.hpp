#ifndef CANDLELIGHT_SETUP_HPP
#define CANDLELIGHT_SETUP_HPP

#include "PluginSetup.hpp"
#include "candlelight_plugin.hpp"
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

#define LT_HDR   "CANDLELIGHT |"
#define LOG_HDR  LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                               //
/////////////////////////////////////////////////////////////////////////////////

#define    ARTEFACTS_PATH         "ARTEFACTS_PATH"
#define    CANDLE_USB_VID         "CANDLE_USB_VID"
#define    CANDLE_USB_PID         "CANDLE_USB_PID"
#define    CANDLE_USB_INDEX       "CANDLE_USB_INDEX"
#define    CANDLE_BITRATE         "CANDLE_BITRATE"
#define    CANDLE_SAMPLE_POINT    "CANDLE_SAMPLE_POINT"
#define    CANDLE_FD_BITRATE      "CANDLE_FD_BITRATE"
#define    CANDLE_FD_SAMPLE_POINT "CANDLE_FD_SAMPLE_POINT"
#define    CANDLE_FD_BRS          "CANDLE_FD_BRS"
#define    CANDLE_MODE_FLAGS      "CANDLE_MODE_FLAGS"
#define    CAN_TX_ID              "CAN_TX_ID"
#define    CAN_RX_ID              "CAN_RX_ID"
#define    CAN_FILTERS            "CAN_FILTERS"
#define    READ_TIMEOUT           "READ_TIMEOUT"
#define    WRITE_TIMEOUT          "WRITE_TIMEOUT"
#define    READ_BUF_SIZE          "READ_BUF_SIZE"
#define    CAN_TP_PROTOCOL        "CAN_TP_PROTOCOL"

// ---- TpConfig tuning parameters (same INI key names as KVCAN/PCAN/SLCAN/UCAN) ----
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

bool CandlelightPlugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    // Runtime instance identity for the GUI comm-dump panel (e.g. "CANDLELIGHT:1"); falls back
    // to plain "CANDLELIGHT" if the interpreter didn't supply one. Done before the "nothing
    // loaded from ini" early-return below so it's always captured.
    m_strInstanceName = psSetParams->strInstanceName.empty() ? "CANDLELIGHT" : psSetParams->strInstanceName;

    if (true == psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        return true;
    }

    PluginSettingsBinder sSettings;
    sSettings.Bind(ARTEFACTS_PATH,         m_strArtefactsPath);
    sSettings.Bind(CANDLE_USB_VID,         [this](const std::string& v) { return setUsbVid(v); });
    sSettings.Bind(CANDLE_USB_PID,         [this](const std::string& v) { return setUsbPid(v); });
    sSettings.Bind(CANDLE_USB_INDEX,       [this](const std::string& v) { return setUsbDeviceIndex(v); });
    sSettings.Bind(CANDLE_BITRATE,         [this](const std::string& v) { return setCanBitrate(v); });
    sSettings.Bind(CANDLE_SAMPLE_POINT,    [this](const std::string& v) { return setCanSamplePoint(v); });
    sSettings.Bind(CANDLE_FD_BITRATE,      [this](const std::string& v) { return setCanFdBitrate(v); });
    sSettings.Bind(CANDLE_FD_SAMPLE_POINT, [this](const std::string& v) { return setCanFdSamplePoint(v); });
    sSettings.Bind(CANDLE_FD_BRS,          [this](const std::string& v) { return setCanFdBrs(v); });
    sSettings.Bind(CANDLE_MODE_FLAGS,      [this](const std::string& v) { return setCanModeFlags(v); });
    // Route through setCanTxId() so the EFF-flag fixup and data-bit clamping
    // are applied whether the ID comes from the INI file or the CONFIG command.
    sSettings.Bind(CAN_TX_ID,              [this](const std::string& v) { return setCanTxId(v); });
    // Optional: only meaningful once CAN_TP_PROTOCOL selects a segmented
    // transport; empty means "mirror CAN_TX_ID" (today's behaviour).
    sSettings.Bind(CAN_RX_ID,              [this](const std::string& v) {
        if (v.empty()) {
            return true;
        }
        return setCanRxId(v);
    });
    // Empty/omitted means TpProtocol::NONE (today's single-frame-only behaviour).
    sSettings.Bind(CAN_TP_PROTOCOL,        [this](const std::string& v) {
        if (v.empty()) {
            return true;
        }
        return setCanTpProtocol(v);
    });
    // TpConfig tuning parameters -- all optional, each keeps TpConfig's own
    // in-struct default until explicitly overridden; same keys as KVCAN/PCAN/SLCAN/UCAN.
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
    sSettings.Bind(CAN_FILTERS,       [this](const std::string& v) {
        if (v.empty()) {
            return true;
        }
        if (false == m_ParseFilters(v)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to parse CAN_FILTERS:"); LOG_STRING(v));
            return false;
        }
        return true;
    });
    sSettings.Bind(READ_TIMEOUT,      m_u32ReadTimeout);
    sSettings.Bind(WRITE_TIMEOUT,     m_u32WriteTimeout);
    // Route through the setter so the [1-64] range check is applied consistently
    // regardless of whether the value came from INI or CONFIG.
    sSettings.Bind(READ_BUF_SIZE,     [this](const std::string& v) { return setCanReadBufferSize(v); });
    sSettings.Bind(ucmdexec::RAW_RESULT_INI_KEY, m_bRawResult);
    sSettings.Bind(ucmdexec::CYCLIC_CACHED_INI_KEY, m_bCyclicCached);

    return sSettings.Apply(psSetParams->mapSettings,
        [](const std::string& strKey, const std::string& strRawValue) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strKey); LOG_STRING(":"); LOG_STRING(strRawValue));
        });

} /* m_LocalSetParams() */

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of Candlelight parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (vid=usb_vid  pid=usb_pid  idx=usb_device_index  b=bitrate  sp=sample_point
 *                     fb=fd_bitrate  fp=fd_sample_point  z=fd_brs  m=mode_flags  x=tx_id  v=rx_id
 *                     r=read_tout  w=write_tout  s=recv_bufsize  t=tp_protocol, plus raw bit-timing
 *                     override: ps, p1, p2, sw, bp and CAN-FD data-phase equivalents dps, dp1, dp2,
 *                     dsw, dbp - see uCandlelight.hpp's GsDeviceBittiming; and TpConfig tuning keys
 *                     - same set as kvcan_setup.hpp/pcan_setup.hpp: bs, stmin, pad, padb, nbs, ncr,
 *                     maxlen, bam, maxpkt, t1, t2, t3, th, jmaxlen, coidx, cosub, coblk, coblksz,
 *                     sdotout, comaxlen, fpinter, fpmaxlen)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_can_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "vid",      .boolSetter = &T::setUsbVid                  },
        { .key = "pid",      .boolSetter = &T::setUsbPid                  },
        { .key = "idx",      .boolSetter = &T::setUsbDeviceIndex          },
        { .key = "b",        .boolSetter = &T::setCanBitrate              },
        { .key = "sp",       .boolSetter = &T::setCanSamplePoint          },
        { .key = "fb",       .boolSetter = &T::setCanFdBitrate            },
        { .key = "fp",       .boolSetter = &T::setCanFdSamplePoint        },
        { .key = "z",        .boolSetter = &T::setCanFdBrs                },
        { .key = "m",        .boolSetter = &T::setCanModeFlags            },
        { .key = "x",        .boolSetter = &T::setCanTxId                 },
        { .key = "v",        .boolSetter = &T::setCanRxId                 },
        { .key = "r",        .boolSetter = &T::setCanReadTimeout          },
        { .key = "w",        .boolSetter = &T::setCanWriteTimeout         },
        { .key = "s",        .boolSetter = &T::setCanReadBufferSize       },
        { .key = "t",        .boolSetter = &T::setCanTpProtocol           },
        // Raw bit-timing override
        { .key = "ps",       .boolSetter = &T::setCanPropSeg              },
        { .key = "p1",       .boolSetter = &T::setCanPhaseSeg1            },
        { .key = "p2",       .boolSetter = &T::setCanPhaseSeg2            },
        { .key = "sw",       .boolSetter = &T::setCanSjw                  },
        { .key = "bp",       .boolSetter = &T::setCanBrp                  },
        { .key = "dps",      .boolSetter = &T::setCanFdPropSeg            },
        { .key = "dp1",      .boolSetter = &T::setCanFdPhaseSeg1          },
        { .key = "dp2",      .boolSetter = &T::setCanFdPhaseSeg2          },
        { .key = "dsw",      .boolSetter = &T::setCanFdSjw                },
        { .key = "dbp",      .boolSetter = &T::setCanFdBrp                },
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

    return generic_setup_params(pOwner, args, table, "CANDLELIGHT SETUP |");
}

#endif // CANDLELIGHT_SETUP_HPP
