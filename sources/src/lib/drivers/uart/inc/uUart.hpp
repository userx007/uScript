#ifndef U_UART_DRIVER_H
#define U_UART_DRIVER_H

#include "ICommDriver.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <mutex>
#ifndef _WIN32
#include <termios.h>
#endif


class UART : public ICommDriver
{

    public:

        static constexpr size_t   UART_MAX_BUFLENGTH         = 256;  /**< Maximum UART buffer length. */
        static constexpr uint32_t UART_READ_DEFAULT_TIMEOUT  = 5000; /**< Default UART read timeout in milliseconds. */
        static constexpr uint32_t UART_WRITE_DEFAULT_TIMEOUT = 5000; /**< Default UART write timeout in milliseconds. */

        /**
         * @brief Parity mode for the serial line. Affects both the transmit side
         * (which parity bit gets generated on the wire) and the receive side
         * (what the UART hardware checks incoming bytes against).
         * @note This driver does not enable OS-level parity-error reporting
         * (Linux: no INPCK; Windows: fParity is set but fErrorChar/fAbortOnError
         * stay off) — a parity error does not get turned into a dropped/replaced
         * byte or a read failure. The physical parity bit is still generated and
         * checked in hardware exactly as configured; this only controls whether
         * a receive-side mismatch is surfaced specially, and it deliberately
         * isn't, since silently substituting bytes on a parity error would be
         * worse for a caller doing its own end-to-end checksums (e.g. PROFIBUS'
         * FCS) than just letting the checksum catch it.
         */
        enum class Parity : uint8_t { None, Even, Odd };

        UART() = default;

        /**
         * @param strDevice        Device path passed straight to open(), e.g. "/dev/ttyUSB0".
         * @param u32Speed         Baud rate passed straight to open().
         * @param strIdentityLabel Display text for the GUI comm-dump panel (see
         *                         describeConnection()). Supplied separately from
         *                         strDevice rather than derived from it, since the
         *                         caller (plugin factory / INI loader) may want a
         *                         different label than the raw device path. Defaults
         *                         to strDevice when empty at describeConnection() time.
         * @param parity           Parity mode. Defaults to None (8N1's "N"), unchanged
         *                         from this driver's original, parity-less behaviour —
         *                         every existing caller that doesn't pass this still
         *                         gets exactly the framing it always got.
         * @param u8DataBits       Data bits per frame, 5-8. Defaults to 8 (8N1's "8").
         * @param u8StopBits       Stop bits per frame, 1-2. Defaults to 1 (8N1's "1").
         */
        explicit UART(const std::string& strDevice, uint32_t u32Speed,
                      const std::string& strIdentityLabel = {},
                      Parity parity = Parity::None,
                      uint8_t u8DataBits = 8,
                      uint8_t u8StopBits = 1)
            : m_strDevice(strDevice)
            , m_strIdentityLabel(strIdentityLabel)
        {
            open(strDevice, u32Speed, parity, u8DataBits, u8StopBits);
        }

        virtual ~UART()
        {
            close();
        }

        /**
         * @brief Open and configure the serial port.
         * @param parity     See the Parity enum's doc comment. Defaults to None (8N1).
         * @param u8DataBits Data bits per frame, 5-8. Defaults to 8 (8N1).
         * @param u8StopBits Stop bits per frame, 1-2. Defaults to 1 (8N1).
         * @note The three framing parameters default to exactly this driver's
         * original hardcoded 8N1 behaviour, so every pre-existing call site
         * (`open(device, speed)`, or the 2-/3-argument constructor) is
         * unaffected by their addition — this is purely additive.
         */
        Status open(const std::string& strDevice, uint32_t u32Speed,
                    Parity parity = Parity::None,
                    uint8_t u8DataBits = 8,
                    uint8_t u8StopBits = 1);
        Status close();
        bool is_open() const override;

        /**
         * @brief Describe this UART's identity for the GUI comm-dump panel.
         * UART is a point-to-point byte stream with no addressable channels,
         * so xtra_params is ignored — every call returns the same static label.
         * Falls back to the device path when no identity label was supplied.
         */
        CommDetails describeConnection(std::string_view /*xtra_params*/ = {}) const override
        {
            return commdump_details(CommFamily::SERIAL,
                                     m_strIdentityLabel.empty() ? m_strDevice : m_strIdentityLabel);
        }

        /**
         * @brief Unified read interface supporting multiple operation modes
         * 
         * @param u32ReadTimeout Timeout in milliseconds (0 = use default)
         * @param buffer Buffer to read data into
         * @param options Read operation configuration
         * @param xtra_params Optional driver-specific addressing hint (ignored by UART —
         *                    UART is a point-to-point byte-stream with no addressable
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
         * @param xtra_params Optional driver-specific addressing hint (ignored by UART —
         *                    UART is a point-to-point byte-stream with no addressable
         *                    channels; the parameter is accepted for interface conformance)
         * @return WriteResult containing status and bytes written
         */
        WriteResult tout_write(uint32_t u32WriteTimeout,
                               std::span<const uint8_t> buffer,
                               std::string_view xtra_params = {}) const override;

        /** @brief Currently configured parity mode (see the Parity enum's doc comment). */
        Parity getParity(void) const { return m_eParity; }

        /** @brief Currently configured data bits per frame (5-8). */
        uint8_t getDataBits(void) const { return m_u8DataBits; }

        /** @brief Currently configured stop bits per frame (1-2). */
        uint8_t getStopBits(void) const { return m_u8StopBits; }

    private:

        int                m_iHandle = -1; /**< Internal handle to the UART device. */
        mutable std::mutex m_mutex;        /**< Mutex for protecting concurrent access to the driver. */
        std::string        m_strDevice;         /**< Device path passed to open(), used as describeConnection() fallback. */
        std::string        m_strIdentityLabel;  /**< GUI comm-dump display label, see describeConnection(). */
        Parity             m_eParity    = Parity::None; /**< Set by open(), see getParity(). */
        uint8_t            m_u8DataBits = 8;             /**< Set by open(), see getDataBits(). */
        uint8_t            m_u8StopBits = 1;             /**< Set by open(), see getStopBits(). */

        // Legacy internal methods (kept for implementation compatibility)
        Status timeout_read (uint32_t u32ReadTimeout, std::span<uint8_t> buffer, size_t& szBytesRead) const;
        Status timeout_read_until (uint32_t u32ReadTimeout, std::span<uint8_t> buffer, uint8_t cDelimiter, size_t& szBytesRead) const;
        Status timeout_wait_for_token (uint32_t u32ReadTimeout, std::span<const uint8_t> token, bool useBuffer) const;
        Status timeout_write (uint32_t u32WriteTimeouts, std::span<const uint8_t> buffer, size_t& szBytesWritten) const;

        Status purge (bool bInput, bool bOutput) const;
        Status setup (uint32_t u32Speed, Parity parity, uint8_t u8DataBits, uint8_t u8StopBits) const;
        Status kmp_stream_match (std::span<const uint8_t> token, const std::vector<int>& viLps, uint32_t u32Timeout, bool bReturnOnTimeout, bool useBuffer) const;
        void   build_kmp_table (std::span<const uint8_t> pattern, size_t szLength, std::vector<int>& viLps) const;

#ifndef _WIN32
        speed_t getBaud(uint32_t u32Speed) const;
#else
        uint32_t getBaud(uint32_t u32Speed) const;
#endif

};


#endif // U_UART_DRIVER_H
