#include "FT2232Base.hpp"
#include "uFT2232SPI.hpp"
#include "uLogger.hpp"

#include <algorithm>
#include <vector>

#define LT_HDR  "FT2232_SPI|"
#define LOG_HDR LOG_STRING(LT_HDR)


// ============================================================================
// open / close
// ============================================================================

FT2232SPI::Status FT2232SPI::open(const SpiConfig& config, uint8_t u8DeviceIndex)
{
    if (config.clockHz == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("open: clockHz must be > 0"));
        return Status::INVALID_PARAM;
    }

    Status s = open_device(config.variant, config.channel, u8DeviceIndex);
    if (s != Status::SUCCESS) {
        return s;
    }

    s = configure_mpsse_spi(config);
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("MPSSE SPI init failed, clock="); LOG_UINT32(config.clockHz));
        FT2232Base::close();
        return s;
    }

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("FT2232 SPI opened: variant=");
              LOG_UINT32(static_cast<uint8_t>(config.variant));
              LOG_STRING("ch="); LOG_UINT32(static_cast<uint8_t>(config.channel));
              LOG_STRING("idx="); LOG_UINT32(u8DeviceIndex);
              LOG_STRING("clock="); LOG_UINT32(config.clockHz);
              LOG_STRING("mode="); LOG_UINT32(static_cast<uint8_t>(config.mode)));

    return Status::SUCCESS;
}

FT2232SPI::Status FT2232SPI::close()
{
    if (is_open()) {
        (void)cs_deassert();
    }
    return FT2232Base::close();
}


// ============================================================================
// PUBLIC UNIFIED INTERFACE  (ICommDriver)
// ============================================================================

FT2232SPI::WriteResult FT2232SPI::tout_write(uint32_t u32WriteTimeout,
                                              std::span<const uint8_t> buffer) const
{
    WriteResult result;
    (void)u32WriteTimeout;

    if (!is_open()) { result.status = Status::PORT_ACCESS; return result; }
    if (buffer.empty()) { result.status = Status::INVALID_PARAM; return result; }

    result.status = cs_assert();
    if (result.status != Status::SUCCESS) return result;

    size_t bytesWritten = 0;
    result.status = spi_write_raw(buffer, bytesWritten);
    result.bytes_written = bytesWritten;

    Status csStatus = cs_deassert();
    if (result.status == Status::SUCCESS) result.status = csStatus;

    return result;
}

FT2232SPI::ReadResult FT2232SPI::tout_read(uint32_t u32ReadTimeout,
                                            std::span<uint8_t> buffer,
                                            const ReadOptions& options) const
{
    ReadResult result;

    if (!is_open()) { result.status = Status::PORT_ACCESS; return result; }

    uint32_t timeout = (u32ReadTimeout == 0) ? FT2232_READ_DEFAULT_TIMEOUT : u32ReadTimeout;

    switch (options.mode)
    {
        case ReadMode::Exact:
        {
            result.status = cs_assert();
            if (result.status != Status::SUCCESS) return result;

            size_t bytesRead = 0;
            result.status = spi_read_raw(buffer, bytesRead, timeout);
            result.bytes_read = bytesRead;
            result.found_terminator = false;

            Status cs = cs_deassert();
            if (result.status == Status::SUCCESS) result.status = cs;
            break;
        }

        case ReadMode::UntilDelimiter:
        {
            if (buffer.size() < 2) { result.status = Status::INVALID_PARAM; break; }

            result.status = cs_assert();
            if (result.status != Status::SUCCESS) return result;

            size_t pos    = 0;
            result.status = Status::READ_TIMEOUT;

            while (pos < buffer.size() - 1) {
                uint8_t byte = 0; size_t got = 0;
                Status  s    = spi_read_raw(std::span<uint8_t>(&byte, 1), got, timeout);
                if (s != Status::SUCCESS || got == 0) { result.status = s; break; }
                if (byte == options.delimiter) {
                    buffer[pos] = '\0';
                    result.found_terminator = true;
                    result.status           = Status::SUCCESS;
                    break;
                }
                buffer[pos++] = byte;
            }

            if (pos == buffer.size() - 1 && result.status == Status::READ_TIMEOUT) {
                result.status = Status::BUFFER_OVERFLOW;
            }

            result.bytes_read = pos;
            { Status cs = cs_deassert(); if (result.status == Status::SUCCESS) result.status = cs; }
            break;
        }

        case ReadMode::UntilToken:
        {
            if (options.token.empty()) { result.status = Status::INVALID_PARAM; break; }

            result.status = cs_assert();
            if (result.status != Status::SUCCESS) return result;

            const auto& token = options.token;
            std::vector<int> lps(token.size(), 0);
            for (size_t i = 1, len = 0; i < token.size(); ) {
                if (token[i] == token[len]) { lps[i++] = static_cast<int>(++len); }
                else if (len != 0) { len = static_cast<size_t>(lps[len - 1]); }
                else { lps[i++] = 0; }
            }

            size_t matched = 0;
            result.status  = Status::READ_TIMEOUT;

            while (true) {
                uint8_t byte = 0; size_t got = 0;
                Status  s    = spi_read_raw(std::span<uint8_t>(&byte, 1), got, timeout);
                if (s != Status::SUCCESS || got == 0) { result.status = s; break; }
                while (matched > 0 && byte != token[matched])
                    matched = static_cast<size_t>(lps[matched - 1]);
                if (byte == token[matched]) { ++matched; }
                if (matched == token.size()) {
                    result.found_terminator = true;
                    result.status           = Status::SUCCESS;
                    break;
                }
            }

            result.bytes_read = 0;
            { Status cs = cs_deassert(); if (result.status == Status::SUCCESS) result.status = cs; }
            break;
        }

        default:
            result.status = Status::INVALID_PARAM;
            break;
    }

    return result;
}

