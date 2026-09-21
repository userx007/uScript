#include "uCh341.hpp"
#include "uLogger.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <windows.h>

// The CH340/CH341 Windows VCP driver (CH341SER.SYS) enumerates the device as
// a standard COMx port, exactly like any other USB-serial adapter. That
// means the exact same CRT-fd-wrapping-a-Win32-HANDLE approach used by
// uUartWindows.cpp applies here unchanged: _sopen_s()/_open() on "\\\\.\\COMx"
// gives us the int handle stored in m_iHandle, and _get_osfhandle() recovers
// the underlying Win32 HANDLE whenever DCB/COMMTIMEOUTS/modem-line APIs are
// needed. The vendor's CH341DLL.dll (used for the chip's HID/parallel/I2C/SPI
// modes) is intentionally NOT needed for this serial-port mode of operation.
//
// The one thing this port can't reproduce from the Linux side is the
// termios2/BOTHER trick for "any" arbitrary baud rate: on Windows the value
// placed in DCB.BaudRate is handed to the VCP driver as-is (the same
// approach uUartWindows.cpp's getBaud() uses), which in practice covers the
// same set of non-standard rates CH341SER.SYS's Windows driver documents as
// supported.

// ============================================================================
// PUBLIC INTERFACE IMPLEMENTATION
// ============================================================================

// Mirrors the Linux <asm/termbits.h> TIOCM_* bit values used by
// uCh341Linux.cpp's get_modem_lines()/set_dtr_rts(), so callers see the same
// bitmask semantics regardless of platform. Windows has no direct TIOCM_*
// definitions, so they're reproduced locally rather than pulled from a
// system header.
namespace {
constexpr unsigned int kTiocmDtr = 0x002;
constexpr unsigned int kTiocmRts = 0x004;
constexpr unsigned int kTiocmCts = 0x020;
constexpr unsigned int kTiocmCd  = 0x040; // Carrier Detect / RLSD
constexpr unsigned int kTiocmRi  = 0x080; // Ring Indicator
constexpr unsigned int kTiocmDsr = 0x100;
} // namespace

CH341::Status CH341::open(const std::string &strDevice, uint32_t u32Speed)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (strDevice.empty() || u32Speed == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Invalid parameter(s):");
                  LOG_STRING(strDevice.c_str());
                  LOG_STRING("Baudrate:"); LOG_UINT32(u32Speed));
        return Status::INVALID_PARAM;
    }

#ifdef _MSC_VER
    int openFlags = _O_RDWR | _O_BINARY;
    int shareMode = _SH_DENYRW; // Deny other processes to access the file
    int fileHandle;

    errno_t err = _sopen_s(&fileHandle, strDevice.c_str(), openFlags, shareMode, _S_IREAD | _S_IWRITE);
    if (err == 0) {
        m_iHandle = fileHandle;
    } else {
        m_iHandle = -1;
    }
#else
    int openFlags = O_RDWR | O_NOINHERIT | O_BINARY;
    m_iHandle     = _open(strDevice.c_str(), openFlags);
#endif

    if (m_iHandle < 0) {
        int errnoRet = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Failed to open ["); LOG_STRING(strDevice.c_str());
                  LOG_UINT32(u32Speed); LOG_STRING("] errno:"); LOG_INT(errnoRet));
        return Status::PORT_ACCESS;
    }

    CH341::Status result = setup(u32Speed);
    if (result != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Failed to configure ["); LOG_STRING(strDevice.c_str());
                  LOG_UINT32(u32Speed); LOG_INT(m_iHandle);
                  LOG_STRING("] Error:"); LOG_INT(result));
        _close(m_iHandle);
        m_iHandle = -1;
        return Status::PORT_ACCESS;
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("CH341 ["); LOG_STRING(strDevice.c_str());
              LOG_UINT32(u32Speed); LOG_STRING("] opened, handle:");
              LOG_INT(m_iHandle));

    return Status::SUCCESS;
}

CH341::Status CH341::close()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_iHandle >= 0) {
        _close(m_iHandle);
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("CH341 closed, handle:"); LOG_INT(m_iHandle));
        m_iHandle = -1;
    }
    return Status::SUCCESS;
}

CH341::Status CH341::purge(bool bInput, bool bOutput) const
{
    HANDLE hCom        = (HANDLE)_get_osfhandle(m_iHandle);
    DWORD purgeOptions = 0;
    if (bInput) {
        purgeOptions |= PURGE_RXCLEAR;
    }
    if (bOutput) {
        purgeOptions |= PURGE_TXCLEAR;
    }

    if (purgeOptions == 0) {
        return Status::SUCCESS;
    }

    if (!PurgeComm(hCom, purgeOptions)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("PurgeComm() failed for handle:"); LOG_INT(m_iHandle));
        return Status::FLUSH_FAILED;
    }

    return Status::SUCCESS;
}

