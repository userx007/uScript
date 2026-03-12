/*
 * FT232H – Linux platform implementation
 *
 * Uses libftdi1 (package: libftdi1-dev on Debian/Ubuntu).
 * CMake dependency: find_package(LibFTDI1 REQUIRED) or PkgConfig libftdi1.
 *
 * The only platform-specific difference from the FT4232H driver is:
 *   - PID 0x6014 instead of 0x6011
 *   - No channel selection (FT232H has one MPSSE interface — always INTERFACE_A)
 *   - MPSSE_DIS_DIV5 not needed here (sent by the protocol open() methods)
 *
 * All MPSSE_* constants are defined in FT232HBase.hpp; this file does not
 * use them directly.
 */

#include "FT232HBase.hpp"
#include "uLogger.hpp"

#include <ftdi.h>

#include <cstring>
#include <chrono>
#include <thread>

#define LT_HDR  "FT232H_BASE |"
#define LOG_HDR LOG_STRING(LT_HDR)

#define CTX (static_cast<struct ftdi_context*>(m_hDevice))


// ============================================================================
// Destructor
// ============================================================================

FT232HBase::~FT232HBase()
{
    FT232HBase::close();
}


// ============================================================================
// open_device
// ============================================================================

FT232HBase::Status FT232HBase::open_device(uint8_t u8DeviceIndex)
{
    struct ftdi_context* ctx = ftdi_new();
    if (!ctx) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("ftdi_new() returned nullptr"));
        return Status::OUT_OF_MEMORY;
    }

    // FT232H has a single MPSSE interface — always INTERFACE_A (= 1)
    if (ftdi_set_interface(ctx, INTERFACE_A) < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ftdi_set_interface() failed:"); LOG_STRING(ftdi_get_error_string(ctx)));
        ftdi_free(ctx);
        return Status::PORT_ACCESS;
    }

    if (ftdi_usb_open_desc_index(ctx,
                                 static_cast<int>(FT232H_VID),
                                 static_cast<int>(FT232H_PID),
                                 nullptr,
                                 nullptr,
                                 static_cast<unsigned int>(u8DeviceIndex)) < 0)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ftdi_usb_open_desc_index() failed, index="); LOG_UINT32(u8DeviceIndex);
                  LOG_STRING("error:"); LOG_STRING(ftdi_get_error_string(ctx)));
        ftdi_free(ctx);
        return Status::PORT_ACCESS;
    }

    if (ftdi_usb_reset(ctx) < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ftdi_usb_reset() failed:"); LOG_STRING(ftdi_get_error_string(ctx)));
        ftdi_usb_close(ctx);
        ftdi_free(ctx);
        return Status::PORT_ACCESS;
    }

    // Enable MPSSE mode; initial direction mask 0x00 = all inputs
    if (ftdi_set_bitmode(ctx, 0x00, BITMODE_MPSSE) < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ftdi_set_bitmode(MPSSE) failed:"); LOG_STRING(ftdi_get_error_string(ctx)));
        ftdi_usb_close(ctx);
        ftdi_free(ctx);
        return Status::PORT_ACCESS;
    }

    // 1 ms latency timer for fastest possible read response
    if (ftdi_set_latency_timer(ctx, 1) < 0) {
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("ftdi_set_latency_timer() failed (non-fatal)"));
    }

    // Large chunk sizes for best bulk throughput
    (void)ftdi_read_data_set_chunksize(ctx,  65536);
    (void)ftdi_write_data_set_chunksize(ctx, 65536);

    m_hDevice = ctx;

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("FT232H opened: device index="); LOG_UINT32(u8DeviceIndex));

    return Status::SUCCESS;
}


// ============================================================================
// close / is_open
// ============================================================================

FT232HBase::Status FT232HBase::close()
{
    if (m_hDevice) {
        ftdi_usb_close(CTX);
        ftdi_free(CTX);
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


// ============================================================================
// MPSSE transport primitives
// ============================================================================

FT232HBase::Status FT232HBase::mpsse_write(const uint8_t* buf, size_t len) const
{
    if (!buf || len == 0)
        return Status::INVALID_PARAM;

    int ret = ftdi_write_data(CTX, const_cast<uint8_t*>(buf), static_cast<int>(len));
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

FT232HBase::Status FT232HBase::mpsse_read(uint8_t* buf, size_t len,
                                           uint32_t timeoutMs,
                                           size_t& bytesRead) const
{
    if (!buf || len == 0)
        return Status::INVALID_PARAM;

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

FT232HBase::Status FT232HBase::mpsse_purge() const
{
    if (ftdi_tcioflush(CTX) < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ftdi_tcioflush() failed:"); LOG_STRING(ftdi_get_error_string(CTX)));
        return Status::FLUSH_FAILED;
    }
    return Status::SUCCESS;
}
