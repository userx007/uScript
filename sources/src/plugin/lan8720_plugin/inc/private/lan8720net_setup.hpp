#ifndef LAN8720NET_SETUP_HPP
#define LAN8720NET_SETUP_HPP

#include "unet_kv_setup.hpp"

#include <string>

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of LAN8720Net parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (i=ip  p=port  r=read_tout  w=write_tout  s=bufsize)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_lan8720net_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = 'i', .voidSetter = &T::setServerIp       },
        { .key = 'p', .boolSetter = &T::setServerPort     },
        { .key = 'r', .boolSetter = &T::setReadTimeout    },
        { .key = 'w', .boolSetter = &T::setWriteTimeout   },
        { .key = 's', .boolSetter = &T::setReadBufferSize },
    };

    return generic_net_kv_set_params(pOwner, args, table, "LAN8720NET SETUP |");
}

#endif // LAN8720NET_SETUP_HPP
