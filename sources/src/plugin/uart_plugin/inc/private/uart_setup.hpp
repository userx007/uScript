#ifndef UART_SETUP_HPP
#define UART_SETUP_HPP

#include "PluginSetup.hpp"

#include <string>

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of UART parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (p=port  b=baudrate  r=read_tout  w=write_tout  s=recv_bufsize)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_uart_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "p",      .boolSetter = &T::setUartPort           },
        { .key = "b",      .boolSetter = &T::setUartBaudrate       },
        { .key = "r",      .boolSetter = &T::setUartReadTimeout    },
        { .key = "w",      .boolSetter = &T::setUartWriteTimeout   },
        { .key = "s",      .boolSetter = &T::setUartReadBufferSize },
        { .key = "raw",    .boolSetter = &T::setRawResult          },
        { .key = "cached", .boolSetter = &T::setCyclicCached       },
    };

    return generic_setup_params(pOwner, args, table, "UART SETUP |");
}

#endif // UART_SETUP_HPP
