#ifndef MODBUS_DRIVER_HPP
#define MODBUS_DRIVER_HPP

#include "uTcpip.hpp"
#include "ICommDriver.hpp"
#include "modbus_protocol.hpp"

#include <memory>
#include <string>
#include <vector>
#include <span>
#include <unordered_map>
#include <cstdio>

/**
 * @brief The "driver side" — everything Modbus-implementation-specific
 * lives here, so ModbusPlugin (modbus_plugin.hpp) can stay a thin,
 * high-level shell. Same three-way split, and the same reasoning for it,
 * as the MQTT plugin's MqttProtocol/MqttDriver/MqttPlugin split:
 *
 *   - **Protocol side**: `ModbusProtocol` (modbus_protocol.hpp) — pure
 *     Modbus TCP ADU encode/decode, no I/O, driver-agnostic. Owned here as
 *     m_protocol (persists for the life of the connection, since
 *     transaction ids are assigned from one running sequence).
 *   - **Driver side**: this class. Implements `ICommDriver` (so it can be
 *     `CommScriptCommandInterpreter`/`CommScriptClient`'s `DriverT`) and
 *     depends on the real, already-existing `TCPIP` driver
 *     (src/lib/drivers/tcpip) — unmodified, wrapped, never reimplemented.
 *     Owns ADU framing over the wire (m_SendAdu()/m_ReadAdu()) and — like
 *     MqttDriver — the GUI comm-dump reporting for every physical request/
 *     response, done by hand for the same reason: supplying send()/
 *     receive() as `pfsend`/`pfrecv` suppresses the interpreter's own
 *     automatic dump (which would otherwise show the pre-parse MODBUS.CMD
 *     argument text, not real wire bytes), so this class dumps the
 *     accurate replacement itself.
 *   - **Plugin side**: `ModbusPlugin` — stores CONFIG, builds this class's
 *     Config from it, and supplies send()/receive() to
 *     `ucmdexec::generic_cmd()`/`generic_script()` as the `pfsend`/`pfrecv`
 *     override. That's the entire plugin.
 *
 * Modbus TCP has no session handshake (unlike MQTT's CONNECT/CONNACK) and
 * no asynchronous, broker-initiated messages — every request has exactly
 * one response, and nothing arrives that wasn't just asked for. So unlike
 * MqttDriver there's no standalone "<" receive mode: receive() always
 * waits for the response to whatever send() just requested. This also
 * means there's no keepalive/idle-session concern the way MQTT has one.
 *
 * send()/receive() are written to match `CommScriptCommandInterpreter<
 * ModbusDriver>::SendFunc`/`RecvFunc` exactly, so the plugin's lambdas are
 * one-liners with no plugin state captured at all — see modbus_plugin.cpp.
 */
class ModbusDriver : public ICommDriver
{
public:
    struct Config {
        std::string host;
        uint16_t port = 502;
        uint32_t connectTimeoutMs = 5000;
        uint32_t responseTimeoutMs = 3000;

        // Runtime instance identity for the GUI comm-dump panel (e.g. "MODBUS"
        // or "MODBUS:1" -- see PluginDataSet::strInstanceName). Falls back to
        // "MODBUS" in the driver constructor when left empty.
        std::string strInstanceName;
    };

    explicit ModbusDriver(Config config);
    ~ModbusDriver();

    /// Opens the real TCPIP connection. No session handshake beyond that —
    /// see class doc comment.
    bool open();
    void close();

    // ---- ICommDriver ----
    // Real physical I/O never actually flows through these when this
    // driver is used via send()/receive() below (which is how
    // ModbusPlugin always uses it) — they exist because
    // CommScriptCommandInterpreter<ModbusDriver> requires DriverT to
    // implement ICommDriver, and may itself call is_open()/
    // describeConnection() for its own bookkeeping regardless of pfsend/
    // pfrecv being set.
    bool is_open() const override;
    CommDetails describeConnection(std::string_view xtra_params = {}) const override;
    ICommDriver::WriteResult tout_write(uint32_t u32WriteTimeout, std::span<const uint8_t> buffer,
                                         std::string_view xtra_params = {}) const override;
    ICommDriver::ReadResult tout_read(uint32_t u32ReadTimeout, std::span<uint8_t> buffer,
                                       const ICommDriver::ReadOptions& options,
                                       std::string_view xtra_params = {}) const override;

