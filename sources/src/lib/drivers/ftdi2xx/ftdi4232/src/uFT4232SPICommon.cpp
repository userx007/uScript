// FT4232Base.hpp is the source of all MPSSE_* opcode constants
// (MPSSE_SET_BITS_LOW, MPSSE_SET_CLK_DIV, MPSSE_SPI_WRITE_NRE, etc.).
// They are defined as protected static constexpr members of FT4232Base
// and are accessible here through FT4232SPI's inheritance chain.
// Included explicitly so this file's dependencies are self-documenting.
#include "FT4232Base.hpp"
#include "uFT4232SPI.hpp"
#include "uLogger.hpp"

#include <algorithm>
#include <vector>

#define LT_HDR  "FT4232_SPI  |"
#define LOG_HDR LOG_STRING(LT_HDR)


// ============================================================================
// MPSSE SPI primer
// ============================================================================
//
// Unlike I²C, SPI uses the MPSSE native byte-shift engine rather than
// SET_BITS_LOW bit-bang. Each transfer is a single MPSSE command:
//
//   { cmd, lenLow, lenHigh [, txData...] }
//
//   len = number_of_bytes - 1  (0x0000 = 1 byte, 0xFFFF = 65536 bytes)
//
// Write-only:   { cmd, lenL, lenH, data[0..N] }            → no response bytes
// Read-only:    { cmd, lenL, lenH }                        → N+1 response bytes
// Full-duplex:  { cmd, lenL, lenH, data[0..N] }            → N+1 response bytes
//
// CS is managed manually via SET_BITS_LOW (0x80) before and after each
// transfer.  All ADBUS pin state changes go through apply_pin_state()
// which tracks the current value and direction masks.
//
// Command selection by SPI mode (CPOL/CPHA):
//
//   Modes 0 & 3  — active clock edge is the FIRST edge after idle:
//     write : 0x11  (shift out on -ve edge → valid before first rising edge)
//     read  : 0x20  (latch in  on +ve edge)
//     xfer  : 0x31  (both)
//
//   Modes 1 & 2  — active clock edge is the SECOND edge:
//     write : 0x10  (shift out on +ve edge)
//     read  : 0x24  (latch in  on -ve edge)
//     xfer  : 0x34  (both)
//
//   CPOL=1 (Modes 2 & 3) only changes the CLK idle level stored in
//   m_pinValue; the shift commands themselves are the same as for the
//   equivalent CPHA group.
//
// Bit order:
//   MSB first → command base unchanged (bit 1 = 0)
//   LSB first → add 0x02 to any shift command (bit 1 = 1)


// ============================================================================
// open / close
// ============================================================================

FT4232SPI::Status FT4232SPI::open(const SpiConfig& config, uint8_t u8DeviceIndex)
{
    if (config.clockHz == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("open: clockHz must be > 0"));
        return Status::INVALID_PARAM;
    }

    Status s = open_device(config.channel, u8DeviceIndex);
    if (s != Status::SUCCESS) {
        return s;
    }

    s = configure_mpsse_spi(config);
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("MPSSE SPI init failed, clock="); LOG_UINT32(config.clockHz));
        FT4232Base::close();
        return s;
    }

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("FT4232H SPI opened: ch=");
              LOG_UINT32(static_cast<uint8_t>(config.channel));
              LOG_STRING("idx="); LOG_UINT32(u8DeviceIndex);
              LOG_STRING("clock="); LOG_UINT32(config.clockHz);
              LOG_STRING("mode="); LOG_UINT32(static_cast<uint8_t>(config.mode)));

    return Status::SUCCESS;
}


FT4232SPI::Status FT4232SPI::close()
{
    if (is_open()) {
        // Best-effort deassert CS to leave the bus in a clean idle state
        (void)cs_deassert();
    }
    return FT4232Base::close();
}


// ============================================================================
// PUBLIC UNIFIED INTERFACE  (ICommDriver)
// ============================================================================

FT4232SPI::WriteResult FT4232SPI::tout_write(uint32_t u32WriteTimeout,
                                              std::span<const uint8_t> buffer) const
{
    WriteResult result;
    (void)u32WriteTimeout; // write is synchronous at the MPSSE level

    if (!is_open()) {
        result.status = Status::PORT_ACCESS;
        return result;
    }
    if (buffer.empty()) {
        result.status = Status::INVALID_PARAM;
        return result;
    }

    // CS assert → write → CS deassert
    result.status = cs_assert();
    if (result.status != Status::SUCCESS) return result;

    size_t bytesWritten = 0;
    result.status = spi_write_raw(buffer, bytesWritten);
    result.bytes_written = bytesWritten;

    // Always deassert CS, even on error
    Status csStatus = cs_deassert();
    if (result.status == Status::SUCCESS) {
        result.status = csStatus;
    }

    return result;
}


