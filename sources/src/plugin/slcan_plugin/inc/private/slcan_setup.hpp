#ifndef SLCAN_SETUP_HPP
#define SLCAN_SETUP_HPP

#include "PluginSetup.hpp"

#include <string>

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of SLCAN parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (i=device  p=uart_baud  b=bitrate  y=fd_rate  m=mode
 *                     a=auto_retx  z=fd_brs  x=tx_id  v=rx_id  r=read_tout
 *                     w=write_tout  s=recv_bufsize  t=tp_protocol, plus TpConfig tuning
 *                     keys - same set as kvcan_setup.hpp/pcan_setup.hpp: bs, stmin, pad,
 *                     padb, nbs, ncr, maxlen, bam, maxpkt, t1, t2, t3, th, jmaxlen, coidx,
 *                     cosub, coblk, coblksz, sdotout, comaxlen, fpinter, fpmaxlen)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_can_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "i",        .voidSetter = &T::setDevice                  },
        { .key = "p",        .boolSetter = &T::setUartBaud                },
        { .key = "b",        .boolSetter = &T::setCanBitrate              },
        { .key = "y",        .boolSetter = &T::setCanFdDataRate           },
        { .key = "m",        .boolSetter = &T::setCanMode                 },
        { .key = "a",        .boolSetter = &T::setCanAutoRetx             },
        { .key = "z",        .boolSetter = &T::setCanFdBrs                },
        { .key = "x",        .boolSetter = &T::setCanTxId                 },
        { .key = "v",        .boolSetter = &T::setCanRxId                 },
        { .key = "r",        .boolSetter = &T::setCanReadTimeout          },
        { .key = "w",        .boolSetter = &T::setCanWriteTimeout         },
        { .key = "s",        .boolSetter = &T::setCanReadBufferSize       },
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

    return generic_setup_params(pOwner, args, table, "SLCAN SETUP |");
}

#endif // SLCAN_SETUP_HPP
