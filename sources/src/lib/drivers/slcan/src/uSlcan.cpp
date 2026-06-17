/**
 * @file  uSlcan.cpp
 * @brief SLCAN driver implementation — WeActStudio USB2CANFDV1 protocol.
 *
 * Wire format reference:
 *   https://github.com/WeActStudio/WeActStudio.USB2CANFDV1 (README.md)
 *
 * Frame encoding/decoding
 * -----------------------
 *
 *   CAN 2.0 standard data  : t<III><L><DD…>[CR]   ID=3 hex, L=1 hex nibble, DD=2 hex/byte
 *   CAN 2.0 extended data  : T<IIIIIIII><L><DD…>[CR]  ID=8 hex
 *   CAN 2.0 std remote     : r<III><L>[CR]
 *   CAN 2.0 ext remote     : R<IIIIIIII><L>[CR]
 *   CANFD std (no BRS)     : d<III><L><DD…>[CR]
 *   CANFD ext (no BRS)     : D<IIIIIIII><L><DD…>[CR]
 *   CANFD std (BRS)        : b<III><L><DD…>[CR]
 *   CANFD ext (BRS)        : B<IIIIIIII><L><DD…>[CR]
 *
 *   L for CANFD = DLC code character:
 *     '0'–'8' = 0–8 bytes, '9'=12, 'A'=16, 'B'=20, 'C'=24, 'D'=32, 'E'=48, 'F'=64
 *
 * Acknowledge
 * -----------
 *   CR  (0x0D) = command accepted
 *   BEL (0x07) = command rejected
 */

#include "uSlcan.hpp"
#include "uLogger.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR   "SLCAN_DRV   |"
#define LOG_HDR  LOG_STRING(LT_HDR)


// ============================================================================
// DLC ↔ length tables  (CAN-FD ISO 11898-1)
// ============================================================================

static constexpr std::array<uint8_t, 16> DLC_TO_LEN_TABLE = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64
};

uint8_t SLCAN::dlc_to_len(uint8_t dlc)
{
    if (dlc >= DLC_TO_LEN_TABLE.size()) return 64;
    return DLC_TO_LEN_TABLE[dlc];
}

uint8_t SLCAN::len_to_dlc(uint8_t len)
{
    if (len <= 8)  return len;
    if (len <= 12) return 9;
    if (len <= 16) return 10;
    if (len <= 20) return 11;
    if (len <= 24) return 12;
    if (len <= 32) return 13;
    if (len <= 48) return 14;
    return 15;
}

// ============================================================================
// Hex helpers
// ============================================================================

static inline char nibble_to_hex(uint8_t v)
{
    return (v < 10) ? ('0' + v) : ('A' + v - 10);
}

static inline int hex_to_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return -1;
}

/** Write @p digits hex characters representing the lower bits of @p value into @p buf. */
static size_t write_hex(uint8_t* buf, uint32_t value, int digits)
{
    for (int i = digits - 1; i >= 0; --i) {
        buf[i] = static_cast<uint8_t>(nibble_to_hex(value & 0x0F));
        value >>= 4;
    }
    return static_cast<size_t>(digits);
}

/** Parse @p digits hex characters from @p src into @p out; returns false on bad char. */
static bool read_hex(const uint8_t* src, int digits, uint32_t& out)
{
    out = 0;
    for (int i = 0; i < digits; ++i) {
        int n = hex_to_nibble(static_cast<char>(src[i]));
        if (n < 0) return false;
        out = (out << 4) | static_cast<uint32_t>(n);
    }
    return true;
}

// ============================================================================
// Construction / destruction
// ============================================================================

SLCAN::SLCAN(const std::string& device, uint32_t speed)
    : m_uart(std::make_shared<UART>())
{
    open(device, speed);
}

SLCAN::~SLCAN()
{
    if (m_channel_open) {
        (void)close_channel();
    }
    close();
}

// ============================================================================
// Port management
// ============================================================================

