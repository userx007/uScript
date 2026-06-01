#include "uI2C.hpp"
#include "uLogger.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <poll.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>   // I2C_SLAVE


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR   "I2C_DRV     |"
#define LOG_HDR  LOG_STRING(LT_HDR)


// ============================================================================
// OPEN / CLOSE
// ============================================================================

I2C::Status I2C::open(const std::string& strDevice, uint8_t u8Address)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (strDevice.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Invalid parameter: empty device path"));
        return Status::INVALID_PARAM;
    }

    m_iHandle = ::open(strDevice.c_str(), O_RDWR | O_CLOEXEC);
    if (m_iHandle < 0)
    {
        int errnoRet = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Failed to open ["); LOG_STRING(strDevice.c_str());
                  LOG_STRING("] errno:"); LOG_INT(errnoRet));
        return Status::PORT_ACCESS;
    }

    // Bind the file descriptor to the target slave address.
    if (::ioctl(m_iHandle, I2C_SLAVE, static_cast<long>(u8Address)) < 0)
    {
        int errnoRet = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ioctl(I2C_SLAVE) failed for address 0x");
                  LOG_HEX8(u8Address); LOG_STRING(" errno:"); LOG_INT(errnoRet));
        ::close(m_iHandle);
        m_iHandle = -1;
        return Status::PORT_ACCESS;
    }

    m_u8Addr = u8Address;

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("I2C ["); LOG_STRING(strDevice.c_str());
              LOG_STRING("] opened for slave 0x"); LOG_HEX8(u8Address);
              LOG_STRING(", handle:"); LOG_INT(m_iHandle));

    return Status::SUCCESS;
}


I2C::Status I2C::close()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_iHandle >= 0)
    {
        ::close(m_iHandle);
        LOG_PRINT(LOG_DEBUG, LOG_HDR;
                  LOG_STRING("I2C closed, handle:"); LOG_INT(m_iHandle));
        m_iHandle = -1;
    }

    return Status::SUCCESS;
}


// ============================================================================
// INTERNAL READ PRIMITIVE
// ============================================================================

I2C::Status I2C::timeout_read(uint32_t u32ReadTimeout,
                              std::span<uint8_t> buffer,
                              size_t& szBytesRead) const
{
    if (buffer.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("timeout_read: invalid parameter"));
        return Status::INVALID_PARAM;
    }

    szBytesRead = 0;

    // Use poll(2) to honour the caller-supplied timeout.
    struct pollfd sPollFd;
    sPollFd.fd      = m_iHandle;
    sPollFd.events  = POLLIN;
    sPollFd.revents = 0;

    const int iPollResult = ::poll(&sPollFd, 1, static_cast<int>(u32ReadTimeout));
    if (iPollResult < 0)
    {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("poll() failed"); LOG_INT(err));
        return Status::READ_ERROR;
    }
    else if (iPollResult == 0)
    {
        return Status::READ_TIMEOUT;
    }

    // poll() reports data available — issue the read.
    const ssize_t sszBytesRead = ::read(m_iHandle, buffer.data(), buffer.size());
    if (sszBytesRead <= 0)
    {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("read() failed or returned 0"); LOG_INT(err));
        return Status::READ_ERROR;
    }

    szBytesRead = static_cast<size_t>(sszBytesRead);
    return Status::SUCCESS;
}


// ============================================================================
// INTERNAL WRITE PRIMITIVE
// ============================================================================

I2C::Status I2C::timeout_write(uint32_t /*u32WriteTimeout*/,
                               std::span<const uint8_t> buffer,
                               size_t& szBytesWritten) const
{
    if (buffer.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid parameter: buffer.empty()"));
        return Status::INVALID_PARAM;
    }

    szBytesWritten = 0;

    // I2C writes are atomic at the kernel level; a single ::write() covers
    // the full buffer. Loop guards against short writes (should not occur
    // in practice on i2c-dev, but mirrors the UART driver for consistency).
    while (szBytesWritten < buffer.size())
    {
        const ssize_t sszWritten = ::write(m_iHandle,
                                           buffer.data() + szBytesWritten,
                                           buffer.size() - szBytesWritten);
        if (sszWritten <= 0)
        {
            const int err = errno;
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("I2C write error"); LOG_INT32(err));
            return Status::WRITE_ERROR;
        }
        szBytesWritten += static_cast<size_t>(sszWritten);
    }

    return Status::SUCCESS;
}
