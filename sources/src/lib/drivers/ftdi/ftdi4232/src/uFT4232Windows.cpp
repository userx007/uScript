// FT4232Base.hpp defines all MPSSE_* opcode constants used by the protocol
// layers (I²C, SPI, GPIO).  This platform file implements only the raw
// transport primitives (mpsse_write, mpsse_read, mpsse_purge, open_device,
// close, is_open) and does not use the opcode constants directly.
#include "FT4232Base.hpp"
#include "uLogger.hpp"

// FTD2XX  — FTDI D2XX driver
// Download: https://www.ftdichip.com/Drivers/D2XX.htm
// CMake:    set(FTD2XX_ROOT "C:/path/to/ftd2xx") in toolchain or CMakeLists
// Link:     ftd2xx.lib  (placed in FTD2XX_ROOT/i386 or x64)
// Header:   ftd2xx.h    (placed in FTD2XX_ROOT)
//
// FT_* types, FT_OK, FT_Open, FT_Write etc. come from this header.
// The MPSSE_* opcode byte constants are NOT from FTD2XX — they are
// defined in FT4232Base.hpp based on FTDI AN_108.
#include <ftd2xx.h>

#include <cstring>
#include <string>
#include <chrono>
#include <thread>


#define LT_HDR  "FT4232_BASE|"
#define LOG_HDR LOG_STRING(LT_HDR)

// Convenience cast
#define FT_HDL (static_cast<FT_HANDLE>(m_hDevice))


// ============================================================================
// Destructor
// ============================================================================

FT4232Base::~FT4232Base()
{
    FT4232Base::close();
}


// ============================================================================
// open_device
// ============================================================================

FT4232Base::Status FT4232Base::open_device(Channel channel, uint8_t u8DeviceIndex)
{
    if (channel != Channel::A && channel != Channel::B) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("open_device: FT4232H MPSSE only available on channels A and B"));
        return Status::INVALID_PARAM;
    }

    // ── Map (physical device index, channel) → FTD2XX device list index ──────
    //
    // FT4232H presents 4 USB interfaces. They appear sequentially in the
    // FTD2XX device list: the n-th chip occupies indices [n*4, n*4+3].
    // Channels A and B are indices 0 and 1 of that chip's block.
    //
    DWORD ftIndex = static_cast<DWORD>(u8DeviceIndex) * 4u
                    + static_cast<DWORD>(channel);  // Channel::A=0, Channel::B=1

    // Verify the device at that index actually has the expected VID/PID.
    {
        FT_DEVICE_LIST_INFO_NODE info;
        std::memset(&info, 0, sizeof(info));

        // FT_GetDeviceInfoDetail fills info for the device at ftIndex.
        // Type FT_DEVICE_4232H = 9 in the FTD2XX SDK.
        DWORD flags, type, devId, locId;
        char  serialNum[16] = {0};
        char  description[64] = {0};
        FT_HANDLE tempHandle = nullptr;

        FT_STATUS infoStatus = FT_GetDeviceInfoDetail(
            ftIndex, &flags, &type, &devId, &locId,
            serialNum, description, &tempHandle);

        if (infoStatus != FT_OK) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("FT_GetDeviceInfoDetail() failed, ftIndex="); LOG_UINT32(ftIndex);
                      LOG_STRING("status="); LOG_UINT32(infoStatus));
            return Status::PORT_ACCESS;
        }

        // devId encodes VID in high word, PID in low word.
        uint16_t vid = static_cast<uint16_t>((devId >> 16) & 0xFFFFu);
        uint16_t pid = static_cast<uint16_t>( devId        & 0xFFFFu);
        if (vid != FT4232H_VID || pid != FT4232H_PID) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Device at ftIndex"); LOG_UINT32(ftIndex);
                      LOG_STRING("is not an FT4232H (VID=0x"); LOG_HEX16(vid);
                      LOG_STRING(", PID=0x"); LOG_HEX16(pid); LOG_STRING(")"));
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

    // ── Reset and enable MPSSE mode ──────────────────────────────────────────
    // Step 1: Reset to a known state
    FT_ResetDevice(handle);

    // Step 2: Set USB parameters
    FT_SetUSBParameters(handle, 65536, 65536);  // read/write transfer sizes

    // Step 3: Configure timeouts (generous; the common layer manages its own)
    FT_SetTimeouts(handle,
                   FT4232_READ_DEFAULT_TIMEOUT,
                   FT4232_WRITE_DEFAULT_TIMEOUT);

    // Step 4: Set latency timer to 1 ms (default 16 ms is too slow)
    FT_SetLatencyTimer(handle, 1);

    // Step 5: Reset the bitmode before switching (recommended by FTDI AN_108)
    if (FT_SetBitMode(handle, 0x00, 0x00) != FT_OK) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FT_SetBitMode(reset) failed"));
        FT_Close(handle);
        return Status::PORT_ACCESS;
    }

    // Step 6: Enable MPSSE mode (mode = 0x02)
    // Mask 0xFF = all pins initially output at 0; the MPSSE commands will
    // configure directions correctly before any bus activity.
    if (FT_SetBitMode(handle, 0xFF, 0x02) != FT_OK) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FT_SetBitMode(MPSSE) failed"));
        FT_Close(handle);
        return Status::PORT_ACCESS;
    }

    // Step 7: Allow the chip to settle
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Step 8: Flush any stale data
    FT_Purge(handle, FT_PURGE_RX | FT_PURGE_TX);

    m_hDevice = static_cast<void*>(handle);

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("FT4232H opened: channel="); LOG_UINT32(static_cast<uint8_t>(channel));
              LOG_STRING("ftIndex="); LOG_UINT32(ftIndex);
              LOG_STRING("device index="); LOG_UINT32(u8DeviceIndex));

    return Status::SUCCESS;
}


// ============================================================================
// close / is_open
// ============================================================================

FT4232Base::Status FT4232Base::close()
{
    if (m_hDevice) {
        FT_Close(FT_HDL);
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("FT4232H closed"));
        m_hDevice = nullptr;
    }
    return Status::SUCCESS;
}


bool FT4232Base::is_open() const
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

/**
 * @brief Write raw MPSSE command bytes via FT_Write
 */
FT4232Base::Status FT4232Base::mpsse_write(const uint8_t* buf, size_t len) const
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


/**
 * @brief Read response bytes via FT_Read with a timeout
 *
 * FT_GetQueueStatus is polled until enough bytes are available or the
 * timeout expires, then FT_Read fetches them all in one call.
 */
FT4232Base::Status FT4232Base::mpsse_read(uint8_t* buf, size_t len,
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


/**
 * @brief Purge device RX and TX FIFOs
 */
FT4232Base::Status FT4232Base::mpsse_purge() const
{
    FT_STATUS ftStat = FT_Purge(FT_HDL, FT_PURGE_RX | FT_PURGE_TX);
    if (ftStat != FT_OK) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("FT_Purge() failed, status="); LOG_UINT32(ftStat));
        return Status::FLUSH_FAILED;
    }
    return Status::SUCCESS;
}
