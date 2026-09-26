#include "uLogger.hpp"
#include "uUart.hpp"

#include <algorithm>
#include <chrono>
#include <compare>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <span>
#include <stdint.h>
#include <stop_token>
#include <string>
#include <termios.h>
#include <unistd.h>

/////////////////////////////////////////////////////////////////////////////////
//                            LOG DEFINITIONS                                  //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
#undef LT_HDR
#endif
#ifdef LOG_HDR
#undef LOG_HDR
#endif

#define LT_HDR  "UART_DRV    |"
#define LOG_HDR LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                            IMPLEMENTATION                                   //
/////////////////////////////////////////////////////////////////////////////////

UART::Status UART::open(const std::string &strDevice, uint32_t u32Speed,
                        Parity eParity, uint8_t u8DataBits, uint8_t u8StopBits)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (strDevice.empty() || u32Speed == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Invalid parameter(s):");
                  LOG_STRING(strDevice.c_str());
                  LOG_STRING("Baudrate:"); LOG_UINT32(u32Speed));
        return Status::INVALID_PARAM;
    }
    if (u8DataBits < 5 || u8DataBits > 8) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid data bits (must be 5-8):"); LOG_UINT32(u8DataBits));
        return Status::INVALID_PARAM;
    }
    if (u8StopBits < 1 || u8StopBits > 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid stop bits (must be 1-2):"); LOG_UINT32(u8StopBits));
        return Status::INVALID_PARAM;
    }

    int openFlags = O_RDWR | O_CLOEXEC;
    m_iHandle     = ::open(strDevice.c_str(), openFlags);

    if (m_iHandle < 0) {
        int errnoRet = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Failed to open ["); LOG_STRING(strDevice.c_str());
                  LOG_UINT32(u32Speed); LOG_STRING("] errno:"); LOG_INT(errnoRet));
        return Status::PORT_ACCESS;
    }

    UART::Status result = setup(u32Speed, eParity, u8DataBits, u8StopBits);
    if (result != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Failed to configure ["); LOG_STRING(strDevice.c_str());
                  LOG_UINT32(u32Speed); LOG_INT(m_iHandle);
                  LOG_STRING("] Error:"); LOG_INT(result));
        ::close(m_iHandle);
        m_iHandle = -1;
        return Status::PORT_ACCESS;
    }

    m_eParity    = eParity;
    m_u8DataBits = u8DataBits;
    m_u8StopBits = u8StopBits;

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("UART ["); LOG_STRING(strDevice.c_str());
              LOG_UINT32(u32Speed); LOG_STRING("] opened, handle:");
              LOG_INT(m_iHandle));

    return Status::SUCCESS;
}

UART::Status UART::close()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_iHandle >= 0) {
        ::close(m_iHandle);
        LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("UART closed, handle:"); LOG_INT(m_iHandle));
        m_iHandle = -1;
    }
    return Status::SUCCESS;
}

UART::Status UART::purge(bool bInput, bool bOutput) const
{
    int flushOptions = 0;
    if (bInput) {
        flushOptions |= TCIFLUSH;
    }
    if (bOutput) {
        flushOptions |= TCOFLUSH;
    }

    if (tcflush(m_iHandle, flushOptions) < 0) {
        int errnoRet = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("tcflush() failed for handle:"); LOG_INT(m_iHandle);
                  LOG_STRING("errno:"); LOG_UINT32(errnoRet));
        return Status::FLUSH_FAILED;
    }

    return Status::SUCCESS;
}

UART::Status UART::timeout_read(uint32_t u32ReadTimeout, std::span<uint8_t> buffer, size_t &szBytesRead,
                                std::stop_token stop_tok) const
{
    if (buffer.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("timeout_read: invalid parameter"));
        return Status::INVALID_PARAM;
    }

    szBytesRead = 0;

    struct pollfd sPollFd;
    sPollFd.fd                 = m_iHandle;
    sPollFd.events             = POLLIN;
    sPollFd.revents            = 0;

    // 0 == infinite timeout: never expire the wait ourselves (poll(2) treats
    // a negative timeout as "wait indefinitely"). Either way, poll in bounded
    // slices so a stop request can be observed promptly instead of only at
    // the end of the (possibly infinite) wait.
    constexpr int kPollSliceMs = 200;
    const bool bInfinite       = (u32ReadTimeout == 0);
    const auto tDeadline       = std::chrono::steady_clock::now() + std::chrono::milliseconds(u32ReadTimeout);

    int iPollResult            = 0;
    while (true) {
        if (stop_tok.stop_requested()) {
            return Status::READ_TIMEOUT;
        }

        int iSliceMs = kPollSliceMs;
        if (!bInfinite) {
            const auto remaining = tDeadline - std::chrono::steady_clock::now();
            if (remaining <= std::chrono::milliseconds(0)) {
                return Status::READ_TIMEOUT;
            }
            iSliceMs = static_cast<int>(std::min<int64_t>(kPollSliceMs,
                                                          std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count()));
        }

        iPollResult = poll(&sPollFd, 1, iSliceMs);
        if (iPollResult < 0) {
            int err = errno;
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("poll() failed"); LOG_INT(err));
            return Status::READ_ERROR;
        }
        if (iPollResult > 0) {
            break;
        }
        // iPollResult == 0: this slice timed out, loop again (bInfinite keeps
        // going forever; a finite deadline re-checks "remaining" above).
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

