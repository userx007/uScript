#ifndef U_CH347_JTAG_DRIVER_H
#define U_CH347_JTAG_DRIVER_H

/**
 * @file uCH347Jtag.hpp
 * @brief CH347 JTAG driver – wraps CH347Jtag_* C API behind the ICommDriver
 *        interface and exposes full JTAG TAP-state-machine control.
 *
 * Buffer layout convention
 * ========================
 * JTAG transfers are fundamentally bit-oriented and context-dependent
 * (IR vs. DR, bit vs. byte granularity, last-packet flag).  The following
 * conventions map the generic ICommDriver interface onto JTAG semantics:
 *
 * tout_write  : Write bits/bytes into DR (default) or IR depending on
 *               JtagWriteOptions embedded in the first byte of a 1-byte
 *               options.token.
 *                 options.token[0] bit7 = 0 → DR write (byte mode)
 *                 options.token[0] bit7 = 1 → IR write (byte mode)
 *               State machine: Run-Test → Shift-DR/IR → Exit DR/IR → Run-Test.
 *
 * tout_read   : ReadMode::Exact – Read bits/bytes from DR or IR.
 *               options.token[0] encoding same as tout_write.
 *
 * For finer control (bit-bang, split packets, TMS sequences, TAP state
 * switching, fast bulk JTAG) use the non-virtual extended API below.
 *
 * Clock rate
 * ==========
 * iClockRate 0–5, higher = faster.  Actual frequency is hardware-dependent.
 */

#include "ICommDriver.hpp"
#include "ch347_lib.h"

#include <string>
#include <span>
#include <vector>
#include <cstdint>

// ---------------------------------------------------------------------------
// JTAG-specific enumerations
// ---------------------------------------------------------------------------

/** Selects IR or DR register for a transfer. */
enum class JtagRegister : uint8_t {
    IR = 0, /**< Instruction Register */
    DR = 1, /**< Data Register        */
};

/** Encoding used in options.token[0] for tout_read / tout_write. */
constexpr uint8_t JTAG_TOKEN_IR_FLAG = 0x80; /**< Set this bit for IR; clear for DR */

// ---------------------------------------------------------------------------

class CH347JTAG : public ICommDriver
{
public:
    // -----------------------------------------------------------------------
    // Constants
    // -----------------------------------------------------------------------
    static constexpr uint32_t JTAG_READ_DEFAULT_TIMEOUT  = 5000; /**< ms */
    static constexpr uint32_t JTAG_WRITE_DEFAULT_TIMEOUT = 5000; /**< ms */
    static constexpr uint8_t  JTAG_MAX_CLOCK_RATE        = 5;

    // -----------------------------------------------------------------------
    // Construction / destruction
    // -----------------------------------------------------------------------

    CH347JTAG() = default;

    /**
     * @brief Construct and immediately open the JTAG interface.
     *
     * @param strDevice   Device path, e.g. "/dev/ch34xpis0"
     * @param iClockRate  0 (slowest) – 5 (fastest)
     */
    explicit CH347JTAG(const std::string& strDevice,
                       uint8_t            iClockRate = 2)
        : m_iHandle(-1)
    {
        open(strDevice, iClockRate);
    }

    virtual ~CH347JTAG() { close(); }

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    Status open(const std::string& strDevice, uint8_t iClockRate = 2);
    Status close();
    bool   is_open() const override;

    // -----------------------------------------------------------------------
    // Configuration
    // -----------------------------------------------------------------------

    /** Get current JTAG clock-rate setting. */
    Status get_clock_rate(uint8_t& iClockRate) const;

    // -----------------------------------------------------------------------
    // ICommDriver interface
    // -----------------------------------------------------------------------

    /**
     * @brief Read bytes from JTAG IR or DR register.
     *
     * State machine: Run-Test → Shift-IR/DR → Exit IR/DR → Run-Test.
     *
     * @param u32ReadTimeout  Timeout hint in ms.
     * @param buffer          Destination; reads buffer.size() bytes.
     * @param options         ReadMode::Exact only.
     *                        options.token must be a 1-byte span:
     *                          byte[0] & JTAG_TOKEN_IR_FLAG → IR read
     *                          byte[0] & ~JTAG_TOKEN_IR_FLAG → DR read
     *                        Leave token empty to default to DR read.
     * @return ReadResult { status, bytesRead, false }
     *
     * @note ReadMode::UntilDelimiter / UntilToken → { Status::NotSupported, 0, false }
     */
    ReadResult tout_read(uint32_t u32ReadTimeout,
                         std::span<uint8_t>  buffer,
                         const ReadOptions&  options) const override;

