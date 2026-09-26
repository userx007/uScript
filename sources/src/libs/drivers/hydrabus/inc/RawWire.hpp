#ifndef HYDRABUS_RAWWIRE_HPP
#define HYDRABUS_RAWWIRE_HPP

#include "Protocol.hpp"

#include <memory>
#include <span>
#include <stddef.h>
#include <stdint.h>
#include <stop_token>
#include <vector>

namespace HydraHAL {
    class Hydrabus;

    /**
     * @brief HydraBus Raw-Wire binary mode handler.
     *
     * Provides direct control over the CLK and SDA lines, bit-banged
     * transfers, and configurable clock/data characteristics.  This class
     * is also the base for SWD.
     *
     * Supported wire modes: 2-Wire (CLK + SDA) and 3-Wire (CLK + SDA + CS).
     * GPIO drive: Push-Pull or Open-Drain.
     *
     * @example
     * @code
     * auto hb = std::make_shared<HydraHAL::Hydrabus>(driver);
     * hb->enter_bbio();
     *
     * HydraHAL::RawWire rw(hb);
     * rw.set_sda(1);      // set SDA high
     * rw.clocks(2);       // send 2 clock pulses
     * auto data = rw.read(2);
     * @endcode
     */

    class RawWire : public Protocol {

        public:
            explicit RawWire(std::shared_ptr<Hydrabus> shpHydrabus);

            // -------------------------------------------------------------------------
            // Low-level pin / bit operations
            // -------------------------------------------------------------------------

            /**
             * @brief Read SDA, then send one clock tick.
             * @return The sampled bit value (0 or 1), as a single byte.
             */
            uint8_t read_bit(std::stop_token stop_tok = {});

            /**
             * @brief Clock in one byte (MSB first).
             * @return The received byte.
             */
            uint8_t read_byte(std::stop_token stop_tok = {});

            /**
             * @brief Send a single clock tick.
             * @return true on success.
             */
            bool clock(std::stop_token stop_tok = {});

            /**
             * @brief Send 1–16 clock ticks (HydraFW 0b0010xxxx bulk path).
             * @note Logs LOG_ERROR and returns false if num < 1 or num > 16.
             */
            bool bulk_ticks(size_t num, std::stop_token stop_tok = {});

            /**
             * @brief Send an arbitrary number of clock ticks (auto-chunked).
             * @note Logs LOG_ERROR and returns false if num < 1.
             */
            bool clocks(size_t num, std::stop_token stop_tok = {});

            /**
             * @brief Write bits MSB-first (HydraFW 0b0011xxxx).
             *
             * @param data     Bytes containing the bits to send.
             * @param num_bits Total number of bits to send from `data`.
             * @return true on success.
             */
            bool write_bits(std::span<const uint8_t> data, size_t num_bits, std::stop_token stop_tok = {});

            /**
             * @brief Bulk-write 1–16 bytes and capture MISO simultaneously.
             *
             * @param data 1–16 bytes.
             * @return Read bytes (same length), empty on error.
             * @note Logs LOG_ERROR and returns empty if data is empty or > 16 bytes.
             */
            std::vector<uint8_t> bulk_write(std::span<const uint8_t> data, std::stop_token stop_tok = {});

            /**
             * @brief Write an arbitrary-length buffer (auto-chunked).
             */
            std::vector<uint8_t> write(std::span<const uint8_t> data, std::stop_token stop_tok = {});

            /**
             * @brief Read `length` bytes by clocking in data.
             */
            std::vector<uint8_t> read(size_t length, std::stop_token stop_tok = {});

            // -------------------------------------------------------------------------
            // Pin control
            // -------------------------------------------------------------------------

            /** @return Current CLK level (cached). */
            int get_clk() const;

            /**
             * @brief Drive the CLK pin to `level` (0 or 1).
             * @return true on success.
             */
            bool set_clk(int iLevel);

            /**
             * @brief Read the current SDA line state from hardware.
             * @return 0 or 1; -1 on error.
             */
            int get_sda();

            /**
             * @brief Drive the SDA pin to `level` (0 or 1).
             * @return true on success.
             */
            bool set_sda(int iLevel);

            // -------------------------------------------------------------------------
            // Configuration
            // -------------------------------------------------------------------------

            /** @brief Set clock max speed in Hz. Valid values: 5000, 50000, 100000, 1000000. */
            bool set_speed(uint32_t u32Speed);

            // ---- Clock polarity (CPOL) -----------------------------------------------
            /** @return 0 = idle low, 1 = idle high. */
            int get_polarity() const;
            /** @param value 0 or 1. @return true on success. */
            bool set_polarity(int iValue);

            // ---- Wire count ----------------------------------------------------------
            /** @return 2 or 3. */
            int get_wires() const;
            /** @param value 2 or 3. @return true on success. */
            bool set_wires(int iValue);

            // ---- GPIO drive mode -----------------------------------------------------
            /** @return 0 = Push-Pull, 1 = Open-Drain. */
            int get_gpio_mode() const;
            /** @param value 0 = Push-Pull, 1 = Open-Drain. @return true on success. */
            bool set_gpio_mode(int iValue);

        protected:
            bool _configure_port();

            static constexpr uint8_t DEFAULT_CONFIG = 0b0000;
            uint8_t _config{DEFAULT_CONFIG};
            int _clk{0};
            int _sda{0};
    };

} // namespace HydraHAL

#endif // HYDRABUS_RAWWIRE_HPP