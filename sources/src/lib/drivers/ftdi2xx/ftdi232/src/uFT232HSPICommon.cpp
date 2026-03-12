/*
 * FT232H SPI – cross-platform MPSSE protocol implementation
 *
 * The MPSSE protocol logic is identical to uFT4232SPICommon.cpp.
 * The only difference is that FT232H uses FT232HBase (no channel selector)
 * and FT232HSPI::SpiConfig has no channel field.
 *
 * All MPSSE opcodes are defined in FT232HBase.hpp (same values as AN_108).
 */

#include "uFT232HSPI.hpp"
#include "uLogger.hpp"

#include <vector>
#include <cstring>

#define LT_HDR  "FT232H_SPI  |"
#define LOG_HDR LOG_STRING(LT_HDR)


// ============================================================================
// open / close
// ============================================================================

FT232HSPI::Status FT232HSPI::open(const SpiConfig& config, uint8_t u8DeviceIndex)
{
    if (is_open()) {
        close();
    }

    // Open the physical device handle
    auto s = open_device(u8DeviceIndex);
    if (s != Status::SUCCESS) return s;

    // Purge any stale data
    mpsse_purge();

    // Configure MPSSE for SPI
    s = configure_mpsse_spi(config);
    if (s != Status::SUCCESS) {
        FT232HBase::close();
        return s;
    }

    m_config = config;
    return Status::SUCCESS;
}

FT232HSPI::Status FT232HSPI::close()
{
    if (is_open()) {
        cs_deassert();
    }
    return FT232HBase::close();
}


// ============================================================================
// configure_mpsse_spi
// ============================================================================

FT232HSPI::Status FT232HSPI::configure_mpsse_spi(const SpiConfig& config)
{
    // Resolve shift commands for the requested mode
    switch (config.mode) {
    case SpiMode::Mode0:
    case SpiMode::Mode3:
        m_cmdWrite = MPSSE_SPI_WRITE_NRE;
        m_cmdRead  = MPSSE_SPI_READ_PRE;
        m_cmdXfer  = MPSSE_SPI_XFER_NRE;
        break;
    default: // Mode1, Mode2
        m_cmdWrite = MPSSE_SPI_WRITE_PRE;
        m_cmdRead  = MPSSE_SPI_READ_NRE;
        m_cmdXfer  = MPSSE_SPI_XFER_PRE;
        break;
    }

    if (config.bitOrder == BitOrder::LsbFirst) {
        m_cmdWrite |= 0x08u;
        m_cmdRead  |= 0x08u;
        m_cmdXfer  |= 0x08u;
    }

    // CS idle level depends on polarity
    bool csIdleHigh = (config.csPolarity == CsPolarity::ActiveLow);

    // SCK idle level depends on CPOL (Mode2/Mode3 = high)
    bool sckIdle = (config.mode == SpiMode::Mode2 ||
                    config.mode == SpiMode::Mode3);

    // Build initial pin state
    m_pinDir   = 0x0Bu; // SCK+MOSI+CS = outputs; MISO = input
    m_pinValue = 0x00u;
    if (sckIdle)   m_pinValue |= 0x01u;           // ADBUS0 = SCK
    if (csIdleHigh) m_pinValue |= config.csPin;

    // Compute clock divisor
    uint32_t divisor = (CLOCK_BASE_HZ / 2u / config.clockHz) - 1u;

    std::vector<uint8_t> init;
    init.reserve(16);
    init.push_back(MPSSE_DIS_DIV5);          // 60 MHz base clock
    init.push_back(MPSSE_DIS_3PHASE);        // SPI: no 3-phase clocking
    init.push_back(MPSSE_DIS_ADAPTIVE);      // disable adaptive clocking
    init.push_back(MPSSE_LOOPBACK_OFF);
    init.push_back(MPSSE_SET_CLK_DIV);
    init.push_back(static_cast<uint8_t>(divisor & 0xFFu));
    init.push_back(static_cast<uint8_t>((divisor >> 8u) & 0xFFu));
    init.push_back(MPSSE_SET_BITS_LOW);
    init.push_back(m_pinValue);
    init.push_back(m_pinDir);

    return mpsse_write(init.data(), init.size());
}


// ============================================================================
// CS helpers
// ============================================================================

FT232HSPI::Status FT232HSPI::apply_pin_state(bool csActive) const
{
    uint8_t val = m_pinValue;
    if (csActive) {
        if (m_config.csPolarity == CsPolarity::ActiveLow)
            val &= static_cast<uint8_t>(~m_config.csPin);
        else
            val |= m_config.csPin;
    } else {
        if (m_config.csPolarity == CsPolarity::ActiveLow)
            val |= m_config.csPin;
        else
            val &= static_cast<uint8_t>(~m_config.csPin);
    }
    uint8_t cmd[3] = { MPSSE_SET_BITS_LOW, val, m_pinDir };
    return mpsse_write(cmd, 3);
}

