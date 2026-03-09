/*
 * FT232H – Windows platform implementation
 *
 * Uses the FTDI D2XX SDK (FTD2XX.dll).
 * Download from https://ftdichip.com/drivers/d2xx-drivers/
 *
 * Structure mirrors uFT4232Windows.cpp exactly, except:
 *   - FT_OPEN_BY_DESCRIPTION string must match the FT232H descriptor
 *   - No channel index offset (FT232H presents a single interface)
 *   - FT_SetUSBParameters uses 65536 byte in/out buffer sizes
 *
 * TODO: fill in FT_OpenEx / FT_ResetDevice / FT_SetBitMode / FT_Write /
 *       FT_Read / FT_Purge calls following the same pattern used in
 *       uFT4232Windows.cpp, substituting FT232H_PID where applicable.
 */

#include "FT232HBase.hpp"
#include "uLogger.hpp"

#define LT_HDR  "FT232H_BASE|"
#define LOG_HDR LOG_STRING(LT_HDR)

// Windows D2XX headers — only included when building for Windows
#if defined(_WIN32) || defined(_WIN64)
#include "ftd2xx.h"
#endif

#define FTHS (static_cast<FT_HANDLE>(m_hDevice))

FT232HBase::~FT232HBase()
{
    FT232HBase::close();
}

FT232HBase::Status FT232HBase::open_device(uint8_t u8DeviceIndex)
{
#if defined(_WIN32) || defined(_WIN64)
    FT_HANDLE hDev = nullptr;
    FT_STATUS ftStatus = FT_Open(static_cast<int>(u8DeviceIndex), &hDev);
    if (ftStatus != FT_OK) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("FT_Open() failed, index="); LOG_UINT32(u8DeviceIndex);
                  LOG_STRING("status="); LOG_UINT32(static_cast<uint32_t>(ftStatus)));
        return Status::PORT_ACCESS;
    }

    if (FT_ResetDevice(hDev) != FT_OK) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FT_ResetDevice() failed"));
        FT_Close(hDev);
        return Status::PORT_ACCESS;
    }

    if (FT_SetBitMode(hDev, 0x00, 0x02 /* BITMODE_MPSSE */) != FT_OK) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FT_SetBitMode(MPSSE) failed"));
        FT_Close(hDev);
        return Status::PORT_ACCESS;
    }

    FT_SetLatencyTimer(hDev, 1);
    FT_SetUSBParameters(hDev, 65536, 65536);

    m_hDevice = hDev;
    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("FT232H opened: device index="); LOG_UINT32(u8DeviceIndex));
    return Status::SUCCESS;
#else
    (void)u8DeviceIndex;
    LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Windows D2XX not available on this platform"));
    return Status::PORT_ACCESS;
#endif
}

FT232HBase::Status FT232HBase::close()
{
    if (m_hDevice) {
#if defined(_WIN32) || defined(_WIN64)
        FT_Close(FTHS);
#endif
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("FT232H closed"));
        m_hDevice = nullptr;
    }
    return Status::SUCCESS;
}

bool FT232HBase::is_open() const
{
    if (!m_hDevice) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Device not open"));
        return false;
    }
    return true;
}

FT232HBase::Status FT232HBase::mpsse_write(const uint8_t* buf, size_t len) const
{
#if defined(_WIN32) || defined(_WIN64)
    if (!buf || len == 0) return Status::INVALID_PARAM;
    DWORD written = 0;
    if (FT_Write(FTHS, const_cast<LPVOID>(static_cast<const void*>(buf)),
                 static_cast<DWORD>(len), &written) != FT_OK
        || written != static_cast<DWORD>(len))
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FT_Write failed"));
        return Status::WRITE_ERROR;
    }
    return Status::SUCCESS;
#else
    (void)buf; (void)len;
    return Status::WRITE_ERROR;
#endif
}

FT232HBase::Status FT232HBase::mpsse_read(uint8_t* buf, size_t len,
                                           uint32_t timeoutMs,
                                           size_t& bytesRead) const
{
#if defined(_WIN32) || defined(_WIN64)
    if (!buf || len == 0) return Status::INVALID_PARAM;
    bytesRead = 0;
    FT_SetTimeouts(FTHS, timeoutMs, 0);
    DWORD got = 0;
    if (FT_Read(FTHS, buf, static_cast<DWORD>(len), &got) != FT_OK) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FT_Read failed"));
        return Status::READ_ERROR;
    }
    bytesRead = static_cast<size_t>(got);
    if (bytesRead < len) return Status::READ_TIMEOUT;
    return Status::SUCCESS;
#else
    (void)buf; (void)len; (void)timeoutMs; (void)bytesRead;
    return Status::READ_ERROR;
#endif
}

FT232HBase::Status FT232HBase::mpsse_purge() const
{
#if defined(_WIN32) || defined(_WIN64)
    if (FT_Purge(FTHS, FT_PURGE_RX | FT_PURGE_TX) != FT_OK) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FT_Purge failed"));
        return Status::FLUSH_FAILED;
    }
    return Status::SUCCESS;
#else
    return Status::FLUSH_FAILED;
#endif
}
