// FT245Base.hpp defines all BITMODE_* constants used by the driver layers
// (Sync, GPIO).  This platform file implements only the raw transport
// primitives (fifo_write, fifo_read, fifo_purge, open_device, close,
// is_open) and does not use the higher-level protocol logic directly.
#include "FT245Base.hpp"
#include "uLogger.hpp"

// libftdi1  — package: libftdi1-dev (Debian/Ubuntu)
//             CMake:   PkgConfig libftdi1  (or vendored via LIBFTDI1_ROOT)
//             Header:  <ftdi.h>  (may also be <libftdi1/ftdi.h> on some distros)
//
// BITMODE_* and other ftdi_* symbols come from this header.
// The BITMODE_* constants defined in FT245Base.hpp are based on FTDI
// application notes and match the values in libftdi1.
#include <ftdi.h>

#include <chrono>
#include <thread>

#define LT_HDR  "FT245_BASE  |"
#define LOG_HDR LOG_STRING(LT_HDR)

// Convenience cast — avoids repeating the cast everywhere in this file
#define CTX (static_cast<struct ftdi_context*>(m_hDevice))


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

    // ── Allocate a new ftdi_context ───────────────────────────────────────────
    struct ftdi_context* ctx = ftdi_new();
    if (!ctx) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ftdi_new() returned nullptr (out of memory?)"));
        return Status::OUT_OF_MEMORY;
    }

    // ── FT245 has a single interface — always INTERFACE_ANY ──────────────────
    if (ftdi_set_interface(ctx, INTERFACE_ANY) < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ftdi_set_interface() failed:");
                  LOG_STRING(ftdi_get_error_string(ctx)));
        ftdi_free(ctx);
        return Status::PORT_ACCESS;
    }

    // ── Open by VID/PID and device index ─────────────────────────────────────
    // FT245BM and FT245R share VID 0x0403, PID 0x6001.
    const uint16_t pid = FT245BM_PID; // same for both variants

    if (ftdi_usb_open_desc_index(ctx,
                                 static_cast<int>(FT245_VID),
                                 static_cast<int>(pid),
                                 nullptr,
                                 nullptr,
                                 static_cast<unsigned int>(u8DeviceIndex)) < 0)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ftdi_usb_open_desc_index() failed, PID=0x"); LOG_HEX16(pid);
                  LOG_STRING("index="); LOG_UINT32(u8DeviceIndex);
                  LOG_STRING("error:"); LOG_STRING(ftdi_get_error_string(ctx)));
        ftdi_free(ctx);
        return Status::PORT_ACCESS;
    }

    // ── Reset to a clean state ───────────────────────────────────────────────
    if (ftdi_usb_reset(ctx) < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ftdi_usb_reset() failed:"); LOG_STRING(ftdi_get_error_string(ctx)));
        ftdi_usb_close(ctx);
        ftdi_free(ctx);
        return Status::PORT_ACCESS;
    }

    // ── Set FIFO bit-mode ────────────────────────────────────────────────────
    // BITMODE_RESET (0x00) = async FIFO (both variants)
    // BITMODE_SYNC_FIFO (0x40) = sync FIFO (FT245BM only)
    const uint8_t mode = (fifoMode == FifoMode::Sync)
                         ? BITMODE_SYNC_FIFO
                         : BITMODE_RESET;

    // Mask 0xFF = all 8 data pins; direction is handled internally by the
    // device in FIFO mode (D0–D7 switch direction per-transfer).
    if (ftdi_set_bitmode(ctx, 0xFFu, mode) < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ftdi_set_bitmode() failed, mode=0x"); LOG_HEX8(mode);
                  LOG_STRING(":"); LOG_STRING(ftdi_get_error_string(ctx)));
        ftdi_usb_close(ctx);
        ftdi_free(ctx);
        return Status::PORT_ACCESS;
    }

    // ── Latency timer ────────────────────────────────────────────────────────
    // 1 ms gives the fastest read response; default is 16 ms.
    if (ftdi_set_latency_timer(ctx, 1) < 0) {
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("ftdi_set_latency_timer() failed (non-fatal)"));
    }

    // ── Read/write chunk sizes ───────────────────────────────────────────────
    (void)ftdi_read_data_set_chunksize(ctx, 65536);
    (void)ftdi_write_data_set_chunksize(ctx, 65536);

    // Flush any stale data in the FIFO
    (void)ftdi_tcioflush(ctx);

    // ── Store state ──────────────────────────────────────────────────────────
    m_variant  = variant;
    m_fifoMode = fifoMode;
    m_hDevice  = ctx;

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("FT245 opened: variant=");
              LOG_UINT32(static_cast<uint8_t>(variant));
              LOG_STRING("fifoMode="); LOG_UINT32(static_cast<uint8_t>(fifoMode));
              LOG_STRING("index="); LOG_UINT32(u8DeviceIndex));

    return Status::SUCCESS;
}


// ============================================================================
// close / is_open
// ============================================================================

FT245Base::Status FT245Base::close()
{
    if (m_hDevice) {
        ftdi_usb_close(CTX);
        ftdi_free(CTX);
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
 * Uses ftdi_write_data() which performs a synchronous USB bulk write.
 */
FT245Base::Status FT245Base::fifo_write(const uint8_t* buf, size_t len) const
{
    if (!buf || len == 0) {
        return Status::INVALID_PARAM;
    }

    int ret = ftdi_write_data(CTX,
                              const_cast<uint8_t*>(buf),
                              static_cast<int>(len));
    if (ret < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ftdi_write_data() failed, ret="); LOG_INT(ret);
                  LOG_STRING(ftdi_get_error_string(CTX)));
        return Status::WRITE_ERROR;
    }
    if (static_cast<size_t>(ret) != len) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ftdi_write_data() short write, wanted="); LOG_UINT32(len);
                  LOG_STRING("got="); LOG_INT(ret));
        return Status::WRITE_ERROR;
    }

    return Status::SUCCESS;
}


/**
 * @brief Read bytes from the FT245 RX FIFO with a timeout
 *
 * ftdi_read_data() is non-blocking; this wrapper polls with 1 ms sleeps
 * until the requested number of bytes arrives or the timeout expires.
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
        int ret = ftdi_read_data(CTX,
                                 buf + bytesRead,
                                 static_cast<int>(len - bytesRead));
        if (ret < 0) {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("ftdi_read_data() error, ret="); LOG_INT(ret);
                      LOG_STRING(ftdi_get_error_string(CTX)));
            return Status::READ_ERROR;
        }

        bytesRead += static_cast<size_t>(ret);

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
 *
 * ftdi_tcioflush() replaces the deprecated ftdi_usb_purge_buffers() family
 * (all three deprecated since libftdi1 1.5).
 */
FT245Base::Status FT245Base::fifo_purge() const
{
    if (ftdi_tcioflush(CTX) < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ftdi_tcioflush() failed:"); LOG_STRING(ftdi_get_error_string(CTX)));
        return Status::FLUSH_FAILED;
    }
    return Status::SUCCESS;
}
