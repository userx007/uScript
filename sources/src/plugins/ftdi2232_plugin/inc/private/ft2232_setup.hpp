#ifndef FT2232_SETUP_HPP
#define FT2232_SETUP_HPP
#include "PluginSetup.hpp"
#include "ft2232_plugin.hpp"
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

#define LT_HDR          "FT2232_P    |"
#define LOG_HDR         LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                  INI FILE CONFIGURATION ITEMS                               //
/////////////////////////////////////////////////////////////////////////////////

#define ARTEFACTS_PATH  "ARTEFACTS_PATH"
#define DEVICE_INDEX    "DEVICE_INDEX"
#define DEFAULT_VARIANT "VARIANT" // "H" or "D"
#define SPI_CHANNEL     "SPI_CHANNEL"
#define I2C_CHANNEL     "I2C_CHANNEL"
#define GPIO_CHANNEL    "GPIO_CHANNEL"
#define SPI_CLOCK       "SPI_CLOCK"
#define I2C_CLOCK       "I2C_CLOCK"
#define I2C_ADDRESS     "I2C_ADDRESS"
#define READ_TIMEOUT    "READ_TIMEOUT" // ms, used by script execution
#define SCRIPT_DELAY    "SCRIPT_DELAY" // ms inter-command delay for scripts
#define UART_BAUD       "UART_BAUD"    // default baud rate for UART module

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
bool FT2232Plugin::m_LocalSetParams(const PluginDataSet *psSetParams)
{
    m_strInstanceName = psSetParams->strInstanceName.empty() ? FT2232_PLUGIN_NAME : psSetParams->strInstanceName;

    if (psSetParams->mapSettings.empty()) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Nothing found in the ini file"));
        return true;
    }

    PluginSettingsBinder sSettings;

    // clang-format off
    sSettings.Bind(DEFAULT_VARIANT, [this](const std::string &v) { return parseVariant(v, m_sIniValues.eDefaultVariant); });
    sSettings.Bind(SPI_CHANNEL,     [this](const std::string &v) { return parseChannel(v, m_sIniValues.eSpiChannel); });
    sSettings.Bind(I2C_CHANNEL,     [this](const std::string &v) { return parseChannel(v, m_sIniValues.eI2cChannel); });
    sSettings.Bind(GPIO_CHANNEL,    [this](const std::string &v) { return parseChannel(v, m_sIniValues.eGpioChannel); });
    sSettings.Bind(ARTEFACTS_PATH,  m_sIniValues.strArtefactsPath);
    sSettings.Bind(DEVICE_INDEX,    m_sIniValues.u8DeviceIndex);
    sSettings.Bind(SPI_CLOCK,       m_sIniValues.u32SpiClockHz);
    sSettings.Bind(I2C_CLOCK,       m_sIniValues.u32I2cClockHz);
    sSettings.Bind(I2C_ADDRESS,     m_sIniValues.u8I2cAddress);
    sSettings.Bind(READ_TIMEOUT,    m_sIniValues.u32ReadTimeout);
    sSettings.Bind(SCRIPT_DELAY,    m_sIniValues.u32ScriptDelay);
    sSettings.Bind(UART_BAUD,       m_sIniValues.u32UartBaudRate);
    // clang-format on

    return sSettings.Apply(psSetParams->mapSettings, nullptr, /*bStopOnFirstError=*/false);
}

/*--------------------------------------------------------------------------------------------------------*/
/**
 * \brief Apply a set of FT2232 parameters expressed as a space-separated key=value string.
 *
 * \param[in] pOwner  pointer to the plugin instance
 * \param[in] args    space-separated key=value pairs
 *                    (x=device_index  v=default_variant  spc=spi_channel  i2cc=i2c_channel
 *                     gc=gpio_channel  spf=spi_clock_hz  i2f=i2c_clock_hz  a=i2c_address
 *                     r=read_tout  sd=script_delay  baud=uart_baudrate)
 * \return true if processing succeeded, false otherwise
 */
/*--------------------------------------------------------------------------------------------------------*/
template <typename T>
bool generic_ft2232_set_params(const T *pOwner, const std::string &strArgs)
{
    // clang-format off
    static constexpr KVSetterEntry<T> table[] = {
        {.key = "x",    .boolSetter = &T::setDeviceIndex},
        {.key = "v",    .boolSetter = &T::setDefaultVariant},
        {.key = "spc",  .boolSetter = &T::setSpiChannel},
        {.key = "i2cc", .boolSetter = &T::setI2cChannel},
        {.key = "gc",   .boolSetter = &T::setGpioChannel},
        {.key = "spf",  .boolSetter = &T::setSpiClockHz},
        {.key = "i2f",  .boolSetter = &T::setI2cClockHz},
        {.key = "a",    .boolSetter = &T::setI2cAddress},
        {.key = "r",    .boolSetter = &T::setReadTimeout},
        {.key = "sd",   .boolSetter = &T::setScriptDelay},
        {.key = "baud", .boolSetter = &T::setUartBaudRate},
    };
    // clang-format on

    return generic_setup_params(pOwner, strArgs, table, LT_HDR);
}

#endif // FT2232_SETUP_HPP
