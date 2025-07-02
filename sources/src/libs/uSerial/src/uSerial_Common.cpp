#include "uSerial.h"
#include "uLogger.hpp"

#define LT_HDR     "UARTDRV :"
#define LOG_HDR    LOG_STRING(LT_HDR)


namespace UART
{

Driver::UartStatusCode Driver::timeout_readline(uint32_t u32ReadTimeout, char *pBuffer, size_t szBufferSize)
{
    return read_until(u32ReadTimeout, pBuffer, szBufferSize, '\n');
}



Driver::UartStatusCode Driver::timeout_wait_for_token(uint32_t u32ReadTimeout, const char *pstrToken)
{
    if (!pstrToken) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid parameter (pstrToken=NULL)"));
        return UartStatusCode::INVALID_PARAM;
    }

    uint32_t u32TokenLength = static_cast<uint32_t>(strlen(pstrToken));
    if (u32TokenLength == 0 || u32TokenLength >= UART_MAX_BUFLENGTH) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Token length invalid or exceeds max buffer length"));
        return UartStatusCode::INVALID_PARAM;
    }

    uint32_t u32Timeout = (u32ReadTimeout == 0) ? UART_READ_DEFAULT_TIMEOUT : u32ReadTimeout;
    bool bReturnOnTimeout = (u32ReadTimeout != 0);
    Driver::UartStatusCode eResult = UartStatusCode::RETVAL_NOT_SET;

    std::vector<int> viLps;
    build_kmp_table(pstrToken, u32TokenLength, viLps);
    uint32_t u32Matched = 0;

    while (eResult == UartStatusCode::RETVAL_NOT_SET) {
        char cByte;
        size_t actualBytesRead = 0;
        Driver::UartStatusCode i32ReadResult = timeout_read(u32Timeout, &cByte, 1, &actualBytesRead);

        if (i32ReadResult != UartStatusCode::SUCCESS || actualBytesRead == 0) {
            eResult = (i32ReadResult == UartStatusCode::READ_TIMEOUT && bReturnOnTimeout)
                      ? UartStatusCode::READ_TIMEOUT
                      : UartStatusCode::PORT_ACCESS;
            break;
        }

        while (u32Matched > 0 && cByte != pstrToken[u32Matched]) {
            u32Matched = viLps[u32Matched - 1];
        }

        if (cByte == pstrToken[u32Matched]) {
            u32Matched++;
            if (u32Matched == u32TokenLength) {
                eResult = UartStatusCode::SUCCESS;
            }
        }
    }

    return eResult;
}



Driver::UartStatusCode Driver::timeout_wait_for_token_buffer(uint32_t u32ReadTimeout, const char *pstrToken, uint32_t u32TokenLength)
{
    if (!pstrToken || u32TokenLength == 0 || u32TokenLength >= UART_MAX_BUFLENGTH) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid token buffer or length"));
        return UartStatusCode::INVALID_PARAM;
    }

    char Buffer[UART_MAX_BUFLENGTH] = {0};
    uint32_t u32Timeout = (u32ReadTimeout == 0) ? UART_READ_DEFAULT_TIMEOUT : u32ReadTimeout;
    bool bReturnOnTimeout = (u32ReadTimeout != 0);
    Driver::UartStatusCode eResult = UartStatusCode::RETVAL_NOT_SET;

    std::vector<int> viLps;
    build_kmp_table(pstrToken, u32TokenLength, viLps);
    uint32_t u32Matched = 0;
    uint32_t u32BufferPos = 0;

    while (eResult == UartStatusCode::RETVAL_NOT_SET) {
        char cByte;
        size_t actualBytesRead = 0;
        Driver::UartStatusCode i32ReadResult = timeout_read(u32Timeout, &cByte, 1, &actualBytesRead);

        if (i32ReadResult != UartStatusCode::SUCCESS || actualBytesRead == 0) {
            eResult = (i32ReadResult == UartStatusCode::READ_TIMEOUT && bReturnOnTimeout)
                      ? UartStatusCode::READ_TIMEOUT
                      : UartStatusCode::PORT_ACCESS;
            break;
        }

        Buffer[u32BufferPos++ % UART_MAX_BUFLENGTH] = cByte;

        while (u32Matched > 0 && cByte != pstrToken[u32Matched]) {
            u32Matched = viLps[u32Matched - 1];
        }

        if (cByte == pstrToken[u32Matched]) {
            u32Matched++;
            if (u32Matched == u32TokenLength) {
                eResult = UartStatusCode::SUCCESS;
            }
        }
    }

    return eResult;
}


void Driver::build_kmp_table(const char *pstrPattern, uint32_t u32Length, std::vector<int>& viLps)
{
    viLps.resize(u32Length);
    int len = 0;
    viLps[0] = 0;

    for (uint32_t i = 1; i < u32Length; ) {
        if (pstrPattern[i] == pstrPattern[len]) {
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


Driver::UartStatusCode Driver::read_until(uint32_t u32TimeoutMs, char *pBuffer, size_t szBufferSize, char cDelimiter)
{
    if (!pBuffer || szBufferSize == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid buffer or size in read_until"));
        return UartStatusCode::INVALID_PARAM;
    }

    constexpr size_t TEMP_BUFFER_SIZE = 64;
    char tempBuffer[TEMP_BUFFER_SIZE];
    size_t szBytesRead = 0;
    Driver::UartStatusCode eResult = UartStatusCode::RETVAL_NOT_SET;

    while (eResult == UartStatusCode::RETVAL_NOT_SET) {
        size_t bytesToRead = std::min(TEMP_BUFFER_SIZE, szBufferSize - szBytesRead - 1);
        size_t actualBytesRead = 0;

        Driver::UartStatusCode readResult = timeout_read(u32TimeoutMs, tempBuffer, bytesToRead, &actualBytesRead);

        if (readResult == UartStatusCode::SUCCESS && actualBytesRead > 0) {
            for (size_t i = 0; i < actualBytesRead && szBytesRead < szBufferSize - 1; ++i) {
                char ch = tempBuffer[i];
                LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("read:"); LOG_HEX8(ch); LOG_STRING("|"); LOG_CHAR(ch));

                if (ch == cDelimiter) {
                    pBuffer[szBytesRead] = '\0';
                    return UartStatusCode::SUCCESS;
                } else {
                    pBuffer[szBytesRead++] = ch;
                }
            }
        } else if (readResult == UartStatusCode::READ_TIMEOUT) {
            eResult = (u32TimeoutMs > 0) ? UartStatusCode::READ_TIMEOUT : UartStatusCode::PORT_ACCESS;
        } else {
            eResult = UartStatusCode::PORT_ACCESS;
        }
    }

    return eResult;
}


std::string Driver::to_string(UartStatusCode code)
{
    switch (code)
    {
        case UartStatusCode::SUCCESS: return "SUCCESS";
        case UartStatusCode::INVALID_PARAM: return "INVALID_PARAM";
        case UartStatusCode::PORT_ACCESS: return "PORT_ACCESS";
        case UartStatusCode::READ_TIMEOUT: return "READ_TIMEOUT";
        case UartStatusCode::WRITE_TIMEOUT: return "WRITE_TIMEOUT";
        case UartStatusCode::OUT_OF_MEMORY: return "OUT_OF_MEMORY";
        case UartStatusCode::RETVAL_NOT_SET: return "RETVAL_NOT_SET";
        default: return "UNKNOWN_ERROR";
    }
};

} // namespace UART

