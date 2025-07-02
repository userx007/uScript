#include "uSerial.h"
#include "uLogger.hpp"

#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <poll.h>


#define LT_HDR     "UARTDRV :"
#define LOG_HDR    LOG_STRING(LT_HDR)

namespace UART
{

Driver::UartStatusCode Driver::open(const std::string& strDevice, uint32_t u32Speed)
{
    if (strDevice.empty() || u32Speed == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Invalid parameter(s):");
                  LOG_STRING(strDevice.c_str());
                  LOG_STRING("Baudrate:"); LOG_UINT32(u32Speed));
        return UartStatusCode::INVALID_PARAM;
    }

    int openFlags = O_RDWR | O_CLOEXEC;
    m_iHandle = ::open(strDevice.c_str(), openFlags);

    if (m_iHandle < 0) {
        int errnoRet = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Failed to open ["); LOG_STRING(strDevice.c_str());
                  LOG_UINT32(u32Speed); LOG_STRING("] errno:"); LOG_INT(errnoRet));
        return UartStatusCode::PORT_ACCESS;
    }

    Driver::UartStatusCode result = setup(u32Speed);
    if (result != UartStatusCode::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Failed to configure ["); LOG_STRING(strDevice.c_str());
                  LOG_UINT32(u32Speed); LOG_INT(m_iHandle);
                  LOG_STRING("] Error:"); LOG_INT(result));
        ::close(m_iHandle);
        m_iHandle = -1;
        return UartStatusCode::PORT_ACCESS;
    }

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("UART ["); LOG_STRING(strDevice.c_str());
              LOG_UINT32(u32Speed); LOG_STRING("] opened, handle:");
              LOG_INT(m_iHandle));

    return UartStatusCode::SUCCESS;
}


Driver::UartStatusCode Driver::close()
{
    if (m_iHandle >= 0) {
        ::close(m_iHandle);
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("UART closed, handle:"); LOG_INT(m_iHandle));
        m_iHandle = -1;
    }
    return UartStatusCode::SUCCESS;
}


Driver::UartStatusCode Driver::purge(bool bInput, bool bOutput)
{
    int flushOptions = 0;
    if (bInput) flushOptions |= TCIFLUSH;
    if (bOutput) flushOptions |= TCOFLUSH;

    if (tcflush(m_iHandle, flushOptions) < 0) {
        int errnoRet = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("tcflush() failed for handle:"); LOG_INT(m_iHandle);
                  LOG_STRING("errno:"); LOG_UINT32(errnoRet));
        return UartStatusCode::PORT_ACCESS;
    }

    return UartStatusCode::SUCCESS;
}

Driver::UartStatusCode Driver::timeout_read(uint32_t u32ReadTimeout, char *pBuffer, size_t szSizeToRead, size_t* pBytesRead)
{
    if (!pBuffer || !pBytesRead) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid parameter in timeout_read"));
        return UartStatusCode::INVALID_PARAM;
    }

    *pBytesRead = 0;
    struct pollfd sPollFd = { .fd = m_iHandle, .events = POLLIN };

    int iPollResult = poll(&sPollFd, 1, u32ReadTimeout);
    if (iPollResult < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("poll() failed"); LOG_INT(errno));
        return UartStatusCode::PORT_ACCESS;
    } else if (iPollResult == 0) {
        return UartStatusCode::READ_TIMEOUT;
    }

    ssize_t sszBytesRead = read(m_iHandle, pBuffer, szSizeToRead);
    if (sszBytesRead <= 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("read() failed or returned 0"); LOG_INT(errno));
        return UartStatusCode::PORT_ACCESS;
    }

    *pBytesRead = static_cast<size_t>(sszBytesRead);
    return UartStatusCode::SUCCESS;
}


Driver::UartStatusCode Driver::timeout_write(uint32_t /*u32WriteTimeout*/, const char *pBuffer, size_t szSizeToWrite)
{
    if (!pBuffer) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid parameter (pBuffer=NULL)"));
        return UartStatusCode::INVALID_PARAM;
    }

    if (szSizeToWrite == 0) return UartStatusCode::SUCCESS;

    size_t szTotalBytesWritten = 0;

    while (szTotalBytesWritten < szSizeToWrite) {
        ssize_t sszBytesWritten = write(m_iHandle, pBuffer + szTotalBytesWritten, szSizeToWrite - szTotalBytesWritten);
        if (sszBytesWritten <= 0) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("UART write error"); LOG_INT32(errno));
            return UartStatusCode::PORT_ACCESS;
        }
        szTotalBytesWritten += sszBytesWritten;
    }

    purge(true, true);
    return UartStatusCode::SUCCESS;
}



Driver::UartStatusCode Driver::setup(uint32_t u32Speed)
{
    struct termios settings;
    if (tcgetattr(m_iHandle, &settings) != 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("tcgetattr() failed for handle:"); LOG_INT(m_iHandle));
        return UartStatusCode::PORT_ACCESS;
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
        return UartStatusCode::PORT_ACCESS;
    }

    purge(true, true);
    return UartStatusCode::SUCCESS;
}


speed_t Driver::getBaud(uint32_t u32Speed)
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

} // namespace UART