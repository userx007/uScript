#include "uSerial.h"
#include "uLogger.hpp"

#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>

#define LT_HDR     "UARTDRV :"
#define LOG_HDR    LOG_STRING(LT_HDR)


namespace UART
{

Driver::UartStatusCode Driver::open(const std::string& strDevice, uint32_t u32Speed)
{
    if (strDevice.empty() || u32Speed == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid parameter(s):");
                  LOG_STRING(strDevice.c_str());
                  LOG_STRING("Baudrate:"); LOG_UINT32(u32Speed));
        return UartStatusCode::INVALID_PARAM;
    }

    int openFlags = O_RDWR | O_NOINHERIT | O_BINARY;
    m_iHandle = _open(strDevice.c_str(), openFlags);

    if (m_iHandle < 0) {
        int errnoRet = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Failed to open ["); LOG_STRING(strDevice.c_str());
                  LOG_UINT32(u32Speed); LOG_STRING("] errno:"); LOG_INT(errnoRet));
        return UartStatusCode::PORT_ACCESS;
    }

    Driver::UartStatusCode result = setup(u32Speed);
    if (result != UartStatusCode::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Failed to configure ["); LOG_STRING(strDevice.c_str());
                  LOG_UINT32(u32Speed); LOG_INT(m_iHandle);
                  LOG_STRING("] Error:"); LOG_INT(result));
        _close(m_iHandle);
        m_iHandle = -1;
        return UartStatusCode::PORT_ACCESS;
    }

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("UART ["); LOG_STRING(strDevice.c_str());
              LOG_UINT32(u32Speed); LOG_STRING("] opened, handle:");
              LOG_INT(m_iHandle));

    return UartStatusCode::SUCCESS;
}


Driver::UartStatusCode Driver::close()
{
    if (m_iHandle >= 0) {
        _close(m_iHandle);
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("UART closed, handle:"); LOG_INT(m_iHandle));
        m_iHandle = -1;
    }
    return UartStatusCode::SUCCESS;
}



Driver::UartStatusCode Driver::purge(bool bInput, bool bOutput)
{
    HANDLE hCom = (HANDLE)_get_osfhandle(m_iHandle);
    DWORD purgeOptions = 0;
    if (bInput) purgeOptions |= PURGE_RXCLEAR;
    if (bOutput) purgeOptions |= PURGE_TXCLEAR;

    if (!PurgeComm(hCom, purgeOptions)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("PurgeComm() failed for handle:"); LOG_INT(m_iHandle));
        return UartStatusCode::PORT_ACCESS;
    }

    return UartStatusCode::SUCCESS;
}


Driver::UartStatusCode Driver::timeout_read(uint32_t u32ReadTimeout, char *pBuffer, size_t szSizeToRead)
{
    if (!pBuffer) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid parameter (pBuffer=NULL)"));
        return UartStatusCode::INVALID_PARAM;
    }

    if (szSizeToRead == 0) return UartStatusCode::SUCCESS;

    HANDLE hCom = (HANDLE)_get_osfhandle(m_iHandle);
    if (hCom == INVALID_HANDLE_VALUE) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid handle from _get_osfhandle"));
        return UartStatusCode::PORT_ACCESS;
    }

    COMMTIMEOUTS originalTimeouts;
    if (!GetCommTimeouts(hCom, &originalTimeouts)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to get original COMMTIMEOUTS"));
        return UartStatusCode::PORT_ACCESS;
    }

    COMMTIMEOUTS newTimeouts = originalTimeouts;
    newTimeouts.ReadIntervalTimeout = 0;
    newTimeouts.ReadTotalTimeoutMultiplier = 0;
    newTimeouts.ReadTotalTimeoutConstant = u32ReadTimeout;

    if (!SetCommTimeouts(hCom, &newTimeouts)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to set COMMTIMEOUTS"));
        return UartStatusCode::PORT_ACCESS;
    }

    size_t szTotalBytesRead = 0;
    while (szTotalBytesRead < szSizeToRead) {
        int iBytesRead = _read(m_iHandle, pBuffer + szTotalBytesRead, static_cast<unsigned int>(szSizeToRead - szTotalBytesRead));
        if (iBytesRead < 0) {
            SetCommTimeouts(hCom, &originalTimeouts); // Restore original timeouts
            return UartStatusCode::PORT_ACCESS;
        } else if (iBytesRead == 0) {
            SetCommTimeouts(hCom, &originalTimeouts);
            return UartStatusCode::READ_TIMEOUT;
        }

        szTotalBytesRead += iBytesRead;
    }

    SetCommTimeouts(hCom, &originalTimeouts); // Restore original timeouts
    return UartStatusCode::SUCCESS;
}


