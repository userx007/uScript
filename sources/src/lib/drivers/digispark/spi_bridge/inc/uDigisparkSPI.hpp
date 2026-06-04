#ifndef U_SPI_BRIDGE_H
#define U_SPI_BRIDGE_H

#include "ICommDriver.hpp"

#include <hidapi/hidapi.h>

#include <cstdint>
#include <mutex>
#include <span>
#include <vector>


/**
 * @file  uDigisparkSPI.hpp
 * @brief Host-side SPI master driver backed by a Digispark ATtiny85 USB→SPI bridge.
 *
 * Inherits from ICommDriver and implements its unified read/write interface.
 *
 * Transport : USB HID (hidapi), 8-byte fixed-length packets
 * Firmware  : spi_bridge.ino  (TinySPI + DigiUSB)
 * VID/PID   : 0x16C0 / 0x05DF  (V-USB shared HID)
 *
 * Pin mapping (ATtiny85):
 *   MOSI → PB0 (pin 0)
 *   MISO → PB1 (pin 1)
 *   SCK  → PB2 (pin 2)
 *   CS   → hardwired or absent (single-slave only)
 *
 * Packet layout  Host → Device:
 *   [CMD][ARG0][ARG1][D0][D1][D2][D3][D4]
 *
 * Packet layout  Device → Host:
 *   [CMD][STATUS/LEN][D0][D1][D2][D3][D4][D5]
 *
 * @note  The SPI and I2C firmwares are mutually exclusive; flash only one
 *        at a time on the same Digispark.
 *
 * @note  ICommDriver::ReadOptions::mode controls how the public tout_read()
 *        dispatches:
 *          - ReadMode::Exact          → SPIReadMode::Read  (MOSI clocked as 0x00)
 *          - ReadMode::UntilToken     → SPIReadMode::Transfer (full-duplex;
 *                                       token span used as MOSI bytes)
 *          - ReadMode::UntilDelimiter → not meaningful for SPI; returns
 *                                       Status::INVALID_PARAM
 */
class SPIBridge : public ICommDriver
{

public:

    // ── Constants ─────────────────────────────────────────────────────────────
    static constexpr uint16_t  SPI_DIGISPARK_VID         = 0x16C0; ///< V-USB HID VID
    static constexpr uint16_t  SPI_DIGISPARK_PID         = 0x05DF; ///< V-USB HID PID
    static constexpr size_t    SPI_PKT_SIZE               = 8;      ///< HID report payload bytes
    static constexpr size_t    SPI_MAX_TRANSFER_PAYLOAD   = 6;      ///< Max bytes per transfer packet
    static constexpr size_t    SPI_MAX_WRITE_PAYLOAD      = 6;      ///< Max bytes per write packet
    static constexpr size_t    SPI_MAX_READ_PAYLOAD       = 6;      ///< Max bytes per read packet
    static constexpr uint32_t  SPI_READ_DEFAULT_TIMEOUT   = 2000;   ///< Default read timeout  [ms]
    static constexpr uint32_t  SPI_WRITE_DEFAULT_TIMEOUT  = 2000;   ///< Default write timeout [ms]


    // ── SPI clock modes (standard CPOL/CPHA) ──────────────────────────────────
    enum class SPIMode : uint8_t
    {
        Mode0 = 0,  ///< CPOL=0, CPHA=0 — most common (sample on rising edge)
        Mode1 = 1,  ///< CPOL=0, CPHA=1
        Mode2 = 2,  ///< CPOL=1, CPHA=0
        Mode3 = 3,  ///< CPOL=1, CPHA=1
        Mode_Last
    };

    /**
     * @brief Clock divider relative to the ATtiny85 16.5 MHz system clock.
     *
     * Approximate SCK frequencies:
     *   Div2  ≈ 8.25 MHz
     *   Div4  ≈ 4.1  MHz  (default)
     *   Div8  ≈ 2.0  MHz
     *   Div16 ≈ 1.0  MHz
     */
    enum class SPIClockDiv : uint8_t
    {
        Div2  = 0,
        Div4  = 1,
        Div8  = 2,
        Div16 = 3,
        Div_Last
    };


