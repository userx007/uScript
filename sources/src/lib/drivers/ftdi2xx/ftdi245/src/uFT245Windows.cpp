// FT245Base.hpp defines all BITMODE_* constants used by the driver layers
// (Sync, GPIO).  This platform file implements only the raw transport
// primitives (fifo_write, fifo_read, fifo_purge, open_device, close,
// is_open) and does not use the higher-level protocol logic directly.
#include "FT245Base.hpp"
#include "uLogger.hpp"

// FTD2XX  — FTDI D2XX driver
// Download: https://www.ftdichip.com/Drivers/D2XX.htm
// CMake:    -DFTD2XX_ROOT=<path>  (vendored under third_party/ftd2xx/)
// Link:     ftd2xx.lib
// Header:   ftd2xx.h
//
// FT_* types, FT_OK, FT_Open, FT_Write etc. come from this header.
// The BITMODE_* constants defined in FT245Base.hpp are based on FTDI
// application notes and match the D2XX FT_BITMODE_* values.
#include <ftd2xx.h>

#include <chrono>
#include <thread>

#define LT_HDR  "FT245_BASE  |"
#define LOG_HDR LOG_STRING(LT_HDR)

// Convenience cast
#define FT_HDL (static_cast<FT_HANDLE>(m_hDevice))


// ============================================================================
// Destructor
// ============================================================================

FT245Base::~FT245Base()
{
    FT245Base::close();
}


// ============================================================================
// open_device
// ============================================================================

FT245Base::Status FT245Base::open_device(Variant  variant,
                                          FifoMode fifoMode,
                                          uint8_t  u8DeviceIndex)
{
    // ── Validate mode vs variant ──────────────────────────────────────────────
    // FT245R does not support synchronous FIFO mode.
    if (variant == Variant::FT245R && fifoMode == FifoMode::Sync) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("open_device: FT245R does not support Sync FIFO mode"));
        return Status::INVALID_PARAM;
    }

    // ── Map physical device index → FTD2XX list index ─────────────────────────
    //
    // The FT245 is a single-interface USB device, so the FTD2XX list index
    // equals the physical device index directly (stride = 1, unlike FT2232
    // which has stride = 2).
    const DWORD ftIndex = static_cast<DWORD>(u8DeviceIndex);

    // ── VID/PID verification ──────────────────────────────────────────────────
    {
        DWORD     flags = 0, type = 0, devId = 0, locId = 0;
        char      serialNum[16]   = {0};
        char      description[64] = {0};
        FT_HANDLE tempHandle      = nullptr;

        if (FT_GetDeviceInfoDetail(ftIndex, &flags, &type, &devId, &locId,
                                   serialNum, description, &tempHandle) != FT_OK) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("FT_GetDeviceInfoDetail() failed, ftIndex=");
                      LOG_UINT32(ftIndex));
            return Status::PORT_ACCESS;
        }

        const uint16_t vid = static_cast<uint16_t>((devId >> 16) & 0xFFFFu);
        const uint16_t pid = static_cast<uint16_t>( devId        & 0xFFFFu);

        if (vid != FT245_VID || pid != FT245BM_PID) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("unexpected VID=0x"); LOG_HEX16(vid);
                      LOG_STRING(" PID=0x"); LOG_HEX16(pid);
                      LOG_STRING(" at ftIndex="); LOG_UINT32(ftIndex));
            return Status::PORT_ACCESS;
        }
    }

    // ── Open ──────────────────────────────────────────────────────────────────
    FT_HANDLE handle = nullptr;
    if (FT_Open(static_cast<int>(ftIndex), &handle) != FT_OK || !handle) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("FT_Open() failed, ftIndex="); LOG_UINT32(ftIndex));
        return Status::PORT_ACCESS;
    }

    // ── Reset and configure ───────────────────────────────────────────────────
    FT_ResetDevice(handle);
    FT_SetUSBParameters(handle, 65536u, 65536u);
    FT_SetLatencyTimer(handle, 1u);
    FT_SetTimeouts(handle, FT245_READ_DEFAULT_TIMEOUT, FT245_WRITE_DEFAULT_TIMEOUT);

    // Reset bitmode first (recommended before changing mode)
    if (FT_SetBitMode(handle, 0x00u, 0x00u) != FT_OK) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FT_SetBitMode(reset) failed"));
        FT_Close(handle);
        return Status::PORT_ACCESS;
    }

    // Set target FIFO mode
    // BITMODE_RESET (0x00) = async FIFO
    // BITMODE_SYNC_FIFO (0x40) = sync FIFO (FT245BM only)
    const UCHAR mode = (fifoMode == FifoMode::Sync)
                       ? static_cast<UCHAR>(BITMODE_SYNC_FIFO)
                       : static_cast<UCHAR>(BITMODE_RESET);

    if (mode != BITMODE_RESET) {
        // Only call SetBitMode again if not staying in default async mode
        if (FT_SetBitMode(handle, 0xFFu, mode) != FT_OK) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("FT_SetBitMode() failed, mode=0x"); LOG_HEX8(mode));
            FT_Close(handle);
            return Status::PORT_ACCESS;
        }
    }

    // Flush any stale data
    FT_Purge(handle, FT_PURGE_RX | FT_PURGE_TX);

    // ── Store state ───────────────────────────────────────────────────────────
    m_variant  = variant;
    m_fifoMode = fifoMode;
    m_hDevice  = static_cast<void*>(handle);

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("FT245 opened: variant="); LOG_UINT32(static_cast<uint8_t>(variant));
              LOG_STRING("fifoMode="); LOG_UINT32(static_cast<uint8_t>(fifoMode));
              LOG_STRING("ftIndex="); LOG_UINT32(ftIndex));

    return Status::SUCCESS;
}


