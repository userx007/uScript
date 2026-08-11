#ifndef PROFIBUS_DRIVER_HPP
#define PROFIBUS_DRIVER_HPP

#include "uUart.hpp"
#include "ICommDriver.hpp"
#include "profibus_protocol.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <span>

/**
 * @brief The "driver side" — everything PROFIBUS-FDL-implementation-specific
 * lives here, so ProfibusPlugin (profibus_plugin.hpp) can stay a thin,
 * high-level shell: CONFIG storage, an INFO summary, and wiring this driver
 * into ucmdexec::generic_cmd()/generic_script(), the same shared mechanism
 * every other comm-driver plugin (UART, TCPIP, KVCAN, MQTT, ...) uses. This
 * mirrors mqtt_driver.hpp's split exactly — see its class doc comment for
 * the rationale; only the transport (UART/RS-485 instead of TCPIP/TLS) and
 * the protocol (PROFIBUS FDL instead of MQTT) differ.
 *
 * Three-way split, per plugin architecture guideline #12:
 *   - **Protocol side**: `ProfibusProtocol` (profibus_protocol.hpp) — pure
 *     FDL telegram encode/decode, no I/O, transport-agnostic. Owned here
 *     as m_protocol (persists for the life of the session, since the FCB
 *     security sequence is per-destination session state).
 *   - **Driver side**: this class. Implements `ICommDriver` (so it can be
 *     `CommScriptCommandInterpreter`/`CommScriptClient`'s `DriverT`) and
 *     depends on the real, already-existing `UART` driver
 *     (src/lib/drivers/uart) — unmodified, wrapped, never reimplemented.
 *     Owns the serial port, the request/response (SDA/SRD/FDL-Status)
 *     telegram exchange (m_SendTelegram()/m_ReadTelegram()), the SYN
 *     inter-telegram pause, and — crucially — the GUI comm-dump reporting
 *     for every physical telegram exchanged (see send()/receive()'s doc
 *     comments for why that lives here rather than being left to the
 *     interpreter's own automatic dump).
 *   - **Plugin side**: `ProfibusPlugin` — stores CONFIG, builds this
 *     class's Config from it, and supplies send()/receive() to
 *     `ucmdexec::generic_cmd()`/`generic_script()` as the `pfsend`/`pfrecv`
 *     override. That's the entire plugin; every PROFIBUS-specific
 *     behaviour is implemented here instead.
 *
 * -------------------------------------------------------------------------
 * Known hardware/timing limitations (read before using against real slaves)
 * -------------------------------------------------------------------------
 * PROFIBUS FDL mandates 8E1 framing (even parity) and a station's UART
 * silicon normally guarantees the inter-telegram SYN pause and per-telegram
 * response deadlines in hardware. This driver now opens the port genuinely
 * 8E1 (open() passes UART::Parity::Even — see uUart.hpp, which added
 * configurable parity/data-bits/stop-bits specifically to make this
 * possible; every other UART driver caller is unaffected, since None/8/1
 * remain its defaults), so real PROFIBUS-DP slave hardware sees correctly
 * parity-framed octets. The timing side is still software-approximated:
 *
 *   - Of PROFIBUS's eight standard bit rates (9.6k, 19.2k, 45.45k, 93.75k,
 *     187.5k, 500k, 1.5M, 3M, 6M, 12M), only the ones that are *also*
 *     standard POSIX termios rates are reachable through UART::open():
 *     9600, 19200, 500000, and — platform-dependent (present when the
 *     build's <termios.h> defines them, true on modern glibc/Linux) —
 *     1500000 and 3000000. The rest (45450, 93750, 187500, 6000000,
 *     12000000) cannot be configured at all; UART::getBaud() silently
 *     falls back to 9600 for anything it doesn't recognise, so
 *     ProfibusPlugin's baud setter rejects unreachable rates outright
 *     rather than silently running at the wrong speed.
 *   - The inter-telegram SYN pause (>= 33 bit times of idle, signalling
 *     "new telegram starts now") is approximated with a plain sleep in
 *     m_EnsureSynPause() based on the configured baud rate. This is a
 *     best-effort software timer on a general-purpose OS, not a
 *     hardware-guaranteed deadline — fine for a diagnostic/bench tool, not
 *     a substitute for certified PROFIBUS silicon (e.g. Siemens SPC3/
 *     ASPC2) in a production installation.
 *   - This driver does not itself verify or react to a receive-side parity
 *     error (see uUart.hpp's Parity enum doc comment for why: this driver
 *     already has its own end-to-end checksum, the FCS, so a corrupted
 *     octet is caught there rather than by OS-level parity-error
 *     handling — see decodeTelegram()'s fcsOk).
 *   - Token-ring participation (SD4) is not implemented: this driver acts
 *     purely as a single master issuing point-to-point SDN/SDA/SRD/FDL-
 *     Status requests, never as a token holder passing SD4 to a peer
 *     master. See ProfibusProtocol's class doc comment for the same scope
 *     note from the protocol side.
 *
 * In short: this is a bench/diagnostic FDL master (and passive bus
 * monitor), correctly framed (including genuine 8E1 parity), checksummed,
 * and aimed at one of its reachable bit rates — not a certified,
 * interoperable-with-everything PROFIBUS-DP master stack, mainly on
 * account of its software-approximated SYN pause/response timing and its
 * lack of token-ring participation.
 */