ICommDriver::Status SLCAN::open(const std::string& device, uint32_t speed)
{
    if (!m_uart) {
        m_uart = std::make_shared<UART>();
    }
    Status s = m_uart->open(device, speed);
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Failed to open UART "); LOG_STRING(device.c_str()));
    }
    return s;
}

ICommDriver::Status SLCAN::close()
{
    if (!m_uart) return Status::SUCCESS;
    return m_uart->close();
}

bool SLCAN::is_open() const
{
    return m_uart && m_uart->is_open();
}

// ============================================================================
// Internal UART helpers
// ============================================================================

ICommDriver::Status SLCAN::uart_write(const uint8_t* data, size_t len, uint32_t timeout_ms) const
{
    if (!m_uart || !m_uart->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("uart_write: port not open"));
        return Status::PORT_ACCESS;
    }
    auto res = m_uart->tout_write(timeout_ms, std::span<const uint8_t>(data, len));
    if (res.status != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("uart_write failed: "); LOG_STRING(to_string(res.status).c_str()));
    }
    return res.status;
}

ICommDriver::Status SLCAN::uart_read_line(uint8_t* buf, size_t buf_size,
                                           size_t& out_len, uint32_t timeout_ms) const
{
    if (!m_uart || !m_uart->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("uart_read_line: port not open"));
        return Status::PORT_ACCESS;
    }

    ReadOptions opts;
    opts.mode      = ReadMode::UntilDelimiter;
    opts.delimiter = SLCAN_CR;

    auto res = m_uart->tout_read(timeout_ms,
                                  std::span<uint8_t>(buf, buf_size),
                                  opts);
    out_len = res.bytes_read;
    return res.status;
}

// ============================================================================
// send_command  — write cmd+CR and wait for ACK byte
// ============================================================================

ICommDriver::Status SLCAN::send_command(std::string_view cmd, uint32_t timeout_ms)
{
    // Build command: cmd bytes + CR
    std::array<uint8_t, 64> tx{};
    if (cmd.size() + 1 > tx.size()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("send_command: cmd too long"));
        return Status::INVALID_PARAM;
    }
    std::memcpy(tx.data(), cmd.data(), cmd.size());
    tx[cmd.size()] = SLCAN_CR;

    LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("CMD >> "); LOG_STRING(std::string(cmd).c_str()));

    Status s = uart_write(tx.data(), cmd.size() + 1, timeout_ms);
    if (s != Status::SUCCESS) return s;

    // Read one-byte ACK: CR (success) or BEL (failure)
    // The adapter echoes CR on success, 0x07 on failure.
    // We read until delimiter=CR or a single-byte read.
    uint8_t ack = 0;
    size_t  got = 0;
    ReadOptions ro;
    ro.mode      = ReadMode::Exact;
    ro.delimiter = SLCAN_CR;
    auto res = m_uart->tout_read(timeout_ms, std::span<uint8_t>(&ack, 1), ro);
    got = res.bytes_read;

    if (res.status != Status::SUCCESS || got == 0) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("ACK read failed: "); LOG_STRING(to_string(res.status).c_str()));
        return res.status;
    }

    if (ack == SLCAN_NAK) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("CMD NAK for: "); LOG_STRING(std::string(cmd).c_str()));
        return Status::NACK;
    }
    // Any other byte (including CR) treated as success
    return Status::SUCCESS;
}

// ============================================================================
// send_command_get_response  — write cmd+CR and read text back until CR
// ============================================================================

ICommDriver::Status SLCAN::send_command_get_response(std::string_view cmd, std::string& resp,
                                                      uint32_t timeout_ms)
{
    std::array<uint8_t, 64> tx{};
    if (cmd.size() + 1 > tx.size()) return Status::INVALID_PARAM;
    std::memcpy(tx.data(), cmd.data(), cmd.size());
    tx[cmd.size()] = SLCAN_CR;

    Status s = uart_write(tx.data(), cmd.size() + 1, timeout_ms);
    if (s != Status::SUCCESS) return s;

    std::array<uint8_t, 128> rx{};
    size_t got = 0;
    s = uart_read_line(rx.data(), rx.size(), got, timeout_ms);
    if (s != Status::SUCCESS) return s;

    resp.assign(reinterpret_cast<char*>(rx.data()), got);
    return Status::SUCCESS;
}

