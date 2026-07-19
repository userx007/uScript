#ifndef U_I2C_BRIDGE_H
#define U_I2C_BRIDGE_H

#include "ICommDriver.hpp"

#include <hidapi/hidapi.h>

#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <vector>


/**
 * @file  uDigisparkI2C.hpp
 * @brief Host-side I2C master driver backed by a Digispark ATtiny85 USB→I2C bridge.
 *        Inherits from ICommDriver and implements its unified read/write interface.
 *
 * Transport : USB HID (hidapi), 8-byte fixed-length packets
 * Firmware  : i2c_bridge.ino  (TinyWireM + DigiUSB)
 * VID/PID   : 0x16C0 / 0x05DF  (V-USB shared HID)
 *
 * Packet layout  Host → Device:
 *   [CMD][ARG0][ARG1][ARG2][D0][D1][D2][D3]
 *
 * Packet layout  Device → Host:
 *   [CMD][STATUS/LEN][D0][D1][D2][D3][D4][D5]
 *
 * -------------------------------------------------------------------
 * ICommDriver interface mapping
 * -------------------------------------------------------------------
 *
 * tout_read()
 *   The base interface ReadOptions::mode selects the I2C operation:
 *     ReadMode::Exact          → CMD_READ   (plain slave read)
 *     ReadMode::UntilToken     → CMD_WRITE_READ  (register/preamble read)
 *     ReadMode::UntilDelimiter → CMD_SCAN   (bus scan, delimiter field unused)
 *
 *   I2C-specific parameters that have no direct equivalent in ReadOptions
 *   are carried through the I2CReadOptions extension struct and passed via
 *   the I2C-specific overload.  The base-interface overload that accepts
 *   plain ReadOptions derives defaults (slave_addr = delimiter field,
 *   read_len = buffer.size()).
 *
 * tout_write()
 *   The base interface bundles the 7-bit slave address as the first byte
 *   of the write buffer:  buffer[0] = slave_addr, buffer[1..N] = data.
 *   This convention is documented below and enforced in the implementation.
 */
class I2CBridge : public ICommDriver
{

public:

    // ── Constants ─────────────────────────────────────────────────────────────
    static constexpr uint16_t  I2C_DIGISPARK_VID        = 0x16C0; ///< V-USB HID VID
    static constexpr uint16_t  I2C_DIGISPARK_PID        = 0x05DF; ///< V-USB HID PID
    static constexpr size_t    I2C_PKT_SIZE              = 8;      ///< HID report payload bytes
    static constexpr size_t    I2C_MAX_WRITE_PAYLOAD     = 5;      ///< Max data bytes per write packet
    static constexpr size_t    I2C_MAX_READ_PAYLOAD      = 6;      ///< Max data bytes per read packet
    static constexpr size_t    I2C_MAX_WRITE_READ_WLEN   = 4;      ///< Max preamble bytes in WriteRead
    static constexpr size_t    I2C_MAX_WRITE_READ_RLEN   = 5;      ///< Max read bytes in WriteRead
    static constexpr uint32_t  I2C_READ_DEFAULT_TIMEOUT  = 2000;   ///< Default read timeout  [ms]
    static constexpr uint32_t  I2C_WRITE_DEFAULT_TIMEOUT = 2000;   ///< Default write timeout [ms]
    static constexpr uint32_t  I2C_SCAN_DEFAULT_TIMEOUT  = 5000;   ///< Bus scan timeout      [ms]


    // ── I2C-specific read modes ────────────────────────────────────────────────
    /**
     * @brief Maps I2C operations onto ICommDriver::ReadMode values:
     *
     *   ICommDriver::ReadMode::Exact          → I2C plain read   (CMD_READ)
     *   ICommDriver::ReadMode::UntilToken     → I2C write-read   (CMD_WRITE_READ)
     *   ICommDriver::ReadMode::UntilDelimiter → I2C bus scan     (CMD_SCAN)
     */
    using ReadMode = ICommDriver::ReadMode;


