#pragma once

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
#include "common.hpp"
#include "Hydrabus.hpp"
#include "AUXPin.hpp"
#include "Protocol.hpp"

// Protocols
#include "SPI.hpp"
#include "I2C.hpp"
#include "UART.hpp"
#include "OneWire.hpp"
#include "RawWire.hpp"
#include "SWD.hpp"
#include "Smartcard.hpp"
#include "NFC.hpp"
#include "MMC.hpp"
#include "SDIO.hpp"

// Utilities
#include "Utils.hpp"