FT4232SPI::ReadResult FT4232SPI::tout_read(uint32_t u32ReadTimeout,
                                            std::span<uint8_t> buffer,
                                            const ReadOptions& options) const
{
    ReadResult result;

    if (!is_open()) {
        result.status = Status::PORT_ACCESS;
        return result;
    }

    uint32_t timeout = (u32ReadTimeout == 0) ? FT4232_READ_DEFAULT_TIMEOUT : u32ReadTimeout;

    switch (options.mode)
    {
        // ------------------------------------------------------------------
        case ReadMode::Exact:
        {
            result.status = cs_assert();
            if (result.status != Status::SUCCESS) return result;

            size_t bytesRead = 0;
            result.status = spi_read_raw(buffer, bytesRead, timeout);
            result.bytes_read = bytesRead;
            result.found_terminator = false;

            Status csStatus = cs_deassert();
            if (result.status == Status::SUCCESS) result.status = csStatus;
            break;
        }

        // ------------------------------------------------------------------
        case ReadMode::UntilDelimiter:
        {
            if (buffer.size() < 2) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("Buffer too small for delimiter + null terminator"));
                result.status = Status::INVALID_PARAM;
                break;
            }

            result.status = cs_assert();
            if (result.status != Status::SUCCESS) return result;

            size_t pos    = 0;
            result.status = Status::READ_TIMEOUT;

            while (pos < buffer.size() - 1) {
                uint8_t byte = 0;
                size_t  got  = 0;
                Status  s    = spi_read_raw(std::span<uint8_t>(&byte, 1), got, timeout);

                if (s != Status::SUCCESS || got == 0) {
                    result.status = s;
                    break;
                }

                if (byte == options.delimiter) {
                    buffer[pos]             = '\0';
                    result.found_terminator = true;
                    result.status           = Status::SUCCESS;
                    break;
                }

                buffer[pos++] = byte;
            }

            if (pos == buffer.size() - 1 && result.status == Status::READ_TIMEOUT) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Buffer full before delimiter found"));
                result.status = Status::BUFFER_OVERFLOW;
            }

            result.bytes_read = pos;

            Status csStatus = cs_deassert();
            if (result.status == Status::SUCCESS) result.status = csStatus;
            break;
        }

        // ------------------------------------------------------------------
        case ReadMode::UntilToken:
        {
            if (options.token.empty()) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Empty token"));
                result.status = Status::INVALID_PARAM;
                break;
            }

            result.status = cs_assert();
            if (result.status != Status::SUCCESS) return result;

            const auto& token = options.token;

            // Build KMP failure table
            std::vector<int> lps(token.size(), 0);
            for (size_t i = 1, len = 0; i < token.size(); ) {
                if (token[i] == token[len]) {
                    lps[i++] = static_cast<int>(++len);
                } else if (len != 0) {
                    len = static_cast<size_t>(lps[len - 1]);
                } else {
                    lps[i++] = 0;
                }
            }

            size_t matched = 0;
            result.status  = Status::READ_TIMEOUT;

            while (true) {
                uint8_t byte = 0;
                size_t  got  = 0;
                Status  s    = spi_read_raw(std::span<uint8_t>(&byte, 1), got, timeout);

                if (s != Status::SUCCESS || got == 0) {
                    result.status = s;
                    break;
                }

                while (matched > 0 && byte != token[matched]) {
                    matched = static_cast<size_t>(lps[matched - 1]);
                }
                if (byte == token[matched]) {
                    ++matched;
                }
                if (matched == token.size()) {
                    result.found_terminator = true;
                    result.status           = Status::SUCCESS;
                    break;
                }
            }

            result.bytes_read = 0;

            Status csStatus = cs_deassert();
            if (result.status == Status::SUCCESS) result.status = csStatus;
            break;
        }

        default:
            result.status = Status::INVALID_PARAM;
            break;
    }

    return result;
}


