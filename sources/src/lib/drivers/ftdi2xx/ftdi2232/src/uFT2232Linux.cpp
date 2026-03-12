// FT2232Base.hpp defines all MPSSE_* opcode constants used by the protocol
// layers (I²C, SPI, GPIO).  This platform file implements only the raw
// transport primitives (mpsse_write, mpsse_read, mpsse_purge, open_device,
// close, is_open) and does not use the opcode constants directly.
#include "FT2232Base.hpp"
#include "uLogger.hpp"

// libftdi1  — package: libftdi1-dev (Debian/Ubuntu)
//             CMake:   PkgConfig libftdi1  (or vendored via LIBFTDI1_ROOT)
//             Header:  <ftdi.h>  (may also be <libftdi1/ftdi.h> on some distros)
//
// BITMODE_MPSSE and other ftdi_* symbols come from this header.
// The MPSSE_* opcode byte constants are NOT from libftdi1 — they are
// defined in FT2232Base.hpp based on FTDI AN_108.
#include <ftdi.h>

#include <cstring>
#include <chrono>
#include <thread>


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "FT2232_BASE |"
#define LOG_HDR    LOG_STRING(LT_HDR)

// Convenience cast — avoids repeating the cast everywhere in this file
#define CTX (static_cast<struct ftdi_context*>(m_hDevice))


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
    // FT2232D only has MPSSE on channel A.  FT2232H supports both A and B.
    if (variant == Variant::FT2232D && channel != Channel::A) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("open_device: FT2232D MPSSE is only available on channel A"));
        return Status::INVALID_PARAM;
    }

    // ── Allocate a new ftdi_context ───────────────────────────────────────────
    struct ftdi_context* ctx = ftdi_new();
    if (!ctx) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ftdi_new() returned nullptr (out of memory?)"));
        return Status::OUT_OF_MEMORY;
    }

    // ── Select the interface (channel) before opening ─────────────────────────
    // INTERFACE_A = 1, INTERFACE_B = 2 in libftdi1.
    ftdi_interface iface = (channel == Channel::A) ? INTERFACE_A : INTERFACE_B;
    if (ftdi_set_interface(ctx, iface) < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ftdi_set_interface() failed:"); LOG_STRING(ftdi_get_error_string(ctx)));
        ftdi_free(ctx);
        return Status::PORT_ACCESS;
    }

    // ── Open by VID/PID and device index ─────────────────────────────────────
    // PID differs between FT2232H and FT2232D.
    // ftdi_usb_open_desc_index(ctx, vid, pid, description, serial, index)
    // Passing NULL for description and serial matches any device.
    const uint16_t pid = (variant == Variant::FT2232H) ? FT2232H_PID : FT2232D_PID;

    if (ftdi_usb_open_desc_index(ctx,
                                 static_cast<int>(FT2232_VID),
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

    // ── Enable MPSSE mode ────────────────────────────────────────────────────
    // Mask 0x00 = all inputs initially; SET_BITS_LOW commands configure them.
    if (ftdi_set_bitmode(ctx, 0x00, BITMODE_MPSSE) < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ftdi_set_bitmode(MPSSE) failed:"); LOG_STRING(ftdi_get_error_string(ctx)));
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

    // ── Store state ──────────────────────────────────────────────────────────
    m_variant = variant;
    m_hDevice = ctx;

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("FT2232 opened: variant=");
              LOG_UINT32(static_cast<uint8_t>(variant));
              LOG_STRING("channel="); LOG_UINT32(static_cast<uint8_t>(channel));
              LOG_STRING("index="); LOG_UINT32(u8DeviceIndex));

    return Status::SUCCESS;
}


// ============================================================================
// close / is_open
// ============================================================================

FT2232Base::Status FT2232Base::close()
{
    if (m_hDevice) {
        ftdi_usb_close(CTX);
        ftdi_free(CTX);
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

/**
 * @brief Write raw MPSSE command bytes to the FT2232
 *
 * Uses ftdi_write_data() which performs a synchronous USB bulk write.
 */
FT2232Base::Status FT2232Base::mpsse_write(const uint8_t* buf, size_t len) const
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
 * @brief Read response bytes from the FT2232 with a timeout
 *
 * ftdi_read_data() is non-blocking; this wrapper polls with 1 ms sleeps
 * until the requested number of bytes arrives or the timeout expires.
 */
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
 * @brief Purge the device's RX and TX FIFOs
 *
 * ftdi_tcioflush() replaces the deprecated ftdi_usb_purge_buffers() family
 * (all three deprecated since libftdi1 1.5).
 */
FT2232Base::Status FT2232Base::mpsse_purge() const
{
    if (ftdi_tcioflush(CTX) < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ftdi_tcioflush() failed:"); LOG_STRING(ftdi_get_error_string(CTX)));
        return Status::FLUSH_FAILED;
    }
    return Status::SUCCESS;
}
