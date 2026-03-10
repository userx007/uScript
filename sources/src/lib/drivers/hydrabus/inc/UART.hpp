#pragma once

#include "Protocol.hpp"
#include <optional>
#include <vector>

namespace HydraHAL {

/**
 * @brief HydraBus UART binary mode handler.
 *
 * Wraps HydraFW's UART binary mode.  Supports configurable baud rate,
 * parity, local echo, and a bridge mode that binds the HydraBus USB CDC
 * port directly to the target UART.
 *
 * @note Bridge mode (enter_bridge()) cannot be exited programmatically;
 *       the user must press the UBTN on the HydraBus hardware to return
 *       to normal operation.
 *
 * @example
 * @code
 * auto hb = std::make_shared<HydraHAL::Hydrabus>(driver);
 * hb->enter_bbio();
 *
 * HydraHAL::UART uart(hb);
 * uart.set_baud(115200);
 * uart.set_echo(true);
 * uart.write({0x41, 0x42, 0x43});   // send "ABC"
 * auto response = uart.read(3);
 * @endcode
 */
class UART : public Protocol {
public:

    enum class Parity : uint8_t {
        None = 0b00,
        Even = 0b01,
        Odd  = 0b10,
    };

    explicit UART(std::shared_ptr<Hydrabus> hydrabus);

    // -------------------------------------------------------------------------
    // Data transfer
    // -------------------------------------------------------------------------

    /**
     * @brief Bulk-write up to 16 bytes (HydraFW 0b0001xxxx).
     *
     * Firmware replies with one status byte per transmitted byte
     * (0x01 = success, other = error).
     *
     * @param data 1–16 bytes.
     * @note Logs LOG_ERROR and returns false if data is empty or > 16 bytes.
     */
    bool bulk_write(std::span<const uint8_t> data);

    /**
     * @brief Write an arbitrary-length buffer (auto-chunked into 16-byte calls).
     */
    bool write(std::span<const uint8_t> data);

    /**
     * @brief Read `length` bytes from the receive buffer.
     * @return Read bytes (may be shorter than requested on timeout).
     */
    std::vector<uint8_t> read(size_t length);

    // -------------------------------------------------------------------------
    // Configuration
    // -------------------------------------------------------------------------

    /** @return Current baud rate. */
    uint32_t get_baud() const;

    /**
     * @brief Set the UART baud rate.
     * @return true on success.
     */
    bool set_baud(uint32_t baud);

    /** @return Current parity setting. */
    Parity get_parity() const;

    /**
     * @brief Set the UART parity.
     * @return true on success.
     */
    bool set_parity(Parity parity);

    // ---- Local echo ---------------------------------------------------------

    /**
     * @return true if local echo is enabled (transmitted bytes are also
     *         looped back into the receive buffer).
     */
    bool get_echo() const;

    /**
     * @brief Enable or disable local echo.
     * @return true on success.
     */
    bool set_echo(bool enable);

    // ---- Bridge mode --------------------------------------------------------

    /**
     * @brief Enter transparent bridge mode.
     *
     * Binds the HydraBus USB CDC interface directly to the target UART.
     * Exit requires a physical UBTN press on the hardware.
     */
    void enter_bridge();

private:
    uint32_t _baud   {9600};
    Parity   _parity {Parity::None};
    bool     _echo   {false};
};

} // namespace HydraHAL
