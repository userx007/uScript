#include "profibus_driver.hpp"
#include "uLogger.hpp"
#include "uGuiNotify.hpp"
#include "uString.hpp"
#include "uNumeric.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <thread>

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR  "PROFIBUS_DRV|"
#define LOG_HDR LOG_STRING(LT_HDR)

static constexpr uint32_t kTelegramContinuationTimeoutMs = 100;
static constexpr const char* kPluginNameForDump = "PROFIBUS";

// -----------------------------------------------------------------------
// Response-pending handoff between send() and the following receive() call
// on the same thread. thread_local for the same reason as MqttDriver's
// tl_bAwaitingAck (see mqtt_driver.cpp): a "PROFIBUS.CMD < &" background
// bus-monitor thread must not race a foreground "> SRD ... | ..." pair's
// own pending-response bookkeeping on this same, persistent driver
// instance (see profibus_plugin.hpp's "Session lifetime").
//
// As with MQTT, a send() whose line omits its "| expected" leaves that
// response unread on the wire; the next standalone "PROFIBUS.CMD <" will
// then consume it as if it were bus-monitor traffic (since only the
// pending flag — not the telegram itself — was thread-local, the response
// is still sitting in the UART's receive buffer). Always pair a ">" line
// with its "| expected" to avoid this.
// -----------------------------------------------------------------------
namespace
{
    enum class PendingKind : uint8_t { None, Sda, Srd, Status };

    thread_local PendingKind tl_pendingKind    = PendingKind::None;
    thread_local uint8_t     tl_pendingFromSa  = 0;
}

ProfibusDriver::ProfibusDriver(Config config)
    : m_config(std::move(config))
{
    if (m_config.strInstanceName.empty()) {
        m_config.strInstanceName = kPluginNameForDump;
    }
    m_mapProfibusCmds.insert({"SDN",    &ProfibusDriver::m_HandleSdn});
    m_mapProfibusCmds.insert({"SDA",    &ProfibusDriver::m_HandleSda});
    m_mapProfibusCmds.insert({"SRD",    &ProfibusDriver::m_HandleSrd});
    m_mapProfibusCmds.insert({"STATUS", &ProfibusDriver::m_HandleStatus});
}

ProfibusDriver::~ProfibusDriver()
{
    close();
}

bool ProfibusDriver::open()
{
    if (m_config.device.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Device not configured"));
        return false;
    }

    m_pUart = std::make_shared<UART>(m_config.device, m_config.baud, m_config.device,
                                      UART::Parity::Even); // PROFIBUS FDL mandates 8E1
    if (!m_pUart->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("UART open failed:"); LOG_STRING(m_config.device));
        m_pUart.reset();
        return false;
    }

    m_protocol.resetFcbState();
    m_lastTxActivity = std::chrono::steady_clock::now();
    m_bIsOpen = true;

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Opened"); LOG_STRING(m_config.device);
              LOG_STRING("@"); LOG_UINT32(m_config.baud); LOG_STRING("ownAddress="); LOG_UINT32(m_config.ownAddress));
    return true;
}

void ProfibusDriver::close()
{
    m_bIsOpen = false;
    if (m_pUart) {
        m_pUart->close();
    }
    m_pUart.reset();
}

bool ProfibusDriver::is_open() const
{
    return m_bIsOpen && m_pUart && m_pUart->is_open();
}

CommDetails ProfibusDriver::describeConnection(std::string_view xtra_params) const
{
    return m_pUart->describeConnection(xtra_params);
}

ICommDriver::WriteResult ProfibusDriver::tout_write(uint32_t u32WriteTimeout, std::span<const uint8_t> buffer,
                                                     std::string_view xtra_params) const
{
    // Thin passthrough — see class doc comment. Never actually used by
    // ProfibusPlugin, which always goes through send() instead.
    return m_pUart->tout_write(u32WriteTimeout, buffer, xtra_params);
}

ICommDriver::ReadResult ProfibusDriver::tout_read(uint32_t u32ReadTimeout, std::span<uint8_t> buffer,
                                                   const ICommDriver::ReadOptions& options, std::string_view xtra_params) const
{
    return m_pUart->tout_read(u32ReadTimeout, buffer, options, xtra_params);
}

// -----------------------------------------------------------------------
// Physical I/O
// -----------------------------------------------------------------------