// ============================================================================
// Channel configuration
// ============================================================================

ICommDriver::Status SLCAN::set_bitrate(CanBitrate rate, uint32_t timeout_ms)
{
    if (m_channel_open) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("set_bitrate called with channel open"));
        return Status::INVALID_PARAM;
    }
    char cmd[4];
    if (static_cast<uint8_t>(rate) < 10) {
        std::snprintf(cmd, sizeof(cmd), "S%X", static_cast<unsigned>(rate));
    } else {
        std::snprintf(cmd, sizeof(cmd), "S%X", static_cast<unsigned>(rate));
    }
    return send_command(cmd, timeout_ms);
}

ICommDriver::Status SLCAN::set_bitrate_custom(uint8_t seg1, uint8_t seg2, uint8_t div,
                                               uint32_t timeout_ms)
{
    if (m_channel_open) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("set_bitrate_custom called with channel open"));
        return Status::INVALID_PARAM;
    }
    char cmd[16];
    if (div == 0) {
        // Sxxyy  (default div=2 → 30 MHz)
        std::snprintf(cmd, sizeof(cmd), "S%02X%02X", seg1, seg2);
    } else {
        // Sddxxyy
        std::snprintf(cmd, sizeof(cmd), "S%02X%02X%02X", div, seg1, seg2);
    }
    return send_command(cmd, timeout_ms);
}

ICommDriver::Status SLCAN::set_fd_data_rate(CanFdDataRate rate, uint32_t timeout_ms)
{
    if (m_channel_open) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("set_fd_data_rate called with channel open"));
        return Status::INVALID_PARAM;
    }
    char cmd[4];
    std::snprintf(cmd, sizeof(cmd), "Y%X", static_cast<unsigned>(rate));
    return send_command(cmd, timeout_ms);
}

ICommDriver::Status SLCAN::set_fd_data_rate_custom(uint8_t seg1, uint8_t seg2, uint8_t div,
                                                    uint32_t timeout_ms)
{
    if (m_channel_open) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("set_fd_data_rate_custom called with channel open"));
        return Status::INVALID_PARAM;
    }
    char cmd[16];
    if (div == 0) {
        std::snprintf(cmd, sizeof(cmd), "Y%02X%02X", seg1, seg2);
    } else {
        std::snprintf(cmd, sizeof(cmd), "Y%02X%02X%02X", div, seg1, seg2);
    }
    return send_command(cmd, timeout_ms);
}

ICommDriver::Status SLCAN::set_mode(CanMode mode, uint32_t timeout_ms)
{
    if (m_channel_open) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("set_mode called with channel open"));
        return Status::INVALID_PARAM;
    }
    char cmd[4];
    std::snprintf(cmd, sizeof(cmd), "M%u", static_cast<unsigned>(mode));
    return send_command(cmd, timeout_ms);
}

ICommDriver::Status SLCAN::set_auto_retx(CanAutoRetx retx, uint32_t timeout_ms)
{
    if (m_channel_open) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("set_auto_retx called with channel open"));
        return Status::INVALID_PARAM;
    }
    char cmd[4];
    std::snprintf(cmd, sizeof(cmd), "A%u", static_cast<unsigned>(retx));
    return send_command(cmd, timeout_ms);
}

ICommDriver::Status SLCAN::set_enhance_mode(SlcanEnhance mode, uint32_t timeout_ms)
{
    if (m_channel_open) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("set_enhance_mode called with channel open"));
        return Status::INVALID_PARAM;
    }
    char cmd[4];
    std::snprintf(cmd, sizeof(cmd), "H%u", static_cast<unsigned>(mode));
    return send_command(cmd, timeout_ms);
}