// ============================================================================
// close / is_open
// ============================================================================

FT245Base::Status FT245Base::close()
{
    if (m_hDevice) {
        FT_Close(FT_HDL);
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("FT245 closed"));
        m_hDevice = nullptr;
    }
    return Status::SUCCESS;
}


bool FT245Base::is_open() const
{
    if (!m_hDevice) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Device not open"));
        return false;
    }
    return true;
}


// ============================================================================
// FIFO transport primitives
// ============================================================================

/**
 * @brief Write raw bytes into the FT245 TX FIFO
 *
 * Uses FT_Write() which performs a synchronous USB bulk write.
 */
FT245Base::Status FT245Base::fifo_write(const uint8_t* buf, size_t len) const
{
    if (!buf || len == 0) {
        return Status::INVALID_PARAM;
    }

    DWORD     written = 0;
    FT_STATUS ftStat  = FT_Write(FT_HDL,
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
 * @brief Read bytes from the FT245 RX FIFO with a timeout
 *
 * Polls FT_GetQueueStatus in a 1 ms sleep loop until the requested number
 * of bytes arrives or the timeout expires.
 */
FT245Base::Status FT245Base::fifo_read(uint8_t* buf, size_t len,
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
        if (FT_GetQueueStatus(FT_HDL, &queued) != FT_OK) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FT_GetQueueStatus() failed"));
            return Status::READ_ERROR;
        }

        if (queued > 0) {
            DWORD toRead = std::min(static_cast<DWORD>(len - bytesRead), queued);
            DWORD got    = 0;
            if (FT_Read(FT_HDL, buf + bytesRead, toRead, &got) != FT_OK) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("FT_Read() failed"));
                return Status::READ_ERROR;
            }
            bytesRead += static_cast<size_t>(got);
        }

        if (bytesRead < len) {
            if (std::chrono::steady_clock::now() >= deadline) {
                LOG_PRINT(LOG_ERROR, LOG_HDR;
                          LOG_STRING("fifo_read timeout: wanted="); LOG_UINT32(len);
                          LOG_STRING("got="); LOG_UINT32(bytesRead));
                return Status::READ_TIMEOUT;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    return Status::SUCCESS;
}


/**
 * @brief Purge the device's RX and TX FIFOs
 */
FT245Base::Status FT245Base::fifo_purge() const
{
    FT_STATUS ftStat = FT_Purge(FT_HDL, FT_PURGE_RX | FT_PURGE_TX);
    if (ftStat != FT_OK) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("FT_Purge() failed, status="); LOG_UINT32(ftStat));
        return Status::FLUSH_FAILED;
    }
    return Status::SUCCESS;
}
