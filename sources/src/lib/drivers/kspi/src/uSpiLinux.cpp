#include "uSpi.hpp"
#include "uLogger.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <poll.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>   // SPI_IOC_WR_*, SPI_IOC_MESSAGE, spi_ioc_transfer


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR   "SPI_DRV     |"
#define LOG_HDR  LOG_STRING(LT_HDR)


// ============================================================================
// OPEN / CLOSE
// ============================================================================

SPI::Status SPI::open(const std::string& strDevice, const SpiConfig& config)
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
        const int errnoRet = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Failed to open ["); LOG_STRING(strDevice.c_str());
                  LOG_STRING("] errno:"); LOG_INT(errnoRet));
        return Status::PORT_ACCESS;
    }

    const Status result = setup(config);
    if (result != Status::SUCCESS)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Failed to configure ["); LOG_STRING(strDevice.c_str());
                  LOG_STRING("] Error:"); LOG_INT(static_cast<int>(result)));
        ::close(m_iHandle);
        m_iHandle = -1;
        return Status::PORT_ACCESS;
    }

    m_config = config;

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("SPI ["); LOG_STRING(strDevice.c_str());
              LOG_STRING("] opened, mode:"); LOG_UINT32(config.mode);
              LOG_STRING(" speed:"); LOG_UINT32(config.speed_hz);
              LOG_STRING(" bpw:"); LOG_UINT32(config.bits_per_word);
              LOG_STRING(", handle:"); LOG_INT(m_iHandle));

    return Status::SUCCESS;
}


SPI::Status SPI::close()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_iHandle >= 0)
    {
        ::close(m_iHandle);
        LOG_PRINT(LOG_DEBUG, LOG_HDR;
                  LOG_STRING("SPI closed, handle:"); LOG_INT(m_iHandle));
        m_iHandle = -1;
    }

    return Status::SUCCESS;
}


// ============================================================================
// BUS CONFIGURATION
// ============================================================================

SPI::Status SPI::setup(const SpiConfig& config) const
{
    // SPI mode (CPOL / CPHA)
    uint8_t mode = config.mode & 0x03u;
    if (::ioctl(m_iHandle, SPI_IOC_WR_MODE, &mode) < 0)
    {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ioctl(SPI_IOC_WR_MODE) failed, errno:"); LOG_INT(err));
        return Status::PORT_ACCESS;
    }

    // LSB / MSB bit order
    uint8_t lsb = config.lsb_first ? 1u : 0u;
    if (::ioctl(m_iHandle, SPI_IOC_WR_LSB_FIRST, &lsb) < 0)
    {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ioctl(SPI_IOC_WR_LSB_FIRST) failed, errno:"); LOG_INT(err));
        return Status::PORT_ACCESS;
    }

    // Bits per word
    uint8_t bpw = config.bits_per_word;
    if (::ioctl(m_iHandle, SPI_IOC_WR_BITS_PER_WORD, &bpw) < 0)
    {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ioctl(SPI_IOC_WR_BITS_PER_WORD) failed, errno:"); LOG_INT(err));
        return Status::PORT_ACCESS;
    }

    // Maximum bus speed
    uint32_t speed = config.speed_hz;
    if (::ioctl(m_iHandle, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0)
    {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ioctl(SPI_IOC_WR_MAX_SPEED_HZ) failed, errno:"); LOG_INT(err));
        return Status::PORT_ACCESS;
    }

    return Status::SUCCESS;
}


// ============================================================================
// INTERNAL FULL-DUPLEX TRANSFER PRIMITIVE
// ============================================================================

SPI::Status SPI::spi_transfer(const uint8_t* txBuf,
                              uint8_t*       rxBuf,
                              size_t         length) const
{
    // Zero-initialise so any un-set fields default to using the values
    // already configured on the file descriptor by setup().
    struct spi_ioc_transfer xfer = {};
    xfer.tx_buf        = reinterpret_cast<uintptr_t>(txBuf);
    xfer.rx_buf        = reinterpret_cast<uintptr_t>(rxBuf);
    xfer.len           = static_cast<uint32_t>(length);
    xfer.speed_hz      = m_config.speed_hz;
    xfer.bits_per_word = m_config.bits_per_word;
    xfer.delay_usecs   = 0;
    xfer.cs_change     = 0;

    if (::ioctl(m_iHandle, SPI_IOC_MESSAGE(1), &xfer) < 0)
    {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("SPI_IOC_MESSAGE failed, errno:"); LOG_INT(err));
        return Status::READ_ERROR;
    }

    return Status::SUCCESS;
}


// ============================================================================
// INTERNAL READ PRIMITIVE
// ============================================================================

SPI::Status SPI::timeout_read(uint32_t u32ReadTimeout,
                              std::span<uint8_t> buffer,
                              size_t& szBytesRead) const
{
    if (buffer.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("timeout_read: invalid parameter"));
        return Status::INVALID_PARAM;
    }

    szBytesRead = 0;

    // poll(2) on a spidev fd unblocks immediately (SPI is synchronous), but
    // we keep the guard consistent with the UART / I2C drivers so that a
    // hung kernel driver does not block indefinitely.
    struct pollfd sPollFd;
    sPollFd.fd      = m_iHandle;
    sPollFd.events  = POLLOUT; // spidev signals writable when the bus is ready
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

    // TX padding: send 0x00 bytes while clocking in the RX data.
    std::vector<uint8_t> txPad(buffer.size(), 0x00u);

    const Status st = spi_transfer(txPad.data(), buffer.data(), buffer.size());
    if (st != Status::SUCCESS)
    {
        return Status::READ_ERROR;
    }

    szBytesRead = buffer.size();
    return Status::SUCCESS;
}


// ============================================================================
// INTERNAL WRITE PRIMITIVE
// ============================================================================

SPI::Status SPI::timeout_write(uint32_t u32WriteTimeout,
                               std::span<const uint8_t> buffer,
                               size_t& szBytesWritten) const
{
    if (buffer.empty())
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid parameter: buffer.empty()"));
        return Status::INVALID_PARAM;
    }

    szBytesWritten = 0;

    // poll guard — consistent with UART / I2C drivers.
    struct pollfd sPollFd;
    sPollFd.fd      = m_iHandle;
    sPollFd.events  = POLLOUT;
    sPollFd.revents = 0;

    const uint32_t u32Timeout   = (u32WriteTimeout == 0) ? SPI_WRITE_DEFAULT_TIMEOUT
                                                         : u32WriteTimeout;
    const int iPollResult = ::poll(&sPollFd, 1, static_cast<int>(u32Timeout));
    if (iPollResult < 0)
    {
        const int err = errno;
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("poll() failed"); LOG_INT(err));
        return Status::WRITE_ERROR;
    }
    else if (iPollResult == 0)
    {
        return Status::WRITE_TIMEOUT;
    }

    // RX sink: allocate a discard buffer for the simultaneous incoming bytes.
    std::vector<uint8_t> rxSink(buffer.size(), 0x00u);

    const Status st = spi_transfer(buffer.data(), rxSink.data(), buffer.size());
    if (st != Status::SUCCESS)
    {
        return Status::WRITE_ERROR;
    }

    szBytesWritten = buffer.size();
    return Status::SUCCESS;
}
