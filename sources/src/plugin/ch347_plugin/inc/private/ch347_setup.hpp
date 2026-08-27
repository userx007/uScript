#ifndef CH347_SETUP_HPP
#define CH347_SETUP_HPP

#include "PluginSetup.hpp"

#include <string>

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of CH347 parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (d=device_path  c=spi_clock_hz  i=i2c_speed  a=i2c_address
 *                     j=jtag_clock_rate  r=read_tout  sd=script_delay)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_ch347_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "d",  .voidSetter = &T::setDevicePath    },
        { .key = "c",  .boolSetter = &T::setSpiClockHz     },
        { .key = "i",  .boolSetter = &T::setI2cSpeed       },
        { .key = "a",  .boolSetter = &T::setI2cAddress     },
        { .key = "j",  .boolSetter = &T::setJtagClockRate  },
        { .key = "r",  .boolSetter = &T::setReadTimeout    },
        { .key = "sd", .boolSetter = &T::setScriptDelay    },
    };

    return generic_setup_params(pOwner, args, table, "CH347 SETUP |");
}

#endif // CH347_SETUP_HPP
