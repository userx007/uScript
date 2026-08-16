#ifndef MODBUS_PROTOCOL_HPP
#define MODBUS_PROTOCOL_HPP

#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief Pure Modbus TCP protocol codec.
 *
 * This is the "protocol side" of the plugin's three-way split (see
 * modbus_plugin.hpp's class doc comment for the other two — same shape as
 * MqttProtocol/MqttDriver/MqttPlugin): it only ever builds and parses
 * Modbus TCP ADUs (MBAP header + PDU) as plain byte buffers. It never
 * touches a socket or a timeout; the only state it carries is the
 * transaction-id sequence a session needs (assigned by build*()), which is
 * protocol-level bookkeeping, not transport state.
 *
 * Framing is simpler than MQTT's: the 7-byte MBAP header is fixed shape
 * (Transaction Identifier, Protocol Identifier, Length, Unit Identifier),
 * and Length says exactly how many bytes follow the first 6 — no
 * variable-length integer, no byte-by-byte parsing needed to know when a
 * packet ends. ModbusDriver's read loop (see its doc comment) reads
 * exactly 6 bytes, decodes Length from that, then reads exactly Length
 * more bytes — that's the whole ADU.
 *
 * ModbusDriver is what wires this to a real transport: it asks this class
 * to build a request, hands the resulting bytes to the real driver's
 * send, reads the response ADU back, and asks this class to decode it.
 */
class ModbusProtocol
{
public:
    // Function codes this class supports — the eight most commonly used
    // Modbus TCP operations.
    static constexpr uint8_t kReadCoils              = 0x01;
    static constexpr uint8_t kReadDiscreteInputs      = 0x02;
    static constexpr uint8_t kReadHoldingRegisters    = 0x03;
    static constexpr uint8_t kReadInputRegisters      = 0x04;
    static constexpr uint8_t kWriteSingleCoil         = 0x05;
    static constexpr uint8_t kWriteSingleRegister     = 0x06;
    static constexpr uint8_t kWriteMultipleCoils      = 0x0F;
    static constexpr uint8_t kWriteMultipleRegisters  = 0x10;
    static constexpr uint8_t kExceptionFlag           = 0x80; // OR'd into the function code on an exception response

    // Modbus limits (Modbus Application Protocol V1.1b3, §6) — this class
    // enforces these when building a request, since a request outside them
    // is malformed regardless of what any particular slave would do with it.
    static constexpr uint16_t kMaxReadBits      = 2000;
    static constexpr uint16_t kMaxReadRegisters = 125;
    static constexpr uint16_t kMaxWriteBits     = 1968;
    static constexpr uint16_t kMaxWriteRegisters = 123;

    struct ReadBitsResult {
        bool ok = false;               // false: malformed response (see ExceptionResult for a broker/slave-reported exception)
        std::vector<bool> values;
    };
    struct ReadRegsResult {
        bool ok = false;
        std::vector<uint16_t> values;
    };

    ModbusProtocol() = default;

    // ---- Builders: pure encode, no I/O ----
    // Each assigns and returns the transaction id used (echoed back by the
    // slave in its response's MBAP header, and how ModbusDriver matches a
    // response to the request that caused it).
    std::vector<uint8_t> buildReadCoils(uint8_t unitId, uint16_t startAddr, uint16_t quantity, uint16_t* pOutTxnId);
    std::vector<uint8_t> buildReadDiscreteInputs(uint8_t unitId, uint16_t startAddr, uint16_t quantity, uint16_t* pOutTxnId);
    std::vector<uint8_t> buildReadHoldingRegisters(uint8_t unitId, uint16_t startAddr, uint16_t quantity, uint16_t* pOutTxnId);
    std::vector<uint8_t> buildReadInputRegisters(uint8_t unitId, uint16_t startAddr, uint16_t quantity, uint16_t* pOutTxnId);
    std::vector<uint8_t> buildWriteSingleCoil(uint8_t unitId, uint16_t addr, bool value, uint16_t* pOutTxnId);
    std::vector<uint8_t> buildWriteSingleRegister(uint8_t unitId, uint16_t addr, uint16_t value, uint16_t* pOutTxnId);
    std::vector<uint8_t> buildWriteMultipleCoils(uint8_t unitId, uint16_t startAddr, const std::vector<bool>& values, uint16_t* pOutTxnId);
    std::vector<uint8_t> buildWriteMultipleRegisters(uint8_t unitId, uint16_t startAddr, const std::vector<uint16_t>& values, uint16_t* pOutTxnId);

    // ---- ADU framing helpers, used by ModbusDriver's read loop ----
    // MBAP header is exactly 7 bytes: Transaction Id(2) + Protocol Id(2) +
    // Length(2) + Unit Id(1). Length counts everything after itself (Unit
    // Id + PDU), so the total ADU size is 6 + Length.
    static constexpr size_t kMbapPrefixSize = 6; // Transaction Id + Protocol Id + Length, before Unit Id
    // Decodes the "Length" field from the first 6 bytes already read —
    // ModbusDriver reads exactly this many more bytes to complete the ADU.
    static uint16_t decodeFollowingLength(const uint8_t prefix[kMbapPrefixSize]);

    // ---- Decoders: pure decode of one already-complete ADU ----
    static uint16_t decodeTransactionId(const std::vector<uint8_t>& adu);
    static uint8_t decodeFunctionCode(const std::vector<uint8_t>& adu); // PDU function code, exception bit included if set
    static bool isException(const std::vector<uint8_t>& adu) { return (decodeFunctionCode(adu) & kExceptionFlag) != 0; }
    static uint8_t decodeExceptionCode(const std::vector<uint8_t>& adu); // valid only if isException() is true

    // quantity: the same value passed to buildReadCoils()/buildReadDiscreteInputs()
    // for this request — needed because the response's byte-packed bits
    // don't self-describe how many of the last byte's bits are padding.
    ReadBitsResult decodeReadBitsResponse(const std::vector<uint8_t>& adu, uint16_t quantity) const;
    ReadRegsResult decodeReadRegsResponse(const std::vector<uint8_t>& adu) const;

    // Write responses (single or multiple, coil or register) all just echo
    // the request's address/value(s) back — this class doesn't re-validate
    // that echo against what was sent, only that the function code matches
    // (not an exception). "true" here plus a non-exception function code is
    // as much confirmation as Modbus itself provides.
    static bool isWriteAck(const std::vector<uint8_t>& adu, uint8_t expectedFunctionCode);

    void resetTransactionIdSequence() { m_nextTransactionId = 1; }

private:
    // Transaction ids: one running sequence per session, echoed back by the
    // slave so out-of-order or pipelined responses can still be matched to
    // their request. 0 is a perfectly valid Modbus transaction id (unlike
    // MQTT packet ids) — no wraparound special-casing needed.
    uint16_t m_nextTransactionId = 1;
    uint16_t m_allocateTransactionId() { return m_nextTransactionId++; }

    std::vector<uint8_t> m_buildRequest(uint8_t unitId, const std::vector<uint8_t>& pdu, uint16_t* pOutTxnId);
};

#endif // MODBUS_PROTOCOL_HPP
