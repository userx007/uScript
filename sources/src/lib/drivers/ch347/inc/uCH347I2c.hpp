#ifndef U_CH347_I2C_DRIVER_H
#define U_CH347_I2C_DRIVER_H

/**
 * @file uCH347I2c.hpp
 * @brief CH347 I2C driver – wraps CH347StreamI2C / CH347I2C_* C API behind
 *        the ICommDriver interface.
 *
 * Buffer layout convention
 * ========================
 * The CH347 I2C host API (CH347StreamI2C) performs a combined
 * START / Write / Repeated-START / Read / STOP sequence in one call,
 * mirroring the common "register-read" pattern used by almost all I2C
 * peripheral devices.
 *
 * tout_write  : Pure write transaction.
 *               buffer[0]        = (7-bit device address << 1) | 0  (WRITE bit)
 *               buffer[1..n]     = register address + payload bytes
 *
 * tout_read   : Combined write-then-read transaction (ReadMode::Exact only).
 *               buffer[0]        = (7-bit device address << 1) | 0  (WRITE bit)
 *               buffer[1..wLen-1]= register / command bytes to write
 *               On return buffer is REPLACED with the raw read bytes.
 *
 *               The split between write and read lengths is conveyed via
 *               I2cReadOptions::writeLen.  If writeLen == 0 the driver
 *               performs a read-only transaction using the device address
 *               stored in I2cReadOptions::devAddr.
 *
 * ReadMode::UntilDelimiter and ReadMode::UntilToken are not meaningful for
 * I2C and return Status::NotSupported.
 *
 * EEPROM helpers
 * ==============
 * Convenience wrappers for CH347ReadEEPROM / CH347WriteEEPROM are provided
 * as non-virtual methods (read_eeprom / write_eeprom).
 */

#include "ICommDriver.hpp"
#include "ch347_lib.h"

#include <string>
#include <span>

// ---------------------------------------------------------------------------
// I2C bus speed presets
// ---------------------------------------------------------------------------

enum class I2cSpeed : int {
    Low      = 0, /**<  20 kHz  */
    Standard = 1, /**< 100 kHz  */
    Fast     = 2, /**< 400 kHz  */
    High     = 3, /**< 750 kHz  */
    Std50    = 4, /**<  50 kHz  */
    Std200   = 5, /**< 200 kHz  */
    Fast1M   = 6, /**<   1 MHz  */
};

// ---------------------------------------------------------------------------
// Per-transaction I2C options
// ---------------------------------------------------------------------------

/**
 * @brief Options controlling a single I2C read transaction.
 *
 * Pack into ReadOptions::token as a single-byte span where:
 *   byte[0] = 7-bit device address (used only when writeLen == 0)
 *
 * For combined write-then-read pass the write portion at the front of
 * the buffer and set writeLen accordingly.
 *
 * @note Prefer using tout_read_i2c() which accepts this struct directly.
 */
struct I2cReadOptions {
    uint8_t  devAddr  = 0x00; /**< 7-bit I2C device address (un-shifted) */
    uint16_t writeLen = 0;    /**< Bytes at the front of buffer to write before reading */
};

// ---------------------------------------------------------------------------

class CH347I2C : public ICommDriver
{
public:
    // -----------------------------------------------------------------------
    // Constants
    // -----------------------------------------------------------------------
    static constexpr uint32_t I2C_READ_DEFAULT_TIMEOUT  = 5000; /**< ms */
    static constexpr uint32_t I2C_WRITE_DEFAULT_TIMEOUT = 5000; /**< ms */

    // -----------------------------------------------------------------------
    // Construction / destruction
    // -----------------------------------------------------------------------

    CH347I2C() = default;

    /**
     * @brief Construct and immediately open a CH347 I2C device.
     *
     * @param strDevice  Device path, e.g. "/dev/ch34xpis0"
     * @param speed      Initial bus speed
     */
    explicit CH347I2C(const std::string& strDevice,
                      I2cSpeed           speed = I2cSpeed::Fast)
        : m_iHandle(-1)
    {
        open(strDevice, speed);
    }

    virtual ~CH347I2C() { close(); }

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    Status open(const std::string& strDevice, I2cSpeed speed = I2cSpeed::Fast);
    Status close();
    bool   is_open() const override;

    // -----------------------------------------------------------------------
    // Configuration helpers (callable after open)
    // -----------------------------------------------------------------------

