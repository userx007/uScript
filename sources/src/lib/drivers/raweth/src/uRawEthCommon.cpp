#include "uRawEth.hpp"
#include "uLogger.hpp"
#include "uKmpMatch.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cctype>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR   "RAWETH_DRV   |"
#define LOG_HDR  LOG_STRING(LT_HDR)


bool RawEth::is_open() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_iHandle >= 0;
}


// ============================================================================
// xtra_params PARSING
// Accepted formats: "AA:BB:CC:DD:EE:FF" or "AA:BB:CC:DD:EE:FF/0800" (hex
// EtherType, optional "0x" prefix). Falls back to the configured defaults
// (and logs a WARNING) on any parse failure rather than failing the call —
// consistent with how the TCP driver treats an unusable xtra_params value
// as advisory rather than fatal.
// ============================================================================

void RawEth::resolve_destination(std::string_view xtra_params,
                                 MacAddr& outDestMac,
                                 uint16_t& outEtherType) const
{
    outDestMac   = m_defaultDestMac;
    outEtherType = m_u16EtherType;

    if (xtra_params.empty())
    {
        return;
    }

    const size_t szSlashPos  = xtra_params.find('/');
    const std::string_view macPart = xtra_params.substr(0, szSlashPos);

    MacAddr parsedMac{};
    if (macPart.size() != 17) // "AA:BB:CC:DD:EE:FF" is exactly 17 characters
    {
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("resolve_destination: malformed MAC in xtra_params, using default"));
        return;
    }

    unsigned int auiBytes[RAWETH_MAC_ADDR_LEN];
    const int iParsed = std::sscanf(std::string(macPart).c_str(),
                                    "%02x:%02x:%02x:%02x:%02x:%02x",
                                    &auiBytes[0], &auiBytes[1], &auiBytes[2],
                                    &auiBytes[3], &auiBytes[4], &auiBytes[5]);
    if (iParsed != RAWETH_MAC_ADDR_LEN)
    {
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("resolve_destination: unparsable MAC in xtra_params, using default"));
        return;
    }
    for (size_t i = 0; i < RAWETH_MAC_ADDR_LEN; ++i)
    {
        parsedMac[i] = static_cast<uint8_t>(auiBytes[i]);
    }
    outDestMac = parsedMac;

    if (szSlashPos == std::string_view::npos)
    {
        return; // No EtherType override supplied — keep the configured default.
    }

    std::string_view etherTypePart = xtra_params.substr(szSlashPos + 1);
    if (etherTypePart.size() >= 2 && etherTypePart[0] == '0' &&
        (etherTypePart[1] == 'x' || etherTypePart[1] == 'X'))
    {
        etherTypePart.remove_prefix(2);
    }
    if (etherTypePart.empty() || etherTypePart.size() > 4)
    {
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("resolve_destination: malformed EtherType in xtra_params, using default"));
        return;
    }

    unsigned int uiEtherType = 0;
    const int iEtherParsed = std::sscanf(std::string(etherTypePart).c_str(), "%x", &uiEtherType);
    if (iEtherParsed != 1 || uiEtherType > 0xFFFF)
    {
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("resolve_destination: unparsable EtherType in xtra_params, using default"));
        return;
    }
    outEtherType = static_cast<uint16_t>(uiEtherType);
}


// ============================================================================
// PUBLIC UNIFIED INTERFACE IMPLEMENTATION
// ============================================================================

RawEth::ReadResult RawEth::tout_read(uint32_t u32ReadTimeout,
                                     std::span<uint8_t> buffer,
                                     const ReadOptions& options,
                                     std::string_view xtra_params) const
{
    ReadResult result;

    if (!xtra_params.empty())
    {
        // Reads aren't addressed the way writes are — the bound socket
        // already filters to this interface + EtherType, and Ethernet has
        // no per-call RX filter analogous to a CAN ID, so there is nothing
        // for xtra_params to select here.
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("tout_read: xtra_params is not used by this driver, ignored"));
    }

    // Resolve the 0 == "use default" convention once, up front, so it applies
    // uniformly to all three read modes below.
    const uint32_t u32Timeout = (u32ReadTimeout == 0) ? RAWETH_READ_DEFAULT_TIMEOUT : u32ReadTimeout;

    switch (options.mode)
    {
        case ReadMode::Exact:
        {
            size_t bytes_read = 0;
            result.status           = timeout_read(u32Timeout, buffer, bytes_read);
            result.bytes_read       = bytes_read;
            result.found_terminator = false;
            break;
        }

        case ReadMode::UntilDelimiter:
        {
            size_t bytes_read = 0;
            result.status           = timeout_read_until(u32Timeout, buffer,
                                                         options.delimiter, bytes_read);
            result.bytes_read       = bytes_read;
            result.found_terminator = (result.status == Status::SUCCESS);
            break;
        }

        case ReadMode::UntilToken:
        {
            result.status           = timeout_wait_for_token(u32Timeout,
                                                             options.token,
                                                             options.use_buffer);
            result.bytes_read       = 0; // Token search does not fill the caller's buffer
            result.found_terminator = (result.status == Status::SUCCESS);
            break;
        }

        default:
            result.status           = Status::INVALID_PARAM;
            result.bytes_read       = 0;
            result.found_terminator = false;
            break;
    }

    return result;
}


