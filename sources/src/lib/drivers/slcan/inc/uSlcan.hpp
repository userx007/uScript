#ifndef U_SLCAN_DRIVER_HPP
#define U_SLCAN_DRIVER_HPP

/**
 * @file  uSlcan.hpp
 * @brief SLCAN driver — WeActStudio USB2CANFDV1 SLCAN protocol over UART.
 *
 * Implements ICommDriver.  The driver wraps a UART instance and translates
 * CAN / CAN-FD frames to/from the ASCII SLCAN wire format described in the
 * WeActStudio USB2CANFDV1 README.
 *
 * ASCII-mode wire format (terminated by CR = 0x0D):
 *   Configuration  : S<n>[CR], Y<n>[CR], O[CR], C[CR], M<n>[CR], A<n>[CR], …
 *   TX std data    : t<III><L><DD…>[CR]
 *   TX ext data    : T<IIIIIIII><L><DD…>[CR]
 *   TX std remote  : r<III><L>[CR]
 *   TX ext remote  : R<IIIIIIII><L>[CR]
 *   TX CANFD std   : d<III><L><DD…>[CR]  (no BRS)
 *   TX CANFD ext   : D<IIIIIIII><L><DD…>[CR]
 *   TX CANFD std   : b<III><L><DD…>[CR]  (BRS)
 *   TX CANFD ext   : B<IIIIIIII><L><DD…>[CR]
 *
 * Binary Enhance mode (prefix byte = 0x80 | cmd_char) is also supported
 * for high-throughput TX.
 *
 * Received frames arrive asynchronously from the adapter via the same serial
 * port using the same ASCII encoding; tout_read() decodes one frame per call.
 *
 * Response after every command:
 *   CR  (0x0D) = success
 *   BEL (0x07) = failure
 */

#include "ICommDriver.hpp"
#include "uUart.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>


// ============================================================================
// Public data types
// ============================================================================

/**
 * @brief CAN frame type tag.
 */
enum class CanFrameType : uint8_t {
    DataFrame,    ///< Data frame
    RemoteFrame   ///< Remote (RTR) frame
};

/**
 * @brief CAN-FD Bit Rate Switch flag.
 */
enum class CanFdBrs : uint8_t {
    Disabled,     ///< No BRS
    Enabled       ///< BRS enabled (data phase at higher bit rate)
};

/**
 * @brief Decoded CAN / CAN-FD receive frame.
 *
 * Filled by SLCAN::decode_rx_frame() / SLCAN::receive_frame().
 */
struct CanFrame {
    bool        is_extended = false;   ///< True → 29-bit extended ID
    bool        is_remote   = false;   ///< True → RTR frame
    bool        is_canfd    = false;   ///< True → CAN-FD frame
    bool        brs         = false;   ///< True → BRS enabled (CAN-FD only)
    uint32_t    id          = 0;       ///< CAN ID (11-bit or 29-bit)
    uint8_t     dlc         = 0;       ///< DLC code (0-15 for CAN-FD, 0-8 for CAN)
    uint8_t     len         = 0;       ///< Actual data byte count
    std::array<uint8_t, 64> data{};   ///< Payload bytes
};

/**
 * @brief Nominal CAN bit rate presets (S command).
 */
enum class CanBitrate : uint8_t {
    BR_10K   = 0x00,   ///< S0 — 10 kbit/s
    BR_20K   = 0x01,   ///< S1 — 20 kbit/s
    BR_50K   = 0x02,   ///< S2 — 50 kbit/s
    BR_100K  = 0x03,   ///< S3 — 100 kbit/s
    BR_125K  = 0x04,   ///< S4 — 125 kbit/s  (adapter default)
    BR_250K  = 0x05,   ///< S5 — 250 kbit/s
    BR_500K  = 0x06,   ///< S6 — 500 kbit/s
    BR_800K  = 0x07,   ///< S7 — 800 kbit/s
    BR_1M    = 0x08,   ///< S8 — 1 Mbit/s
    BR_83K3  = 0x09,   ///< S9 — 83.3 kbit/s
    BR_75K   = 0x0A,   ///< SA — 75 kbit/s
    BR_62K5  = 0x0B,   ///< SB — 62.5 kbit/s
    BR_33K3  = 0x0C,   ///< SC — 33.3 kbit/s
    BR_5K    = 0x0D,   ///< SD — 5 kbit/s
};

