#include "uUart.hpp"
#include "uLogger.hpp"

#define LT_HDR     "UART_DRIVER:"
#define LOG_HDR    LOG_STRING(LT_HDR)


bool UART::is_open()  const
{
    if (m_iHandle < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Port not open.."));
        return false;
    }
    return true;
}



UART::Status UART::timeout_wait_for_token (uint32_t u32ReadTimeout, std::span<const uint8_t> token, bool useBuffer) const
{
    size_t szTokenLength = token.size();
    if (token.empty() || szTokenLength == 0 || szTokenLength >= UART_MAX_BUFLENGTH) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid token or length"));
        return Status::INVALID_PARAM;
    }

    uint32_t u32Timeout = (u32ReadTimeout == 0) ? UART_READ_DEFAULT_TIMEOUT : u32ReadTimeout;
    bool bReturnOnTimeout = (u32ReadTimeout != 0);

    std::vector<int> viLps;
    build_kmp_table(token, szTokenLength, viLps);

    return kmp_stream_match(token, viLps, u32Timeout, bReturnOnTimeout, useBuffer);
}



void UART::build_kmp_table (std::span<const uint8_t> pattern, size_t szLength, std::vector<int>& viLps) const
{
    viLps.resize(szLength);
    int len = 0;
    viLps[0] = 0;

    for (size_t i = 1; i < szLength; ) {
        if (pattern[i] == pattern[len]) {
            viLps[i++] = ++len;
        } else {
            if (len != 0) {
                len = viLps[len - 1];
            } else {
                viLps[i++] = 0;
            }
        }
    }
}



UART::Status UART::kmp_stream_match (std::span<const uint8_t> token, const std::vector<int>& viLps, uint32_t u32Timeout, bool bReturnOnTimeout, bool useBuffer) const
{
    uint8_t Buffer[UART_MAX_BUFLENGTH] = {0};
    uint32_t u32Matched = 0;
    uint32_t u32BufferPos = 0;

    while (true) {
        uint8_t cByte;
        size_t actualBytesRead = 0;
        UART::Status i32ReadResult = timeout_read(u32Timeout, std::span<uint8_t>(&cByte, 1), actualBytesRead);

        if (i32ReadResult != Status::SUCCESS || actualBytesRead == 0) {
            return (i32ReadResult == Status::READ_TIMEOUT && bReturnOnTimeout)
                   ? Status::READ_TIMEOUT
                   : Status::READ_ERROR;
        }

        if (useBuffer) {
            Buffer[u32BufferPos++ % UART_MAX_BUFLENGTH] = cByte;
        }

        while (u32Matched > 0 && cByte != token[u32Matched]) {
            u32Matched = viLps[u32Matched - 1];
        }

        if (cByte == token[u32Matched]) {
            u32Matched++;
            if (u32Matched == token.size()) {
                return Status::SUCCESS;
            }
        }
    }
}



UART::Status UART::timeout_read_until (uint32_t u32ReadTimeout, std::span<uint8_t> buffer, uint8_t cDelimiter) const
{
    if (buffer.size() < 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Buffer too small for delimiter + null terminator"));
        return Status::INVALID_PARAM;
    }

    constexpr size_t TEMP_BUFFER_SIZE = 64;
    std::array<uint8_t, TEMP_BUFFER_SIZE> tempBuffer = {0};
    size_t szBytesRead = 0;
    UART::Status eResult = Status::RETVAL_NOT_SET;

    while (eResult == Status::RETVAL_NOT_SET) {
        size_t bytesRemaining = buffer.size() - szBytesRead - 1;  // reserve space for '\0'
        if (bytesRemaining == 0) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Buffer full before delimiter found"));
            return Status::BUFFER_OVERFLOW;
        }

        size_t bytesToRead = std::min(TEMP_BUFFER_SIZE, bytesRemaining);
        size_t actualBytesRead = 0;

        std::span<uint8_t> readSpan(tempBuffer.data(), bytesToRead);
        UART::Status readResult = timeout_read(u32ReadTimeout, readSpan, actualBytesRead);

        if (readResult == Status::SUCCESS && actualBytesRead > 0) {
            for (size_t i = 0; i < actualBytesRead && szBytesRead < buffer.size() - 1; ++i) {
                uint8_t ch = readSpan[i];
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("read:"); LOG_HEX8(ch); LOG_STRING("|"); LOG_CHAR(ch));

                if (ch == cDelimiter) {
                    buffer[szBytesRead] = '\0';  // safe null-termination
                    return Status::SUCCESS;
                } else {
                    buffer[szBytesRead++] = ch;
                }
            }
        } else if (readResult == Status::READ_TIMEOUT) {
            eResult = (u32ReadTimeout > 0) ? Status::READ_TIMEOUT : Status::PORT_ACCESS;
        } else {
            eResult = Status::PORT_ACCESS;
        }
    }

    return eResult;
}
