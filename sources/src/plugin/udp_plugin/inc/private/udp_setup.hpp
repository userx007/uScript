#ifndef UDP_SETUP_HPP
#define UDP_SETUP_HPP

#include "PluginSetup.hpp"

#include <string>

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of UDP parameters expressed as a space-separated key=value string.
 *
 * \note This is the CONFIG-time key=value grammar (default peer + timeouts).
 *       It is deliberately distinct from CMD's "d:host:port <payload>"
 *       destination-override token (see UDPPlugin::m_SplitDestOverride),
 *       which selects a one-off peer for a single datagram rather than
 *       reconfiguring the plugin's default peer.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (h=host  p=port  c=connect_tout  r=read_tout  w=write_tout  s=recv_bufsize)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_udp_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "h", .voidSetter = &T::setUdpHost           },
        { .key = "p", .boolSetter = &T::setUdpPort            },
        { .key = "c", .boolSetter = &T::setConnectTimeout     },
        { .key = "r", .boolSetter = &T::setReadTimeout        },
        { .key = "w", .boolSetter = &T::setWriteTimeout       },
        { .key = "s", .boolSetter = &T::setUdpReadBufferSize  },
        { .key = "raw", .boolSetter = &T::setRawResult },
    };

    return generic_setup_params(pOwner, args, table, "UDP SETUP |");
}

#endif // UDP_SETUP_HPP