/**
 * @brief CAN-FD data segment bit rate presets (Y command).
 */
enum class CanFdDataRate : uint8_t {
    FD_1M  = 0x01,   ///< Y1 — 1 Mbit/s
    FD_2M  = 0x02,   ///< Y2 — 2 Mbit/s  (adapter default)
    FD_3M  = 0x03,   ///< Y3 — 3 Mbit/s
    FD_4M  = 0x04,   ///< Y4 — 4 Mbit/s
    FD_5M  = 0x05,   ///< Y5 — 5 Mbit/s
};

/**
 * @brief Bus mode (M command).
 */
enum class CanMode : uint8_t {
    Normal = 0,   ///< M0 — normal (default)
    Silent = 1,   ///< M1 — listen-only / silent
};

/**
 * @brief Auto-retransmission (A command).
 */
enum class CanAutoRetx : uint8_t {
    Disabled = 0,   ///< A0 — off (default)
    Enabled  = 1,   ///< A1 — on (not recommended)
};

/**
 * @brief SLCAN Enhance mode (H command).
 */
enum class SlcanEnhance : uint8_t {
    Disabled = 0,   ///< H0 — ASCII mode (default)
    Enabled  = 1,   ///< H1 — binary enhance mode
};


// ============================================================================
// SLCAN driver class
// ============================================================================

/**
 * @brief SLCAN driver for the WeActStudio USB2CANFDV1 CAN-FD adapter.
 *
 * Derives from ICommDriver so it can be used anywhere a generic comm driver
 * is expected.  The unified tout_read() / tout_write() interface is mapped as:
 *
 *   tout_write() — serialises a CAN frame (passed in binary in the buffer)
 *                  to SLCAN ASCII and writes it to the UART.
 *
 *   tout_read()  — reads one ASCII SLCAN frame from the UART and decodes it
 *                  back to binary into the caller's buffer.
 *
 * The xtra_params field in both methods is interpreted as a serialised
 * CanFrame in a special format when using the raw ICommDriver interface;
 * but the richer typed API (send_frame / receive_frame) is strongly preferred.
 *
 * @note Channel configuration (bitrate, mode, open/close) must be done before
 *       sending frames.  The channel must be open before send_frame() is called.
 */
class SLCAN : public ICommDriver
{
public:

    // ------------------------------------------------------------------
    // Constants
    // ------------------------------------------------------------------

    static constexpr uint8_t  SLCAN_CR            = '\r';     ///< Command terminator
    static constexpr uint8_t  SLCAN_ACK           = '\r';     ///< Success response
    static constexpr uint8_t  SLCAN_NAK           = 0x07;     ///< Failure (BEL)
    static constexpr uint32_t SLCAN_DEFAULT_TIMEOUT = 1000;   ///< ms

    /// Maximum ASCII frame string length: cmd(1) + id(8) + dlc(1) + data(128) + CR(1)
    static constexpr size_t   SLCAN_MAX_FRAME_LEN  = 140;
    /// Maximum binary receive line length (same budget)
    static constexpr size_t   SLCAN_RX_BUF_LEN     = 160;

    // ------------------------------------------------------------------
    // Construction / destruction
    // ------------------------------------------------------------------

    /**
     * @brief Construct without opening a port.
     */
    SLCAN() = default;

    /**
     * @brief Construct and immediately open the serial port.
     * @param device  OS device path (e.g. "/dev/ttyACM0", "COM3")
     * @param speed   UART baud rate in bit/s (typically 115200 or higher)
     */
    explicit SLCAN(const std::string& device, uint32_t speed);

    virtual ~SLCAN();

    // ------------------------------------------------------------------
    // Port management
    // ------------------------------------------------------------------

    /**
     * @brief Open the serial port to the SLCAN adapter.
     * @param device  OS device path
     * @param speed   UART baud rate
     * @return SUCCESS or error code
     */
    Status open(const std::string& device, uint32_t speed);

    /**
     * @brief Close the serial port.
     */
    Status close();

    /**
     * @brief Returns true if the serial port is open.
     */
    bool is_open() const override;

    // ------------------------------------------------------------------
    // Channel configuration  (must be called before open_channel)
    // ------------------------------------------------------------------

    /**
     * @brief Set nominal CAN bit rate using preset (S command).
     * @param rate  Preset bit rate
     * @param timeout_ms  Command timeout in ms
     */
    Status set_bitrate(CanBitrate rate, uint32_t timeout_ms = SLCAN_DEFAULT_TIMEOUT);