    /**
     * @brief Write bytes to JTAG IR or DR register.
     *
     * State machine: Run-Test → Shift-IR/DR → Exit IR/DR → Run-Test.
     *
     * @param u32WriteTimeout Timeout hint in ms.
     * @param buffer          Data to shift in.
     *                        The target register (IR/DR) is selected by the
     *                        last-set options passed to tout_read, or can be
     *                        overridden by calling write_register() directly.
     * @return WriteResult { status, bytesWritten }
     *
     * @note For explicit IR vs DR selection use write_register() below.
     */
    WriteResult tout_write(uint32_t u32WriteTimeout,
                           std::span<const uint8_t> buffer) const override;

    // -----------------------------------------------------------------------
    // Extended JTAG helpers (non-virtual)
    // -----------------------------------------------------------------------

    /** Perform a TAP logic reset (≥6 TCK cycles with TMS high). */
    Status tap_reset() const;

    /** Hard-reset the JTAG device via TRST pin. */
    Status tap_reset_trst(bool highLevel) const;

    /** Drive TMS to reach the specified TAP state. */
    Status tap_set_state(uint8_t tapState) const;

    /**
     * @brief Shift a TMS sequence to navigate the state machine.
     * @param tmsBytes  TMS byte array (LSB-first bit stream)
     * @param step      Number of valid bits in tmsBytes
     * @param skip      Starting bit offset within tmsBytes
     */
    Status tap_tms_change(std::span<const uint8_t> tmsBytes,
                          uint32_t step, uint32_t skip) const;

    /**
     * @brief Write to IR or DR register (byte granularity).
     * State machine: Run-Test → Shift-IR/DR → Exit IR/DR → Run-Test.
     */
    Status write_register(JtagRegister    reg,
                          std::span<const uint8_t> buffer) const;

    /**
     * @brief Read from IR or DR register (byte granularity).
     * State machine: Run-Test → Shift-IR/DR → Exit IR/DR → Run-Test.
     */
    Status read_register(JtagRegister       reg,
                         std::span<uint8_t> buffer) const;

    /**
     * @brief Combined write-then-read (bitband mode, for small payloads).
     *
     * State machine: Run-Test → Shift-IR/DR → Exit IR/DR → Run-Test.
     *
     * @param reg         IR or DR
     * @param writeBuf    Bits to write
     * @param readBuf     Buffer receiving read bits; size = expected read bits
     * @return ReadResult { status, bitsRead, false }
     */
    ReadResult write_read(JtagRegister              reg,
                          std::span<const uint8_t>  writeBuf,
                          std::span<uint8_t>        readBuf) const;

    /**
     * @brief Fast bulk write-then-read (optimised for firmware download).
     *
     * Hardware buffer is 4 KB; combined write+read must not exceed 4096 bytes.
     *
     * @param reg         IR or DR
     * @param writeBuf    Bytes to write
     * @param readBuf     Buffer receiving read bytes; size = expected bytes
     * @return ReadResult { status, bytesRead, false }
     */
    ReadResult write_read_fast(JtagRegister              reg,
                               std::span<const uint8_t>  writeBuf,
                               std::span<uint8_t>        readBuf) const;

    /**
     * @brief Bitband-mode read/write staying in Shift-DR/IR across calls.
     *
     * Use when a logical transfer spans multiple USB packets.
     * Set isLastPacket=true on the final call to exit Shift state.
     *
     * @param dataBuffer   In/out bit buffer
     * @param dataBitsNb   Number of bits to transfer
     * @param isRead       true = capture MISO bits into dataBuffer
     * @param isLastPacket true = exit to Exit-DR/IR after this packet
     */
    Status io_scan(std::span<uint8_t> dataBuffer,
                   uint32_t           dataBitsNb,
                   bool               isRead,
                   bool               isLastPacket) const;

    /**
     * @brief Build a bit-bang protocol packet with TMS clock changes.
     *        (Thin wrapper around CH347Jtag_ClockTms / IdleClock.)
     *
     * @param pkt    Working packet buffer (caller-allocated, ≥ packet size)
     * @param tms    TMS value to clock
     * @param bi     Current byte index within pkt
     * @return New byte index after appending the TMS entry
     */
    static uint32_t build_tms_clock(std::span<uint8_t> pkt,
                                    uint32_t           tms,
                                    uint32_t           bi);

    /** Append an idle (TCK low) entry to a bit-bang packet. */
    static uint32_t build_idle_clock(std::span<uint8_t> pkt, uint32_t bi);

private:
    int     m_iHandle     = -1;
    /** Last target register used by tout_write (DR by default). */
    mutable JtagRegister m_lastReg = JtagRegister::DR;
};

#endif // U_CH347_JTAG_DRIVER_H
