#ifndef U_CH347_DEVICE_H
#define U_CH347_DEVICE_H

/**
 * @file uCH347Device.hpp
 * @brief Shared-ownership device handle for CH347.
 *
 * The CH347 USB adapter exposes SPI, I2C, GPIO and JTAG over a single
 * device handle obtained from CH347OpenDevice().  This class owns that
 * handle and hands lightweight references to the per-protocol driver objects.
 *
 * Platform notes
 * ==============
 * Linux  : The handle is a POSIX file-descriptor (int).  Each sub-driver
 *          opens its own fd via CH347OpenDevice(path).
 * Windows: The handle is a device index (ULONG).  The DLL manages the
 *          underlying HANDLE internally, so multiple calls to
 *          CH347OpenDevice("0") are reference-counted by the DLL.
 *
 * Device path / index
 * ===================
 * Linux  : strDevice is a filesystem path, e.g. "/dev/ch34xpis0".
 * Windows: strDevice must be a decimal device-index string, e.g. "0".
 *
 * Typical usage
 * =============
 * @code
 *   // Linux
 *   CH347Device dev("/dev/ch34xpis0");
 *
 *   // Windows (first device)
 *   CH347Device dev("0");
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
 * Each sub-driver holds a raw (non-owning) copy of the device handle;
 * the CH347Device destructor closes the parent handle once after the
 * sub-drivers have each closed their own.
 */

#include "ch347_compat.h"   // platform-unified CH347 API + CH347_HANDLE
#include "uCH347Spi.hpp"
#include "uCH347I2c.hpp"
#include "uCH347Gpio.hpp"
#include "uCH347Jtag.hpp"

#include <string>
#include <memory>
#include <stdexcept>

class CH347Device
{
public:
    // -----------------------------------------------------------------------
    // Construction / destruction
    // -----------------------------------------------------------------------

    /**
     * @brief Open the CH347 device and initialise all sub-drivers.
     *
     * @param strDevice  Device path (Linux) or decimal index string (Windows).
     * @param spiCfg     SPI bus configuration passed to CH347SPI.
     * @param i2cSpeed   I2C bus speed preset.
     * @param jtagRate   JTAG clock-rate (0-5).
     * @throws std::runtime_error if the device cannot be opened.
     */
    explicit CH347Device(const std::string& strDevice,
                         const mSpiCfgS&    spiCfg    = {},
                         I2cSpeed           i2cSpeed  = I2cSpeed::Fast,
                         uint8_t            jtagRate  = 2)
    {
        m_iFd = CH347OpenDevice(strDevice.c_str());
        if (m_iFd == CH347_INVALID_HANDLE)
            throw std::runtime_error("CH347Device: cannot open " + strDevice);

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
        if (m_iFd != CH347_INVALID_HANDLE)
            CH347CloseDevice(m_iFd);
    }

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

    /** Raw device handle (fd on Linux, device index on Windows). */
    CH347_HANDLE handle() const { return m_iFd; }

    /** Set USB-level read/write timeouts (affects all sub-drivers). */
    bool set_timeout(uint32_t writeMs, uint32_t readMs)
    {
        return CH34xSetTimeout(m_iFd, writeMs, readMs);
    }

    /** Query firmware / bcd-device version byte. */
    bool get_firmware_version(uint8_t& version)
    {
        return CH34x_GetChipVersion(m_iFd, &version);
    }

    /** Query USB device VID/PID as packed (VID<<16 | PID). */
    bool get_device_id(uint32_t& id)
    {
        return CH34X_GetDeviceID(m_iFd, &id);
    }

private:
    CH347_HANDLE m_iFd = CH347_INVALID_HANDLE;

    std::unique_ptr<CH347SPI>  m_spi;
    std::unique_ptr<CH347I2C>  m_i2c;
    std::unique_ptr<CH347GPIO> m_gpio;
    std::unique_ptr<CH347JTAG> m_jtag;
};

#endif // U_CH347_DEVICE_H
