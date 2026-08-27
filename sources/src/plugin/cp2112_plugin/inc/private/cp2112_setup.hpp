#ifndef CP2112_SETUP_HPP
#define CP2112_SETUP_HPP

#include "PluginSetup.hpp"

#include <string>

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of CP2112 parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (x=device_index  c=i2c_clock_hz  a=i2c_address  r=read_tout  sd=script_delay)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_cp2112_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "x",  .boolSetter = &T::setDeviceIndex  },
        { .key = "c",  .boolSetter = &T::setI2cClockHz   },
        { .key = "a",  .boolSetter = &T::setI2cAddress   },
        { .key = "r",  .boolSetter = &T::setReadTimeout  },
        { .key = "sd", .boolSetter = &T::setScriptDelay  },
    };

    return generic_setup_params(pOwner, args, table, "CP2112 SETUP |");
}

#endif // CP2112_SETUP_HPP
