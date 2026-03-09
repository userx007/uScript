#include "CP2112Base.hpp"
#include "uLogger.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <cstring>
#include <cstdio>
#include <linux/hidraw.h>
#include <sys/ioctl.h>


#define LT_HDR  "CP2112_BASE|"
#define LOG_HDR LOG_STRING(LT_HDR)


// ============================================================================
// CP2112Base destructor
// ============================================================================

CP2112Base::~CP2112Base()
{
    // Qualified call — intentionally bypasses any virtual override so we
    // don't accidentally call into a partially-destroyed subclass.
    CP2112Base::close();
}


// ============================================================================
// open_device  — enumerate /dev/hidraw* and open the n-th CP2112
// ============================================================================

CP2112Base::Status CP2112Base::open_device(uint8_t u8DeviceIndex)
{
    uint8_t matchCount  = 0;
    int     candidateFd = -1;

    for (int idx = 0; idx < 16; ++idx) {
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
                  LOG_STRING("CP2112 not found at index"); LOG_UINT32(u8DeviceIndex);
                  LOG_STRING("(VID=0x10C4, PID=0xEA90)"));
        return Status::PORT_ACCESS;
    }

    m_hDevice = candidateFd;

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("CP2112 handle opened: fd="); LOG_INT(m_hDevice);
              LOG_STRING("index="); LOG_UINT32(u8DeviceIndex));

    return Status::SUCCESS;
}


// ============================================================================
// close
// ============================================================================

CP2112Base::Status CP2112Base::close()
{
    if (m_hDevice >= 0) {
        ::close(m_hDevice);
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("CP2112 handle closed, fd:"); LOG_INT(m_hDevice));
        m_hDevice = -1;
    }
    return Status::SUCCESS;
}


// ============================================================================
// is_open
// ============================================================================

bool CP2112Base::is_open() const
{
    if (m_hDevice < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Device not open"));
        return false;
    }
    return true;
}


// ============================================================================
// HID primitives
// ============================================================================

CP2112Base::Status CP2112Base::hid_set_feature(const uint8_t* buf, size_t len) const
{
    if (!buf || len != HID_REPORT_SIZE) {
        return Status::INVALID_PARAM;
    }

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


CP2112Base::Status CP2112Base::hid_get_feature(uint8_t* buf, size_t len) const
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


CP2112Base::Status CP2112Base::hid_interrupt_write(const uint8_t* buf, size_t len) const
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
                  LOG_STRING("HID interrupt write short:"); LOG_INT(written));
        return Status::WRITE_ERROR;
    }

    return Status::SUCCESS;
}


CP2112Base::Status CP2112Base::hid_interrupt_read(uint8_t* buf, size_t len,
                                                   uint32_t timeoutMs,
                                                   size_t& bytesRead) const
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