FT4232SPI::TransferResult FT4232SPI::spi_transfer(std::span<const uint8_t> txBuf,
                                                   std::span<uint8_t>       rxBuf,
                                                   uint32_t u32TimeoutMs) const
{
    TransferResult result;

    if (!is_open()) {
        result.status = Status::PORT_ACCESS;
        return result;
    }
    if (txBuf.size() != rxBuf.size()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("spi_transfer: txBuf and rxBuf must be the same size"));
        result.status = Status::INVALID_PARAM;
        return result;
    }
    if (txBuf.empty()) {
        result.status = Status::INVALID_PARAM;
        return result;
    }

    uint32_t timeout = (u32TimeoutMs == 0) ? FT4232_READ_DEFAULT_TIMEOUT : u32TimeoutMs;

    result.status = cs_assert();
    if (result.status != Status::SUCCESS) return result;

    size_t bytesXferd = 0;
    result.status = spi_xfer_raw(txBuf, rxBuf, bytesXferd, timeout);
    result.bytes_xfered = bytesXferd;

    Status csStatus = cs_deassert();
    if (result.status == Status::SUCCESS) result.status = csStatus;

    return result;
}


// ============================================================================
// MPSSE CONFIGURATION
// ============================================================================

FT4232SPI::Status FT4232SPI::configure_mpsse_spi(const SpiConfig& config)
{
    m_config = config;

    // ── Resolve MPSSE shift commands from mode ────────────────────────────────
    //
    // Modes 0 & 3: active edge is the FIRST clock edge after idle
    //   → data clocked out on -ve, latched in on +ve
    // Modes 1 & 2: active edge is the SECOND clock edge
    //   → data clocked out on +ve, latched in on -ve

    const bool firstEdgeActive = (config.mode == SpiMode::Mode0 ||
                                  config.mode == SpiMode::Mode3);

    m_cmdWrite = firstEdgeActive ? MPSSE_SPI_WRITE_NRE : MPSSE_SPI_WRITE_PRE;
    m_cmdRead  = firstEdgeActive ? MPSSE_SPI_READ_PRE  : MPSSE_SPI_READ_NRE;
    m_cmdXfer  = firstEdgeActive ? MPSSE_SPI_XFER_NRE  : MPSSE_SPI_XFER_PRE;

    // LSB-first: add 0x02 to each command (bit 1 = LSB first)
    if (config.bitOrder == BitOrder::LsbFirst) {
        m_cmdWrite = static_cast<uint8_t>(m_cmdWrite | 0x02u);
        m_cmdRead  = static_cast<uint8_t>(m_cmdRead  | 0x02u);
        m_cmdXfer  = static_cast<uint8_t>(m_cmdXfer  | 0x02u);
    }

    // ── Determine ADBUS idle state ───────────────────────────────────────────
    //
    // CLK idle level = CPOL:
    //   CPOL=0 (Modes 0/1) → CLK idle LOW  → bit 0 of m_pinValue = 0
    //   CPOL=1 (Modes 2/3) → CLK idle HIGH → bit 0 of m_pinValue = 1
    //
    // CS idle level:
    //   Active-low  (default) → CS idle HIGH → csPin bit = 1 in m_pinValue
    //   Active-high           → CS idle LOW  → csPin bit = 0 in m_pinValue

    const bool cpolHigh = (config.mode == SpiMode::Mode2 ||
                           config.mode == SpiMode::Mode3);

    m_pinValue = 0x00u;
    if (cpolHigh) {
        m_pinValue |= 0x01u;                         // CLK idle HIGH
    }
    if (config.csPolarity == CsPolarity::ActiveLow) {
        m_pinValue |= config.csPin;                  // CS idle HIGH (deasserted)
    }

    // ── ADBUS direction mask ──────────────────────────────────────────────────
    // SCK=out(0), MOSI=out(1), MISO=in(2), CS=out — everything else = input
    m_pinDir = static_cast<uint8_t>(0x03u | config.csPin); // SCK + MOSI + CS = outputs

    // ── Clock divisor ────────────────────────────────────────────────────────
    // SCK = 60 MHz / ((1 + divisor) × 2)
    // divisor = (30,000,000 / clockHz) - 1, clamped to [0, 0xFFFF]
    uint32_t divisor = (config.clockHz > 0 && config.clockHz < 30000000u)
                       ? (30000000u / config.clockHz) - 1u
                       : 0u;
    divisor = std::min(divisor, static_cast<uint32_t>(0xFFFFu));

    const uint8_t divLow  = static_cast<uint8_t>( divisor       & 0xFFu);
    const uint8_t divHigh = static_cast<uint8_t>((divisor >> 8) & 0xFFu);

    // ── MPSSE synchronisation ─────────────────────────────────────────────────
    // Send a deliberately bad opcode; the MPSSE echoes 0xFA + bad_byte.
    // This flushes any leftover state from a previous session.
    {
        const uint8_t sync[1] = { 0xAA };
        (void)mpsse_write(sync, 1);
        uint8_t echo[2] = { 0 };
        size_t  got     = 0;
        (void)mpsse_read(echo, 2, 200, got);
    }

    // ── Build initialisation sequence ────────────────────────────────────────
    std::vector<uint8_t> init;
    init.reserve(20);

    init.push_back(MPSSE_DIS_DIV5);      // 60 MHz base clock
    init.push_back(MPSSE_DIS_ADAPTIVE);  // No adaptive clocking
    init.push_back(MPSSE_DIS_3PHASE);    // No 3-phase clocking (SPI uses 2-phase)
    init.push_back(MPSSE_LOOPBACK_OFF);  // No internal loopback

    // Set clock divisor
    init.push_back(MPSSE_SET_CLK_DIV);
    init.push_back(divLow);
    init.push_back(divHigh);

    // Set initial pin state: CLK at CPOL idle, CS deasserted, MOSI=0
    init.push_back(MPSSE_SET_BITS_LOW);
    init.push_back(m_pinValue);
    init.push_back(m_pinDir);

    init.push_back(MPSSE_SEND_IMMEDIATE);

    Status s = mpsse_write(init.data(), init.size());
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("configure_mpsse_spi: init sequence failed"));
    }

    return s;
}


