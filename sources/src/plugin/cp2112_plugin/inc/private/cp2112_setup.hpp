#ifndef CP2112_SETUP_HPP
#define CP2112_SETUP_HPP

#include "PluginSetup.hpp"
#include "cp2112_plugin.hpp"
#include "uPluginSettings.hpp"

#include <string>

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of CP2112 parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (x=device_index  c=i2c_clock_hz  a=i2c_address  r=read_tout  sd=script_delay)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_cp2112_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "x",  .boolSetter = &T::setDeviceIndex  },
        { .key = "c",  .boolSetter = &T::setI2cClockHz   },
        { .key = "a",  .boolSetter = &T::setI2cAddress   },
        { .key = "r",  .boolSetter = &T::setReadTimeout  },
        { .key = "sd", .boolSetter = &T::setScriptDelay  },
    };

    return generic_setup_params(pOwner, args, table, "CP2112 SETUP |");
}

///////////////////////////////////////////////////////////////////
//                   INI KEY STRINGS                             //
///////////////////////////////////////////////////////////////////

#define ARTEFACTS_PATH  "ARTEFACTS_PATH"
#define DEVICE_INDEX    "DEVICE_INDEX"
#define I2C_CLOCK       "I2C_CLOCK"
#define I2C_ADDRESS     "I2C_ADDRESS"
#define READ_TIMEOUT    "READ_TIMEOUT"   
#define SCRIPT_DELAY    "SCRIPT_DELAY"   

///////////////////////////////////////////////////////////////////
//                   PLUGIN ENTRY POINTS                         //
///////////////////////////////////////////////////////////////////

bool CP2112Plugin::m_LocalSetParams(const PluginDataSet* ps)
{
    // Runtime instance identity for the GUI comm-dump panel (e.g. "CP2112:1"); falls back to the fixed plugin name if the
    // interpreter didn't supply one.
    m_strInstanceName = ps->strInstanceName.empty() ? CP2112_PLUGIN_NAME : ps->strInstanceName;

    if (!ps || ps->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("No settings in config"));
        return true;
    }

    PluginSettingsBinder sSettings;
    sSettings.Bind(ARTEFACTS_PATH, m_sIniValues.strArtefactsPath);
    sSettings.Bind(DEVICE_INDEX,   m_sIniValues.u8DeviceIndex);
    sSettings.Bind(I2C_CLOCK,      m_sIniValues.u32I2cClockHz);
    sSettings.Bind(I2C_ADDRESS,    m_sIniValues.u8I2cAddress);
    sSettings.Bind(READ_TIMEOUT,   m_sIniValues.u32ReadTimeout);
    sSettings.Bind(SCRIPT_DELAY,   m_sIniValues.u32ScriptDelay);

    // accumulate mode: matches the original getX() lambdas ("ok &= ...")
    const bool bOk = sSettings.Apply(ps->mapSettings, nullptr, /*bStopOnFirstError=*/false);

    if (!bOk) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("One or more config values failed to parse"));
    }

    return bOk;
}

#endif // CP2112_SETUP_HPP
