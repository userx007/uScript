#ifndef ENC28J60NET_SETUP_HPP
#define ENC28J60NET_SETUP_HPP

#include "PluginSetup.hpp"

#include <string>

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of ENC28J60Net parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (i=ip  p=port  r=read_tout  w=write_tout  s=bufsize)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_enc28j60net_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "i", .voidSetter = &T::setServerIp       },
        { .key = "p", .boolSetter = &T::setServerPort     },
        { .key = "r", .boolSetter = &T::setReadTimeout    },
        { .key = "w", .boolSetter = &T::setWriteTimeout   },
        { .key = "s", .boolSetter = &T::setReadBufferSize },
        { .key = "raw", .boolSetter = &T::setRawResult },
        { .key = "cached", .boolSetter = &T::setCyclicCached },
    };

    return generic_setup_params(pOwner, args, table, "ENC28J60NET SETUP |");
}

#endif // ENC28J60NET_SETUP_HPP