ICommDriver::Status ProfibusDriver::m_PhysicalSend(std::span<const uint8_t> data, uint32_t timeoutMs) const
{
    auto res = m_pUart->tout_write(timeoutMs, data);
    return res.status;
}

ICommDriver::Status ProfibusDriver::m_PhysicalRecv(std::span<uint8_t> buffer, uint32_t timeoutMs, size_t& outBytesRead) const
{
    auto res = m_pUart->tout_read(timeoutMs, buffer, ICommDriver::ReadOptions{.mode = ICommDriver::ReadMode::Exact});
    outBytesRead = res.bytes_read;
    return res.status;
}

// -----------------------------------------------------------------------
// SYN inter-telegram pause — see class doc comment for why this is a
// best-effort software timer, not a hardware-guaranteed one.
// -----------------------------------------------------------------------

void ProfibusDriver::m_EnsureSynPause() const
{
    const uint32_t baud = (m_config.baud == 0) ? 19200 : m_config.baud;
    const auto synPause = std::chrono::microseconds((33ULL * 1'000'000ULL + baud - 1) / baud); // ceil(33 bit-times)

    const auto elapsed = std::chrono::steady_clock::now() - m_lastTxActivity;
    if (elapsed < synPause) {
        std::this_thread::sleep_for(synPause - elapsed);
    }
}

// -----------------------------------------------------------------------
// Telegram-level send/receive — every physical PROFIBUS exchange goes
// through these two, which report the real, complete telegram bytes to the
// GUI comm-dump panel by hand (see profibus_driver.hpp's class doc comment
// for why — supplying send()/receive() as pfsend/pfrecv suppresses the
// interpreter's own automatic dumping, so this class must produce an
// accurate replacement).
// -----------------------------------------------------------------------

ICommDriver::Status ProfibusDriver::m_SendTelegram(const std::vector<uint8_t>& telegram, std::string_view xtra_params) const
{
    m_EnsureSynPause();

    const auto st = m_PhysicalSend(std::span<const uint8_t>(telegram.data(), telegram.size()), 1000);
    if (st == ICommDriver::Status::SUCCESS) {
        m_lastTxActivity = std::chrono::steady_clock::now();
        if (gui_mode_active()) {
            gui_notify_comm_dump(m_config.strInstanceName, describeConnection(xtra_params),
                                  CommDir::Tx, telegram.data(), static_cast<uint32_t>(telegram.size()));
        }
    }
    return st;
}