class ProfibusDriver : public ICommDriver
{
public:
    struct Config {
        // Transport
        std::string device;              // e.g. "/dev/ttyUSB0"
        uint32_t baud = 19200;            // see class doc comment for which rates are actually reachable

        // FDL session parameters
        uint8_t ownAddress = 2;           // this master's own station address (0-125; 126/127 are reserved, see setOwnAddress())
        uint32_t responseTimeoutMs = 200; // how long to wait for a slave's SDA/SRD/FDL-Status response
        bool defaultHighPriority = false; // used when a CMD line doesn't say otherwise

        // Whether the standalone "<" receive (passive bus monitor) is enabled;
        // always true in practice — kept as a Config field for symmetry with
        // MqttDriver::Config and to leave room for a future toggle.

        // Runtime instance identity for the GUI comm-dump panel (e.g.
        // "PROFIBUS" or "PROFIBUS:1" -- see PluginDataSet::strInstanceName).
        // Falls back to "PROFIBUS" in the driver constructor when left empty.
        std::string strInstanceName;
    };

    explicit ProfibusDriver(Config config);
    ~ProfibusDriver();

    /**
     * @brief Opens the real UART port at the configured baud and resets the
     * FCB security-sequence state. Called once by ProfibusPlugin's factory
     * lambda before this driver is ever handed to the interpreter — every
     * other method assumes this already succeeded. Unlike MqttDriver::open()
     * there is no session handshake to perform (FDL has none) — opening the
     * port IS the whole of "opening" this driver.
     */
    bool open();
    void close();

    // ---- ICommDriver ----
    // Real physical I/O never actually flows through these when this driver
    // is used via send()/receive() below (which is how ProfibusPlugin
    // always uses it) — they exist because CommScriptCommandInterpreter<
    // ProfibusDriver> requires DriverT to implement ICommDriver, and may
    // itself call is_open()/describeConnection() for its own bookkeeping
    // regardless of pfsend/pfrecv being set. tout_write()/tout_read() are
    // thin passthroughs to the real UART driver for exactly that reason —
    // completeness, not an alternate code path this class relies on.
    bool is_open() const override;
    CommDetails describeConnection(std::string_view xtra_params = {}) const override;
    ICommDriver::WriteResult tout_write(uint32_t u32WriteTimeout, std::span<const uint8_t> buffer,
                                         std::string_view xtra_params = {}) const override;
    ICommDriver::ReadResult tout_read(uint32_t u32ReadTimeout, std::span<uint8_t> buffer,
                                       const ICommDriver::ReadOptions& options,
                                       std::string_view xtra_params = {}) const override;

    /**
     * @brief The "intermediary layer": parses the PROFIBUS.CMD argument text
     * in dataSpan (e.g. "SRD 5 AABBCC"), determines the FDL service,
     * builds and sends the corresponding telegram, and reports it to the
     * GUI comm-dump panel — see m_SendTelegram(). Matches
     * `CommScriptCommandInterpreter<ProfibusDriver>::SendFunc`'s exact
     * signature, so ProfibusPlugin passes this straight through as `pfsend`
     * (see profibus_plugin.cpp): supplying pfsend/pfrecv suppresses the
     * interpreter's own automatic dump (which would otherwise show the
     * pre-parse argument text, not real wire bytes), so this class must —
     * and does — dump the accurate replacement itself.
     */
    ICommDriver::WriteResult send(uint32_t u32WriteTimeout, std::span<const uint8_t> dataSpan,
                                   std::string_view xtra_params) const;

    /**
     * @brief The other half: for SDA/SRD/STATUS, waits for the response the
     * preceding send() call on this same command line made outstanding; for
     * SDN (no response expected) or a standalone "PROFIBUS.CMD <", instead
     * passively waits for and decodes the next telegram seen on the bus at
     * all (a bus-monitor read — useful to observe another master's traffic,
     * or a slave's unsolicited retry). See m_TlAwaitingResponse's doc
     * comment (profibus_driver.cpp) for how the two cases are told apart.
     * Matches `RecvFunc`'s exact signature.
     */
    ICommDriver::ReadResult receive(uint32_t u32ReadTimeout, std::span<uint8_t> dataSpan,
                                     const ICommDriver::ReadOptions& options, std::string_view xtra_params) const;

private:
    Config m_config;
    std::shared_ptr<UART> m_pUart;
    mutable ProfibusProtocol m_protocol;
    bool m_bIsOpen = false;

    // Physical I/O — thin wrapper over m_pUart->tout_write()/tout_read(),
    // kept as its own function only so every telegram-level caller goes
    // through one place (mirrors MqttDriver's m_PhysicalSend/m_PhysicalRecv,
    // even though there is no TLS layer here to make the indirection
    // otherwise necessary).
    ICommDriver::Status m_PhysicalSend(std::span<const uint8_t> data, uint32_t timeoutMs) const;
    ICommDriver::Status m_PhysicalRecv(std::span<uint8_t> buffer, uint32_t timeoutMs, size_t& outBytesRead) const;