    // ── Internal transfer mode ────────────────────────────────────────────────
    /**
     * @brief Low-level SPI operation variant, used by private helpers.
     *
     * Public callers use ICommDriver::ReadOptions::ReadMode to select
     * behaviour; this enum is an implementation detail.
     */
    enum class SPIReadMode
    {
        Transfer,  ///< Full-duplex: send N bytes on MOSI, capture N bytes on MISO → CMD_SPI_TRANSFER
        Read,      ///< MOSI clocked as 0x00, capture N bytes on MISO             → CMD_SPI_READ
    };


    // ── SPI-specific read options (used by convenience helpers / private layer) ──
    /**
     * @brief SPI-specific options consumed by the private command layer.
     *
     * Not exposed through the ICommDriver interface; convenience helpers
     * (transfer, read_reg, write_reg) build this struct internally.
     */
    struct SPIReadOptions
    {
        SPIReadMode          mode         = SPIReadMode::Transfer;
        size_t               length       = 0;     ///< Bytes to clock
        std::vector<uint8_t> mosi_data;            ///< Bytes to send (Transfer only, max SPI_MAX_TRANSFER_PAYLOAD)
    };


    // ── Lifecycle ─────────────────────────────────────────────────────────────

    SPIBridge() = default;

    /** Convenience constructor: opens the device immediately. */
    explicit SPIBridge(uint16_t u16Vid, uint16_t u16Pid)
    {
        open(u16Vid, u16Pid);
    }

    ~SPIBridge() override
    {
        close();
    }

    Status open (uint16_t u16Vid = SPI_DIGISPARK_VID,
                 uint16_t u16Pid = SPI_DIGISPARK_PID);
    Status close();

    /**
     * @brief Check if the HID device is open and ready.
     * @return true if the device handle is valid
     */
    bool is_open() const override;


    // ── Configuration ─────────────────────────────────────────────────────────

    /**
     * @brief Send CMD_SPI_CONFIG to the firmware.
     *
     * Must be called before the first transfer if the default (Mode0, Div4)
     * does not match the target device.
     *
     * @param eMode  SPI clock polarity / phase mode
     * @param eDiv   Clock divider
     * @return Status::SUCCESS or error code
     */
    Status configure(SPIMode     eMode = SPIMode::Mode0,
                     SPIClockDiv eDiv  = SPIClockDiv::Div4);


    // ── ICommDriver interface ─────────────────────────────────────────────────

    /**
     * @brief Unified SPI read, implementing ICommDriver::tout_read.
     *
     * ICommDriver::ReadOptions::mode controls dispatch:
     *
     *   ReadMode::Exact
     *     Simple MISO read: clocks options.length (= buffer.size()) dummy
     *     bytes on MOSI (0x00) and fills buffer with the MISO response.
     *     Maps to SPIReadMode::Read / CMD_SPI_READ.
     *     options.token and options.delimiter are ignored.
     *     ReadResult::found_terminator is always false.
     *
     *   ReadMode::UntilToken
     *     Full-duplex transfer: options.token is used as the MOSI payload.
     *     buffer receives the corresponding MISO bytes.
     *     options.token.size() must equal buffer.size() and must be
     *     <= SPI_MAX_TRANSFER_PAYLOAD.
     *     Maps to SPIReadMode::Transfer / CMD_SPI_TRANSFER.
     *     ReadResult::found_terminator is always false (no framing on SPI).
     *
     *   ReadMode::UntilDelimiter
     *     Not applicable to SPI; returns Status::INVALID_PARAM immediately.
     *
     * @param u32ReadTimeout  Timeout in ms; 0 → SPI_READ_DEFAULT_TIMEOUT
     * @param buffer          Output buffer for MISO data
     * @param options         ICommDriver read configuration
     * @return ReadResult     { status, bytes_read, found_terminator=false }
     */
    ReadResult tout_read(uint32_t              u32ReadTimeout,
                         std::span<uint8_t>    buffer,
                         const ReadOptions&    options) const override;