    /**
     * @brief The "intermediary layer": parses the MODBUS.CMD argument text
     * in dataSpan (e.g. "READ_HOLDING_REGISTERS 1 100 4"), determines the
     * Modbus function, builds and sends the corresponding request ADU, and
     * reports it to the GUI comm-dump panel — see m_SendAdu(). Matches
     * `CommScriptCommandInterpreter<ModbusDriver>::SendFunc`'s exact
     * signature, so ModbusPlugin passes this straight through as `pfsend`.
     */
    ICommDriver::WriteResult send(uint32_t u32WriteTimeout, std::span<const uint8_t> dataSpan,
                                   std::string_view xtra_params) const;

    /**
     * @brief Waits for the response ADU to whatever send() just requested
     * (matched by transaction id), decodes it per that request's function
     * code, and writes a short human-readable confirmation into dataSpan:
     * comma-separated "0"/"1" per bit for a read-bits function, comma-
     * separated decimal values for a read-registers function, or "OK" for
     * a write. On a Modbus exception response, writes "EXCEPTION:<code>"
     * instead and fails. Matches `RecvFunc`'s exact signature.
     */
    ICommDriver::ReadResult receive(uint32_t u32ReadTimeout, std::span<uint8_t> dataSpan,
                                     const ICommDriver::ReadOptions& options, std::string_view xtra_params) const;

private:
    Config m_config;
    std::shared_ptr<TCPIP> m_pTcpip;
    mutable ModbusProtocol m_protocol;

    // Sends one complete Modbus ADU (built by ModbusProtocol) and reports
    // it to the GUI comm-dump panel on success.
    ICommDriver::Status m_SendAdu(const std::vector<uint8_t>& adu, std::string_view xtra_params) const;

    // Reads one complete Modbus ADU: exactly 6 bytes (the MBAP prefix up
    // to and including Length), then exactly Length more bytes (Unit Id +
    // PDU) — see ModbusProtocol::decodeFollowingLength(). Reports the full
    // ADU to the GUI comm-dump panel as a single row on success. timeoutMs
    // bounds only the wait for the prefix's first byte; once a response
    // has started arriving, the rest is read with its own short fixed
    // timeout (a stall mid-response is a broken-connection problem, not a
    // "nothing to receive yet" one).
    ICommDriver::Status m_ReadAdu(std::vector<uint8_t>& aduOut, uint32_t timeoutMs, std::string_view xtra_params) const;

    // ---- Intermediary layer: MODBUS.CMD argument decomposition ----
    // Tokenizes on whitespace only, after stripping a trailing NUL byte —
    // the interpreter's STRING_RAW conversion (ustring::stringToVector())
    // appends one by default, and while Modbus itself has no UTF-8-string
    // restriction to violate the way MQTT topics do, an extra empty
    // trailing token would still misparse a command's argument count, so
    // it's stripped here for the same reason.
    static void m_TokenizeArgs(std::span<const uint8_t> dataSpan, std::vector<std::string>& outTokens);

    // MODBUS sub-command handlers (the "specific callback associated to
    // that command"). Each parses its own arguments — every command's
    // first argument is always the Modbus unit (slave) id, with no default,
    // to keep argument parsing unambiguous (a bare number can't otherwise
    // be told apart as "unit id" vs. "address") — builds and sends its
    // request via m_protocol/m_SendAdu(), and records what receive()
    // should wait for and how to decode it. Returns false on bad arguments
    // or a send failure.
    bool m_HandleReadCoils(const std::vector<std::string>& args, std::string_view xtra_params) const;
    bool m_HandleReadDiscreteInputs(const std::vector<std::string>& args, std::string_view xtra_params) const;
    bool m_HandleReadHoldingRegisters(const std::vector<std::string>& args, std::string_view xtra_params) const;
    bool m_HandleReadInputRegisters(const std::vector<std::string>& args, std::string_view xtra_params) const;
    bool m_HandleWriteSingleCoil(const std::vector<std::string>& args, std::string_view xtra_params) const;
    bool m_HandleWriteSingleRegister(const std::vector<std::string>& args, std::string_view xtra_params) const;
    bool m_HandleWriteMultipleCoils(const std::vector<std::string>& args, std::string_view xtra_params) const;
    bool m_HandleWriteMultipleRegisters(const std::vector<std::string>& args, std::string_view xtra_params) const;

    // Shared by the four read handlers: parses "<unit_id> <start_addr>
    // <quantity>" (exactly 3 tokens) and validates quantity against
    // maxQuantity.
    bool m_ParseReadArgs(const std::vector<std::string>& args, uint16_t maxQuantity,
                         uint8_t& outUnitId, uint16_t& outAddr, uint16_t& outQuantity) const;

    using ModbusSubCmdHandler = bool (ModbusDriver::*)(const std::vector<std::string>&, std::string_view) const;
    std::unordered_map<std::string, ModbusSubCmdHandler> m_mapCmds;
};

#endif // MODBUS_DRIVER_HPP