    /**
     * @brief Set nominal CAN bit rate using custom segments (Sxxyy or Sddxxyy).
     * @param seg1  Bit time segment 1 (0x02–0xFF)
     * @param seg2  Bit time segment 2 (0x02–0x80)
     * @param div   Clock divider (0x01–0xFF); pass 0 to omit (uses default 2)
     */
    Status set_bitrate_custom(uint8_t seg1, uint8_t seg2, uint8_t div = 0,
                              uint32_t timeout_ms = SLCAN_DEFAULT_TIMEOUT);

    /**
     * @brief Set CAN-FD data segment bit rate using preset (Y command).
     * @param rate  Preset data bit rate
     */
    Status set_fd_data_rate(CanFdDataRate rate, uint32_t timeout_ms = SLCAN_DEFAULT_TIMEOUT);

    /**
     * @brief Set CAN-FD data segment bit rate using custom segments (Yxxyy or Yddxxyy).
     * @param seg1  Data segment 1 (0x01–0x20)
     * @param seg2  Data segment 2 (0x01–0x10)
     * @param div   Clock divider (0x01–0x20); pass 0 to omit
     */
    Status set_fd_data_rate_custom(uint8_t seg1, uint8_t seg2, uint8_t div = 0,
                                   uint32_t timeout_ms = SLCAN_DEFAULT_TIMEOUT);

    /**
     * @brief Set bus mode (M command).  Channel must be closed.
     */
    Status set_mode(CanMode mode, uint32_t timeout_ms = SLCAN_DEFAULT_TIMEOUT);

    /**
     * @brief Enable/disable auto-retransmission (A command).  Channel must be closed.
     */
    Status set_auto_retx(CanAutoRetx retx, uint32_t timeout_ms = SLCAN_DEFAULT_TIMEOUT);

    /**
     * @brief Set SLCAN enhance mode (H command).  Channel must be closed.
     */
    Status set_enhance_mode(SlcanEnhance mode, uint32_t timeout_ms = SLCAN_DEFAULT_TIMEOUT);

    /**
     * @brief Set standard ID filter (f command).  Channel must be closed.
     * @param id    11-bit filter ID  (0–0x7FF)
     * @param mask  11-bit filter mask (0 = accept all)
     */
    Status set_std_filter(uint16_t id, uint16_t mask,
                          uint32_t timeout_ms = SLCAN_DEFAULT_TIMEOUT);

    /**
     * @brief Set extended ID filter (F command).  Channel must be closed.
     * @param id    29-bit filter ID  (0–0x1FFFFFFF)
     * @param mask  29-bit filter mask (0 = accept all)
     */
    Status set_ext_filter(uint32_t id, uint32_t mask,
                          uint32_t timeout_ms = SLCAN_DEFAULT_TIMEOUT);

    // ------------------------------------------------------------------
    // Channel open / close
    // ------------------------------------------------------------------

    /**
     * @brief Open the CAN channel (O command).
     */
    Status open_channel(uint32_t timeout_ms = SLCAN_DEFAULT_TIMEOUT);

    /**
     * @brief Close the CAN channel (C command).
     */
    Status close_channel(uint32_t timeout_ms = SLCAN_DEFAULT_TIMEOUT);

    // ------------------------------------------------------------------
    // Diagnostic queries
    // ------------------------------------------------------------------

    /**
     * @brief Read adapter firmware version (V command).
     * @param[out] version  Version string returned by the adapter
     */
    Status get_version(std::string& version, uint32_t timeout_ms = SLCAN_DEFAULT_TIMEOUT);

    /**
     * @brief Read failure state (E command).
     * @param[out] error_str  Error description returned by the adapter
     */
    Status get_error_state(std::string& error_str, uint32_t timeout_ms = SLCAN_DEFAULT_TIMEOUT);

    // ------------------------------------------------------------------
    // Frame TX / RX  (typed, preferred API)
    // ------------------------------------------------------------------

    /**
     * @brief Transmit a CAN or CAN-FD frame.
     *
     * Encodes the frame to SLCAN ASCII and writes it to the UART.  Waits for
     * the adapter's CR/BEL acknowledgement.
     *
     * @param frame       Frame to transmit
     * @param brs         BRS flag (CAN-FD only; ignored for CAN 2.0)
     * @param timeout_ms  TX timeout in ms
     * @return SUCCESS, WRITE_ERROR, or WRITE_TIMEOUT
     */
    Status send_frame(const CanFrame& frame, uint32_t timeout_ms = SLCAN_DEFAULT_TIMEOUT);

