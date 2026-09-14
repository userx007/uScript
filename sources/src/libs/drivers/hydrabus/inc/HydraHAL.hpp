#ifndef HYDRABUS_HAL_HPP
#define HYDRABUS_HAL_HPP

/**
 * @file HydraHAL.hpp
 * @brief Convenience umbrella — include this single header to pull in
 *        every class in the HydraHAL library.
 *
 * Typical usage:
 * @code
 * #include "HydraHAL.hpp"
 *
 * // Construct your ICommDriver implementation, then:
 * auto driver = std::make_shared<MySerialDriver>("/dev/ttyACM0");
 * auto hb     = std::make_shared<HydraHAL::Hydrabus>(driver);
 * hb->enter_bbio();
 *
 * HydraHAL::SPI spi(hb);
 * spi.set_speed(HydraHAL::SPI::Speed::SPI1_10M);
 * @endcode
 */

// Foundation
#include "AUXPin.hpp"
#include "Hydrabus.hpp"
#include "Protocol.hpp"
#include "Support.hpp"

// Protocols
#include "I2C.hpp"
#include "MMC.hpp"
#include "NFC.hpp"
#include "OneWire.hpp"
#include "RawWire.hpp"
#include "SDIO.hpp"
#include "SPI.hpp"
#include "SWD.hpp"
#include "Smartcard.hpp"
#include "UART.hpp"

// Utilities
#include "Utils.hpp"

#endif // HYDRABUS_HAL_HPP
