#ifndef DSPKI2C_SETUP_HPP
#define DSPKI2C_SETUP_HPP

#include "PluginSetup.hpp"

#include <string>

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of Digispark I2C parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (v=usb_vid  p=usb_pid  a=slave_addr  r=read_tout  w=write_tout  s=recv_bufsize)
 * \return true if processing succeeded, false otherwise
 *
 * \note Short-circuits to true (without applying anything) while the plugin isn't yet enabled -
 *       this is the argument-validation-only dry run, before any real Digispark device is
 *       expected to be attached.
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_i2c_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "v",      .boolSetter = &T::setVid             },
        { .key = "p",      .boolSetter = &T::setPid             },
        { .key = "a",      .boolSetter = &T::setSlaveAddr       },
        { .key = "r",      .boolSetter = &T::setReadTimeout     },
        { .key = "w",      .boolSetter = &T::setWriteTimeout    },
        { .key = "s",      .boolSetter = &T::setReadBufferSize  },
        { .key = "raw",    .boolSetter = &T::setRawResult       },
        { .key = "cached", .boolSetter = &T::setCyclicCached    },
    };

    if (args.empty()) {
        LOG_PRINT(LOG_INFO, LOG_STRING("DSPKI2C SETUP |"); LOG_STRING("Missing args"));
        return false;
    }

    // Short-circuit to true (without applying anything) while the plugin isn't yet enabled -
    // this is the argument-validation-only dry run, before any real Digispark device is
    // expected to be attached.
    if (false == pOwner->isEnabled()) {
        return true;
    }

    return parseAndCallSetupHandlers(pOwner, args, table, "DSPKI2C SETUP |");
}

#endif // DSPKI2C_SETUP_HPP
