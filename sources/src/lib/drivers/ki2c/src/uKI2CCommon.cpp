#include "uKI2C.hpp"
#include "uLogger.hpp"
#include "uNumeric.hpp"

#include <array>
#include <string_view>

#include <errno.h>
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

#define LT_HDR   "KI2C_DRV    |"
#define LOG_HDR  LOG_STRING(LT_HDR)


bool KI2C::is_open() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_iHandle >= 0;
}


// ============================================================================
// PUBLIC UNIFIED INTERFACE IMPLEMENTATION
// ============================================================================

KI2C::ReadResult KI2C::tout_read(uint32_t u32ReadTimeout,
                               std::span<uint8_t> buffer,
                               const ReadOptions& options,
                               std::string_view xtra_params) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    ReadResult result;

    /* ---------- resolve effective slave address, apply transient override --
     *
     * ioctl(I2C_SLAVE) binds this fd to a single 7-bit slave address; unlike
     * SocketCAN's per-frame kernel filtering there is no concurrent listener
     * that could "miss" a reply while the address is being changed — I2C is
     * a synchronous point-to-point exchange on this same fd — so it is
     * enough to install the override before the read and restore m_u8Addr
     * immediately after. m_mutex is held for the whole call (unlike KVCAN,
     * which releases it before blocking I/O), so no other tout_read()/
     * tout_write() call on this instance can interleave a different address
     * in between.
     *
     * Format: decimal or 0x-prefixed hex address, e.g. "0x50" or "80".
     * An empty (or unparsable) xtra_params uses m_u8Addr, the address bound
     * by open().
     */
    bool bTransientAddr = false;

    if (!xtra_params.empty())
    {
        uint8_t u8Override = 0;

        if (numeric::str2uint8(xtra_params, u8Override))
        {
            if (u8Override != m_u8Addr)
            {
                if (::ioctl(m_iHandle, I2C_SLAVE, static_cast<long>(u8Override)) == 0)
                {
                    bTransientAddr = true;
                    LOG_PRINT(LOG_DEBUG, LOG_HDR;
                              LOG_STRING("tout_read: transient slave address:");
                              LOG_HEX8(u8Override));
                }
                else
                {
                    LOG_PRINT(LOG_WARNING, LOG_HDR;
                              LOG_STRING("tout_read: failed to set transient address, errno:");
                              LOG_INT(errno));
                }
            }
            // else: override equals the already-bound default — nothing to do.
        }
        else
        {
            LOG_PRINT(LOG_WARNING, LOG_HDR;
                      LOG_STRING("tout_read: xtra_params not a valid I2C address, ignored"));
        }
    }

    switch (options.mode)
    {
        case ReadMode::Exact:
        {
            size_t bytes_read = 0;
            result.status         = timeout_read(u32ReadTimeout, buffer, bytes_read);
            result.bytes_read     = bytes_read;
            result.found_terminator = false;
            break;
        }

        case ReadMode::UntilDelimiter:
        {
            size_t bytes_read = 0;
            result.status         = timeout_read_until(u32ReadTimeout, buffer,
                                                       options.delimiter, bytes_read);
            result.bytes_read     = bytes_read;
            result.found_terminator = (result.status == Status::SUCCESS);
            break;
        }

        case ReadMode::UntilToken:
        {
            result.status         = timeout_wait_for_token(u32ReadTimeout,
                                                           options.token,
                                                           options.use_buffer);
            result.bytes_read     = 0; // Token search does not fill the user buffer
            result.found_terminator = (result.status == Status::SUCCESS);
            break;
        }

        default:
            result.status         = Status::INVALID_PARAM;
            result.bytes_read     = 0;
            result.found_terminator = false;
            break;
    }

    /* ---------- restore the default slave address --------------------------
     * Errors here are non-fatal and only logged; the read result above is
     * already determined at this point.
     */
    if (bTransientAddr)
    {
        if (::ioctl(m_iHandle, I2C_SLAVE, static_cast<long>(m_u8Addr)) < 0)
        {
            LOG_PRINT(LOG_WARNING, LOG_HDR;
                      LOG_STRING("tout_read: failed to restore default address, errno:");
                      LOG_INT(errno));
        }
        LOG_PRINT(LOG_DEBUG, LOG_HDR;
                  LOG_STRING("tout_read: restored default slave address:"); LOG_HEX8(m_u8Addr));
    }

    return result;
}


KI2C::WriteResult KI2C::tout_write(uint32_t u32WriteTimeout,
                                 std::span<const uint8_t> buffer,
                                 std::string_view xtra_params) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    WriteResult result;

    // See tout_read() above for why a plain install-before/restore-after is
    // sufficient here and doesn't need KVCAN's snapshot/restore machinery.
    bool bTransientAddr = false;

    if (!xtra_params.empty())
    {
        uint8_t u8Override = 0;

        if (numeric::str2uint8(xtra_params, u8Override))
        {
            if (u8Override != m_u8Addr)
            {
                if (::ioctl(m_iHandle, I2C_SLAVE, static_cast<long>(u8Override)) == 0)
                {
                    bTransientAddr = true;
                    LOG_PRINT(LOG_DEBUG, LOG_HDR;
                              LOG_STRING("tout_write: transient slave address:");
                              LOG_HEX8(u8Override));
                }
                else
                {
                    LOG_PRINT(LOG_WARNING, LOG_HDR;
                              LOG_STRING("tout_write: failed to set transient address, errno:");
                              LOG_INT(errno));
                }
            }
        }
        else
        {
            LOG_PRINT(LOG_WARNING, LOG_HDR;
                      LOG_STRING("tout_write: xtra_params not a valid I2C address, ignored"));
        }
    }

    size_t bytes_written = 0;

    result.status        = timeout_write(u32WriteTimeout, buffer, bytes_written);
    result.bytes_written = bytes_written;

    if (bTransientAddr)
    {
        if (::ioctl(m_iHandle, I2C_SLAVE, static_cast<long>(m_u8Addr)) < 0)
        {
            LOG_PRINT(LOG_WARNING, LOG_HDR;
                      LOG_STRING("tout_write: failed to restore default address, errno:");
                      LOG_INT(errno));
        }
        LOG_PRINT(LOG_DEBUG, LOG_HDR;
                  LOG_STRING("tout_write: restored default slave address:"); LOG_HEX8(m_u8Addr));
    }

    return result;
}


