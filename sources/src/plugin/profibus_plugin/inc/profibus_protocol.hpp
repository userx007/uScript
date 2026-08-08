#ifndef PROFIBUS_PROTOCOL_HPP
#define PROFIBUS_PROTOCOL_HPP

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Pure PROFIBUS FDL (Fieldbus Data Link, IEC 61158-2 / EN 50170)
 * telegram codec.
 *
 * This is the "protocol side" of the plugin's three-way split (see
 * profibus_plugin.hpp's class doc comment for the other two, and
 * mqtt_plugin.hpp for the pattern this mirrors): it only ever builds and
 * parses PROFIBUS FDL telegrams as plain byte buffers. It never touches a
 * serial port, a timeout, or anything else transport-related — ProfibusDriver
 * owns all of that. The only state this class carries is the per-destination
 * Frame Count Bit (FCB) needed for the FDL "security sequence" that
 * acknowledged (SDA) and send/request (SRD) services require — protocol-level
 * bookkeeping, not transport state, exactly like MqttProtocol's packet-id
 * sequence.
 *
 * -----------------------------------------------------------------------
 * Scope — what this class implements, and what it deliberately does not
 * -----------------------------------------------------------------------
 * PROFIBUS is a layered protocol: FDL (layer 2, this class) carries raw
 * octets between two stations; PROFIBUS-DP (the application profile
 * layered on top, using SAP addressing — Slave_Diag, Set_Prm, Chk_Cfg,
 * Data_Exchange, ...) gives those octets their DP-specific meaning. This
 * class implements FDL only: SDN/SDA/SRD/FDL-Status telegram framing and
 * the FCB/FCV security sequence. It does not know what a GSD file is, does
 * not implement the DP slave state machine (WAIT_PRM/WAIT_CFG/DATA_EX),
 * and does not interpret the payload it carries — that payload is opaque
 * bytes supplied by (SDN/SRD) or returned to (a decoded response) the
 * caller. This is the FDL equivalent of a raw TCP socket vs. an HTTP
 * client: enough to talk to a real DP slave's layer 2 and observe/drive it
 * for diagnostics or bench testing, not a certified DP master stack.
 *
 * Every build*() returns a complete, ready-to-send telegram (start
 * delimiter(s) + address/control fields + optional data + FCS + end
 * delimiter, per the telegram's format). Every decode*() assumes it is
 * given exactly one complete, already-received telegram — none of these
 * functions read a stream or ask "is there more coming"; that framing
 * question belongs to whoever is doing the actual reading (ProfibusDriver,
 * see its m_ReadTelegram()).
 *
 * Bit-level reference: the FC (Frame Control) byte layout and FCB/FCV
 * security-sequence rules implemented here follow the PROFIBUS Manual's
 * published "Function code" and "Checksum" pages
 * (https://www.felser.ch/profibus-manual/funktionscode.html and
 * .../pruefsumme.html) — cited in the relevant functions below rather than
 * repeated in full here.
 */
class ProfibusProtocol
{
public:
    // Start/end delimiters — identical across every open PROFIBUS
    // reference (see e.g. https://www.felser.ch/profibus-manual/telegrammformate.html).
    static constexpr uint8_t kSD1 = 0x10; // Telegram without data field
    static constexpr uint8_t kSD2 = 0x68; // Telegram with variable-length data field
    static constexpr uint8_t kSD3 = 0xA2; // Telegram with fixed 8-byte data field
    static constexpr uint8_t kSD4 = 0xDC; // Token telegram (3 bytes, no FCS/ED) — not built by this class, see class doc comment
    static constexpr uint8_t kSC  = 0xE5; // Short acknowledgement (single byte, no other fields)
    static constexpr uint8_t kED  = 0x16; // End delimiter

    static constexpr uint8_t kBroadcastAddress = 127; // FDL broadcast (all slaves), used with SDN

    // FC (Frame Control), REQUEST direction, bits 3-0 — function selector.
    // Combined with the frame-type bit (0x40) and FCB/FCV (see
    // buildRequestFc()) to form the complete byte. Values per the PROFIBUS
    // Manual's function-code table (felser.ch, cited in the class doc
    // comment above).
    static constexpr uint8_t kFnSdaLow          = 0x03; // Send Data with Acknowledge, low priority
    static constexpr uint8_t kFnSdnLow          = 0x04; // Send Data with No acknowledge, low priority
    static constexpr uint8_t kFnSdaHigh         = 0x05;
    static constexpr uint8_t kFnSdnHigh         = 0x06;
    static constexpr uint8_t kFnRequestFdlStatus = 0x09;
    static constexpr uint8_t kFnSrdLow          = 0x0C; // Send and Request Data, low priority
    static constexpr uint8_t kFnSrdHigh         = 0x0D;

    // FC (Frame Control), RESPONSE direction, bits 3-0 — status code.
    static constexpr uint8_t kRspOk            = 0x00;
    static constexpr uint8_t kRspUserError     = 0x01;
    static constexpr uint8_t kRspNoResources   = 0x02;
    static constexpr uint8_t kRspSapNotEnabled = 0x03;
    static constexpr uint8_t kRspDataLow       = 0x08; // Normal case for a DP data response
    static constexpr uint8_t kRspNoResponseData = 0x09;
    static constexpr uint8_t kRspDataHigh      = 0x0A; // Data ready, diagnostic pending
    static constexpr uint8_t kRspDataNotReceivedLow  = 0x0C;
    static constexpr uint8_t kRspDataNotReceivedHigh = 0x0D;

    enum class TelegramKind : uint8_t { SD1, SD2, SD3, SC, SD4, Malformed };

    struct DecodedTelegram
    {
        TelegramKind kind = TelegramKind::Malformed;
        uint8_t da = 0;                 // Destination Address (SD1/SD2/SD3/SD4 only)
        uint8_t sa = 0;                 // Source Address (SD1/SD2/SD3/SD4 only)
        uint8_t fc = 0;                 // Frame Control (SD1/SD2/SD3 only)
        std::vector<uint8_t> du;        // Data Unit / payload (SD2/SD3 only; empty for SD1/SC/SD4)
        bool fcsOk = false;             // Checksum verified (SD1/SD2/SD3 only; always true for SC/SD4, which carry none)
    };

    // Decoded meaning of a RESPONSE FC byte (see decodeResponseFc()).
    struct ResponseFc
    {
        bool isRequestFrame = false; // true would mean this FC actually belongs to a request, not a response — caller error
        uint8_t stationType = 0;     // 0=Slave, 1=Master not ready, 2=Master ready without token, 3=Master ready in token ring
        uint8_t statusCode  = 0;     // one of the kRsp* constants above
    };

    ProfibusProtocol() = default;

    // ---- Builders: pure encode, no I/O ----
    // (Callers — ProfibusDriver — are expected to have already validated
    // station addresses and data length before calling; these assume
    // well-formed input, mirroring MqttProtocol's builders.)

    // Send Data with No acknowledge: fire-and-forget, no response expected.
    // Used for broadcasts (da == kBroadcastAddress, e.g. Global_Control) as
    // well as unicast "don't care about the reply" exchanges. FCB/FCV are
    // always 0 for SDN, per the security-sequence rules (no ack to track).
    std::vector<uint8_t> buildSdn(uint8_t da, uint8_t sa, const std::vector<uint8_t>& data, bool highPriority = false) const;

    // Send Data with Acknowledge: the responder replies with a bare SC
    // (or, on a malformed/rejected request, nothing — the caller's read
    // will simply time out). Carries the FCB/FCV security sequence, so the
    // per-da FCB state (m_lastFcbForDa) is read and updated here.
    std::vector<uint8_t> buildSda(uint8_t da, uint8_t sa, const std::vector<uint8_t>& data, bool highPriority = false);

    // Send and Request Data: the FDL service PROFIBUS-DP's own Data_Exchange
    // is built on top of — send data out, get the responder's reply data
    // back in the very same telegram cycle. Also carries the FCB/FCV
    // security sequence.
    std::vector<uint8_t> buildSrd(uint8_t da, uint8_t sa, const std::vector<uint8_t>& data, bool highPriority = false);

    // Request FDL Status: SD1, no data. FCB=FCV=0 always (excluded from
    // the security sequence, same as SDN — see the PROFIBUS Manual's
    // "Function code" page, Frame Count Bit section).
    std::vector<uint8_t> buildFdlStatusRequest(uint8_t da, uint8_t sa, bool highPriority = false) const;

    static std::vector<uint8_t> buildShortAck() { return { kSC }; }

    // ---- Decoders ----

    // Peeks only the start delimiter of a buffer that may not yet be a
    // complete telegram — used by ProfibusDriver::m_ReadTelegram() to
    // decide how many more bytes to read before calling decodeTelegram().
    static TelegramKind classifyStartDelimiter(uint8_t sd);

    // Decodes and checksum-verifies one already-complete telegram buffer
    // (as classified/assembled by ProfibusDriver::m_ReadTelegram()).
    // kind == Malformed on any structural problem (bad length, mismatched
    // LE/LEr, wrong end delimiter, ...); fcsOk == false on a checksum
    // mismatch in an otherwise well-formed telegram.
    static DecodedTelegram decodeTelegram(const std::vector<uint8_t>& raw);

    // Decodes a RESPONSE FC byte per the PROFIBUS Manual's function-code
    // table (station type + status). isRequestFrame == true signals the
    // caller passed a request-direction FC (bit 6 set) by mistake.
    static ResponseFc decodeResponseFc(uint8_t fc);

    // Human-readable label for a decoded response status code, for
    // ProfibusDriver's receive()/monitor output (e.g. "DATA_LOW", "USER_ERROR").
    static const char* responseStatusName(uint8_t statusCode);

    // ---- FCS (Frame Check Sequence) ----
    // Simple 8-bit arithmetic sum (no carry) of DA, SA, FC and DU — NOT a
    // CRC. Per the PROFIBUS Manual's "Checksum" page: SD1 sums DA+SA+FC
    // only (du is expected empty); SD2/SD3 additionally sum every DU byte.
    static uint8_t computeFcs(uint8_t da, uint8_t sa, uint8_t fc, const std::vector<uint8_t>& du);

    // Forgets all per-destination FCB state — call when (re)opening the
    // session, mirroring MqttProtocol::resetPacketIdSequence().
    void resetFcbState() { m_lastFcbForDa.clear(); }

private:
    // FC (Frame Control) for a REQUEST telegram: bit6=1 (request), bit5=FCB,
    // bit4=FCV, bits3-0=function (already low/high-priority-selected by the
    // caller, e.g. kFnSdaLow vs kFnSdaHigh). See the PROFIBUS Manual's
    // "Function code" page (cited in the class doc comment) for the full
    // bit table this implements.
    static uint8_t buildRequestFc(uint8_t function, bool fcb, bool fcv);

    // Implements the FCB/FCV "security sequence" (PROFIBUS Manual,
    // "Function code" > "Frame Count Bit"): the first request to a given DA
    // is sent with FCV=0, FCB=1 ("first request" marker); every subsequent
    // request to that same DA toggles FCB with FCV=1. Returns the
    // (fcb, fcv) pair to use for this call and updates m_lastFcbForDa.
    std::pair<bool, bool> m_NextFcbFcv(uint8_t da);

    // Builds a complete SD1 (no data), SD2 (variable data) or SD3 (fixed
    // 8-byte data) telegram around the given da/sa/fc/data — SD2 is chosen
    // for any non-empty, non-8-byte data length; SD3 exactly matches the
    // fixed-length wire format when data.size() == 8; SD1 when data is
    // empty. Shared by buildSdn()/buildSda()/buildSrd() (buildFdlStatusRequest()
    // always uses SD1 directly, since Request FDL Status never carries data).
    static std::vector<uint8_t> m_BuildDataTelegram(uint8_t da, uint8_t sa, uint8_t fc, const std::vector<uint8_t>& data);

    // Per-destination-address FCB state for the acknowledged services
    // (SDA/SRD) — see m_NextFcbFcv(). Keyed by DA; absent == "no request
    // sent to this DA yet on this session", matching the "first request"
    // (FCV=0, FCB=1) branch of the security sequence.
    std::unordered_map<uint8_t, bool> m_lastFcbForDa;
};

#endif // PROFIBUS_PROTOCOL_HPP