FT2232SPI::TransferResult FT2232SPI::spi_transfer(std::span<const uint8_t> txBuf,
                                                   std::span<uint8_t>       rxBuf,
                                                   uint32_t u32TimeoutMs) const
{
    TransferResult result;

    if (!is_open()) { result.status = Status::PORT_ACCESS; return result; }
    if (txBuf.size() != rxBuf.size() || txBuf.empty()) {
        result.status = Status::INVALID_PARAM; return result;
    }

    uint32_t timeout = (u32TimeoutMs == 0) ? FT2232_READ_DEFAULT_TIMEOUT : u32TimeoutMs;

    result.status = cs_assert();
    if (result.status != Status::SUCCESS) return result;

    size_t bytesXferd = 0;
    result.status = spi_xfer_raw(txBuf, rxBuf, bytesXferd, timeout);
    result.bytes_xfered = bytesXferd;

    Status cs = cs_deassert();
    if (result.status == Status::SUCCESS) result.status = cs;

    return result;
}


// ============================================================================
// MPSSE CONFIGURATION
// ============================================================================

FT2232SPI::Status FT2232SPI::configure_mpsse_spi(const SpiConfig& config)
{
    m_config = config;

    // Resolve shift commands from CPHA
    const bool firstEdgeActive = (config.mode == SpiMode::Mode0 ||
                                  config.mode == SpiMode::Mode3);
    m_cmdWrite = firstEdgeActive ? MPSSE_SPI_WRITE_NRE : MPSSE_SPI_WRITE_PRE;
    m_cmdRead  = firstEdgeActive ? MPSSE_SPI_READ_PRE  : MPSSE_SPI_READ_NRE;
    m_cmdXfer  = firstEdgeActive ? MPSSE_SPI_XFER_NRE  : MPSSE_SPI_XFER_PRE;

    if (config.bitOrder == BitOrder::LsbFirst) {
        m_cmdWrite = static_cast<uint8_t>(m_cmdWrite | 0x02u);
        m_cmdRead  = static_cast<uint8_t>(m_cmdRead  | 0x02u);
        m_cmdXfer  = static_cast<uint8_t>(m_cmdXfer  | 0x02u);
    }

    // Idle pin state: CPOL determines CLK level, csPolarity determines CS level
    const bool cpolHigh = (config.mode == SpiMode::Mode2 || config.mode == SpiMode::Mode3);
    m_pinValue = 0x00u;
    if (cpolHigh) m_pinValue |= 0x01u;
    if (config.csPolarity == CsPolarity::ActiveLow) m_pinValue |= config.csPin;

    m_pinDir = static_cast<uint8_t>(0x03u | config.csPin); // SCK+MOSI+CS = outputs

    // Clock divisor:  SCK = clock_base_hz() / ((1 + divisor) * 2)
    const uint32_t half     = clock_base_hz() / 2u;
    uint32_t       divisor  = (config.clockHz > 0 && config.clockHz < half)
                              ? (half / config.clockHz) - 1u
                              : 0u;
    divisor = std::min(divisor, static_cast<uint32_t>(0xFFFFu));

    const uint8_t divLow  = static_cast<uint8_t>( divisor       & 0xFFu);
    const uint8_t divHigh = static_cast<uint8_t>((divisor >> 8) & 0xFFu);

    // MPSSE sync
    { const uint8_t s[1] = {0xAA}; (void)mpsse_write(s, 1);
      uint8_t e[2]={0}; size_t g=0; (void)mpsse_read(e, 2, 200, g); }

    std::vector<uint8_t> init;
    init.reserve(20);

    push_clock_init(init);           // DIS_DIV5 for FT2232H; nothing for FT2232D
    init.push_back(MPSSE_DIS_ADAPTIVE);
    init.push_back(MPSSE_DIS_3PHASE);
    init.push_back(MPSSE_LOOPBACK_OFF);

    init.push_back(MPSSE_SET_CLK_DIV);
    init.push_back(divLow);
    init.push_back(divHigh);

    init.push_back(MPSSE_SET_BITS_LOW);
    init.push_back(m_pinValue);
    init.push_back(m_pinDir);

    init.push_back(MPSSE_SEND_IMMEDIATE);

    Status s = mpsse_write(init.data(), init.size());
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("configure_mpsse_spi: init failed"));
    }
    return s;
}


