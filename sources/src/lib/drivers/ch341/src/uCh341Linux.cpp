#include "uCh341.hpp"
#include "uLogger.hpp"

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <poll.h>
#include <sys/ioctl.h>

// termios2 / BOTHER are not exposed by the glibc <termios.h> wrapper, so we
// pull them from the kernel uapi headers directly. This is what lets us ask
// the CH341 for an arbitrary baud rate instead of being limited to the
// fixed Bxxxxx speed_t enum used by classic UART drivers.
#include <asm/termbits.h>
#include <asm/ioctls.h>

#ifndef TCGETS2
#define TCGETS2 0x542A
#endif
#ifndef TCSETS2
#define TCSETS2 0x542B
#endif
#ifndef BOTHER
#define BOTHER 0010000
#endif


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "CH341_DRV   |"
#define LOG_HDR    LOG_STRING(LT_HDR)


CH341::Status CH341::open(const std::string& strDevice, uint32_t u32Speed)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (strDevice.empty() || u32Speed == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Invalid parameter(s):");
                  LOG_STRING(strDevice.c_str());
                  LOG_STRING("Baudrate:"); LOG_UINT32(u32Speed));
        return Status::INVALID_PARAM;
    }

    int openFlags = O_RDWR | O_NOCTTY | O_CLOEXEC;
    m_iHandle = ::open(strDevice.c_str(), openFlags);

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
        ::close(m_iHandle);
        m_iHandle = -1;
        return Status::PORT_ACCESS;
    }

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("CH341 ["); LOG_STRING(strDevice.c_str());
              LOG_UINT32(u32Speed); LOG_STRING("] opened, handle:");
              LOG_INT(m_iHandle));

    return Status::SUCCESS;
}



CH341::Status CH341::close()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_iHandle >= 0) {
        ::close(m_iHandle);
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("CH341 closed, handle:"); LOG_INT(m_iHandle));
        m_iHandle = -1;
    }
    return Status::SUCCESS;
}



CH341::Status CH341::purge(bool bInput, bool bOutput)  const
{
    // NOTE: we deliberately use the TCFLSH ioctl (rather than glibc's
    // tcflush()) because this translation unit uses the kernel termios2
    // ABI (asm/termbits.h) for arbitrary-baud support, which is not safe
    // to mix with glibc's conflicting <termios.h> struct termios definition.
    int iFlushArg;
    if (bInput && bOutput) {
        iFlushArg = TCIOFLUSH;
    } else if (bInput) {
        iFlushArg = TCIFLUSH;
    } else if (bOutput) {
        iFlushArg = TCOFLUSH;
    } else {
        return Status::SUCCESS;
    }

    if (ioctl(m_iHandle, TCFLSH, iFlushArg) < 0) {
        int errnoRet = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("TCFLSH failed for handle:"); LOG_INT(m_iHandle);
                  LOG_STRING("errno:"); LOG_UINT32(errnoRet));
        return Status::FLUSH_FAILED;
    }

    return Status::SUCCESS;
}



CH341::Status CH341::timeout_read(uint32_t u32ReadTimeout, std::span<uint8_t> buffer, size_t& szBytesRead) const
{
    if (buffer.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("timeout_read: invalid parameter"));
        return Status::INVALID_PARAM;
    }

    szBytesRead = 0;

    struct pollfd sPollFd;
    sPollFd.fd = m_iHandle;
    sPollFd.events = POLLIN;
    sPollFd.revents = 0;

    // 0 == infinite timeout: block until data is available.
    const int iPollTimeout = (u32ReadTimeout == 0) ? -1 : static_cast<int>(u32ReadTimeout);

    int iPollResult = poll(&sPollFd, 1, iPollTimeout);
    if (iPollResult < 0) {
        int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("poll() failed"); LOG_INT(err));
        return Status::READ_ERROR;
    } else if (iPollResult == 0) {
        return Status::READ_TIMEOUT;
    }

    ssize_t sszBytesRead = read(m_iHandle, buffer.data(), buffer.size());
    if (sszBytesRead <= 0) {
        int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("read() failed or returned 0"); LOG_INT(err));
        return Status::READ_ERROR;
    }

    szBytesRead = static_cast<size_t>(sszBytesRead);
    return Status::SUCCESS;
}



