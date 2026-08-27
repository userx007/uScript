#ifndef CH341_SETUP_HPP
#define CH341_SETUP_HPP

#include "PluginSetup.hpp"

#include <string>

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of CH341 parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (p=port  b=baudrate  r=read_tout  w=write_tout  s=recv_bufsize)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_ch341_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "p",      .boolSetter = &T::setCh341Port           },
        { .key = "b",      .boolSetter = &T::setCh341Baudrate       },
        { .key = "r",      .boolSetter = &T::setCh341ReadTimeout    },
        { .key = "w",      .boolSetter = &T::setCh341WriteTimeout   },
        { .key = "s",      .boolSetter = &T::setCh341ReadBufferSize },
        { .key = "raw",    .boolSetter = &T::setRawResult           },
        { .key = "cached", .boolSetter = &T::setCyclicCached        },
    };

    return generic_setup_params(pOwner, args, table, "CH341 SETUP |");
}

#endif // CH341_SETUP_HPP