ICommDriver::Status SLCAN::set_std_filter(uint16_t id, uint16_t mask, uint32_t timeout_ms)
{
    if (m_channel_open) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("set_std_filter called with channel open"));
        return Status::INVALID_PARAM;
    }
    char cmd[16];
    // fIIIMMM — 3 hex digits each
    std::snprintf(cmd, sizeof(cmd), "f%03X%03X", id & 0x7FFu, mask & 0x7FFu);
    return send_command(cmd, timeout_ms);
}

ICommDriver::Status SLCAN::set_ext_filter(uint32_t id, uint32_t mask, uint32_t timeout_ms)
{
    if (m_channel_open) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("set_ext_filter called with channel open"));
        return Status::INVALID_PARAM;
    }
    char cmd[24];
    // FIIIIIIIIMMMMMMMM — 8 hex digits each
    std::snprintf(cmd, sizeof(cmd), "F%08X%08X", id & 0x1FFFFFFFu, mask & 0x1FFFFFFFu);
    return send_command(cmd, timeout_ms);
}

// ============================================================================
// Channel open / close
// ============================================================================

ICommDriver::Status SLCAN::open_channel(uint32_t timeout_ms)
{
    Status s = send_command("O", timeout_ms);
    if (s == Status::SUCCESS) {
        m_channel_open = true;
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("CAN channel opened"));
    }
    return s;
}

ICommDriver::Status SLCAN::close_channel(uint32_t timeout_ms)
{
    Status s = send_command("C", timeout_ms);
    m_channel_open = false;   // mark closed even on error to avoid loops
    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("CAN channel closed"));
    return s;
}

// ============================================================================
// Diagnostic queries
// ============================================================================

ICommDriver::Status SLCAN::get_version(std::string& version, uint32_t timeout_ms)
{
    return send_command_get_response("V", version, timeout_ms);
}

ICommDriver::Status SLCAN::get_error_state(std::string& error_str, uint32_t timeout_ms)
{
    return send_command_get_response("E", error_str, timeout_ms);
}

// ============================================================================
// Frame encoding (static)
// ============================================================================

size_t SLCAN::encode_frame(const CanFrame& frame, std::span<uint8_t> out)
{
    if (out.size() < SLCAN_MAX_FRAME_LEN) return 0;

    uint8_t* p = out.data();
    size_t   n = 0;

    // Select command character
    char cmd;
    if (frame.is_canfd) {
        if (frame.is_extended) {
            cmd = frame.brs ? 'B' : 'D';
        } else {
            cmd = frame.brs ? 'b' : 'd';
        }
    } else {
        if (frame.is_remote) {
            cmd = frame.is_extended ? 'R' : 'r';
        } else {
            cmd = frame.is_extended ? 'T' : 't';
        }
    }

    p[n++] = static_cast<uint8_t>(cmd);

    // ID field
    if (frame.is_extended) {
        n += write_hex(p + n, frame.id & 0x1FFFFFFFu, 8);
    } else {
        n += write_hex(p + n, frame.id & 0x7FFu, 3);
    }

    // DLC / length character
    uint8_t dlc = frame.is_canfd ? len_to_dlc(frame.len) : frame.len;

    // For CANFD the DLC character is the hex nibble ('0'–'F')
    if (frame.is_canfd) {
        if (dlc < 10) {
            p[n++] = static_cast<uint8_t>('0' + dlc);
        } else {
            p[n++] = static_cast<uint8_t>('A' + dlc - 10);
        }
    } else {
        // CAN 2.0: DLC is 0–8, printed as single decimal digit
        p[n++] = static_cast<uint8_t>('0' + (dlc & 0x0F));
    }

    // Data bytes (not for remote frames)
    if (!frame.is_remote) {
        uint8_t data_len = frame.is_canfd ? dlc_to_len(dlc) : frame.len;
        for (uint8_t i = 0; i < data_len; ++i) {
            p[n++] = static_cast<uint8_t>(nibble_to_hex((frame.data[i] >> 4) & 0x0F));
            p[n++] = static_cast<uint8_t>(nibble_to_hex( frame.data[i]       & 0x0F));
        }
    }

    p[n++] = SLCAN_CR;
    return n;
}

