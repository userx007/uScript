#ifndef FT4232_SETUP_HPP
#define FT4232_SETUP_HPP

#include "PluginSetup.hpp"
#include "ft4232_plugin.hpp"
#include "uPluginSettings.hpp"

#include <string>

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of FT4232 parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (x=device_index  spc=spi_channel  i2cc=i2c_channel  gc=gpio_channel
 *                     uc=uart_channel  spf=spi_clock_hz  i2f=i2c_clock_hz  a=i2c_address
 *                     baud=uart_baudrate  r=read_tout  sd=script_delay)
 * \return true if processing succeeded, false otherwise
*/
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_ft4232_set_params (const T *pOwner, const std::string &args)
{
    static constexpr KVSetterEntry<T> table[] = {
        { .key = "x",    .boolSetter = &T::setDeviceIndex  },
        { .key = "spc",  .boolSetter = &T::setSpiChannel   },
        { .key = "i2cc", .boolSetter = &T::setI2cChannel   },
        { .key = "gc",   .boolSetter = &T::setGpioChannel  },
        { .key = "uc",   .boolSetter = &T::setUartChannel  },
        { .key = "spf",  .boolSetter = &T::setSpiClockHz   },
        { .key = "i2f",  .boolSetter = &T::setI2cClockHz   },
        { .key = "a",    .boolSetter = &T::setI2cAddress   },
        { .key = "baud", .boolSetter = &T::setUartBaudRate },
        { .key = "r",    .boolSetter = &T::setReadTimeout  },
        { .key = "sd",   .boolSetter = &T::setScriptDelay  },
    };

    return generic_setup_params(pOwner, args, table, "FT4232 SETUP |");
}

///////////////////////////////////////////////////////////////////
//                   INI KEY STRINGS                             //
///////////////////////////////////////////////////////////////////

#define ARTEFACTS_PATH  "ARTEFACTS_PATH"
#define DEVICE_INDEX    "DEVICE_INDEX"
#define SPI_CHANNEL     "SPI_CHANNEL"
#define I2C_CHANNEL     "I2C_CHANNEL"
#define GPIO_CHANNEL    "GPIO_CHANNEL"
#define UART_CHANNEL    "UART_CHANNEL"
#define SPI_CLOCK       "SPI_CLOCK"
#define I2C_CLOCK       "I2C_CLOCK"
#define I2C_ADDRESS     "I2C_ADDRESS"
#define UART_BAUD       "UART_BAUD"
#define READ_TIMEOUT    "READ_TIMEOUT"   // ms — used by script execution
#define SCRIPT_DELAY    "SCRIPT_DELAY"   // ms — inter-command delay for scripts

///////////////////////////////////////////////////////////////////
//                   PLUGIN ENTRY POINTS                         //
///////////////////////////////////////////////////////////////////

bool FT4232Plugin::m_LocalSetParams(const PluginDataSet* ps)
{
    // Runtime instance identity for the GUI comm-dump panel (e.g. "FT4232:1"); falls back to the fixed plugin name if the
    // interpreter didn't supply one.
    m_strInstanceName = ps->strInstanceName.empty() ? FT4232_PLUGIN_NAME : ps->strInstanceName;

    if (!ps || ps->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("No settings in config"));
        return true;
    }

    PluginSettingsBinder sSettings;
    sSettings.Bind(ARTEFACTS_PATH, m_sIniValues.strArtefactsPath);
    sSettings.Bind(DEVICE_INDEX,   m_sIniValues.u8DeviceIndex);
    sSettings.Bind(SPI_CHANNEL,    [this](const std::string& v){ return parseChannel(v, m_sIniValues.eSpiChannel); });
    sSettings.Bind(I2C_CHANNEL,    [this](const std::string& v){ return parseChannel(v, m_sIniValues.eI2cChannel); });
    sSettings.Bind(GPIO_CHANNEL,   [this](const std::string& v){ return parseChannel(v, m_sIniValues.eGpioChannel); });
    sSettings.Bind(UART_CHANNEL,   [this](const std::string& v){ return parseChannel(v, m_sIniValues.eUartChannel); });
    sSettings.Bind(SPI_CLOCK,      m_sIniValues.u32SpiClockHz);
    sSettings.Bind(I2C_CLOCK,      m_sIniValues.u32I2cClockHz);
    sSettings.Bind(I2C_ADDRESS,    m_sIniValues.u8I2cAddress);
    sSettings.Bind(UART_BAUD,      m_sIniValues.u32UartBaudRate);
    sSettings.Bind(READ_TIMEOUT,   m_sIniValues.u32ReadTimeout);
    sSettings.Bind(SCRIPT_DELAY,   m_sIniValues.u32ScriptDelay);

    // accumulate mode: matches the original getX() lambdas ("ok &= ...")
    const bool bOk = sSettings.Apply(ps->mapSettings, nullptr, /*bStopOnFirstError=*/false);

    if (!bOk)
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("One or more config values failed to parse"));

    return bOk;
}

#endif // FT4232_SETUP_HPP