RawEth::WriteResult RawEth::tout_write(uint32_t u32WriteTimeout,
                                       std::span<const uint8_t> buffer,
                                       std::string_view xtra_params) const
{
    WriteResult result;

    // Unlike tout_read(), a write genuinely needs a destination: raw
    // Ethernet has no connection to imply one, so xtra_params (or the
    // open()-configured default) selects the destination MAC / EtherType
    // the same way a CAN ID selects a frame.
    MacAddr  destMac;
    uint16_t u16EtherType;
    resolve_destination(xtra_params, destMac, u16EtherType);

    const uint32_t u32Timeout = (u32WriteTimeout == 0) ? RAWETH_WRITE_DEFAULT_TIMEOUT : u32WriteTimeout;

    size_t bytes_written = 0;
    result.status        = timeout_write(u32Timeout, buffer, destMac, u16EtherType, bytes_written);
    result.bytes_written = bytes_written;

    return result;
}


// ============================================================================
// PRIVATE LEGACY IMPLEMENTATION (INTERNAL USE ONLY)
// ============================================================================

RawEth::Status RawEth::timeout_wait_for_token(uint32_t u32ReadTimeout,
                                              std::span<const uint8_t> token,
                                              bool useBuffer) const
{
    const size_t szTokenLength = token.size();
    if (token.empty() || szTokenLength == 0 || szTokenLength >= RAWETH_MAX_BUFLENGTH)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid token or length"));
        return Status::INVALID_PARAM;
    }

    std::vector<int> viLps;
    build_kmp_table(token, szTokenLength, viLps);

    // u32ReadTimeout has already been resolved from 0 by tout_read(), so a
    // timeout here always reflects a real, caller-meaningful deadline.
    return kmp_stream_match(token, viLps, u32ReadTimeout, /*bReturnOnTimeout=*/true, useBuffer);
}


void RawEth::build_kmp_table(std::span<const uint8_t> pattern,
                             size_t szLength,
                             std::vector<int>& viLps) const
{
    ukmp::build_kmp_table(pattern, szLength, viLps);
}


RawEth::Status RawEth::kmp_stream_match(std::span<const uint8_t> token,
                                        const std::vector<int>& viLps,
                                        uint32_t u32Timeout,
                                        bool bReturnOnTimeout,
                                        bool useBuffer) const
{
    // Receive one frame's payload at a time and feed it byte-by-byte into
    // KMP. A frame boundary may (and usually will) split the token; the KMP
    // state machine handles that transparently since it only cares about
    // the byte sequence, not frame boundaries. The ring buffer (used only
    // when useBuffer) is sized independently, to the driver's overall max
    // buffer length rather than a single frame's payload.
    return ukmp::kmp_stream_match(
        [this](uint32_t timeout, std::span<uint8_t> buf, size_t& bytesRead) { return timeout_read(timeout, buf, bytesRead); },
        token, viLps, u32Timeout, bReturnOnTimeout, useBuffer,
        /*szChunkBufferSize=*/RAWETH_MAX_PAYLOAD, /*szRingBufferSize=*/RAWETH_MAX_BUFLENGTH);
}


RawEth::Status RawEth::timeout_read_until(uint32_t u32ReadTimeout,
                                          std::span<uint8_t> buffer,
                                          uint8_t cDelimiter,
                                          size_t& szBytesRead) const
{
    if (buffer.size() < 2)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Buffer too small for delimiter + null terminator"));
        return Status::INVALID_PARAM;
    }

    szBytesRead = 0;
    RawEth::Status eResult = Status::RETVAL_NOT_SET;

    std::array<uint8_t, RAWETH_MAX_PAYLOAD> frame_payload = {};

    while (eResult == Status::RETVAL_NOT_SET)
    {
        const size_t bytesRemaining = buffer.size() - szBytesRead - 1; // reserve for '\0'
        if (bytesRemaining == 0)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Buffer full before delimiter found"));
            return Status::BUFFER_OVERFLOW;
        }

        // Receive one frame's worth of payload bytes.
        size_t payloadBytes = 0;
        const RawEth::Status readResult =
            timeout_read(u32ReadTimeout,
                        std::span<uint8_t>(frame_payload.data(), frame_payload.size()),
                        payloadBytes);

        if (readResult == Status::SUCCESS && payloadBytes > 0)
        {
            // NOTE: as with the CAN/TCP drivers, any bytes received after
            // the delimiter within this same frame are discarded when we
            // return early below. Callers that expect back-to-back
            // delimited messages packed into successive frames should
            // prefer ReadMode::UntilToken or size their frames to one
            // message at a time.
            for (size_t i = 0; i < payloadBytes && szBytesRead < buffer.size() - 1; ++i)
            {
                const uint8_t ch = frame_payload[i];

                if (ch == cDelimiter)
                {
                    buffer[szBytesRead] = '\0';
                    return Status::SUCCESS;
                }
                buffer[szBytesRead++] = ch;
            }
        }
        else if (readResult == Status::READ_TIMEOUT)
        {
            eResult = (u32ReadTimeout > 0) ? Status::READ_TIMEOUT : Status::PORT_ACCESS;
        }
        else
        {
            eResult = Status::PORT_ACCESS;
        }
    }

    return eResult;
}
