#ifndef U_I2C_BRIDGE_H
#define U_I2C_BRIDGE_H

#include <hidapi/hidapi.h>

#include <cstdint>
#include <mutex>
#include <span>
#include <vector>


/**
 * @file  uI2C.hpp
 * @brief Host-side I2C master driver backed by a Digispark ATtiny85 USB→I2C bridge.
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
 */
class I2CBridge
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


    // ── Status codes ──────────────────────────────────────────────────────────
    enum class Status : int32_t
    {
        SUCCESS         =  0,
        INVALID_PARAM   = -1,  ///< Null buffer, length out of range, …
        PORT_ACCESS     = -2,  ///< HID open / write failed
        READ_TIMEOUT    = -3,  ///< hid_read_timeout expired
        READ_ERROR      = -4,  ///< hid_read returned -1
        WRITE_ERROR     = -5,  ///< hid_write returned -1
        NACK            = -6,  ///< Slave did not acknowledge
        BUFFER_OVERFLOW = -7,  ///< Caller buffer too small
        RETVAL_NOT_SET  = -99,
    };


    // ── Read modes ────────────────────────────────────────────────────────────
    enum class I2CReadMode
    {
        Read,       ///< Read N bytes from slave → CMD_READ
        WriteRead,  ///< Write preamble then repeated-START read → CMD_WRITE_READ
        Scan,       ///< Probe all 7-bit addresses → CMD_SCAN
    };


    // ── Option / result structs ───────────────────────────────────────────────

    /**
     * @brief Options forwarded to tout_read().
     */
    struct I2CReadOptions
    {
        I2CReadMode          mode       = I2CReadMode::Read;
        uint8_t              slave_addr = 0x00;  ///< 7-bit slave address
        size_t               read_len   = 0;     ///< Bytes to clock in
        std::vector<uint8_t> write_data;         ///< Preamble (WriteRead only, max I2C_MAX_WRITE_READ_WLEN)
    };

    /**
     * @brief Result returned by tout_read().
     *
     * For I2CReadMode::Scan, bytes_read is the count of responding addresses,
     * and the addresses themselves are placed in the caller's buffer.
     */
    struct ReadResult
    {
        Status  status     = Status::RETVAL_NOT_SET;
        size_t  bytes_read = 0;
        bool    nack       = false;
    };

    /**
     * @brief Result returned by tout_write().
     */
    struct WriteResult
    {
        Status  status        = Status::RETVAL_NOT_SET;
        size_t  bytes_written = 0;
        bool    nack          = false;
    };

    /**
     * @brief Result returned by scan().
     */
    struct ScanResult
    {
        Status               status = Status::RETVAL_NOT_SET;
        std::vector<uint8_t> addresses;  ///< Found 7-bit slave addresses (1–126)
    };


    // ── Lifecycle ─────────────────────────────────────────────────────────────

    I2CBridge() = default;

    /** Convenience constructor: opens the device immediately. */
    explicit I2CBridge(uint16_t u16Vid, uint16_t u16Pid)
    {
        open(u16Vid, u16Pid);
    }

    virtual ~I2CBridge()
    {
        close();
    }

    Status open (uint16_t u16Vid = I2C_DIGISPARK_VID,
                 uint16_t u16Pid = I2C_DIGISPARK_PID);
    Status close();
    bool   is_open() const;


    // ── Unified public interface ───────────────────────────────────────────────

    /**
     * @brief Unified I2C read interface (mirrors UART::tout_read style).
     *
     * @param u32ReadTimeout  Timeout in milliseconds; 0 → use I2C_READ_DEFAULT_TIMEOUT
     * @param buffer          Output buffer for received bytes / scan addresses
     * @param options         Operation configuration
     * @return ReadResult     Contains status, bytes_read and nack flag
     *
     * @details
     * - I2CReadMode::Read
     *     Sends CMD_READ to firmware; fills buffer with options.read_len bytes
     *     from options.slave_addr.
     *
     * - I2CReadMode::WriteRead
     *     Sends CMD_WRITE_READ; issues options.write_data then a repeated-START
     *     read of options.read_len bytes.  Equivalent to a register-read sequence.
     *
     * - I2CReadMode::Scan
     *     Sends CMD_SCAN; probes all 127 addresses and fills buffer with the
     *     addresses that ACKed.  bytes_read = number of devices found.
     *     buffer must be at least 127 bytes to hold every possible address.
     */
    ReadResult tout_read(uint32_t               u32ReadTimeout,
                         std::span<uint8_t>     buffer,
                         const I2CReadOptions&  options) const;

    /**
     * @brief Unified I2C write interface (mirrors UART::tout_write style).
     *
     * @param u32WriteTimeout Timeout in milliseconds; 0 → use I2C_WRITE_DEFAULT_TIMEOUT
     * @param u8SlaveAddr     7-bit slave address
     * @param buffer          Data to write (max I2C_MAX_WRITE_PAYLOAD bytes)
     * @return WriteResult    Contains status, bytes_written and nack flag
     */
    WriteResult tout_write(uint32_t                  u32WriteTimeout,
                           uint8_t                   u8SlaveAddr,
                           std::span<const uint8_t>  buffer) const;

    /**
     * @brief Convenience wrapper: scan the I2C bus.
     *
     * @param u32Timeout  Timeout for the whole scan operation (default I2C_SCAN_DEFAULT_TIMEOUT)
     * @return ScanResult containing the list of responding addresses
     */
    ScanResult scan(uint32_t u32Timeout = I2C_SCAN_DEFAULT_TIMEOUT) const;


private:

    hid_device*        m_pDevice = nullptr;  ///< hidapi device handle
    mutable std::mutex m_mutex;              ///< Protects concurrent access

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
