#ifndef FT245_SETUP_HPP
#define FT245_SETUP_HPP

#include "PluginSetup.hpp"

#include <string>

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of FT245 parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (x=device_index  v=default_variant  fm=default_fifo_mode
 *                     r=read_tout  sd=script_delay)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_ft245_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "x",  .boolSetter = &T::setDeviceIndex     },
        { .key = "v",  .boolSetter = &T::setDefaultVariant  },
        { .key = "fm", .boolSetter = &T::setDefaultFifoMode },
        { .key = "r",  .boolSetter = &T::setReadTimeout     },
        { .key = "sd", .boolSetter = &T::setScriptDelay     },
    };

    return generic_setup_params(pOwner, args, table, "FT245 SETUP |");
}

#endif // FT245_SETUP_HPP
