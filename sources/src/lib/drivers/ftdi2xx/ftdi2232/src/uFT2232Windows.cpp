// FT2232Base.hpp defines all MPSSE_* opcode constants used by the protocol
// layers (I²C, SPI, GPIO).  This platform file implements only the raw
// transport primitives (mpsse_write, mpsse_read, mpsse_purge, open_device,
// close, is_open) and does not use the opcode constants directly.
#include "FT2232Base.hpp"
#include "uLogger.hpp"

// FTD2XX  — FTDI D2XX driver
// Download: https://www.ftdichip.com/Drivers/D2XX.htm
// CMake:    -DFTD2XX_ROOT=<path>  (vendored under third_party/ftd2xx/)
// Link:     ftd2xx.lib
// Header:   ftd2xx.h
//
// FT_* types, FT_OK, FT_Open, FT_Write etc. come from this header.
// The MPSSE_* opcode byte constants are NOT from FTD2XX — they are
// defined in FT2232Base.hpp based on FTDI AN_108.
#include <ftd2xx.h>

#include <cstring>
#include <chrono>
#include <thread>


#define LT_HDR  "FT2232_BASE |"
#define LOG_HDR LOG_STRING(LT_HDR)

// Convenience cast
#define FT_HDL (static_cast<FT_HANDLE>(m_hDevice))


// ============================================================================
// Destructor
// ============================================================================

FT2232Base::~FT2232Base()
{
    FT2232Base::close();
}


// ============================================================================
// open_device
// ============================================================================

FT2232Base::Status FT2232Base::open_device(Variant  variant,
                                            Channel  channel,
                                            uint8_t  u8DeviceIndex)
{
    // ── Channel validation ────────────────────────────────────────────────────
    if (variant == Variant::FT2232D && channel != Channel::A) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("open_device: FT2232D MPSSE is only available on channel A"));
        return Status::INVALID_PARAM;
    }

    // ── Map (physical device index, channel) → FTD2XX device list index ──────
    //
    // FT2232H/D presents 2 USB interfaces (A=0, B=1) to the OS.
    // In the FTD2XX device list the n-th chip occupies indices [n*2, n*2+1].
    //
    // Note: this differs from FT4232H which has 4 interfaces (stride = 4).
    //
    DWORD ftIndex = static_cast<DWORD>(u8DeviceIndex) * 2u
                    + static_cast<DWORD>(channel); // Channel::A=0, Channel::B=1

    // ── VID/PID verification ──────────────────────────────────────────────────
    {
        DWORD flags, type, devId, locId;
        char  serialNum[16]   = {0};
        char  description[64] = {0};
        FT_HANDLE tempHandle  = nullptr;

        FT_STATUS infoStatus = FT_GetDeviceInfoDetail(
            ftIndex, &flags, &type, &devId, &locId,
            serialNum, description, &tempHandle);

        if (infoStatus != FT_OK) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("FT_GetDeviceInfoDetail() failed, ftIndex="); LOG_UINT32(ftIndex);
                      LOG_STRING("status="); LOG_UINT32(infoStatus));
            return Status::PORT_ACCESS;
        }

        // devId: high word = VID, low word = PID
        const uint16_t vid       = static_cast<uint16_t>((devId >> 16) & 0xFFFFu);
        const uint16_t pid       = static_cast<uint16_t>( devId        & 0xFFFFu);
        const uint16_t expectPid = (variant == Variant::FT2232H) ? FT2232H_PID : FT2232D_PID;

        if (vid != FT2232_VID || pid != expectPid) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Device at ftIndex"); LOG_UINT32(ftIndex);
                      LOG_STRING("VID=0x"); LOG_HEX16(vid);
                      LOG_STRING("PID=0x"); LOG_HEX16(pid);
                      LOG_STRING("expected PID=0x"); LOG_HEX16(expectPid));
            return Status::PORT_ACCESS;
        }
    }

    // ── Open the device ───────────────────────────────────────────────────────
    FT_HANDLE handle = nullptr;
    FT_STATUS ftStat = FT_Open(static_cast<int>(ftIndex), &handle);
    if (ftStat != FT_OK || !handle) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("FT_Open() failed, ftIndex="); LOG_UINT32(ftIndex);
                  LOG_STRING("status="); LOG_UINT32(ftStat));
        return Status::PORT_ACCESS;
    }

    // ── Reset and configure ───────────────────────────────────────────────────
    FT_ResetDevice(handle);
    FT_SetUSBParameters(handle, 65536, 65536);
    FT_SetTimeouts(handle, FT2232_READ_DEFAULT_TIMEOUT, FT2232_WRITE_DEFAULT_TIMEOUT);
    FT_SetLatencyTimer(handle, 1);

    // Reset bitmode before enabling MPSSE (recommended by FTDI AN_108)
    if (FT_SetBitMode(handle, 0x00, 0x00) != FT_OK) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FT_SetBitMode(reset) failed"));
        FT_Close(handle);
        return Status::PORT_ACCESS;
    }

    // Enable MPSSE mode (0x02)
    if (FT_SetBitMode(handle, 0xFF, 0x02) != FT_OK) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FT_SetBitMode(MPSSE) failed"));
        FT_Close(handle);
        return Status::PORT_ACCESS;
    }

    // Allow the chip to settle then flush stale data
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    FT_Purge(handle, FT_PURGE_RX | FT_PURGE_TX);

    // ── Store state ───────────────────────────────────────────────────────────
    m_variant = variant;
    m_hDevice = static_cast<void*>(handle);

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("FT2232 opened: variant="); LOG_UINT32(static_cast<uint8_t>(variant));
              LOG_STRING("channel="); LOG_UINT32(static_cast<uint8_t>(channel));
              LOG_STRING("ftIndex="); LOG_UINT32(ftIndex);
              LOG_STRING("device index="); LOG_UINT32(u8DeviceIndex));

    return Status::SUCCESS;
}


