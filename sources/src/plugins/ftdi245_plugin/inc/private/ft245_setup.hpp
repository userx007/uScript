#ifndef FT245_SETUP_HPP
#define FT245_SETUP_HPP

#include "PluginSetup.hpp"
#include "ft245_plugin.hpp"
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

#define LT_HDR            "FT245_P     |"
#define LOG_HDR           LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                               //
/////////////////////////////////////////////////////////////////////////////////

#define ARTEFACTS_PATH    "ARTEFACTS_PATH"
#define DEVICE_INDEX      "DEVICE_INDEX"
#define DEFAULT_VARIANT   "VARIANT"      // "BM" or "R"
#define DEFAULT_FIFO_MODE "FIFO_MODE"    // "async" or "sync"
#define READ_TIMEOUT      "READ_TIMEOUT" // ms, used by script execution
#define SCRIPT_DELAY      "SCRIPT_DELAY" // ms inter-command delay for scripts

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
bool FT245Plugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    m_strInstanceName = psSetParams->strInstanceName.empty() ? FT245_PLUGIN_NAME : psSetParams->strInstanceName;

    if (psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing found in the ini file"));
        return true;
    }

    PluginSettingsBinder sSettings;

    // clang-format off
    sSettings.Bind(DEFAULT_VARIANT,     [this](const std::string &v) { return parseVariant(v, m_sIniValues.eDefaultVariant); });
    sSettings.Bind(DEFAULT_FIFO_MODE,   [this](const std::string &v) { return parseFifoMode(v, m_sIniValues.eDefaultFifoMode); });
    sSettings.Bind(ARTEFACTS_PATH,      m_sIniValues.strArtefactsPath);
    sSettings.Bind(DEVICE_INDEX,        m_sIniValues.u8DeviceIndex);
    sSettings.Bind(READ_TIMEOUT,        m_sIniValues.u32ReadTimeout);
    sSettings.Bind(SCRIPT_DELAY,        m_sIniValues.u32ScriptDelay);
    // clang-format on

    return sSettings.Apply(psSetParams->mapSettings, nullptr, /*bStopOnFirstError=*/false);
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of FT245 parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (x=device_index  v=default_variant  fm=default_fifo_mode
 *                     r=read_tout  sd=script_delay)
 * \return true if processing succeeded, false otherwise
 */
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_ft245_set_params(const T *pOwner, const std::string &args)
{
    // clang-format off
    static constexpr KVSetterEntry<T> table[] = {
        {.key = "x",    .boolSetter = &T::setDeviceIndex},
        {.key = "v",    .boolSetter = &T::setDefaultVariant},
        {.key = "fm",   .boolSetter = &T::setDefaultFifoMode},
        {.key = "r",    .boolSetter = &T::setReadTimeout},
        {.key = "sd",   .boolSetter = &T::setScriptDelay},
    };
    // clang-format on

    return generic_setup_params(pOwner, args, table, LT_HDR);
}

#endif // FT245_SETUP_HPP
