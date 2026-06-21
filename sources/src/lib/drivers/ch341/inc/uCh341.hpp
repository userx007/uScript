#ifndef U_CH341_DRIVER_H
#define U_CH341_DRIVER_H

#include "ICommDriver.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <mutex>

/**
 * @brief Userspace wrapper for the CH340/CH341 USB-to-serial kernel driver.
 *
 * The CH341 kernel module (src/ch341.c) registers a standard Linux tty
 * driver (major CH341_TTY_MAJOR / "/dev/ttyCH341USBx"). From userspace the
 * device therefore behaves like any other serial port: it is opened with
 * open(2) and configured with the termios API. The one practical
 * difference versus a generic UART is that the CH341 ASIC can generate a
 * much wider range of non-standard baud rates; the kernel driver honours
 * BOTHER/c_ispeed/c_ospeed (termios2) so this wrapper configures the line
 * using TCGETS2/TCSETS2 instead of the fixed cfsetispeed() speed_t enum
 * used by classic UART drivers, allowing arbitrary baud values to be
 * requested directly.
 *
 * Implementation/structure intentionally mirrors the UART driver
 * (uUart.hpp / uUartLinux.cpp / uUartCommon.cpp) so the two drivers stay
 * consistent and interchangeable behind ICommDriver.
 */
class CH341 : public ICommDriver
{

    public:

        static constexpr size_t   CH341_MAX_BUFLENGTH         = 256;  /**< Maximum CH341 buffer length. */
        static constexpr uint32_t CH341_READ_DEFAULT_TIMEOUT  = 5000; /**< Default CH341 read timeout in milliseconds. */
        static constexpr uint32_t CH341_WRITE_DEFAULT_TIMEOUT = 5000; /**< Default CH341 write timeout in milliseconds. */

        CH341() = default;

        explicit CH341(const std::string& strDevice, uint32_t u32Speed)
        {
            open(strDevice, u32Speed);
        }

        virtual ~CH341()
        {
            close();
        }

        /**
         * @brief Open and configure the CH341 tty node (e.g. "/dev/ttyCH341USB0").
         * @param strDevice Path to the tty device created by the ch341 kernel driver.
         * @param u32Speed  Requested baud rate (arbitrary values supported via BOTHER).
         */
        Status open(const std::string& strDevice, uint32_t u32Speed);
        Status close();
        bool is_open() const override;

        /**
         * @brief Unified read interface supporting multiple operation modes
         *
         * @param u32ReadTimeout Timeout in milliseconds (0 = use default)
         * @param buffer Buffer to read data into
         * @param options Read operation configuration
         * @param xtra_params Optional driver-specific addressing hint (ignored by CH341 —
         *                    it is a point-to-point byte-stream with no addressable
         *                    channels; the parameter is accepted for interface conformance)
         * @return ReadResult containing status, bytes read, and terminator found flag
         *
         * @details
         * - ReadMode::Exact: Reads up to buffer.size() bytes
         * - ReadMode::UntilDelimiter: Reads until delimiter is found, null-terminates
         * - ReadMode::UntilToken: Searches for token sequence using KMP algorithm
         */
        ReadResult tout_read(uint32_t u32ReadTimeout,
                             std::span<uint8_t> buffer,
                             const ReadOptions& options,
                             std::string_view xtra_params = {}) const override;

        /**
         * @brief Unified write interface
         *
         * @param u32WriteTimeout Timeout in milliseconds (0 = use default)
         * @param buffer Data to write
         * @param xtra_params Optional driver-specific addressing hint (ignored by CH341 —
         *                    it is a point-to-point byte-stream with no addressable
         *                    channels; the parameter is accepted for interface conformance)
         * @return WriteResult containing status and bytes written
         */
        WriteResult tout_write(uint32_t u32WriteTimeout,
                               std::span<const uint8_t> buffer,
                               std::string_view xtra_params = {}) const override;

        /**
         * @brief Read current modem/control line status (DCD, DSR, RI, CTS, ...)
         *        as reported by the CH341 status endpoint, via TIOCMGET.
         * @param u32Lines Bitmask of TIOCM_* lines on success.
         * @return Status::SUCCESS on success.
         */
        Status get_modem_lines(unsigned int& u32Lines) const;

        /**
         * @brief Assert/clear DTR and/or RTS output lines, via TIOCMBIS/TIOCMBIC.
         */
        Status set_dtr_rts(bool bDtr, bool bRts) const;

    private:

        int                m_iHandle = -1; /**< Internal handle to the CH341 tty device. */
        mutable std::mutex m_mutex;        /**< Mutex for protecting concurrent access to the driver. */

        // Legacy internal methods (kept for implementation compatibility, mirrors UART)
        Status timeout_read (uint32_t u32ReadTimeout, std::span<uint8_t> buffer, size_t& szBytesRead) const;
        Status timeout_read_until (uint32_t u32ReadTimeout, std::span<uint8_t> buffer, uint8_t cDelimiter, size_t& szBytesRead) const;
        Status timeout_wait_for_token (uint32_t u32ReadTimeout, std::span<const uint8_t> token, bool useBuffer) const;
        Status timeout_write (uint32_t u32WriteTimeouts, std::span<const uint8_t> buffer, size_t& szBytesWritten) const;

        Status purge (bool bInput, bool bOutput) const;
        Status setup (uint32_t u32Speed) const;
        Status kmp_stream_match (std::span<const uint8_t> token, const std::vector<int>& viLps, uint32_t u32Timeout, bool bReturnOnTimeout, bool useBuffer) const;
        void   build_kmp_table (std::span<const uint8_t> pattern, size_t szLength, std::vector<int>& viLps) const;

};


#endif // U_CH341_DRIVER_H
