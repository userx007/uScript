#ifndef UARTMON_SETUP_HPP
#define UARTMON_SETUP_HPP

#include "PluginSetup.hpp"
#include "uartmon_plugin.hpp"
#include "uPluginSettings.hpp"

#include <string>

/////////////////////////////////////////////////////////////////////////////////
//                            LOG DEFINITIONS                                  //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR   "UARTMON_P   |"
#define LOG_HDR  LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                               //
/////////////////////////////////////////////////////////////////////////////////

#define    POLLING_INTERVAL   "POLLING_INTERVAL"

/////////////////////////////////////////////////////////////////////////////////
//                  CONFIGURATION INTERFACES                                   //
/////////////////////////////////////////////////////////////////////////////////

/*--------------------------------------------------------------------------------------------------------*/
/**
  * \brief processing of the plugin specific settings.
  *
  * Pulls the plugin-specific keys out of the ini-backed PluginDataSet and feeds them through the
  * same setter surface the CONFIG command uses so an ini file
  * and a runtime CONFIG command are always interpreted identically
*/
/*--------------------------------------------------------------------------------------------------------*/
bool UartmonPlugin::m_LocalSetParams( const PluginDataSet *psSetParams )
{
    if (true == psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing was loaded from the ini file ..."));
        return true;
    }

    PluginSettingsBinder sSettings;
    sSettings.Bind(POLLING_INTERVAL, m_u32PollingInterval);

    return sSettings.Apply(psSetParams->mapSettings,
        [](const std::string& strKey, const std::string& strRawValue) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING(strKey); LOG_STRING(":"); LOG_STRING(strRawValue));
        });
}

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

    return generic_setup_params(pOwner, args, table, LT_HDR);
}

#endif // UARTMON_SETUP_HPP