// ============================================================================
// CS MANAGEMENT
// ============================================================================

FT4232SPI::Status FT4232SPI::apply_pin_state(bool csActive) const
{
    // Start from the stored idle pin value then flip the CS pin
    uint8_t val = m_pinValue;

    if (csActive) {
        if (m_config.csPolarity == CsPolarity::ActiveLow) {
            val &= static_cast<uint8_t>(~m_config.csPin); // drive CS low
        } else {
            val |= m_config.csPin;                         // drive CS high
        }
    } else {
        if (m_config.csPolarity == CsPolarity::ActiveLow) {
            val |= m_config.csPin;                         // release CS high
        } else {
            val &= static_cast<uint8_t>(~m_config.csPin); // release CS low
        }
    }

    const uint8_t cmd[3] = { MPSSE_SET_BITS_LOW, val, m_pinDir };
    return mpsse_write(cmd, sizeof(cmd));
}


FT4232SPI::Status FT4232SPI::cs_assert() const
{
    return apply_pin_state(true);
}


FT4232SPI::Status FT4232SPI::cs_deassert() const
{
    return apply_pin_state(false);
}


// ============================================================================
// CORE SPI OPERATIONS  (no CS management — caller controls that)
// ============================================================================

/**
 * @brief Write N bytes using MPSSE native serial shift (no read)
 *
 * Command layout:
 *   { m_cmdWrite, lenLow, lenHigh, data[0..N-1] }
 *   len = N - 1  (MPSSE encodes length as count - 1)
 *
 * No response bytes are queued; mpsse_read() is not called.
 */
FT4232SPI::Status FT4232SPI::spi_write_raw(std::span<const uint8_t> data,
                                            size_t& bytesWritten) const
{
    bytesWritten = 0;

    if (data.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("spi_write_raw: empty buffer"));
        return Status::INVALID_PARAM;
    }

    // MPSSE max bytes per shift command = 65536
    constexpr size_t MPSSE_MAX_CHUNK = 65536u;
    size_t offset = 0;

    while (offset < data.size()) {
        size_t chunkSize = std::min(data.size() - offset, MPSSE_MAX_CHUNK);
        size_t lenField  = chunkSize - 1u;

        std::vector<uint8_t> cmd;
        cmd.reserve(3 + chunkSize);

        cmd.push_back(m_cmdWrite);
        cmd.push_back(static_cast<uint8_t>( lenField       & 0xFFu));
        cmd.push_back(static_cast<uint8_t>((lenField >> 8) & 0xFFu));
        cmd.insert(cmd.end(), data.begin() + static_cast<ptrdiff_t>(offset),
                              data.begin() + static_cast<ptrdiff_t>(offset + chunkSize));

        Status s = mpsse_write(cmd.data(), cmd.size());
        if (s != Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("spi_write_raw: mpsse_write failed at offset"); LOG_UINT32(offset));
            return s;
        }

        offset       += chunkSize;
        bytesWritten += chunkSize;
    }

    return Status::SUCCESS;
}


