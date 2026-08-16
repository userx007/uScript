#ifndef TCPIP_SETUP_HPP
#define TCPIP_SETUP_HPP

#include "PluginSetup.hpp"

#include <string>

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of TCP parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (h=host  p=port  c=connect_tout  r=read_tout  w=write_tout  s=recv_bufsize)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_tcp_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "h", .voidSetter = &T::setTcpHost           },
        { .key = "p", .boolSetter = &T::setTcpPort            },
        { .key = "c", .boolSetter = &T::setConnectTimeout     },
        { .key = "r", .boolSetter = &T::setReadTimeout        },
        { .key = "w", .boolSetter = &T::setWriteTimeout       },
        { .key = "s", .boolSetter = &T::setTcpReadBufferSize  },
        { .key = "raw", .boolSetter = &T::setRawResult },
        { .key = "cached", .boolSetter = &T::setCyclicCached },
    };

    return generic_setup_params(pOwner, args, table, "TCPIP SETUP |");
}

#endif // TCPIP_SETUP_HPP
