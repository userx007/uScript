#ifndef HYDRABUS_SETUP_HPP
#define HYDRABUS_SETUP_HPP

#include "PluginSetup.hpp"

#include <string>

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of HYDRABUS parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (p=port  b=baudrate  r=read_tout  w=write_tout  s=recv_bufsize  sd=script_delay)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_hydrabus_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "p",  .voidSetter = &T::setUartPort        },
        { .key = "b",  .boolSetter = &T::setUartBaudrate    },
        { .key = "r",  .boolSetter = &T::setReadTimeout     },
        { .key = "w",  .boolSetter = &T::setWriteTimeout    },
        { .key = "s",  .boolSetter = &T::setReadBufferSize  },
        { .key = "sd", .boolSetter = &T::setScriptDelay     },
    };

    return generic_setup_params(pOwner, args, table, "HYDRABUS SETUP |");
}

#endif // HYDRABUS_SETUP_HPP