// ============================================================================
// CS MANAGEMENT
// ============================================================================

FT2232SPI::Status FT2232SPI::apply_pin_state(bool csActive) const
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

    const uint8_t cmd[3] = { MPSSE_SET_BITS_LOW, val, m_pinDir };
    return mpsse_write(cmd, sizeof(cmd));
}

FT2232SPI::Status FT2232SPI::cs_assert()   const { return apply_pin_state(true);  }
FT2232SPI::Status FT2232SPI::cs_deassert() const { return apply_pin_state(false); }


// ============================================================================
// CORE SPI OPERATIONS
// ============================================================================

FT2232SPI::Status FT2232SPI::spi_write_raw(std::span<const uint8_t> data,
                                            size_t& bytesWritten) const
{
    bytesWritten = 0;
    if (data.empty()) return Status::INVALID_PARAM;

    constexpr size_t MAX = 65536u;
    size_t offset = 0;

    while (offset < data.size()) {
        size_t chunk = std::min(data.size() - offset, MAX);
        size_t lenF  = chunk - 1u;

        std::vector<uint8_t> cmd;
        cmd.reserve(3 + chunk);
        cmd.push_back(m_cmdWrite);
        cmd.push_back(static_cast<uint8_t>( lenF       & 0xFFu));
        cmd.push_back(static_cast<uint8_t>((lenF >> 8) & 0xFFu));
        cmd.insert(cmd.end(),
                   data.begin() + static_cast<ptrdiff_t>(offset),
                   data.begin() + static_cast<ptrdiff_t>(offset + chunk));

        Status s = mpsse_write(cmd.data(), cmd.size());
        if (s != Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("spi_write_raw failed at offset"); LOG_UINT32(offset));
            return s;
        }

        offset       += chunk;
        bytesWritten += chunk;
    }

    return Status::SUCCESS;
}

FT2232SPI::Status FT2232SPI::spi_read_raw(std::span<uint8_t> data,
                                           size_t& bytesRead,
                                           uint32_t timeoutMs) const
{
    bytesRead = 0;
    if (data.empty()) return Status::INVALID_PARAM;

    constexpr size_t MAX = 65536u;
    size_t offset = 0;

    while (offset < data.size()) {
        size_t chunk = std::min(data.size() - offset, MAX);
        size_t lenF  = chunk - 1u;

        const uint8_t cmd[3] = {
            m_cmdRead,
            static_cast<uint8_t>( lenF       & 0xFFu),
            static_cast<uint8_t>((lenF >> 8) & 0xFFu)
        };
        const uint8_t flush[1] = { MPSSE_SEND_IMMEDIATE };

        Status s = mpsse_write(cmd, sizeof(cmd));
        if (s != Status::SUCCESS) return s;
        s = mpsse_write(flush, sizeof(flush));
        if (s != Status::SUCCESS) return s;

        size_t got = 0;
        s = mpsse_read(data.data() + offset, chunk, timeoutMs, got);
        if (s != Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("spi_read_raw failed, got="); LOG_UINT32(got));
            bytesRead += got;
            return s;
        }

        offset    += chunk;
        bytesRead += chunk;
    }

    return Status::SUCCESS;
}

FT2232SPI::Status FT2232SPI::spi_xfer_raw(std::span<const uint8_t> txBuf,
                                           std::span<uint8_t>       rxBuf,
                                           size_t& bytesXferd,
                                           uint32_t timeoutMs) const
{
    bytesXferd = 0;
    if (txBuf.size() != rxBuf.size() || txBuf.empty()) return Status::INVALID_PARAM;

    constexpr size_t MAX = 65536u;
    size_t offset = 0;

    while (offset < txBuf.size()) {
        size_t chunk = std::min(txBuf.size() - offset, MAX);
        size_t lenF  = chunk - 1u;

        std::vector<uint8_t> cmd;
        cmd.reserve(3 + chunk);
        cmd.push_back(m_cmdXfer);
        cmd.push_back(static_cast<uint8_t>( lenF       & 0xFFu));
        cmd.push_back(static_cast<uint8_t>((lenF >> 8) & 0xFFu));
        cmd.insert(cmd.end(),
                   txBuf.begin() + static_cast<ptrdiff_t>(offset),
                   txBuf.begin() + static_cast<ptrdiff_t>(offset + chunk));

        const uint8_t flush[1] = { MPSSE_SEND_IMMEDIATE };

        Status s = mpsse_write(cmd.data(), cmd.size());
        if (s != Status::SUCCESS) return s;
        s = mpsse_write(flush, sizeof(flush));
        if (s != Status::SUCCESS) return s;

        size_t got = 0;
        s = mpsse_read(rxBuf.data() + offset, chunk, timeoutMs, got);
        if (s != Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("spi_xfer_raw failed, got="); LOG_UINT32(got));
            bytesXferd += got;
            return s;
        }

        offset     += chunk;
        bytesXferd += chunk;
    }

    return Status::SUCCESS;
}