CH341::Status CH341::timeout_write(uint32_t /*u32WriteTimeout*/, std::span<const uint8_t> buffer, size_t& szBytesWritten) const
{
    if (buffer.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid parameter: buffer.empty()"));
        return Status::INVALID_PARAM;
    }

    szBytesWritten = 0;

    while (szBytesWritten < buffer.size()) {
        ssize_t sszBytesWritten = ::write(m_iHandle, buffer.data() + szBytesWritten, buffer.size() - szBytesWritten);
        if (sszBytesWritten <= 0) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("CH341 write error"); LOG_INT32(errno));
            return Status::WRITE_ERROR;
        }
        szBytesWritten += sszBytesWritten;
    }

    return Status::SUCCESS;
}



/**
 * @brief Configure the line using termios2/BOTHER so an arbitrary baud rate
 *        is passed straight through to the ch341 kernel driver, which in
 *        turn programs the ASIC's UART clock divider directly (see
 *        ch341_tty_set_termios() / ch341_get_divisor() in src/ch341.c).
 *        This avoids being limited to the fixed Bxxxxx speed_t enum and
 *        lets the chip's wide non-standard baud range be used as-is.
 */
CH341::Status CH341::setup(uint32_t u32Speed) const
{
    struct termios2 settings;
    std::memset(&settings, 0, sizeof(settings));

    if (ioctl(m_iHandle, TCGETS2, &settings) != 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("TCGETS2 failed for handle:"); LOG_INT(m_iHandle));
        return Status::PORT_ACCESS;
    }

    settings.c_cflag &= ~CBAUD;
    settings.c_cflag |= BOTHER;
    settings.c_ispeed = u32Speed;
    settings.c_ospeed = u32Speed;

    settings.c_cflag &= ~PARENB;
    settings.c_cflag &= ~CSTOPB;
    settings.c_cflag &= ~CSIZE;
    settings.c_cflag |= CS8 | CLOCAL | CREAD;
    settings.c_lflag = 0;
    settings.c_iflag &= ~(IXON | IXOFF | ISTRIP | INLCR | IGNCR | ICRNL);
    settings.c_oflag &= ~(OPOST | ONLCR | OCRNL | ONOCR | ONLRET | OFILL);

    if (ioctl(m_iHandle, TCSETS2, &settings) != 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("TCSETS2 failed for handle:"); LOG_INT(m_iHandle));
        return Status::PORT_ACCESS;
    }

    purge(true, true);
    return Status::SUCCESS;
}



CH341::Status CH341::get_modem_lines(unsigned int& u32Lines) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_iHandle < 0) {
        return Status::PORT_ACCESS;
    }

    int iLines = 0;
    if (ioctl(m_iHandle, TIOCMGET, &iLines) != 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("TIOCMGET failed for handle:"); LOG_INT(m_iHandle));
        return Status::READ_ERROR;
    }

    u32Lines = static_cast<unsigned int>(iLines);
    return Status::SUCCESS;
}



CH341::Status CH341::set_dtr_rts(bool bDtr, bool bRts) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_iHandle < 0) {
        return Status::PORT_ACCESS;
    }

    int iSet = 0;
    int iClear = 0;

    if (bDtr) iSet |= TIOCM_DTR; else iClear |= TIOCM_DTR;
    if (bRts) iSet |= TIOCM_RTS; else iClear |= TIOCM_RTS;

    if (iSet && ioctl(m_iHandle, TIOCMBIS, &iSet) != 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("TIOCMBIS failed for handle:"); LOG_INT(m_iHandle));
        return Status::WRITE_ERROR;
    }
    if (iClear && ioctl(m_iHandle, TIOCMBIC, &iClear) != 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("TIOCMBIC failed for handle:"); LOG_INT(m_iHandle));
        return Status::WRITE_ERROR;
    }

    return Status::SUCCESS;
}
