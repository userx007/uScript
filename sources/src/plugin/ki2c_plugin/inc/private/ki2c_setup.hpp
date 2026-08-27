#ifndef KI2C_SETUP_HPP
#define KI2C_SETUP_HPP

#include "PluginSetup.hpp"

#include <string>

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of I2C parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (d=device  a=address  r=read_tout  w=write_tout  s=recv_bufsize)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_i2c_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "d",      .voidSetter = &T::setI2CDevice          },
        { .key = "a",      .boolSetter = &T::setI2CAddress         },
        { .key = "r",      .boolSetter = &T::setI2CReadTimeout     },
        { .key = "w",      .boolSetter = &T::setI2CWriteTimeout    },
        { .key = "s",      .boolSetter = &T::setI2CReadBufferSize  },
        { .key = "raw",    .boolSetter = &T::setRawResult          },
        { .key = "cached", .boolSetter = &T::setCyclicCached       },
    };

    return generic_setup_params(pOwner, args, table, "KI2C SETUP |");
}

#endif // KI2C_SETUP_HPP
