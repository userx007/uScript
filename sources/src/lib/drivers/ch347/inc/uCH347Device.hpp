#ifndef U_CH347_DEVICE_H
#define U_CH347_DEVICE_H

/**
 * @file uCH347Device.hpp
 * @brief Shared-ownership device handle for CH347.
 *
 * The CH347 USB adapter exposes SPI, I2C, GPIO and JTAG over a single file
 * descriptor obtained from CH347OpenDevice().  This class owns that descriptor
 * and hands lightweight references to the per-protocol driver objects.
 *
 * Typical usage
 * =============
 * @code
 *   CH347Device dev("/dev/ch34xpis0");
 *
 *   mSpiCfgS spiCfg{};
 *   spiCfg.iMode       = 0;   // SPI Mode 0
 *   spiCfg.iByteOrder  = 1;   // MSB first
 *
 *   auto& spi  = dev.spi();
 *   spi.set_frequency(1'000'000);
 *
 *   auto& i2c  = dev.i2c();
 *   i2c.set_speed(I2cSpeed::Fast);
 *
 *   auto& gpio = dev.gpio();
 *   gpio.pin_write(GPIO_PIN_3, true);
 * @endcode
 *
 * Each sub-driver holds a raw (non-owning) copy of the file descriptor;
 * the CH347Device destructor closes it once.
 */

#include "uCH347Spi.hpp"
#include "uCH347I2c.hpp"
#include "uCH347Gpio.hpp"
#include "uCH347Jtag.hpp"

#include <string>
#include <stdexcept>

class CH347Device
{
public:
    // -----------------------------------------------------------------------
    // Construction / destruction
    // -----------------------------------------------------------------------

    explicit CH347Device(const std::string& strDevice,
                         const mSpiCfgS&    spiCfg    = {},
                         I2cSpeed           i2cSpeed  = I2cSpeed::Fast,
                         uint8_t            jtagRate  = 2)
    {
        m_iFd = CH347OpenDevice(strDevice.c_str());
        if (m_iFd < 0)
            throw std::runtime_error("CH347Device: cannot open " + strDevice);

        /* Initialise sub-drivers with the shared fd */
        m_spi  = std::make_unique<CH347SPI> ();
        m_i2c  = std::make_unique<CH347I2C> ();
        m_gpio = std::make_unique<CH347GPIO>();
        m_jtag = std::make_unique<CH347JTAG>();

        m_spi ->open(strDevice, spiCfg);
        m_i2c ->open(strDevice, i2cSpeed);
        m_gpio->open(strDevice);
        m_jtag->open(strDevice, jtagRate);
    }

    ~CH347Device()
    {
        m_spi.reset();
        m_i2c.reset();
        m_gpio.reset();
        m_jtag.reset();
        if (m_iFd >= 0)
            CH347CloseDevice(m_iFd);
    }

    /* Non-copyable, movable */
    CH347Device(const CH347Device&)            = delete;
    CH347Device& operator=(const CH347Device&) = delete;
    CH347Device(CH347Device&&)                 = default;
    CH347Device& operator=(CH347Device&&)      = default;

    // -----------------------------------------------------------------------
    // Sub-driver accessors
    // -----------------------------------------------------------------------

    CH347SPI&  spi()  { return *m_spi;  }
    CH347I2C&  i2c()  { return *m_i2c;  }
    CH347GPIO& gpio() { return *m_gpio; }
    CH347JTAG& jtag() { return *m_jtag; }

    const CH347SPI&  spi()  const { return *m_spi;  }
    const CH347I2C&  i2c()  const { return *m_i2c;  }
    const CH347GPIO& gpio() const { return *m_gpio; }
    const CH347JTAG& jtag() const { return *m_jtag; }

    // -----------------------------------------------------------------------
    // Shared utilities
    // -----------------------------------------------------------------------

    int fd() const { return m_iFd; }

    /** Set USB-level read/write timeouts (affects all sub-drivers). */
    bool set_timeout(uint32_t writeMs, uint32_t readMs)
    {
        return CH34xSetTimeout(m_iFd, writeMs, readMs);
    }

    /** Query firmware version. */
    bool get_firmware_version(uint8_t& version)
    {
        return CH34x_GetChipVersion(m_iFd, &version);
    }

    /** Query USB device VID/PID. */
    bool get_device_id(uint32_t& id)
    {
        return CH34X_GetDeviceID(m_iFd, &id);
    }

private:
    int m_iFd = -1;

    std::unique_ptr<CH347SPI>  m_spi;
    std::unique_ptr<CH347I2C>  m_i2c;
    std::unique_ptr<CH347GPIO> m_gpio;
    std::unique_ptr<CH347JTAG> m_jtag;
};

#endif // U_CH347_DEVICE_H
