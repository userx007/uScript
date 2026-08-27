#ifndef FT4232_SETUP_HPP
#define FT4232_SETUP_HPP

#include "PluginSetup.hpp"

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

#endif // FT4232_SETUP_HPP