CH341::Status CH341::timeout_read(uint32_t u32ReadTimeout, std::span<uint8_t> buffer, size_t &szBytesRead,
                                  std::stop_token stop_tok) const
{
    if (buffer.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("timeout_read: invalid parameter"));
        return Status::INVALID_PARAM;
    }

    szBytesRead = 0;

    HANDLE hCom = (HANDLE)_get_osfhandle(m_iHandle);
    if (hCom == INVALID_HANDLE_VALUE) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid handle from _get_osfhandle"));
        return Status::PORT_ACCESS;
    }

    COMMTIMEOUTS originalTimeouts;
    if (!GetCommTimeouts(hCom, &originalTimeouts)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to get original COMMTIMEOUTS"));
        return Status::PORT_ACCESS;
    }

    // Same "poll in bounded slices" idea as the POSIX side's poll() loop, so
    // a stop request can be observed at slice granularity rather than
    // blocking the whole timeout.
    constexpr DWORD kReadSliceMs           = 200;
    const bool bInfinite                   = (u32ReadTimeout == 0);
    const auto tDeadline                   = std::chrono::steady_clock::now() + std::chrono::milliseconds(u32ReadTimeout);

    COMMTIMEOUTS newTimeouts               = originalTimeouts;
    newTimeouts.ReadIntervalTimeout        = 0;
    newTimeouts.ReadTotalTimeoutMultiplier = 0;

    size_t szTotalBytesRead                = 0;
    while (szTotalBytesRead < buffer.size()) {
        if (stop_tok.stop_requested()) {
            SetCommTimeouts(hCom, &originalTimeouts);
            return Status::READ_TIMEOUT;
        }

        DWORD dwSliceMs = kReadSliceMs;
        if (!bInfinite) {
            const auto remaining = tDeadline - std::chrono::steady_clock::now();
            if (remaining <= std::chrono::milliseconds(0)) {
                SetCommTimeouts(hCom, &originalTimeouts);
                return Status::READ_TIMEOUT;
            }
            dwSliceMs = static_cast<DWORD>(std::min<int64_t>(kReadSliceMs,
                                                             std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count()));
        }

        newTimeouts.ReadTotalTimeoutConstant = dwSliceMs;
        if (!SetCommTimeouts(hCom, &newTimeouts)) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to set COMMTIMEOUTS"));
            SetCommTimeouts(hCom, &originalTimeouts);
            return Status::PORT_ACCESS;
        }

        DWORD dwBytesToRead = static_cast<DWORD>(buffer.size() - szTotalBytesRead);
        int iBytesRead      = _read(m_iHandle, buffer.data() + szTotalBytesRead, dwBytesToRead);

        if (iBytesRead < 0) {
            int err = errno;
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("_read() failed"); LOG_INT(err));
            SetCommTimeouts(hCom, &originalTimeouts);
            return Status::READ_ERROR;
        } else if (iBytesRead == 0) {
            SetCommTimeouts(hCom, &originalTimeouts);
            return Status::READ_TIMEOUT;
        }

        szTotalBytesRead += iBytesRead;
    }

    SetCommTimeouts(hCom, &originalTimeouts);
    szBytesRead = szTotalBytesRead;
    return Status::SUCCESS;
}

CH341::Status CH341::timeout_write(uint32_t u32WriteTimeout, std::span<const uint8_t> buffer, size_t &szBytesWritten,
                                   std::stop_token /*stop_tok*/) const
{
    if (buffer.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid parameter: buffer.empty()"));
        return Status::INVALID_PARAM;
    }

    HANDLE hCom = (HANDLE)_get_osfhandle(m_iHandle);
    if (hCom == INVALID_HANDLE_VALUE) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid handle from _get_osfhandle"));
        return Status::PORT_ACCESS;
    }

    COMMTIMEOUTS originalTimeouts;
    if (!GetCommTimeouts(hCom, &originalTimeouts)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to get original COMMTIMEOUTS"));
        return Status::PORT_ACCESS;
    }

    COMMTIMEOUTS newTimeouts                = originalTimeouts;
    newTimeouts.WriteTotalTimeoutMultiplier = 0;
    newTimeouts.WriteTotalTimeoutConstant   = u32WriteTimeout;

    if (!SetCommTimeouts(hCom, &newTimeouts)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to set COMMTIMEOUTS"));
        return Status::PORT_ACCESS;
    }

    szBytesWritten = 0;
    while (szBytesWritten < buffer.size()) {
        int iBytesWritten = _write(m_iHandle, buffer.data() + szBytesWritten,
                                   static_cast<unsigned int>(buffer.size() - szBytesWritten));
        if (iBytesWritten <= 0) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CH341 write error"); LOG_INT(errno));
            SetCommTimeouts(hCom, &originalTimeouts);
            return (iBytesWritten == 0) ? Status::WRITE_TIMEOUT : Status::WRITE_ERROR;
        }
        szBytesWritten += iBytesWritten;
    }

    SetCommTimeouts(hCom, &originalTimeouts);
    return Status::SUCCESS;
}