Driver::UartStatusCode Driver::timeout_write(uint32_t u32WriteTimeout, const char *pBuffer, size_t szSizeToWrite)
{
    if (!pBuffer) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid parameter (pBuffer=NULL)"));
        return UartStatusCode::INVALID_PARAM;
    }

    if (szSizeToWrite == 0) return UartStatusCode::SUCCESS;

    HANDLE hCom = (HANDLE)_get_osfhandle(m_iHandle);
    if (hCom == INVALID_HANDLE_VALUE) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid handle from _get_osfhandle"));
        return UartStatusCode::PORT_ACCESS;
    }

    COMMTIMEOUTS originalTimeouts;
    if (!GetCommTimeouts(hCom, &originalTimeouts)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;  LOG_STRING("Failed to get original COMMTIMEOUTS"));
        return UartStatusCode::PORT_ACCESS;
    }

    COMMTIMEOUTS newTimeouts = originalTimeouts;
    newTimeouts.WriteTotalTimeoutMultiplier = 0;
    newTimeouts.WriteTotalTimeoutConstant = u32WriteTimeout;

    if (!SetCommTimeouts(hCom, &newTimeouts)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to set COMMTIMEOUTS"));
        return UartStatusCode::PORT_ACCESS;
    }

    size_t szTotalBytesWritten = 0;
    while (szTotalBytesWritten < szSizeToWrite) {
        int iBytesWritten = _write(m_iHandle, pBuffer + szTotalBytesWritten, static_cast<unsigned int>(szSizeToWrite - szTotalBytesWritten));
        if (iBytesWritten <= 0) {
            SetCommTimeouts(hCom, &originalTimeouts);
            return (iBytesWritten == 0) ? UartStatusCode::WRITE_TIMEOUT : UartStatusCode::PORT_ACCESS;
        }
        szTotalBytesWritten += iBytesWritten;
    }

    SetCommTimeouts(hCom, &originalTimeouts);
    purge(true, true);
    return UartStatusCode::SUCCESS;
}


Driver::UartStatusCode Driver::setup(uint32_t u32Speed)
{
    HANDLE hCom = (HANDLE)_get_osfhandle(m_iHandle);
    DCB dcb = {0};
    dcb.DCBlength = sizeof(DCB);

    if (!GetCommState(hCom, &dcb)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("GetCommState() failed for handle:"); LOG_INT(m_iHandle));
        return UartStatusCode::PORT_ACCESS;
    }

    dcb.BaudRate = u32Speed;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fInX = FALSE;
    dcb.fOutX = FALSE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fNull = FALSE;
    dcb.fErrorChar = FALSE;
    dcb.fAbortOnError = FALSE;

    if (!SetCommState(hCom, &dcb)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SetCommState() failed for handle:"); LOG_INT(m_iHandle));
        return UartStatusCode::PORT_ACCESS;
    }

    purge(true, true);
    return UartStatusCode::SUCCESS;
}


uint32_t Driver::getBaud(uint32_t u32Speed)
{
    // On Windows, baud rates are usually passed directly as integers.
    // You can validate or log unsupported values here if needed.
    return u32Speed;
}

} // namespace UART