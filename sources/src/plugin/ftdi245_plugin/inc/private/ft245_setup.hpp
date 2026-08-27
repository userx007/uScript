#ifndef FT245_SETUP_HPP
#define FT245_SETUP_HPP

#include "PluginSetup.hpp"
#include "ft245_plugin.hpp"
#include "uPluginSettings.hpp"

#include <string>

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
bool generic_ft245_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "x",  .boolSetter = &T::setDeviceIndex     },
        { .key = "v",  .boolSetter = &T::setDefaultVariant  },
        { .key = "fm", .boolSetter = &T::setDefaultFifoMode },
        { .key = "r",  .boolSetter = &T::setReadTimeout     },
        { .key = "sd", .boolSetter = &T::setScriptDelay     },
    };

    return generic_setup_params(pOwner, args, table, "FT245 SETUP |");
}

///////////////////////////////////////////////////////////////////
//                   INI KEY STRINGS                             //
///////////////////////////////////////////////////////////////////

#define ARTEFACTS_PATH   "ARTEFACTS_PATH"
#define DEVICE_INDEX     "DEVICE_INDEX"
#define DEFAULT_VARIANT  "VARIANT"        // "BM" or "R"
#define DEFAULT_FIFO_MODE "FIFO_MODE"     // "async" or "sync"
#define READ_TIMEOUT     "READ_TIMEOUT"   // ms, used by script execution
#define SCRIPT_DELAY     "SCRIPT_DELAY"   // ms inter-command delay for scripts

///////////////////////////////////////////////////////////////////
//                   PLUGIN ENTRY POINTS                         //
///////////////////////////////////////////////////////////////////

bool FT245Plugin::m_LocalSetParams(const PluginDataSet* ps)
{
    // Runtime instance identity for the GUI comm-dump panel (e.g. "FT245:1"); falls back to the fixed plugin name if the
    // interpreter didn't supply one.
    m_strInstanceName = ps->strInstanceName.empty() ? FT245_PLUGIN_NAME : ps->strInstanceName;

    if (!ps || ps->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("No settings in config"));
        return true;
    }

    PluginSettingsBinder sSettings;
    sSettings.Bind(ARTEFACTS_PATH,     m_sIniValues.strArtefactsPath);
    sSettings.Bind(DEVICE_INDEX,       m_sIniValues.u8DeviceIndex);
    sSettings.Bind(DEFAULT_VARIANT,    [this](const std::string& v){ return parseVariant(v, m_sIniValues.eDefaultVariant); });
    sSettings.Bind(DEFAULT_FIFO_MODE,  [this](const std::string& v){ return parseFifoMode(v, m_sIniValues.eDefaultFifoMode); });
    sSettings.Bind(READ_TIMEOUT,       m_sIniValues.u32ReadTimeout);
    sSettings.Bind(SCRIPT_DELAY,       m_sIniValues.u32ScriptDelay);

    // accumulate mode: matches the original getX() lambdas ("ok &= ...")
    const bool bOk = sSettings.Apply(ps->mapSettings, nullptr, /*bStopOnFirstError=*/false);

    if (!bOk)
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("One or more config values failed to parse"));

    return bOk;
}

#endif // FT245_SETUP_HPP
