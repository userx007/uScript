#ifndef UARTMON_SETUP_HPP
#define UARTMON_SETUP_HPP

#include "PluginSetup.hpp"

#include <string>

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of UARTMON parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs (i=polling_interval_ms)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_uartmon_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "i", .boolSetter = &T::setPollingInterval },
    };

    return generic_setup_params(pOwner, args, table, "UARTMON SETUP |");
}

#endif // UARTMON_SETUP_HPP