    // ── I2C-specific option struct (extends ICommDriver::ReadOptions) ──────────
    /**
     * @brief Extended read options carrying I2C-specific parameters.
     *
     * Pass this struct wherever a const ICommDriver::ReadOptions& is expected;
     * it is binary-compatible (derived, no virtual members, same leading fields).
     * The I2C-specific overload of tout_read() accepts this type directly.
     *
     * Field mapping from ICommDriver::ReadOptions:
     *   mode      → selects CMD_READ / CMD_WRITE_READ / CMD_SCAN (see ReadMode above)
     *   delimiter → unused for I2C (kept for interface compatibility)
     *   token     → preamble bytes for CMD_WRITE_READ (replaces write_data)
     *   use_buffer→ unused for I2C
     *
     * I2C-specific additions:
     *   slave_addr → 7-bit target address
     *   read_len   → number of bytes to clock in
     */
    struct I2CReadOptions : public ICommDriver::ReadOptions
    {
        uint8_t slave_addr = 0x00;  ///< 7-bit slave address
        size_t  read_len   = 0;     ///< Bytes to clock in (CMD_READ / CMD_WRITE_READ)
    };

    /**
     * @brief Result of a bus scan operation.
     */
    struct ScanResult
    {
        Status               status = Status::RETVAL_NOT_SET;
        std::vector<uint8_t> addresses;  ///< Found 7-bit slave addresses (1–126)
    };


    // ── Lifecycle ─────────────────────────────────────────────────────────────

    I2CBridge() = default;

    /**
     * @brief Convenience constructor: opens the device immediately.
     * @param strIdentityLabel Display text for the GUI comm-dump panel (see
     *                         describeConnection()), supplied separately —
     *                         e.g. "digispark-i2c-0".
     */
    explicit I2CBridge(uint16_t u16Vid, uint16_t u16Pid,
                       const std::string& strIdentityLabel = {})
        : m_strIdentityLabel(strIdentityLabel)
    {
        open(u16Vid, u16Pid);
    }

    virtual ~I2CBridge() override
    {
        close();
    }

    Status open (uint16_t u16Vid = I2C_DIGISPARK_VID,
                 uint16_t u16Pid = I2C_DIGISPARK_PID);
    Status close();

    /** @copydoc ICommDriver::is_open() */
    bool is_open() const override;

    /**
     * @brief Describe this connection for the GUI comm-dump panel.
     *
     * The slave address travels inside the write buffer / I2CReadOptions,
     * not through xtra_params (see tout_read()/tout_write() docs above), so
     * xtra_params is accepted here but ignored — the label reflects only the
     * static bridge identity.
     */
    CommDetails describeConnection(std::string_view /*xtra_params*/ = {}) const override
    {
        return commdump_details(CommFamily::I2C,
                                 m_strIdentityLabel.empty() ? "Digispark I2C" : m_strIdentityLabel);
    }


    // ── ICommDriver interface ─────────────────────────────────────────────────

    /**
     * @brief Unified I2C read — implements ICommDriver::tout_read().
     *
     * @param u32ReadTimeout  Timeout in milliseconds; 0 → use I2C_READ_DEFAULT_TIMEOUT
     * @param buffer          Output buffer for received bytes / scan addresses
     * @param options         ICommDriver::ReadOptions (or I2CReadOptions) selecting the operation
     * @return ReadResult     Status + bytes_read; found_terminator = !nack
     *
     * @details  See class-level documentation for the ReadMode → CMD mapping.
     *
     * When called with a plain ICommDriver::ReadOptions (not I2CReadOptions):
     *   - slave_addr is taken from options.delimiter
     *   - read_len   is taken from buffer.size()
     *   - preamble   is taken from options.token
     *
     * For full control, downcast options to I2CReadOptions or use the
     * I2C-specific overload.
     */
    ReadResult tout_read(uint32_t                        u32ReadTimeout,
                         std::span<uint8_t>              buffer,
                         const ICommDriver::ReadOptions& options,
                         std::string_view                xtra_params = {}) const override;

