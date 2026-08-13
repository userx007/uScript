#ifndef WEBSOCKET_SETUP_HPP
#define WEBSOCKET_SETUP_HPP

#include "PluginSetup.hpp"

#include <string>

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of WebSocket parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (h=host  p=port  u=path  o=subprotocol  c=connect_tout  r=read_tout  w=write_tout  s=recv_bufsize)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_websocket_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "h", .voidSetter = &T::setWsHost           },
        { .key = "p", .boolSetter = &T::setWsPort            },
        { .key = "u", .boolSetter = &T::setWsPath             },
        { .key = "o", .voidSetter = &T::setWsSubprotocol     },
        { .key = "c", .boolSetter = &T::setConnectTimeout     },
        { .key = "r", .boolSetter = &T::setReadTimeout        },
        { .key = "w", .boolSetter = &T::setWriteTimeout       },
        { .key = "s", .boolSetter = &T::setWsReadBufferSize   },
        { .key = "raw", .boolSetter = &T::setRawResult },
    };

    return generic_setup_params(pOwner, args, table, "WEBSOCKET SETUP |");
}

#endif // WEBSOCKET_SETUP_HPP