ICommDriver::Status ProfibusDriver::m_ReadTelegram(ProfibusProtocol::DecodedTelegram& telegramOut, uint32_t timeoutMs,
                                                    std::string_view xtra_params) const
{
    std::vector<uint8_t> raw;

    uint8_t firstByte = 0;
    {
        uint8_t buf[1];
        size_t got = 0;
        auto st = m_PhysicalRecv(std::span<uint8_t>(buf, 1), timeoutMs, got);
        if (st != ICommDriver::Status::SUCCESS || got == 0) {
            return ICommDriver::Status::READ_TIMEOUT;
        }
        firstByte = buf[0];
    }
    raw.push_back(firstByte);

    // How many more bytes this start delimiter's format requires. SD2's
    // total length depends on its own LE byte (read as part of this fixed
    // prefix, then re-validated by ProfibusProtocol::decodeTelegram() —
    // see this function's doc comment in profibus_driver.hpp for the
    // desync caveat when LE is itself corrupted).
    size_t moreBytes = 0;
    switch (ProfibusProtocol::classifyStartDelimiter(firstByte)) {
        case ProfibusProtocol::TelegramKind::SC:  moreBytes = 0;  break;
        case ProfibusProtocol::TelegramKind::SD1: moreBytes = 5;  break;
        case ProfibusProtocol::TelegramKind::SD3: moreBytes = 13; break;
        case ProfibusProtocol::TelegramKind::SD4: moreBytes = 2;  break;
        case ProfibusProtocol::TelegramKind::SD2: moreBytes = 2;  break; // LE, LEr first — length of the rest depends on LE
        case ProfibusProtocol::TelegramKind::Malformed:
        default:
            moreBytes = 0; // unrecognised leading byte — nothing more to usefully read; report as Malformed below
            break;
    }

    if (moreBytes > 0) {
        std::vector<uint8_t> chunk(moreBytes);
        size_t got = 0;
        auto st = m_PhysicalRecv(std::span<uint8_t>(chunk.data(), chunk.size()), kTelegramContinuationTimeoutMs, got);
        // Partial reads are looped here rather than accepted as-is — a
        // telegram, once started, arrives back-to-back with no SYN pauses
        // between its own bytes (see class doc comment), so anything short
        // of the full count within the continuation timeout is a real stall.
        while (st == ICommDriver::Status::SUCCESS && got < chunk.size()) {
            size_t more = 0;
            st = m_PhysicalRecv(std::span<uint8_t>(chunk.data() + got, chunk.size() - got), kTelegramContinuationTimeoutMs, more);
            got += more;
        }
        if (st != ICommDriver::Status::SUCCESS || got < chunk.size()) {
            return ICommDriver::Status::READ_TIMEOUT;
        }
        raw.insert(raw.end(), chunk.begin(), chunk.end());

        // SD2: now that LE is known, read the rest (repeated SD2 + DA + SA +
        // FC + DU[LE-3] + FCS + ED). Clamped to the spec's 246-byte data
        // ceiling so a corrupted LE can't drive an unbounded allocation.
        if (ProfibusProtocol::classifyStartDelimiter(firstByte) == ProfibusProtocol::TelegramKind::SD2) {
            const uint8_t le = raw[1];
            const size_t duLen = (le >= 3) ? std::min<size_t>(le - 3, 246) : 0;
            const size_t restLen = 1 /*SD2*/ + 3 /*DA,SA,FC*/ + duLen + 2 /*FCS,ED*/;

            std::vector<uint8_t> rest(restLen);
            size_t gotRest = 0;
            auto st2 = m_PhysicalRecv(std::span<uint8_t>(rest.data(), rest.size()), kTelegramContinuationTimeoutMs, gotRest);
            while (st2 == ICommDriver::Status::SUCCESS && gotRest < rest.size()) {
                size_t more = 0;
                st2 = m_PhysicalRecv(std::span<uint8_t>(rest.data() + gotRest, rest.size() - gotRest), kTelegramContinuationTimeoutMs, more);
                gotRest += more;
            }
            if (st2 != ICommDriver::Status::SUCCESS || gotRest < rest.size()) {
                return ICommDriver::Status::READ_TIMEOUT;
            }
            raw.insert(raw.end(), rest.begin(), rest.end());
        }
    }

    telegramOut = ProfibusProtocol::decodeTelegram(raw);

    // Dumped unconditionally, even when Malformed/fcsOk==false — for a
    // bench/diagnostic tool, seeing the actual bad bytes is the point (see
    // this function's doc comment in profibus_driver.hpp).
    if (gui_mode_active()) {
        gui_notify_comm_dump(m_config.strInstanceName, describeConnection(xtra_params),
                              CommDir::Rx, raw.data(), static_cast<uint32_t>(raw.size()));
    }

    return ICommDriver::Status::SUCCESS;
}

bool ProfibusDriver::m_WaitForResponse(uint8_t expectedFromSa, uint32_t timeoutMs,
                                        ProfibusProtocol::DecodedTelegram& outTelegram, std::string_view xtra_params) const
{
    // 0 == infinite timeout: never expire this wait, and forward 0 straight
    // through to m_ReadTelegram() on each attempt.
    const bool bInfinite = (timeoutMs == 0);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (true) {
        uint32_t remainingMs = 0;
        if (!bInfinite) {
            const auto remaining = deadline - std::chrono::steady_clock::now();
            if (remaining <= std::chrono::milliseconds(0)) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Timed out waiting for response from station"); LOG_UINT32(expectedFromSa));
                return false;
            }
            remainingMs = static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count());
        }

        ProfibusProtocol::DecodedTelegram t;
        auto st = m_ReadTelegram(t, remainingMs, xtra_params);
        if (st != ICommDriver::Status::SUCCESS) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Read failed waiting for response:"); LOG_STRING(ICommDriver::to_string(st)));
            return false;
        }

        if (t.kind == ProfibusProtocol::TelegramKind::Malformed || !t.fcsOk) {
            LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Discarding malformed/checksum-failed telegram while waiting for response"));
            continue;
        }
        if (t.kind == ProfibusProtocol::TelegramKind::SD4) {
            LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Ignoring token telegram while awaiting response"));
            continue;
        }
        if (t.kind == ProfibusProtocol::TelegramKind::SC) {
            // SC carries no DA/SA — accepted unconditionally as the pending
            // response. See class doc comment: this assumes point-to-point/
            // idle-bus usage (bench tool talking to one slave at a time),
            // since SC alone can't be attributed to a specific exchange.
            outTelegram = t;
            return true;
        }
        // SD1/SD2/SD3: must be addressed back to us, from the station we asked.
        if (t.da != m_config.ownAddress || t.sa != expectedFromSa) {
            LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Unexpected telegram while waiting for response, DA=");
                      LOG_UINT32(t.da); LOG_STRING("SA="); LOG_UINT32(t.sa); LOG_STRING("— still waiting"));
            continue;
        }
        outTelegram = t;
        return true;
    }
}

