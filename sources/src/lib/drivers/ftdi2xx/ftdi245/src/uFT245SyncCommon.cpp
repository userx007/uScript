#include "FT245Base.hpp"
#include "uFT245Sync.hpp"
#include "uLogger.hpp"

#include <algorithm>
#include <vector>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "FT245_SYNC  |"
#define LOG_HDR    LOG_STRING(LT_HDR)


// ============================================================================
// open / close
// ============================================================================

FT245Sync::Status FT245Sync::open(const SyncConfig& config, uint8_t u8DeviceIndex)
{
    Status s = open_device(config.variant, config.fifoMode, u8DeviceIndex);
    if (s != Status::SUCCESS) {
        return s;
    }

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("FT245 Sync opened: variant=");
              LOG_UINT32(static_cast<uint8_t>(config.variant));
              LOG_STRING("fifoMode="); LOG_UINT32(static_cast<uint8_t>(config.fifoMode));
              LOG_STRING("idx="); LOG_UINT32(u8DeviceIndex));

    return Status::SUCCESS;
}

FT245Sync::Status FT245Sync::close()
{
    if (is_open()) {
        (void)fifo_purge(); // best-effort flush before releasing handle
    }
    return FT245Base::close();
}


// ============================================================================
// PUBLIC UNIFIED INTERFACE  (ICommDriver)
// ============================================================================

FT245Sync::WriteResult FT245Sync::tout_write(uint32_t u32WriteTimeout,
                                              std::span<const uint8_t> buffer) const
{
    WriteResult result;

    if (!is_open()) {
        result.status = Status::PORT_ACCESS;
        return result;
    }

    if (buffer.empty()) {
        result.status        = Status::SUCCESS;
        result.bytes_written = 0;
        return result;
    }

    // The base fifo_write performs a single bulk transfer.  For large payloads
    // we chunk at 65536 bytes to stay within USB bulk transfer limits and to
    // provide deterministic write latency per-chunk.
    constexpr size_t MAX_CHUNK = 65536u;
    const uint32_t   timeout   = (u32WriteTimeout == 0u) ? FT245_WRITE_DEFAULT_TIMEOUT
                                                          : u32WriteTimeout;
    (void)timeout; // timeout enforced at platform layer

    size_t offset = 0;

    while (offset < buffer.size()) {
        const size_t chunk = std::min(buffer.size() - offset, MAX_CHUNK);
        Status s = fifo_write(buffer.data() + offset, chunk);

        if (s != Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("tout_write: fifo_write failed at offset=");
                      LOG_UINT32(offset));
            result.status = s;
            return result;
        }

        offset               += chunk;
        result.bytes_written += chunk;
    }

    result.status = Status::SUCCESS;
    return result;
}


FT245Sync::ReadResult FT245Sync::tout_read(uint32_t u32ReadTimeout,
                                            std::span<uint8_t> buffer,
                                            const ReadOptions& options) const
{
    ReadResult result;

    if (!is_open()) {
        result.status = Status::PORT_ACCESS;
        return result;
    }

    if (buffer.empty()) {
        result.status    = Status::SUCCESS;
        result.bytes_read = 0;
        return result;
    }

    const uint32_t timeout = (u32ReadTimeout == 0u) ? FT245_READ_DEFAULT_TIMEOUT
                                                     : u32ReadTimeout;

    switch (options.mode)
    {
        // ── Exact: fill the entire buffer ─────────────────────────────────────
        case ReadMode::Exact:
        {
            size_t bytesRead = 0;
            result.status           = fifo_read(buffer.data(), buffer.size(),
                                                timeout, bytesRead);
            result.bytes_read       = bytesRead;
            result.found_terminator = false;
            break;
        }

        // ── UntilDelimiter: accumulate until delimiter byte seen ───────────────
        case ReadMode::UntilDelimiter:
        {
            if (buffer.size() < 2) {
                result.status = Status::INVALID_PARAM;
                break;
            }

            size_t pos    = 0;
            result.status = Status::READ_TIMEOUT;

            while (pos < buffer.size() - 1) {
                uint8_t byte  = 0;
                size_t  got   = 0;
                Status  s     = fifo_read(&byte, 1, timeout, got);

                if (s != Status::SUCCESS || got == 0) { result.status = s; break; }

                if (byte == options.delimiter) {
                    buffer[pos]             = '\0';
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
            break;
        }

        // ── UntilToken: KMP search for byte sequence ───────────────────────────
        case ReadMode::UntilToken:
        {
            if (options.token.empty()) {
                result.status = Status::INVALID_PARAM;
                break;
            }

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
                Status  s    = fifo_read(&byte, 1, timeout, got);

                if (s != Status::SUCCESS || got == 0) { result.status = s; break; }

                while (matched > 0 && byte != token[matched]) {
                    matched = static_cast<size_t>(lps[matched - 1]);
                }
                if (byte == token[matched]) { ++matched; }
                if (matched == token.size()) {
                    result.found_terminator = true;
                    result.status           = Status::SUCCESS;
                    break;
                }
            }

            result.bytes_read = 0;
            break;
        }

        default:
            result.status = Status::INVALID_PARAM;
            break;
    }

    return result;
}