UART::Status UART::timeout_write(uint32_t /*u32WriteTimeout*/, std::span<const uint8_t> buffer, size_t &szBytesWritten,
                                 std::stop_token /*stop_tok*/) const
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

    return Status::SUCCESS;
}

UART::Status UART::setup(uint32_t u32Speed, Parity eParity, uint8_t u8DataBits, uint8_t u8StopBits) const
{
    struct termios settings;
    if (tcgetattr(m_iHandle, &settings) != 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("tcgetattr() failed for handle:"); LOG_INT(m_iHandle));
        return Status::PORT_ACCESS;
    }

    speed_t baud = getBaud(u32Speed); // Use a mapping function if needed
    cfsetospeed(&settings, baud);
    cfsetispeed(&settings, baud);

    // Data bits
    settings.c_cflag &= ~CSIZE;
    switch (u8DataBits) {
    case 5:
        settings.c_cflag |= CS5;
        break;
    case 6:
        settings.c_cflag |= CS6;
        break;
    case 7:
        settings.c_cflag |= CS7;
        break;
    case 8:
    default:
        settings.c_cflag |= CS8;
        break;
    }

    // Parity — PARENB/PARODD control the hardware's transmit-side generation
    // and receive-side checking of the eParity bit; see the Parity enum's doc
    // comment (uUart.hpp) for why this driver leaves INPCK off regardless.
    settings.c_cflag &= ~(PARENB | PARODD);
    if (eParity == Parity::Even) {
        settings.c_cflag |= PARENB;
    } else if (eParity == Parity::Odd) {
        settings.c_cflag |= PARENB | PARODD;
    }

    // Stop bits
    if (u8StopBits >= 2) {
        settings.c_cflag |= CSTOPB;
    } else {
        settings.c_cflag &= ~CSTOPB;
    }

    settings.c_cflag |= CLOCAL;
    settings.c_lflag = 0;
    settings.c_iflag &= ~(IXON | IXOFF | ISTRIP | INLCR | IGNCR | ICRNL | IUCLC);
    settings.c_oflag &= ~(OPOST | OLCUC | ONLCR | OCRNL | ONOCR | ONLRET | OFILL);

    if (tcsetattr(m_iHandle, TCSANOW, &settings) != 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("tcsetattr() failed for handle:"); LOG_INT(m_iHandle));
        return Status::PORT_ACCESS;
    }

    // POSIX only requires tcsetattr() to apply *some* of the requested
    // changes to report success (see `man tcsetattr`) — some backends
    // (notably Linux pseudo-terminals, which have no real eParity hardware
    // to emulate) silently drop bits like PARENB while still returning 0.
    // Read the settings back and warn (rather than fail open() outright,
    // since a caller on a genuinely constrained device might still want
    // to proceed with whatever the port could apply) if the framing that
    // actually took effect doesn't match what was asked for — this is the
    // only way to catch that silent downgrade.
    struct termios verify;
    if (tcgetattr(m_iHandle, &verify) == 0) {
        if ((verify.c_cflag & (PARENB | PARODD | CSIZE | CSTOPB)) != (settings.c_cflag & (PARENB | PARODD | CSIZE | CSTOPB))) {
            LOG_PRINT(LOG_WARNING, LOG_HDR;
                      LOG_STRING("Port accepted tcsetattr() but framing did not fully apply — requested cflag:");
                      LOG_UINT32(static_cast<uint32_t>(settings.c_cflag & (PARENB | PARODD | CSIZE | CSTOPB)));
                      LOG_STRING("actual:"); LOG_UINT32(static_cast<uint32_t>(verify.c_cflag & (PARENB | PARODD | CSIZE | CSTOPB)));
                      LOG_STRING("(a pseudo-terminal cannot emulate eParity — this is expected on a PTY, not on real serial hardware)"));
        }
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
