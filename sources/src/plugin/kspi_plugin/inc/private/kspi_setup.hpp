#ifndef KSPI_SETUP_HPP
#define KSPI_SETUP_HPP

#include "PluginSetup.hpp"

#include <string>

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of SPI parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (d=device  m=mode  z=speed_hz  b=bits_per_word
 *                     r=read_tout  w=write_tout  s=recv_bufsize)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_spi_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "d",      .voidSetter = &T::setSpiDevice          },
        { .key = "m",      .boolSetter = &T::setSpiMode            },
        { .key = "z",      .boolSetter = &T::setSpiSpeedHz         },
        { .key = "b",      .boolSetter = &T::setSpiBitsPerWord     },
        { .key = "r",      .boolSetter = &T::setSpiReadTimeout     },
        { .key = "w",      .boolSetter = &T::setSpiWriteTimeout    },
        { .key = "s",      .boolSetter = &T::setSpiReadBufferSize  },
        { .key = "raw",    .boolSetter = &T::setRawResult          },
        { .key = "cached", .boolSetter = &T::setCyclicCached       },
    };

    return generic_setup_params(pOwner, args, table, "KSPI SETUP |");
}

#endif // KSPI_SETUP_HPP