// ============================================================================
// Frame decoding (static)
// ============================================================================

bool SLCAN::decode_rx_frame(const uint8_t* line, size_t len, CanFrame& frame)
{
    if (!line || len < 5) return false;

    // Strip trailing CR / whitespace
    while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n' || line[len-1] == 0)) {
        --len;
    }
    if (len < 4) return false;

    char   cmd         = static_cast<char>(line[0]);
    bool   is_extended = false;
    bool   is_remote   = false;
    bool   is_canfd    = false;
    bool   brs         = false;
    int    id_digits   = 0;

    switch (cmd) {
        case 't': is_extended=false; is_remote=false; is_canfd=false; id_digits=3; break;
        case 'T': is_extended=true;  is_remote=false; is_canfd=false; id_digits=8; break;
        case 'r': is_extended=false; is_remote=true;  is_canfd=false; id_digits=3; break;
        case 'R': is_extended=true;  is_remote=true;  is_canfd=false; id_digits=8; break;
        case 'd': is_extended=false; is_remote=false; is_canfd=true;  brs=false;   id_digits=3; break;
        case 'D': is_extended=true;  is_remote=false; is_canfd=true;  brs=false;   id_digits=8; break;
        case 'b': is_extended=false; is_remote=false; is_canfd=true;  brs=true;    id_digits=3; break;
        case 'B': is_extended=true;  is_remote=false; is_canfd=true;  brs=true;    id_digits=8; break;
        default:  return false;
    }

    const uint8_t* pos = line + 1;
    size_t remaining   = len - 1;

    if (remaining < static_cast<size_t>(id_digits + 1)) return false;

    // Parse ID
    uint32_t id = 0;
    if (!read_hex(pos, id_digits, id)) return false;
    pos       += id_digits;
    remaining -= id_digits;

    // Parse DLC character
    char dlc_char = static_cast<char>(*pos);
    pos++;
    remaining--;

    uint8_t dlc_code = 0;
    if (dlc_char >= '0' && dlc_char <= '9') {
        dlc_code = static_cast<uint8_t>(dlc_char - '0');
    } else if (dlc_char >= 'A' && dlc_char <= 'F') {
        dlc_code = static_cast<uint8_t>(10 + dlc_char - 'A');
    } else if (dlc_char >= 'a' && dlc_char <= 'f') {
        dlc_code = static_cast<uint8_t>(10 + dlc_char - 'a');
    } else {
        return false;
    }

    uint8_t data_len = is_canfd ? dlc_to_len(dlc_code) : (dlc_code <= 8 ? dlc_code : 8);

    // Parse data bytes (not for remote frames)
    std::array<uint8_t, 64> data{};
    if (!is_remote) {
        if (remaining < static_cast<size_t>(data_len * 2)) return false;
        for (uint8_t i = 0; i < data_len; ++i) {
            uint32_t byte_val = 0;
            if (!read_hex(pos, 2, byte_val)) return false;
            data[i] = static_cast<uint8_t>(byte_val);
            pos += 2;
        }
    }

    // Populate output struct
    frame.is_extended = is_extended;
    frame.is_remote   = is_remote;
    frame.is_canfd    = is_canfd;
    frame.brs         = brs;
    frame.id          = id;
    frame.dlc         = dlc_code;
    frame.len         = data_len;
    frame.data        = data;

    return true;
}

// ============================================================================
// send_frame
// ============================================================================

