#ifndef DSPKSPI_SETUP_HPP
#define DSPKSPI_SETUP_HPP

#include "PluginSetup.hpp"

#include <string>

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of Digispark SPI parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (vid=usb_vid  pid=usb_pid  m=mode  d=clock_div  r=read_tout  w=write_tout  s=recv_bufsize)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_spi_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "vid",    .boolSetter = &T::setSpiVid             },
        { .key = "pid",    .boolSetter = &T::setSpiPid             },
        { .key = "m",      .boolSetter = &T::setSpiMode            },
        { .key = "d",      .boolSetter = &T::setSpiClockDiv        },
        { .key = "r",      .boolSetter = &T::setSpiReadTimeout     },
        { .key = "w",      .boolSetter = &T::setSpiWriteTimeout    },
        { .key = "s",      .boolSetter = &T::setSpiReadBufferSize  },
        { .key = "raw",    .boolSetter = &T::setRawResult          },
        { .key = "cached", .boolSetter = &T::setCyclicCached       },
    };

    if (args.empty()) {
        LOG_PRINT(LOG_INFO, LOG_STRING("DSPKSPI SETUP |"); LOG_STRING("Missing args"));
        return false;
    }

    // Short-circuit to true (without applying anything) while the plugin isn't yet enabled -
    // this is the argument-validation-only dry run, before any real Digispark device is
    // expected to be attached.
    if (false == pOwner->isEnabled()) {
        return true;
    }

    return parseAndCallSetupHandlers(pOwner, args, table, "DSPKSPI SETUP |");
}

#endif // DSPKSPI_SETUP_HPP