/**
 * @brief Configure the line via DCB. Mirrors the Linux setup()'s fixed 8N1,
 *        no-flow-control framing (CS8 | CLOCAL | CREAD there); the baud
 *        value is passed straight through, same as uUartWindows.cpp's
 *        getBaud(), which is what lets CH341SER.SYS's non-standard rates
 *        through unfiltered.
 */
CH341::Status CH341::setup(uint32_t u32Speed) const
{
    HANDLE hCom = (HANDLE)_get_osfhandle(m_iHandle);
    DCB dcb;
    ZeroMemory(&dcb, sizeof(DCB));
    dcb.DCBlength = sizeof(DCB);

    if (!GetCommState(hCom, &dcb)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("GetCommState() failed for handle:"); LOG_INT(m_iHandle));
        return Status::PORT_ACCESS;
    }

    dcb.BaudRate      = u32Speed;
    dcb.ByteSize      = 8;
    dcb.Parity        = NOPARITY;
    dcb.StopBits      = ONESTOPBIT;
    dcb.fParity       = FALSE;
    dcb.fBinary       = TRUE;
    dcb.fInX          = FALSE;
    dcb.fOutX         = FALSE;
    dcb.fRtsControl   = RTS_CONTROL_DISABLE;
    dcb.fDtrControl   = DTR_CONTROL_DISABLE;
    dcb.fOutxCtsFlow  = FALSE;
    dcb.fOutxDsrFlow  = FALSE;
    dcb.fNull         = FALSE;
    dcb.fErrorChar    = FALSE;
    dcb.fAbortOnError = FALSE;

    if (!SetCommState(hCom, &dcb)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SetCommState() failed for handle:"); LOG_INT(m_iHandle));
        return Status::PORT_ACCESS;
    }

    purge(true, true);
    return Status::SUCCESS;
}

CH341::Status CH341::get_modem_lines(unsigned int &u32Lines) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_iHandle < 0) {
        return Status::PORT_ACCESS;
    }

    HANDLE hCom = (HANDLE)_get_osfhandle(m_iHandle);
    if (hCom == INVALID_HANDLE_VALUE) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid handle from _get_osfhandle"));
        return Status::PORT_ACCESS;
    }

    DWORD dwModemStatus = 0;
    if (!GetCommModemStatus(hCom, &dwModemStatus)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("GetCommModemStatus() failed for handle:"); LOG_INT(m_iHandle));
        return Status::READ_ERROR;
    }

    // GetCommModemStatus only reports the input lines (CTS/DSR/RING/RLSD) —
    // unlike Linux's TIOCMGET, Windows has no API to read back the DTR/RTS
    // *output* state we last set via EscapeCommFunction(), so those two
    // bits are simply left clear here.
    unsigned int u32Result = 0;
    if (dwModemStatus & MS_CTS_ON) {
        u32Result |= kTiocmCts;
    }
    if (dwModemStatus & MS_DSR_ON) {
        u32Result |= kTiocmDsr;
    }
    if (dwModemStatus & MS_RING_ON) {
        u32Result |= kTiocmRi;
    }
    if (dwModemStatus & MS_RLSD_ON) {
        u32Result |= kTiocmCd;
    }

    u32Lines = u32Result;
    return Status::SUCCESS;
}

CH341::Status CH341::set_dtr_rts(bool bDtr, bool bRts) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_iHandle < 0) {
        return Status::PORT_ACCESS;
    }

    HANDLE hCom = (HANDLE)_get_osfhandle(m_iHandle);
    if (hCom == INVALID_HANDLE_VALUE) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid handle from _get_osfhandle"));
        return Status::PORT_ACCESS;
    }

    if (!EscapeCommFunction(hCom, bDtr ? SETDTR : CLRDTR)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("EscapeCommFunction(DTR) failed for handle:"); LOG_INT(m_iHandle));
        return Status::WRITE_ERROR;
    }
    if (!EscapeCommFunction(hCom, bRts ? SETRTS : CLRRTS)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("EscapeCommFunction(RTS) failed for handle:"); LOG_INT(m_iHandle));
        return Status::WRITE_ERROR;
    }

    return Status::SUCCESS;
}