    /** Change I2C bus speed (no need to re-open). */
    Status set_speed(I2cSpeed speed);

    /**
     * @brief Enable / disable I2C clock stretching.
     * @param enable  true = slave may hold SCL low to pause the master
     */
    Status set_clock_stretch(bool enable);

    /**
     * @brief Set signal drive mode.
     * @param mode  0 = open-drain (standard), 1 = push-pull
     */
    Status set_drive_mode(uint8_t mode);

    /**
     * @brief Control whether the master continues after a NACK.
     * @param mode  0 = stop on NACK, 1 = continue on NACK
     */
    Status set_ignore_nack(uint8_t mode);

    /**
     * @brief Insert a millisecond-level delay between I2C transactions.
     * @param iDelay  0–500 ms
     */
    Status set_inter_transaction_delay_ms(int iDelay);

    /**
     * @brief Fine-tune the delay between the 8th and 9th (ACK) clock edge.
     * @param iDelayUs  0–0x3FF microseconds
     */
    Status set_ack_clock_delay_us(int iDelayUs);

    // -----------------------------------------------------------------------
    // ICommDriver interface
    // -----------------------------------------------------------------------

    /**
     * @brief Combined-write-then-read I2C transaction.
     *
     * @param u32ReadTimeout  Timeout hint in ms (passed to CH34xSetTimeout
     *                        if non-zero and different from the current value).
     * @param buffer          Layout:
     *                          [0..writeLen-1] bytes to send  (write phase)
     *                          On return, [0..readLen-1] holds received bytes
     *                          where readLen = buffer.size() - writeLen.
     *                          If I2cReadOptions::writeLen == 0 the whole
     *                          buffer is used as the read destination.
     * @param options         ReadMode::Exact required.
     *                        options.token must be a 1-byte span whose single
     *                        byte is the 7-bit device address (un-shifted).
     *                        Alternatively use tout_read_i2c() below.
     * @return ReadResult { status, readBytesReceived, false }
     *
     * @note ReadMode::UntilDelimiter / UntilToken → { Status::NotSupported, 0, false }
     */
    ReadResult tout_read(uint32_t u32ReadTimeout,
                         std::span<uint8_t>  buffer,
                         const ReadOptions&  options) const override;

    /**
     * @brief Pure-write I2C transaction.
     *
     * @param u32WriteTimeout Timeout hint in ms.
     * @param buffer          buffer[0] = (devAddr << 1) | 0  (WRITE bit included)
     *                        buffer[1..] = register address + payload
     * @return WriteResult { status, bytesWritten }
     */
    WriteResult tout_write(uint32_t u32WriteTimeout,
                           std::span<const uint8_t> buffer) const override;

    // -----------------------------------------------------------------------
    // Extended helpers (I2C-specific, not part of ICommDriver)
    // -----------------------------------------------------------------------

    /**
     * @brief Combined write-then-read with explicit options struct.
     *
     * @param buffer     Write bytes followed by (overwritten) read bytes
     * @param opts       Device address and write/read split
     * @param retAck     If non-null, receives the number of ACKs seen
     * @return ReadResult { status, readBytesReceived, false }
     */
    ReadResult tout_read_i2c(std::span<uint8_t>      buffer,
                             const I2cReadOptions&   opts,
                             int*                    retAck = nullptr) const;

    // -----------------------------------------------------------------------
    // EEPROM helpers
    // -----------------------------------------------------------------------

    /**
     * @brief Read bytes from an I2C EEPROM connected to the CH347.
     *
     * @param eepromType  One of the EEPROM_TYPE enum values (ID_24C01 … ID_24C4096)
     * @param iAddr       Start address within the EEPROM
     * @param buffer      Destination buffer; reads buffer.size() bytes
     * @return Status
     */
    Status read_eeprom(EEPROM_TYPE         eepromType,
                       int                 iAddr,
                       std::span<uint8_t>  buffer) const;

    /**
     * @brief Write bytes to an I2C EEPROM connected to the CH347.
     *
     * @param eepromType  One of the EEPROM_TYPE enum values
     * @param iAddr       Start address within the EEPROM
     * @param buffer      Source data; writes buffer.size() bytes
     * @return Status
     */
    Status write_eeprom(EEPROM_TYPE              eepromType,
                        int                      iAddr,
                        std::span<const uint8_t> buffer) const;

private:
    int m_iHandle = -1;
};

#endif // U_CH347_I2C_DRIVER_H
