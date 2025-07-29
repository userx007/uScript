#ifndef UUART_DRIVER_H
#define UUART_DRIVER_H

#include "ICommDriver.hpp"

#include <string>
#include <vector>
#include <span>
#ifndef _WIN32
#include <termios.h>
#endif


class UART : public ICommDriver
{

    public:

        static constexpr size_t   UART_MAX_BUFLENGTH         = 256;  /**< Maximum UART buffer length. */
        static constexpr uint32_t UART_READ_DEFAULT_TIMEOUT  = 5000; /**< Default UART read timeout in milliseconds. */
        static constexpr uint32_t UART_WRITE_DEFAULT_TIMEOUT = 5000; /**< Default UART write timeout in milliseconds. */

        UART() = default;

        explicit UART(const std::string& strDevice, uint32_t u32Speed) : m_iHandle(-1)
        {
            open(strDevice, u32Speed);
        }

        virtual ~UART()
        {
            close();
        }

        Status open(const std::string& strDevice, uint32_t u32Speed);
        Status close();
        bool is_open() const;

        Status timeout_read (uint32_t u32ReadTimeout, std::span<uint8_t> buffer, size_t* pBytesRead) const;
        Status timeout_read_until (uint32_t u32TimeoutMs, std::span<uint8_t> buffer, uint8_t cDelimiter) const;
        Status timeout_wait_for_token (uint32_t u32ReadTimeout, std::span<const uint8_t> token, bool useBuffer) const;
        Status timeout_write (uint32_t u32WriteTimeouts, std::span<const uint8_t> buffer) const;

    private:

        int m_iHandle; /**< Internal handle to the UART device. */

        Status purge (bool bInput, bool bOutput) const;
        Status setup (uint32_t u32Speed) const;
        Status kmp_stream_match (std::span<const uint8_t> token, const std::vector<int>& viLps, uint32_t u32Timeout, bool bReturnOnTimeout, bool useBuffer) const;
        void   build_kmp_table (std::span<const uint8_t> pattern, size_t szLength, std::vector<int>& viLps) const;

#ifndef _WIN32
        speed_t getBaud(uint32_t u32Speed) const;
#else
        uint32_t getBaud(uint32_t u32Speed) const;
#endif

};


#endif // UUART_DRIVER_H