// -----------------------------------------------------------------------
// Intermediary layer
// -----------------------------------------------------------------------

// Tokenizes on whitespace only, same convention as MqttDriver::m_TokenizeArgs()
// (see its doc comment in mqtt_driver.cpp) — including stripping a trailing
// NUL byte the interpreter's STRING_RAW conversion appends by default.
void ProfibusDriver::m_TokenizeArgs(std::span<const uint8_t> dataSpan, std::vector<std::string>& outTokens)
{
    outTokens.clear();

    size_t len = dataSpan.size();
    while (len > 0 && dataSpan[len - 1] == 0) {
        --len;
    }
    std::string text(reinterpret_cast<const char*>(dataSpan.data()), len);
    text = ustring::trim(text);

    size_t i = 0;
    const size_t n = text.size();
    while (i < n) {
        while (i < n && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        if (i >= n) break;
        size_t start = i;
        while (i < n && !std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        outTokens.push_back(text.substr(start, i - start));
    }
}

bool ProfibusDriver::m_ParseHexBytes(const std::string& hex, std::vector<uint8_t>& outBytes)
{
    outBytes.clear();
    if (hex.size() % 2 != 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Hex data must have an even number of digits:"); LOG_STRING(hex));
        return false;
    }
    outBytes.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        auto nibble = [&](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        const int hi = nibble(hex[i]);
        const int lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid hex digit in:"); LOG_STRING(hex));
            outBytes.clear();
            return false;
        }
        outBytes.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return true;
}

std::string ProfibusDriver::m_BytesToHex(const std::vector<uint8_t>& bytes)
{
    static const char* digits = "0123456789ABCDEF";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (uint8_t b : bytes) {
        out.push_back(digits[(b >> 4) & 0x0F]);
        out.push_back(digits[b & 0x0F]);
    }
    return out;
}

const char* ProfibusDriver::m_StationTypeName(uint8_t stationType)
{
    switch (stationType) {
        case 0: return "SLAVE";
        case 1: return "MASTER_NOT_READY";
        case 2: return "MASTER_READY_NO_TOKEN";
        case 3: return "MASTER_READY_IN_RING";
        default: return "UNKNOWN";
    }
}

std::string ProfibusDriver::m_FormatTelegramResult(const ProfibusProtocol::DecodedTelegram& t, bool wasStatusQuery)
{
    if (t.kind == ProfibusProtocol::TelegramKind::Malformed) {
        return "MALFORMED";
    }
    if (!t.fcsOk) {
        return "FCS_ERROR";
    }
    if (t.kind == ProfibusProtocol::TelegramKind::SC) {
        return "ACK"; // see m_WaitForResponse()'s doc comment on SC's inherent ambiguity
    }
    if (t.kind == ProfibusProtocol::TelegramKind::SD4) {
        return "TOKEN";
    }

    const auto rfc = ProfibusProtocol::decodeResponseFc(t.fc);
    if (rfc.isRequestFrame) {
        return "UNEXPECTED_REQUEST_FRAME";
    }
    if (wasStatusQuery) {
        return std::string(m_StationTypeName(rfc.stationType)) + ":" + ProfibusProtocol::responseStatusName(rfc.statusCode);
    }
    if (t.du.empty()) {
        return ProfibusProtocol::responseStatusName(rfc.statusCode);
    }
    return m_BytesToHex(t.du);
}

// -----------------------------------------------------------------------
// send() / receive()
// -----------------------------------------------------------------------

ICommDriver::WriteResult ProfibusDriver::send(uint32_t u32WriteTimeout, std::span<const uint8_t> dataSpan,
                                               std::string_view xtra_params) const
{
    (void)u32WriteTimeout;
    ICommDriver::WriteResult result;

    tl_pendingKind = PendingKind::None; // clear any state left by an earlier, unrelated send() on this thread

    if (!is_open()) {
        result.status = ICommDriver::Status::PORT_ACCESS;
        return result;
    }

    std::vector<std::string> tokens;
    m_TokenizeArgs(dataSpan, tokens);
    if (tokens.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("PROFIBUS.CMD > requires a command: SDN, SDA, SRD or STATUS"));
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }

    std::string cmdKeyword = tokens[0];
    std::transform(cmdKeyword.begin(), cmdKeyword.end(), cmdKeyword.begin(),
                    [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    auto it = m_mapProfibusCmds.find(cmdKeyword);
    if (it == m_mapProfibusCmds.end()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown PROFIBUS command:"); LOG_STRING(tokens[0]));
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }

    const std::vector<std::string> cmdArgs(tokens.begin() + 1, tokens.end());
    if (!(this->*(it->second))(cmdArgs, xtra_params)) {
        result.status = ICommDriver::Status::OPERATION_FAILED;
        return result;
    }

    result.status = ICommDriver::Status::SUCCESS;
    result.bytes_written = dataSpan.size();
    return result;
}

ICommDriver::ReadResult ProfibusDriver::receive(uint32_t u32ReadTimeout, std::span<uint8_t> dataSpan,
                                                 const ICommDriver::ReadOptions& options, std::string_view xtra_params) const
{
    (void)options;
    ICommDriver::ReadResult result;

    if (!is_open()) {
        result.status = ICommDriver::Status::PORT_ACCESS;
        return result;
    }

    if (tl_pendingKind == PendingKind::None) {
        // Standalone "PROFIBUS.CMD <" — passive bus monitor, see class doc comment.
        return m_DoStandaloneReceive(u32ReadTimeout, dataSpan, xtra_params);
    }

    const PendingKind kind = tl_pendingKind;
    const uint8_t fromSa   = tl_pendingFromSa;
    tl_pendingKind = PendingKind::None; // consume-once

    ProfibusProtocol::DecodedTelegram telegram;
    if (!m_WaitForResponse(fromSa, u32ReadTimeout, telegram, xtra_params)) {
        result.status = ICommDriver::Status::READ_TIMEOUT;
        return result;
    }

    const std::string text = m_FormatTelegramResult(telegram, kind == PendingKind::Status);
    const size_t len = std::min(dataSpan.size(), text.size());
    std::memcpy(dataSpan.data(), text.data(), len);

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Response from station"); LOG_UINT32(fromSa);
              LOG_STRING(":"); LOG_STRING(text));

    result.status = ICommDriver::Status::SUCCESS;
    result.bytes_read = len;
    return result;
}

ICommDriver::ReadResult ProfibusDriver::m_DoStandaloneReceive(uint32_t timeoutMs, std::span<uint8_t> buffer, std::string_view xtra_params) const
{
    ICommDriver::ReadResult result;

    ProfibusProtocol::DecodedTelegram t;
    auto st = m_ReadTelegram(t, timeoutMs, xtra_params);
    if (st != ICommDriver::Status::SUCCESS) {
        result.status = st;
        return result;
    }

    std::string text;
    switch (t.kind) {
        case ProfibusProtocol::TelegramKind::SC:
            text = "SC";
            break;
        case ProfibusProtocol::TelegramKind::SD4:
            text = "TOKEN DA=" + std::to_string(t.da) + " SA=" + std::to_string(t.sa);
            break;
        case ProfibusProtocol::TelegramKind::Malformed:
            text = "MALFORMED";
            break;
        case ProfibusProtocol::TelegramKind::SD1:
        case ProfibusProtocol::TelegramKind::SD2:
        case ProfibusProtocol::TelegramKind::SD3:
        default: {
            const std::string checksum = t.fcsOk ? "" : " FCS_ERROR";
            const std::string payload  = t.du.empty()
                ? std::string(ProfibusProtocol::responseStatusName(ProfibusProtocol::decodeResponseFc(t.fc).statusCode))
                : m_BytesToHex(t.du);
            text = "DA=" + std::to_string(t.da) + " SA=" + std::to_string(t.sa) +
                   " FC=0x" + m_BytesToHex({t.fc}) + " " + payload + checksum;
            break;
        }
    }

    const size_t len = std::min(buffer.size(), text.size());
    std::memcpy(buffer.data(), text.data(), len);

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("Bus monitor:"); LOG_STRING(text));

    result.status = ICommDriver::Status::SUCCESS;
    result.bytes_read = len;
    return result;
}

// -----------------------------------------------------------------------
// FDL sub-command handlers
// -----------------------------------------------------------------------

bool ProfibusDriver::m_HandleSdn(const std::vector<std::string>& args, std::string_view xtra_params) const
{
    if (args.empty() || args.size() > 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Usage: SDN <station> [hexdata]"));
        return false;
    }
    uint8_t da = 0;
    if (!numeric::str2uint8(args[0], da)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SDN: invalid station address:"); LOG_STRING(args[0]));
        return false;
    }
    std::vector<uint8_t> data;
    if (args.size() == 2 && !m_ParseHexBytes(args[1], data)) {
        return false;
    }

    auto pkt = m_protocol.buildSdn(da, m_config.ownAddress, data, m_config.defaultHighPriority);
    if (m_SendTelegram(pkt, xtra_params) != ICommDriver::Status::SUCCESS) return false;

    // No response expected — tl_pendingKind stays None (see send()).
    return true;
}

bool ProfibusDriver::m_HandleSda(const std::vector<std::string>& args, std::string_view xtra_params) const
{
    if (args.empty() || args.size() > 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Usage: SDA <station> [hexdata]"));
        return false;
    }
    uint8_t da = 0;
    if (!numeric::str2uint8(args[0], da)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SDA: invalid station address:"); LOG_STRING(args[0]));
        return false;
    }
    std::vector<uint8_t> data;
    if (args.size() == 2 && !m_ParseHexBytes(args[1], data)) {
        return false;
    }

    auto pkt = m_protocol.buildSda(da, m_config.ownAddress, data, m_config.defaultHighPriority);
    if (m_SendTelegram(pkt, xtra_params) != ICommDriver::Status::SUCCESS) return false;

    tl_pendingKind   = PendingKind::Sda;
    tl_pendingFromSa = da;
    return true;
}

bool ProfibusDriver::m_HandleSrd(const std::vector<std::string>& args, std::string_view xtra_params) const
{
    if (args.empty() || args.size() > 2) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Usage: SRD <station> [hexdata]"));
        return false;
    }
    uint8_t da = 0;
    if (!numeric::str2uint8(args[0], da)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("SRD: invalid station address:"); LOG_STRING(args[0]));
        return false;
    }
    std::vector<uint8_t> data;
    if (args.size() == 2 && !m_ParseHexBytes(args[1], data)) {
        return false;
    }

    auto pkt = m_protocol.buildSrd(da, m_config.ownAddress, data, m_config.defaultHighPriority);
    if (m_SendTelegram(pkt, xtra_params) != ICommDriver::Status::SUCCESS) return false;

    LOG_PRINT(LOG_VERBOSE, LOG_HDR; LOG_STRING("SRD -> station"); LOG_UINT32(da); LOG_STRING("bytes out:"); LOG_SIZET(data.size()));

    tl_pendingKind   = PendingKind::Srd;
    tl_pendingFromSa = da;
    return true;
}

bool ProfibusDriver::m_HandleStatus(const std::vector<std::string>& args, std::string_view xtra_params) const
{
    if (args.size() != 1) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Usage: STATUS <station>"));
        return false;
    }
    uint8_t da = 0;
    if (!numeric::str2uint8(args[0], da)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("STATUS: invalid station address:"); LOG_STRING(args[0]));
        return false;
    }

    auto pkt = m_protocol.buildFdlStatusRequest(da, m_config.ownAddress, m_config.defaultHighPriority);
    if (m_SendTelegram(pkt, xtra_params) != ICommDriver::Status::SUCCESS) return false;

    tl_pendingKind   = PendingKind::Status;
    tl_pendingFromSa = da;
    return true;
}