/**
 * @brief Read N bytes using MPSSE native serial shift (dummy 0x00 sent on MOSI)
 *
 * Command layout (write phase — dummy bytes are clocked out automatically):
 *   { m_cmdRead, lenLow, lenHigh }
 *   len = N - 1
 *
 * Response bytes (N bytes) are collected with mpsse_read().
 */
FT4232SPI::Status FT4232SPI::spi_read_raw(std::span<uint8_t> data,
                                           size_t& bytesRead,
                                           uint32_t timeoutMs) const
{
    bytesRead = 0;

    if (data.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("spi_read_raw: empty buffer"));
        return Status::INVALID_PARAM;
    }

    constexpr size_t MPSSE_MAX_CHUNK = 65536u;
    size_t offset = 0;

    while (offset < data.size()) {
        size_t chunkSize = std::min(data.size() - offset, MPSSE_MAX_CHUNK);
        size_t lenField  = chunkSize - 1u;

        // Send the read command
        const uint8_t cmd[3] = {
            m_cmdRead,
            static_cast<uint8_t>( lenField       & 0xFFu),
            static_cast<uint8_t>((lenField >> 8) & 0xFFu)
        };

        // SEND_IMMEDIATE flushes the command to the chip so response bytes
        // start arriving without waiting for the USB packet to fill up.
        const uint8_t flush[1] = { MPSSE_SEND_IMMEDIATE };

        Status s = mpsse_write(cmd, sizeof(cmd));
        if (s != Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("spi_read_raw: command write failed at offset"); LOG_UINT32(offset));
            return s;
        }

        s = mpsse_write(flush, sizeof(flush));
        if (s != Status::SUCCESS) return s;

        // Collect the response bytes for this chunk
        size_t got = 0;
        s = mpsse_read(data.data() + offset, chunkSize, timeoutMs, got);
        if (s != Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("spi_read_raw: mpsse_read failed, got="); LOG_UINT32(got));
            bytesRead += got;
            return s;
        }

        offset    += chunkSize;
        bytesRead += chunkSize;
    }

    return Status::SUCCESS;
}


/**
 * @brief Full-duplex: simultaneously write txBuf and read rxBuf
 *
 * Command layout:
 *   { m_cmdXfer, lenLow, lenHigh, txData[0..N-1] }
 *   len = N - 1
 *
 * N response bytes (the MISO data) are collected with mpsse_read().
 */
FT4232SPI::Status FT4232SPI::spi_xfer_raw(std::span<const uint8_t> txBuf,
                                           std::span<uint8_t>       rxBuf,
                                           size_t& bytesXferd,
                                           uint32_t timeoutMs) const
{
    bytesXferd = 0;

    if (txBuf.size() != rxBuf.size() || txBuf.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("spi_xfer_raw: invalid buffer sizes"));
        return Status::INVALID_PARAM;
    }

    constexpr size_t MPSSE_MAX_CHUNK = 65536u;
    size_t offset = 0;

    while (offset < txBuf.size()) {
        size_t chunkSize = std::min(txBuf.size() - offset, MPSSE_MAX_CHUNK);
        size_t lenField  = chunkSize - 1u;

        std::vector<uint8_t> cmd;
        cmd.reserve(3 + chunkSize);

        cmd.push_back(m_cmdXfer);
        cmd.push_back(static_cast<uint8_t>( lenField       & 0xFFu));
        cmd.push_back(static_cast<uint8_t>((lenField >> 8) & 0xFFu));
        cmd.insert(cmd.end(),
                   txBuf.begin() + static_cast<ptrdiff_t>(offset),
                   txBuf.begin() + static_cast<ptrdiff_t>(offset + chunkSize));

        const uint8_t flush[1] = { MPSSE_SEND_IMMEDIATE };

        Status s = mpsse_write(cmd.data(), cmd.size());
        if (s != Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("spi_xfer_raw: command write failed at offset"); LOG_UINT32(offset));
            return s;
        }

        s = mpsse_write(flush, sizeof(flush));
        if (s != Status::SUCCESS) return s;

        size_t got = 0;
        s = mpsse_read(rxBuf.data() + offset, chunkSize, timeoutMs, got);
        if (s != Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("spi_xfer_raw: mpsse_read failed, got="); LOG_UINT32(got));
            bytesXferd += got;
            return s;
        }

        offset     += chunkSize;
        bytesXferd += chunkSize;
    }

    return Status::SUCCESS;
}