    /**
     * @brief Receive one CAN or CAN-FD frame from the adapter.
     *
     * Reads an ASCII SLCAN line (terminated by CR) and decodes it.
     *
     * @param[out] frame      Decoded frame
     * @param      timeout_ms RX timeout in ms
     * @return SUCCESS, READ_TIMEOUT, or READ_ERROR
     */
    Status receive_frame(CanFrame& frame, uint32_t timeout_ms = SLCAN_DEFAULT_TIMEOUT);

    // ------------------------------------------------------------------
    // ICommDriver generic interface (binary / raw)
    // ------------------------------------------------------------------

    /**
     * @brief Generic read — receives one SLCAN frame and stores the raw
     *        ASCII line (including the trailing CR) into @p buffer.
     *
     *  options.mode is ignored; the method always reads until CR.
     */
    ReadResult tout_read(uint32_t u32ReadTimeout,
                         std::span<uint8_t> buffer,
                         const ReadOptions& options,
                         std::string_view xtra_params = {}) const override;

    /**
     * @brief Generic write — sends @p buffer verbatim over the UART
     *        (must already be a valid SLCAN command / frame string).
     */
    WriteResult tout_write(uint32_t u32WriteTimeout,
                           std::span<const uint8_t> buffer,
                           std::string_view xtra_params = {}) const override;

    // ------------------------------------------------------------------
    // Encoding / decoding helpers (static, testable)
    // ------------------------------------------------------------------

    /**
     * @brief Encode a CAN frame to an SLCAN ASCII command string.
     * @param frame   Frame to encode
     * @param[out] out  Output buffer; must be at least SLCAN_MAX_FRAME_LEN bytes
     * @return Number of bytes written (including trailing CR), or 0 on error
     */
    static size_t encode_frame(const CanFrame& frame, std::span<uint8_t> out);

    /**
     * @brief Decode an SLCAN ASCII receive line into a CanFrame.
     * @param line   ASCII bytes (may include trailing CR; null-terminator optional)
     * @param len    Number of bytes in @p line
     * @param[out] frame  Decoded frame
     * @return true on success
     */
    static bool decode_rx_frame(const uint8_t* line, size_t len, CanFrame& frame);

    /**
     * @brief Convert a CAN-FD DLC code to actual byte count.
     * @param dlc  DLC nibble (0x00 – 0x0F)
     * @return Byte count (0–64)
     */
    static uint8_t dlc_to_len(uint8_t dlc);

    /**
     * @brief Convert a byte count to the nearest valid CAN-FD DLC code.
     * @param len  Byte count (0–64)
     * @return DLC nibble
     */
    static uint8_t len_to_dlc(uint8_t len);

private:

    // ------------------------------------------------------------------
    // Internal helpers
    // ------------------------------------------------------------------

    /**
     * @brief Send a simple ASCII command and await CR/BEL acknowledgement.
     * @param cmd         Command string (without CR; CR is appended internally)
     * @param timeout_ms  Timeout in ms
     */
    Status send_command(std::string_view cmd, uint32_t timeout_ms);

    /**
     * @brief Send a command and read back the text response terminated by CR.
     * @param cmd         Command string (without CR)
     * @param[out] resp   Response text (excluding CR)
     */
    Status send_command_get_response(std::string_view cmd, std::string& resp,
                                     uint32_t timeout_ms);

    /**
     * @brief Write raw bytes to the UART.
     */
    Status uart_write(const uint8_t* data, size_t len, uint32_t timeout_ms) const;

    /**
     * @brief Read bytes from UART until CR (0x0D) or timeout.
     * @param[out] buf     Destination buffer (including CR)
     * @param[out] out_len Number of bytes written into buf
     */
    Status uart_read_line(uint8_t* buf, size_t buf_size,
                          size_t& out_len, uint32_t timeout_ms) const;

    // ------------------------------------------------------------------
    // Members
    // ------------------------------------------------------------------

    std::shared_ptr<UART> m_uart;           ///< Underlying UART driver
    bool                  m_channel_open = false; ///< Tracks open_channel state
};


#endif // U_SLCAN_DRIVER_HPP