    // Blocks until at least (33 bit-times at m_config.baud) have elapsed
    // since the last byte this driver put on the wire — the FDL "SYN
    // pause" that signals a new telegram is starting. See class doc
    // comment for why this is a best-effort software timer, not a
    // hardware-guaranteed one.
    void m_EnsureSynPause() const;
    mutable std::chrono::steady_clock::time_point m_lastTxActivity;

    // Sends one complete FDL telegram (built by ProfibusProtocol) via
    // m_EnsureSynPause() + m_PhysicalSend(), and reports it to the GUI
    // comm-dump panel on success.
    ICommDriver::Status m_SendTelegram(const std::vector<uint8_t>& telegram, std::string_view xtra_params) const;

    // Reads one complete FDL telegram (start delimiter first, then however
    // many more bytes that delimiter's format requires) via
    // m_PhysicalRecv(), decodes+checksum-verifies it via
    // ProfibusProtocol::decodeTelegram(), and reports it to the GUI
    // comm-dump panel as a single row on success. timeoutMs bounds only the
    // wait for the first (start-delimiter) byte; once a telegram has
    // started arriving, the rest is read with its own short fixed timeout
    // (a stall mid-telegram is a broken-link problem, not a "nothing to
    // receive yet" one) — same convention as MqttDriver::m_ReadPacket().
    ICommDriver::Status m_ReadTelegram(ProfibusProtocol::DecodedTelegram& telegramOut, uint32_t timeoutMs,
                                        std::string_view xtra_params) const;

    // Reads telegrams (via m_ReadTelegram()) until one whose SA/DA match
    // the outstanding exchange turns up, or timeoutMs elapses — anything
    // else read meanwhile (another station's traffic, a stray token, a
    // malformed/parity-glitched byte sequence) is logged and discarded.
    // Mirrors MqttDriver::m_WaitForAckPacket().
    bool m_WaitForResponse(uint8_t expectedFromSa, uint32_t timeoutMs,
                            ProfibusProtocol::DecodedTelegram& outTelegram, std::string_view xtra_params) const;

    // ---- Intermediary layer: PROFIBUS.CMD argument decomposition ----
    // Tokenizes on whitespace only, same convention (and same trailing-NUL
    // strip — see MqttDriver::m_TokenizeArgs()'s doc comment in
    // mqtt_driver.cpp for why) as every other CMD-parsing driver in this
    // codebase.
    static void m_TokenizeArgs(std::span<const uint8_t> dataSpan, std::vector<std::string>& outTokens);

    // Parses a hex-digit string ("AABBCC") into raw bytes. Returns false
    // (and logs) on an odd-length string or a non-hex-digit character.
    static bool m_ParseHexBytes(const std::string& hex, std::vector<uint8_t>& outBytes);
    static std::string m_BytesToHex(const std::vector<uint8_t>& bytes);

    // FDL sub-command handlers (the "specific callback associated to that
    // command"). Each builds and sends its telegram via m_protocol/
    // m_SendTelegram(), and — for a service whose completion is confirmed
    // by a response (SDA/SRD/STATUS) — records what receive() should wait
    // for next (see receive()'s doc comment). Returns false on bad
    // arguments or a send failure.
    bool m_HandleSdn(const std::vector<std::string>& args, std::string_view xtra_params) const;
    bool m_HandleSda(const std::vector<std::string>& args, std::string_view xtra_params) const;
    bool m_HandleSrd(const std::vector<std::string>& args, std::string_view xtra_params) const;
    bool m_HandleStatus(const std::vector<std::string>& args, std::string_view xtra_params) const;

    using ProfibusSubCmdHandler = bool (ProfibusDriver::*)(const std::vector<std::string>&, std::string_view) const;
    std::unordered_map<std::string, ProfibusSubCmdHandler> m_mapProfibusCmds;

    // The "<" side when no response is outstanding: passively waits for and
    // decodes the next telegram seen on the bus (any DA/SA — a bus-monitor
    // read, not an exchange this driver itself initiated) and writes a
    // one-line human-readable summary into buffer. Called from receive()
    // — see its doc comment.
    ICommDriver::ReadResult m_DoStandaloneReceive(uint32_t timeoutMs, std::span<uint8_t> buffer, std::string_view xtra_params) const;

    // Formats a DecodedTelegram (a response to our own SDA/SRD/STATUS, or
    // whatever the bus monitor saw) as the short human-readable text this
    // driver returns through receive()'s buffer — e.g. "AABBCC" for a data
    // reply, "ACK" for a bare SC, "SLAVE:DATA_LOW" for an FDL-Status reply.
    static std::string m_FormatTelegramResult(const ProfibusProtocol::DecodedTelegram& t, bool wasStatusQuery);
    static const char* m_StationTypeName(uint8_t stationType);
};

#endif // PROFIBUS_DRIVER_HPP