ICommDriver::Status SLCAN::send_frame(const CanFrame& frame, uint32_t timeout_ms)
{
    if (!m_channel_open) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("send_frame: channel not open"));
        return Status::PORT_ACCESS;
    }

    std::array<uint8_t, SLCAN_MAX_FRAME_LEN> tx{};
    size_t n = encode_frame(frame, std::span<uint8_t>(tx));
    if (n == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("send_frame: encode failed"));
        return Status::INVALID_PARAM;
    }

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("TX id="); LOG_HEX32(frame.id);
              LOG_STRING(" len="); LOG_UINT8(frame.len);
              LOG_STRING(frame.is_extended ? " EXT" : " STD");
              LOG_STRING(frame.is_canfd    ? " CANFD" : " CAN");
              LOG_STRING(frame.brs         ? " BRS" : ""));

    // Write the encoded ASCII frame
    Status ws = uart_write(tx.data(), n, timeout_ms);
    if (ws != Status::SUCCESS) return ws;

    // Read single ACK byte
    uint8_t ack = 0;
    ReadOptions ro;
    ro.mode = ReadMode::Exact;
    auto res = m_uart->tout_read(timeout_ms, std::span<uint8_t>(&ack, 1), ro);

    if (res.status != Status::SUCCESS || res.bytes_read == 0) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("send_frame: ACK timeout"));
        return (res.status == Status::READ_TIMEOUT) ? Status::WRITE_TIMEOUT : Status::WRITE_ERROR;
    }
    if (ack == SLCAN_NAK) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("send_frame: NAK"));
        return Status::NACK;
    }
    return Status::SUCCESS;
}

// ============================================================================
// receive_frame
// ============================================================================

ICommDriver::Status SLCAN::receive_frame(CanFrame& frame, uint32_t timeout_ms)
{
    std::array<uint8_t, SLCAN_RX_BUF_LEN> buf{};
    size_t got = 0;

    Status s = uart_read_line(buf.data(), buf.size(), got, timeout_ms);
    if (s != Status::SUCCESS) {
        if (s == Status::READ_TIMEOUT) {
            return Status::READ_TIMEOUT;
        }
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("receive_frame: read error: "); LOG_STRING(to_string(s).c_str()));
        return Status::READ_ERROR;
    }

    if (!decode_rx_frame(buf.data(), got, frame)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("receive_frame: decode failed (len="); LOG_SIZET(got); LOG_STRING(")"));
        return Status::READ_ERROR;
    }

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("RX id="); LOG_HEX32(frame.id);
              LOG_STRING(" len="); LOG_UINT8(frame.len);
              LOG_STRING(frame.is_extended ? " EXT" : " STD");
              LOG_STRING(frame.is_canfd    ? " CANFD" : " CAN"));

    return Status::SUCCESS;
}

// ============================================================================
// ICommDriver generic interface
// ============================================================================

SLCAN::ReadResult SLCAN::tout_read(uint32_t u32ReadTimeout,
                                    std::span<uint8_t> buffer,
                                    const ReadOptions& /*options*/,
                                    std::string_view   /*xtra_params*/) const
{
    ReadResult result;

    if (!m_uart || !m_uart->is_open()) {
        result.status = Status::PORT_ACCESS;
        return result;
    }

    // Delegate: read one SLCAN line (until CR) into the caller's buffer
    ReadOptions ro;
    ro.mode      = ReadMode::UntilDelimiter;
    ro.delimiter = SLCAN_CR;

    auto res = m_uart->tout_read(u32ReadTimeout, buffer, ro);
    result.status          = res.status;
    result.bytes_read      = res.bytes_read;
    result.found_terminator = res.found_terminator;
    return result;
}

SLCAN::WriteResult SLCAN::tout_write(uint32_t u32WriteTimeout,
                                      std::span<const uint8_t> buffer,
                                      std::string_view /*xtra_params*/) const
{
    WriteResult result;

    if (!m_uart || !m_uart->is_open()) {
        result.status = Status::PORT_ACCESS;
        return result;
    }

    auto res = m_uart->tout_write(u32WriteTimeout, buffer);
    result.status        = res.status;
    result.bytes_written = res.bytes_written;
    return result;
}