    /**
     * @brief I2C-specific overload providing direct access to I2CReadOptions.
     *
     * Preferred over the base overload when the caller already has I2C context.
     * Not virtual — resolves statically when the concrete type is known.
     */
    ReadResult tout_read(uint32_t              u32ReadTimeout,
                         std::span<uint8_t>    buffer,
                         const I2CReadOptions& options) const;

    /**
     * @brief Unified I2C write — implements ICommDriver::tout_write().
     *
     * @param u32WriteTimeout Timeout in milliseconds; 0 → use I2C_WRITE_DEFAULT_TIMEOUT
     * @param buffer          Write payload.  buffer[0] MUST be the 7-bit slave address;
     *                        buffer[1..N] are the data bytes (max I2C_MAX_WRITE_PAYLOAD).
     * @return WriteResult    Status + bytes_written (excludes the address byte)
     *
     * @note The slave address is prepended in the buffer to remain compatible with
     *       the ICommDriver interface, which does not have a dedicated address parameter.
     *       Use tout_write(timeout, slaveAddr, data) for a more ergonomic I2C call.
     */
    WriteResult tout_write(uint32_t                 u32WriteTimeout,
                           std::span<const uint8_t> buffer,
                           std::string_view         xtra_params = {}) const override;

    /**
     * @brief Ergonomic I2C write with an explicit slave address.
     *
     * Not virtual — resolves statically when the concrete type is known.
     *
     * @param u32WriteTimeout Timeout in milliseconds; 0 → use I2C_WRITE_DEFAULT_TIMEOUT
     * @param u8SlaveAddr     7-bit slave address
     * @param buffer          Data to write (max I2C_MAX_WRITE_PAYLOAD bytes)
     */
    WriteResult tout_write(uint32_t                 u32WriteTimeout,
                           uint8_t                  u8SlaveAddr,
                           std::span<const uint8_t> buffer) const;

    /**
     * @brief Convenience wrapper: scan the I2C bus.
     *
     * Internally routes through the ICommDriver tout_read() override with
     * ReadMode::UntilDelimiter.
     *
     * @param u32Timeout  Timeout for the whole scan (default I2C_SCAN_DEFAULT_TIMEOUT)
     * @return ScanResult containing the list of responding addresses
     */
    ScanResult scan(uint32_t u32Timeout = I2C_SCAN_DEFAULT_TIMEOUT) const;


private:

    hid_device*        m_pDevice = nullptr;  ///< hidapi device handle
    mutable std::mutex m_mutex;              ///< Protects concurrent access
    std::string        m_strIdentityLabel;   ///< GUI comm-dump display label, see describeConnection()

    // ── Firmware command codes (must match i2c_bridge.ino) ───────────────────
    static constexpr uint8_t CMD_SCAN        = 0x01;
    static constexpr uint8_t CMD_WRITE       = 0x02;
    static constexpr uint8_t CMD_READ        = 0x03;
    static constexpr uint8_t CMD_WRITE_READ  = 0x04;

    static constexpr uint8_t FW_STATUS_OK   = 0x00;
    static constexpr uint8_t FW_STATUS_NACK = 0x01;
    static constexpr uint8_t FW_STATUS_ERR  = 0xFF;

    // ── Low-level HID transport ───────────────────────────────────────────────
    Status hid_pkt_send(std::span<const uint8_t> payload) const;
    Status hid_pkt_recv(std::span<uint8_t> packet, uint32_t u32Timeout) const;

    // ── Private command implementations (called with m_mutex held) ───────────
    ReadResult  priv_cmd_read      (uint32_t u32Timeout, std::span<uint8_t> buffer,
                                    const I2CReadOptions& opts) const;
    ReadResult  priv_cmd_write_read(uint32_t u32Timeout, std::span<uint8_t> buffer,
                                    const I2CReadOptions& opts) const;
    ReadResult  priv_cmd_scan      (uint32_t u32Timeout, std::span<uint8_t> buffer) const;
    WriteResult priv_cmd_write     (uint32_t u32Timeout, uint8_t u8SlaveAddr,
                                    std::span<const uint8_t> data) const;
};


#endif // U_I2C_BRIDGE_H
