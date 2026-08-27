#ifndef RAWETH_SETUP_HPP
#define RAWETH_SETUP_HPP

#include "PluginSetup.hpp"

#include <string>

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of RawEth parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (i=iface  d=dest_mac  t=ethertype  x=promiscuous  r=read_tout  w=write_tout  s=recv_bufsize)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_raweth_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "i",      .boolSetter = &T::setIface                 },
        { .key = "d",      .boolSetter = &T::setDestMac                },
        { .key = "t",      .boolSetter = &T::setEtherType              },
        { .key = "x",      .boolSetter = &T::setPromiscuous            },
        { .key = "r",      .boolSetter = &T::setReadTimeout            },
        { .key = "w",      .boolSetter = &T::setWriteTimeout           },
        { .key = "s",      .boolSetter = &T::setRawEthReadBufferSize   },
        { .key = "raw",    .boolSetter = &T::setRawResult              },
        { .key = "cached", .boolSetter = &T::setCyclicCached           },
    };

    return generic_setup_params(pOwner, args, table, "RAWETH SETUP |");
}

#endif // RAWETH_SETUP_HPP
