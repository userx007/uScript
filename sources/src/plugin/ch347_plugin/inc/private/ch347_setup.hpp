#ifndef CH347_SETUP_HPP
#define CH347_SETUP_HPP

#include "PluginSetup.hpp"
#include "ch347_plugin.hpp"
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

#define LT_HDR   "CH347_P     |"
#define LOG_HDR  LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                               //
/////////////////////////////////////////////////////////////////////////////////

#define ARTEFACTS_PATH   "ARTEFACTS_PATH"
#define DEVICE_PATH      "DEVICE_PATH"
#define SPI_CLOCK        "SPI_CLOCK"
#define I2C_SPEED        "I2C_SPEED"
#define I2C_ADDRESS      "I2C_ADDRESS"
#define JTAG_CLOCK_RATE  "JTAG_CLOCK_RATE"
#define READ_TIMEOUT     "READ_TIMEOUT"
#define SCRIPT_DELAY     "SCRIPT_DELAY"


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
bool CH347Plugin::m_LocalSetParams(const PluginDataSet* ps)
{
    // Runtime instance identity for the GUI comm-dump panel (e.g. "CH347:1"); falls back to the fixed plugin name if the
    // interpreter didn't supply one.
    m_strInstanceName = ps->strInstanceName.empty() ? CH347_PLUGIN_NAME : ps->strInstanceName;

    if (!ps || ps->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("No settings in config"));
        return true;
    }

    PluginSettingsBinder sSettings;
    sSettings.Bind(ARTEFACTS_PATH,  m_sIniValues.strArtefactsPath);
    sSettings.Bind(DEVICE_PATH,     m_sIniValues.strDevicePath);
    sSettings.Bind(SPI_CLOCK,       m_sIniValues.u32SpiClockHz);
    sSettings.Bind(I2C_SPEED,       [this](const std::string& v){ return parseI2cSpeed(v, m_sIniValues.eI2cSpeed); });
    sSettings.Bind(I2C_ADDRESS,     m_sIniValues.u8I2cAddress);
    sSettings.Bind(JTAG_CLOCK_RATE, m_sIniValues.u8JtagClockRate);
    sSettings.Bind(READ_TIMEOUT,    m_sIniValues.u32ReadTimeout);
    sSettings.Bind(SCRIPT_DELAY,    m_sIniValues.u32ScriptDelay);

    // accumulate mode: matches the original per-key getX() lambdas, which used
    // "ok &= ..." so every key is still attempted even after an earlier failure
    const bool bOk = sSettings.Apply(ps->mapSettings, nullptr, /*bStopOnFirstError=*/false);

    if (!bOk)
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("One or more config values failed to parse"));

    return bOk;
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of CH347 parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (d=device_path  c=spi_clock_hz  i=i2c_speed  a=i2c_address
 *                     j=jtag_clock_rate  r=read_tout  sd=script_delay)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_ch347_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "d",  .voidSetter = &T::setDevicePath    },
        { .key = "c",  .boolSetter = &T::setSpiClockHz     },
        { .key = "i",  .boolSetter = &T::setI2cSpeed       },
        { .key = "a",  .boolSetter = &T::setI2cAddress     },
        { .key = "j",  .boolSetter = &T::setJtagClockRate  },
        { .key = "r",  .boolSetter = &T::setReadTimeout    },
        { .key = "sd", .boolSetter = &T::setScriptDelay    },
    };

    return generic_setup_params(pOwner, args, table, "CH347 SETUP |");
}

#endif // CH347_SETUP_HPP