// ============================================================================
// close / is_open
// ============================================================================

FT2232Base::Status FT2232Base::close()
{
    if (m_hDevice) {
        FT_Close(FT_HDL);
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("FT2232 closed"));
        m_hDevice = nullptr;
    }
    return Status::SUCCESS;
}


bool FT2232Base::is_open() const
{
    if (!m_hDevice) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Device not open"));
        return false;
    }
    return true;
}


// ============================================================================
// MPSSE transport primitives
// ============================================================================

FT2232Base::Status FT2232Base::mpsse_write(const uint8_t* buf, size_t len) const
{
    if (!buf || len == 0) {
        return Status::INVALID_PARAM;
    }

    DWORD written = 0;
    FT_STATUS ftStat = FT_Write(FT_HDL,
                                const_cast<LPVOID>(static_cast<const void*>(buf)),
                                static_cast<DWORD>(len),
                                &written);

    if (ftStat != FT_OK) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("FT_Write() failed, status="); LOG_UINT32(ftStat));
        return Status::WRITE_ERROR;
    }
    if (written != static_cast<DWORD>(len)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("FT_Write() short write, wanted="); LOG_UINT32(len);
                  LOG_STRING("got="); LOG_UINT32(written));
        return Status::WRITE_ERROR;
    }

    return Status::SUCCESS;
}


FT2232Base::Status FT2232Base::mpsse_read(uint8_t* buf, size_t len,
                                           uint32_t timeoutMs,
                                           size_t& bytesRead) const
{
    if (!buf || len == 0) {
        return Status::INVALID_PARAM;
    }

    bytesRead = 0;

    auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::milliseconds(timeoutMs);

    while (bytesRead < len) {
        DWORD queued = 0;
        FT_STATUS ftStat = FT_GetQueueStatus(FT_HDL, &queued);
        if (ftStat != FT_OK) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("FT_GetQueueStatus() failed, status="); LOG_UINT32(ftStat));
            return Status::READ_ERROR;
        }

        if (queued > 0) {
            DWORD toRead = std::min(static_cast<DWORD>(len - bytesRead), queued);
            DWORD got    = 0;
            ftStat = FT_Read(FT_HDL, buf + bytesRead, toRead, &got);
            if (ftStat != FT_OK) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("FT_Read() failed, status="); LOG_UINT32(ftStat));
                return Status::READ_ERROR;
            }
            bytesRead += static_cast<size_t>(got);
        }

        if (bytesRead < len) {
            if (std::chrono::steady_clock::now() >= deadline) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("mpsse_read timeout: wanted="); LOG_UINT32(len);
                          LOG_STRING("got="); LOG_UINT32(bytesRead));
                return Status::READ_TIMEOUT;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    return Status::SUCCESS;
}


FT2232Base::Status FT2232Base::mpsse_purge() const
{
    FT_STATUS ftStat = FT_Purge(FT_HDL, FT_PURGE_RX | FT_PURGE_TX);
    if (ftStat != FT_OK) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("FT_Purge() failed, status="); LOG_UINT32(ftStat));
        return Status::FLUSH_FAILED;
    }
    return Status::SUCCESS;
}