// ============================================================================
// PRIVATE LEGACY IMPLEMENTATION (INTERNAL USE ONLY)
// ============================================================================

KI2C::Status KI2C::timeout_wait_for_token(uint32_t u32ReadTimeout,
                                        std::span<const uint8_t> token,
                                        bool useBuffer) const
{
    const size_t szTokenLength = token.size();
    if (token.empty() || szTokenLength == 0 || szTokenLength >= KI2C_MAX_BUFLENGTH)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid token or length"));
        return Status::INVALID_PARAM;
    }

    uint32_t u32Timeout     = (u32ReadTimeout == 0) ? KI2C_READ_DEFAULT_TIMEOUT : u32ReadTimeout;
    bool     bReturnOnTimeout = (u32ReadTimeout != 0);

    std::vector<int> viLps;
    build_kmp_table(token, szTokenLength, viLps);

    return kmp_stream_match(token, viLps, u32Timeout, bReturnOnTimeout, useBuffer);
}


void KI2C::build_kmp_table(std::span<const uint8_t> pattern,
                          size_t szLength,
                          std::vector<int>& viLps) const
{
    viLps.resize(szLength);
    int len  = 0;
    viLps[0] = 0;

    for (size_t i = 1; i < szLength; )
    {
        if (pattern[i] == pattern[len])
        {
            viLps[i++] = ++len;
        }
        else
        {
            if (len != 0)
            {
                len = viLps[len - 1];
            }
            else
            {
                viLps[i++] = 0;
            }
        }
    }
}


KI2C::Status KI2C::kmp_stream_match(std::span<const uint8_t> token,
                                  const std::vector<int>& viLps,
                                  uint32_t u32Timeout,
                                  bool bReturnOnTimeout,
                                  bool useBuffer) const
{
    uint8_t  Buffer[KI2C_MAX_BUFLENGTH] = {0};
    uint32_t u32Matched   = 0;
    uint32_t u32BufferPos = 0;

    while (true)
    {
        uint8_t cByte          = 0;
        size_t  actualBytesRead = 0;

        KI2C::Status i32ReadResult =
            timeout_read(u32Timeout, std::span<uint8_t>(&cByte, 1), actualBytesRead);

        if (i32ReadResult != Status::SUCCESS || actualBytesRead == 0)
        {
            return (i32ReadResult == Status::READ_TIMEOUT && bReturnOnTimeout)
                   ? Status::READ_TIMEOUT
                   : Status::READ_ERROR;
        }

        if (useBuffer)
        {
            Buffer[u32BufferPos++ % KI2C_MAX_BUFLENGTH] = cByte;
        }

        while (u32Matched > 0 && cByte != token[u32Matched])
        {
            u32Matched = static_cast<uint32_t>(viLps[u32Matched - 1]);
        }

        if (cByte == token[u32Matched])
        {
            ++u32Matched;
            if (u32Matched == token.size())
            {
                return Status::SUCCESS;
            }
        }
    }
}


KI2C::Status KI2C::timeout_read_until(uint32_t u32ReadTimeout,
                                    std::span<uint8_t> buffer,
                                    uint8_t cDelimiter,
                                    size_t& szBytesRead) const
{
    if (buffer.size() < 2)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Buffer too small for delimiter + null terminator"));
        return Status::INVALID_PARAM;
    }

    szBytesRead = 0;
    KI2C::Status eResult = Status::RETVAL_NOT_SET;

    while (eResult == Status::RETVAL_NOT_SET)
    {
        const size_t bytesRemaining = buffer.size() - szBytesRead - 1; // reserve for '\0'
        if (bytesRemaining == 0)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Buffer full before delimiter found"));
            return Status::BUFFER_OVERFLOW;
        }

        uint8_t cByte          = 0;
        size_t  actualBytesRead = 0;

        KI2C::Status readResult =
            timeout_read(u32ReadTimeout, std::span<uint8_t>(&cByte, 1), actualBytesRead);

        if (readResult == Status::SUCCESS && actualBytesRead > 0)
        {
            if (cByte == cDelimiter)
            {
                buffer[szBytesRead] = '\0';
                return Status::SUCCESS;
            }
            buffer[szBytesRead++] = cByte;
        }
        else if (readResult == Status::READ_TIMEOUT)
        {
            eResult = (u32ReadTimeout > 0) ? Status::READ_TIMEOUT : Status::PORT_ACCESS;
        }
        else
        {
            eResult = Status::PORT_ACCESS;
        }
    }

    return eResult;
}