FT232HSPI::Status FT232HSPI::cs_assert()   const { return apply_pin_state(true);  }
FT232HSPI::Status FT232HSPI::cs_deassert() const { return apply_pin_state(false); }


// ============================================================================
// Core transfer helpers
// ============================================================================

FT232HSPI::Status FT232HSPI::spi_write_raw(std::span<const uint8_t> data,
                                             size_t& bytesWritten) const
{
    bytesWritten = 0;
    size_t len = data.size();
    uint16_t lenField = static_cast<uint16_t>(len - 1u);

    std::vector<uint8_t> cmd;
    cmd.reserve(3 + len);
    cmd.push_back(m_cmdWrite);
    cmd.push_back(static_cast<uint8_t>(lenField & 0xFFu));
    cmd.push_back(static_cast<uint8_t>((lenField >> 8u) & 0xFFu));
    cmd.insert(cmd.end(), data.begin(), data.end());

    auto s = mpsse_write(cmd.data(), cmd.size());
    if (s == Status::SUCCESS) bytesWritten = len;
    return s;
}

FT232HSPI::Status FT232HSPI::spi_read_raw(std::span<uint8_t> data,
                                            size_t& bytesRead,
                                            uint32_t timeoutMs) const
{
    bytesRead = 0;
    size_t len = data.size();
    uint16_t lenField = static_cast<uint16_t>(len - 1u);

    uint8_t cmd[4];
    cmd[0] = m_cmdRead;
    cmd[1] = static_cast<uint8_t>(lenField & 0xFFu);
    cmd[2] = static_cast<uint8_t>((lenField >> 8u) & 0xFFu);
    cmd[3] = MPSSE_SEND_IMMEDIATE;

    auto s = mpsse_write(cmd, 4);
    if (s != Status::SUCCESS) return s;
    return mpsse_read(data.data(), len,
                      timeoutMs ? timeoutMs : FT232H_READ_DEFAULT_TIMEOUT,
                      bytesRead);
}

FT232HSPI::Status FT232HSPI::spi_xfer_raw(std::span<const uint8_t> txBuf,
                                            std::span<uint8_t>       rxBuf,
                                            size_t& bytesXferd,
                                            uint32_t timeoutMs) const
{
    bytesXferd = 0;
    size_t len = txBuf.size();
    uint16_t lenField = static_cast<uint16_t>(len - 1u);

    std::vector<uint8_t> cmd;
    cmd.reserve(4 + len);
    cmd.push_back(m_cmdXfer);
    cmd.push_back(static_cast<uint8_t>(lenField & 0xFFu));
    cmd.push_back(static_cast<uint8_t>((lenField >> 8u) & 0xFFu));
    cmd.insert(cmd.end(), txBuf.begin(), txBuf.end());
    cmd.push_back(MPSSE_SEND_IMMEDIATE);

    auto s = mpsse_write(cmd.data(), cmd.size());
    if (s != Status::SUCCESS) return s;
    return mpsse_read(rxBuf.data(), len,
                      timeoutMs ? timeoutMs : FT232H_READ_DEFAULT_TIMEOUT,
                      bytesXferd);
}


// ============================================================================
// ICommDriver interface
// ============================================================================

FT232HSPI::WriteResult
FT232HSPI::tout_write(uint32_t /*u32WriteTimeout*/,
                       std::span<const uint8_t> buffer) const
{
    WriteResult r;
    r.status        = Status::RETVAL_NOT_SET;
    r.bytes_written = 0;

    if (auto s = cs_assert(); s != Status::SUCCESS) { r.status = s; return r; }

    size_t written = 0;
    r.status = spi_write_raw(buffer, written);
    r.bytes_written = written;

    cs_deassert();
    return r;
}

FT232HSPI::ReadResult
FT232HSPI::tout_read(uint32_t u32ReadTimeout,
                      std::span<uint8_t> buffer,
                      const ReadOptions& /*options*/) const
{
    ReadResult r;
    r.status     = Status::RETVAL_NOT_SET;
    r.bytes_read = 0;

    if (auto s = cs_assert(); s != Status::SUCCESS) { r.status = s; return r; }

    size_t got = 0;
    r.status = spi_read_raw(buffer, got,
                             u32ReadTimeout ? u32ReadTimeout : FT232H_READ_DEFAULT_TIMEOUT);
    r.bytes_read = got;

    cs_deassert();
    return r;
}

FT232HSPI::TransferResult
FT232HSPI::spi_transfer(std::span<const uint8_t> txBuf,
                         std::span<uint8_t>       rxBuf,
                         uint32_t u32TimeoutMs) const
{
    TransferResult r;

    if (txBuf.size() != rxBuf.size()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("spi_transfer: tx/rx buffer size mismatch"));
        r.status = Status::INVALID_PARAM;
        return r;
    }

    if (auto s = cs_assert(); s != Status::SUCCESS) { r.status = s; return r; }

    size_t xferd = 0;
    r.status = spi_xfer_raw(txBuf, rxBuf, xferd, u32TimeoutMs);
    r.bytes_xfered = xferd;

    cs_deassert();
    return r;
}
