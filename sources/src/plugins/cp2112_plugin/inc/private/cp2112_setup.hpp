#ifndef CP2112_SETUP_HPP
#define CP2112_SETUP_HPP
#include "PluginSetup.hpp"
#include "cp2112_plugin.hpp"
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

#define LT_HDR         "CP2112_P    |"
#define LOG_HDR        LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                               //
/////////////////////////////////////////////////////////////////////////////////

#define ARTEFACTS_PATH "ARTEFACTS_PATH"
#define DEVICE_INDEX   "DEVICE_INDEX"
#define I2C_CLOCK      "I2C_CLOCK"
#define I2C_ADDRESS    "I2C_ADDRESS"
#define READ_TIMEOUT   "READ_TIMEOUT"
#define SCRIPT_DELAY   "SCRIPT_DELAY"

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
bool CP2112Plugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    m_strInstanceName = psSetParams->strInstanceName.empty() ? CP2112_PLUGIN_NAME : psSetParams->strInstanceName;

    if (psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing found in the ini file"));
        return true;
    }

    PluginSettingsBinder sSettings;

    // clang-format off
    sSettings.Bind(ARTEFACTS_PATH,  m_sIniValues.strArtefactsPath);
    sSettings.Bind(DEVICE_INDEX,    m_sIniValues.u8DeviceIndex);
    sSettings.Bind(I2C_CLOCK,       m_sIniValues.u32I2cClockHz);
    sSettings.Bind(I2C_ADDRESS,     m_sIniValues.u8I2cAddress);
    sSettings.Bind(READ_TIMEOUT,    m_sIniValues.u32ReadTimeout);
    sSettings.Bind(SCRIPT_DELAY,    m_sIniValues.u32ScriptDelay);
    // clang-format on

    return sSettings.Apply(psSetParams->mapSettings, nullptr, /*bStopOnFirstError=*/false);
}

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
bool generic_cp2112_set_params(const T *pOwner, const std::string &strArgs)
{
    // clang-format off
    static constexpr KVSetterEntry<T> table[] = {
        {.key = "x",    .boolSetter = &T::setDeviceIndex},
        {.key = "c",    .boolSetter = &T::setI2cClockHz},
        {.key = "a",    .boolSetter = &T::setI2cAddress},
        {.key = "r",    .boolSetter = &T::setReadTimeout},
        {.key = "sd",   .boolSetter = &T::setScriptDelay},
    };
    // clang-format on

    return generic_setup_params(pOwner, strArgs, table, LT_HDR);
}

#endif // CP2112_SETUP_HPP