    /**
     * @brief Unified SPI write, implementing ICommDriver::tout_write.
     *
     * Sends buffer bytes on MOSI; MISO data is discarded.
     *
     * @param u32WriteTimeout Timeout in ms; 0 → SPI_WRITE_DEFAULT_TIMEOUT
     * @param buffer          Data to clock out (max SPI_MAX_WRITE_PAYLOAD bytes)
     * @return WriteResult    { status, bytes_written }
     */
    WriteResult tout_write(uint32_t                  u32WriteTimeout,
                           std::span<const uint8_t>  buffer) const override;


    // ── Convenience helpers ───────────────────────────────────────────────────

    /**
     * @brief Full-duplex transfer: send and receive simultaneously.
     *
     * Thin wrapper around tout_read(ReadMode::UntilToken).
     *
     * @param u32Timeout  Timeout in milliseconds
     * @param mosi        Bytes to send on MOSI (max SPI_MAX_TRANSFER_PAYLOAD)
     * @param miso        Output buffer for MISO bytes (must be >= mosi.size())
     * @return Status
     */
    Status transfer(uint32_t                 u32Timeout,
                    std::span<const uint8_t> mosi,
                    std::span<uint8_t>       miso) const;

    /**
     * @brief Write a value to a register (MSB=0 write convention).
     *
     * Sends [reg & 0x7F, value] — compatible with SPI devices that use
     * the MSB of the address byte to distinguish read from write
     * (e.g. BME280, ICM-42688-P, MAX31865).
     *
     * @param u8Reg    Register address (bit7 will be cleared)
     * @param u8Value  Byte to write
     * @return Status
     */
    Status write_reg(uint8_t u8Reg, uint8_t u8Value);

    /**
     * @brief Read one or more bytes starting at a register address (MSB=1 read convention).
     *
     * Sends [reg | 0x80, 0x00 × length] and returns the MISO bytes after
     * the address byte — compatible with common SPI register-map devices.
     *
     * @param u8Reg    Register address (bit7 will be set)
     * @param buffer   Output buffer (max SPI_MAX_TRANSFER_PAYLOAD - 1 bytes)
     * @return Status
     */
    Status read_reg(uint8_t u8Reg, std::span<uint8_t> buffer);


private:

    hid_device*        m_pDevice = nullptr;  ///< hidapi device handle
    mutable std::mutex m_mutex;              ///< Protects concurrent access

    // ── Firmware command codes (must match spi_bridge.ino) ───────────────────
    static constexpr uint8_t CMD_SPI_TRANSFER = 0x10;
    static constexpr uint8_t CMD_SPI_WRITE    = 0x11;
    static constexpr uint8_t CMD_SPI_READ     = 0x12;
    static constexpr uint8_t CMD_SPI_CONFIG   = 0x13;

    static constexpr uint8_t FW_STATUS_OK  = 0x00;
    static constexpr uint8_t FW_STATUS_ERR = 0xFF;

    // ── Low-level HID transport ───────────────────────────────────────────────
    Status hid_pkt_send(std::span<const uint8_t> payload) const;
    Status hid_pkt_recv(std::span<uint8_t> packet, uint32_t u32Timeout) const;

    // ── Private command implementations (called with m_mutex held) ───────────
    ReadResult  priv_cmd_transfer(uint32_t u32Timeout, std::span<uint8_t> buffer,
                                  const SPIReadOptions& opts) const;
    ReadResult  priv_cmd_read    (uint32_t u32Timeout, std::span<uint8_t> buffer,
                                  size_t szLen) const;
    WriteResult priv_cmd_write   (uint32_t u32Timeout,
                                  std::span<const uint8_t> data) const;
};


#endif // U_SPI_BRIDGE_H
