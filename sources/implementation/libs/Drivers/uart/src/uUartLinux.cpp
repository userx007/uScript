#include "uUart.hpp"
#include "uLogger.hpp"

#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <poll.h>


#define LT_HDR     "UART_DRIVER:"
#define LOG_HDR    LOG_STRING(LT_HDR)


UART::Status UART::open(const std::string& strDevice, uint32_t u32Speed)
{
    if (strDevice.empty() || u32Speed == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Invalid parameter(s):");
                  LOG_STRING(strDevice.c_str());
                  LOG_STRING("Baudrate:"); LOG_UINT32(u32Speed));
        return Status::INVALID_PARAM;
    }

    int openFlags = O_RDWR | O_CLOEXEC;
    m_iHandle = ::open(strDevice.c_str(), openFlags);

    if (m_iHandle < 0) {
        int errnoRet = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Failed to open ["); LOG_STRING(strDevice.c_str());
                  LOG_UINT32(u32Speed); LOG_STRING("] errno:"); LOG_INT(errnoRet));
        return Status::PORT_ACCESS;
    }

    UART::Status result = setup(u32Speed);
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
              LOG_STRING("UART ["); LOG_STRING(strDevice.c_str());
              LOG_UINT32(u32Speed); LOG_STRING("] opened, handle:");
              LOG_INT(m_iHandle));

    return Status::SUCCESS;
}



UART::Status UART::close()
{
    if (m_iHandle >= 0) {
        ::close(m_iHandle);
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("UART closed, handle:"); LOG_INT(m_iHandle));
        m_iHandle = -1;
    }
    return Status::SUCCESS;
}



UART::Status UART::purge(bool bInput, bool bOutput)  const
{
    int flushOptions = 0;
    if (bInput) flushOptions |= TCIFLUSH;
    if (bOutput) flushOptions |= TCOFLUSH;

    if (tcflush(m_iHandle, flushOptions) < 0) {
        int errnoRet = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("tcflush() failed for handle:"); LOG_INT(m_iHandle);
                  LOG_STRING("errno:"); LOG_UINT32(errnoRet));
        return Status::FLUSH_FAILED;
    }

    return Status::SUCCESS;
}



UART::Status UART::timeout_read(uint32_t u32ReadTimeout, std::span<uint8_t> buffer, size_t& szBytesRead) const
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

    int iPollResult = poll(&sPollFd, 1, u32ReadTimeout);
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



UART::Status UART::timeout_write(uint32_t /*u32WriteTimeout*/, std::span<const uint8_t> buffer, size_t& szBytesWritten) const
{
    if (buffer.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid parameter: buffer.empty()"));
        return Status::INVALID_PARAM;
    }

    if (buffer.size() == 0) {
        szBytesWritten = 0;
        return Status::SUCCESS;
    }

    szBytesWritten = 0;

    while (szBytesWritten < buffer.size()) {
        ssize_t sszBytesWritten = ::write(m_iHandle, buffer.data() + szBytesWritten, buffer.size() - szBytesWritten);
        if (sszBytesWritten <= 0) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("UART write error"); LOG_INT32(errno));
            return Status::WRITE_ERROR;
        }
        szBytesWritten += sszBytesWritten;
    }

    purge(true, true);
    return Status::SUCCESS;
}



UART::Status UART::setup(uint32_t u32Speed) const
{
    struct termios settings;
    if (tcgetattr(m_iHandle, &settings) != 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("tcgetattr() failed for handle:"); LOG_INT(m_iHandle));
        return Status::PORT_ACCESS;
    }

    speed_t baud = getBaud(u32Speed);  // Use a mapping function if needed
    cfsetospeed(&settings, baud);
    cfsetispeed(&settings, baud);

    settings.c_cflag &= ~PARENB;
    settings.c_cflag &= ~CSTOPB;
    settings.c_cflag &= ~CSIZE;
    settings.c_cflag |= CS8 | CLOCAL;
    settings.c_lflag = 0;
    settings.c_iflag &= ~(IXON | IXOFF | ISTRIP | INLCR | IGNCR | ICRNL | IUCLC);
    settings.c_oflag &= ~(OPOST | OLCUC | ONLCR | OCRNL | ONOCR | ONLRET | OFILL);

    if (tcsetattr(m_iHandle, TCSANOW, &settings) != 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("tcsetattr() failed for handle:"); LOG_INT(m_iHandle));
        return Status::PORT_ACCESS;
    }

    purge(true, true);
    return Status::SUCCESS;
}



speed_t UART::getBaud(uint32_t u32Speed) const
{
    switch (u32Speed) {
        case 0:
            return B0;
        case 50:
            return B50;
        case 75:
            return B75;
        case 110:
            return B110;
        case 134:
            return B134;
        case 150:
            return B150;
        case 200:
            return B200;
        case 300:
            return B300;
        case 600:
            return B600;
        case 1200:
            return B1200;
        case 1800:
            return B1800;
        case 2400:
            return B2400;
        case 4800:
            return B4800;
        case 9600:
            return B9600;
        case 19200:
            return B19200;
        case 38400:
            return B38400;
        case 57600:
            return B57600;
        case 115200:
            return B115200;
        case 230400:
            return B230400;
#ifdef B460800
        case 460800:
            return B460800;
#endif
#ifdef B500000
        case 500000:
            return B500000;
#endif
#ifdef B576000
        case 576000:
            return B576000;
#endif
#ifdef B921600
        case 921600:
            return B921600;
#endif
#ifdef B1000000
        case 1000000:
            return B1000000;
#endif
#ifdef B1152000
        case 1152000:
            return B1152000;
#endif
#ifdef B1500000
        case 1500000:
            return B1500000;
#endif
#ifdef B2000000
        case 2000000:
            return B2000000;
#endif
#ifdef B2500000
        case 2500000:
            return B2500000;
#endif
#ifdef B3000000
        case 3000000:
            return B3000000;
#endif
#ifdef B3500000
        case 3500000:
            return B3500000;
#endif
#ifdef B4000000
        case 4000000:
            return B4000000;
#endif
        default:
            LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Unsupported baud defaulting to B9600"); LOG_UINT32(u32Speed));
            return B9600;
    }
}
