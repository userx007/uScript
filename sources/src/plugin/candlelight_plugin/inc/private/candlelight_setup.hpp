#ifndef CANDLELIGHT_SETUP_HPP
#define CANDLELIGHT_SETUP_HPP

#include "PluginSetup.hpp"

#include <string>

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
