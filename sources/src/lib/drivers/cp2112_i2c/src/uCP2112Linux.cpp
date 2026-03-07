#include "uCP2112.hpp"
#include "uLogger.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <cstring>
#include <cstdio>
#include <linux/hidraw.h>
#include <sys/ioctl.h>


#define LT_HDR  "CP2112_DRIVER|"
#define LOG_HDR LOG_STRING(LT_HDR)


// ============================================================================
// open / close
// ============================================================================

CP2112::Status CP2112::open(uint8_t u8I2CAddress, uint32_t u32ClockHz, uint8_t u8DeviceIndex)
{
    if (u32ClockHz == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid clock speed"));
        return Status::INVALID_PARAM;
    }

    // Scan /dev/hidraw0 … /dev/hidraw15 to find the n-th CP2112
    uint8_t  matchCount = 0;
    int      candidateFd = -1;

    for (int idx = 0; idx < 16; ++idx)
    {
        char path[32];
        std::snprintf(path, sizeof(path), "/dev/hidraw%d", idx);

        int fd = ::open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;

        struct hidraw_devinfo info;
        std::memset(&info, 0, sizeof(info));

        if (ioctl(fd, HIDIOCGRAWINFO, &info) < 0) {
            ::close(fd);
            continue;
        }

        if (static_cast<uint16_t>(info.vendor)  == CP2112_VID &&
            static_cast<uint16_t>(info.product) == CP2112_PID)
        {
            if (matchCount == u8DeviceIndex) {
                candidateFd = fd;
                break;
            }
            ++matchCount;
        }

        ::close(fd);
    }

    if (candidateFd < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("CP2112 device index"); LOG_UINT32(u8DeviceIndex);
                  LOG_STRING("not found (VID=0x10C4, PID=0xEA90)"));
        return Status::PORT_ACCESS;
    }

    m_hDevice      = candidateFd;
    m_u8I2CAddress = u8I2CAddress;

    // Apply SMBus/I2C clock configuration
    Status result = configure_smbus(u32ClockHz);
    if (result != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Failed to configure SMBus clock"); LOG_UINT32(u32ClockHz));
        ::close(m_hDevice);
        m_hDevice = -1;
        return result;
    }

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("CP2112 opened: index="); LOG_UINT32(u8DeviceIndex);
              LOG_STRING("fd="); LOG_INT(m_hDevice);
              LOG_STRING("I2C addr=0x"); LOG_HEX8(u8I2CAddress);
              LOG_STRING("clock="); LOG_UINT32(u32ClockHz));

    return Status::SUCCESS;
}


CP2112::Status CP2112::close()
{
    if (m_hDevice >= 0) {
        (void)cancel_transfer();
        ::close(m_hDevice);
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("CP2112 closed, fd:"); LOG_INT(m_hDevice));
        m_hDevice = -1;
    }
    return Status::SUCCESS;
}


// ============================================================================
// Low-level HID I/O
// ============================================================================

/**
 * @brief Send a HID Feature report to the CP2112
 *
 * Uses the HIDIOCSFEATURE ioctl on the /dev/hidraw node.
 * buf[0] must be the report ID; total length must equal HID_REPORT_SIZE (64).
 */
CP2112::Status CP2112::hid_set_feature(const uint8_t* buf, size_t len) const
{
    if (!buf || len != HID_REPORT_SIZE) {
        return Status::INVALID_PARAM;
    }

    // HIDIOCSFEATURE(len) expects the full report including the report-ID byte
    int ret = ioctl(m_hDevice, HIDIOCSFEATURE(len), buf);
    if (ret < 0) {
        int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("HIDIOCSFEATURE failed, report=0x"); LOG_HEX8(buf[0]);
                  LOG_STRING("errno="); LOG_INT(err));
        return Status::WRITE_ERROR;
    }

    return Status::SUCCESS;
}


/**
 * @brief Receive a HID Feature report from the CP2112
 *
 * Uses the HIDIOCGFEATURE ioctl.
 * buf[0] must be set to the desired report ID before calling.
 * On return, buf contains the full 64-byte report (buf[0] = report ID).
 */
CP2112::Status CP2112::hid_get_feature(uint8_t* buf, size_t len) const
{
    if (!buf || len != HID_REPORT_SIZE) {
        return Status::INVALID_PARAM;
    }

    int ret = ioctl(m_hDevice, HIDIOCGFEATURE(len), buf);
    if (ret < 0) {
        int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("HIDIOCGFEATURE failed, report=0x"); LOG_HEX8(buf[0]);
                  LOG_STRING("errno="); LOG_INT(err));
        return Status::READ_ERROR;
    }

    return Status::SUCCESS;
}


/**
 * @brief Write a HID Interrupt OUT report to the CP2112
 *
 * Uses write() on the /dev/hidraw node.
 * buf[0] = report ID (0x0D for Data Write), total length = 64 bytes.
 */
CP2112::Status CP2112::hid_interrupt_write(const uint8_t* buf, size_t len) const
{
    if (!buf || len != HID_REPORT_SIZE) {
        return Status::INVALID_PARAM;
    }

    ssize_t written = ::write(m_hDevice, buf, len);
    if (written < 0) {
        int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("HID interrupt write failed, errno="); LOG_INT(err));
        return Status::WRITE_ERROR;
    }
    if (static_cast<size_t>(written) != len) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("HID interrupt write short write:"); LOG_INT(written));
        return Status::WRITE_ERROR;
    }

    return Status::SUCCESS;
}


/**
 * @brief Read one HID Interrupt IN report from the CP2112 with a timeout
 *
 * Uses poll() + read() on the /dev/hidraw node.
 * On success, buf[0] = report ID (typically 0x0C for Data Read Response).
 *
 * @param timeoutMs  0 means block indefinitely (not recommended; use a value > 0)
 */
CP2112::Status CP2112::hid_interrupt_read(uint8_t* buf, size_t len,
                                          uint32_t timeoutMs, size_t& bytesRead) const
{
    if (!buf || len != HID_REPORT_SIZE) {
        return Status::INVALID_PARAM;
    }

    bytesRead = 0;

    struct pollfd pfd;
    pfd.fd      = m_hDevice;
    pfd.events  = POLLIN;
    pfd.revents = 0;

    int pollRet = poll(&pfd, 1, static_cast<int>(timeoutMs));

    if (pollRet < 0) {
        int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("poll() failed, errno="); LOG_INT(err));
        return Status::READ_ERROR;
    }

    if (pollRet == 0) {
        return Status::READ_TIMEOUT;
    }

    ssize_t ret = ::read(m_hDevice, buf, len);
    if (ret < 0) {
        int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("HID interrupt read failed, errno="); LOG_INT(err));
        return Status::READ_ERROR;
    }

    bytesRead = static_cast<size_t>(ret);
    return Status::SUCCESS;
}
