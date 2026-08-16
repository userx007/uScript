#include "modbus_protocol.hpp"

std::vector<uint8_t> ModbusProtocol::m_buildRequest(uint8_t unitId, const std::vector<uint8_t>& pdu, uint16_t* pOutTxnId)
{
    const uint16_t txnId = m_allocateTransactionId();
    if (pOutTxnId) {
        *pOutTxnId = txnId;
    }

    const uint16_t followingLength = static_cast<uint16_t>(1 + pdu.size()); // Unit Id + PDU

    std::vector<uint8_t> adu;
    adu.reserve(7 + pdu.size());
    adu.push_back(static_cast<uint8_t>((txnId >> 8) & 0xFF));
    adu.push_back(static_cast<uint8_t>(txnId & 0xFF));
    adu.push_back(0x00); // Protocol Identifier hi — always 0 for Modbus
    adu.push_back(0x00); // Protocol Identifier lo
    adu.push_back(static_cast<uint8_t>((followingLength >> 8) & 0xFF));
    adu.push_back(static_cast<uint8_t>(followingLength & 0xFF));
    adu.push_back(unitId);
    adu.insert(adu.end(), pdu.begin(), pdu.end());
    return adu;
}

std::vector<uint8_t> ModbusProtocol::buildReadCoils(uint8_t unitId, uint16_t startAddr, uint16_t quantity, uint16_t* pOutTxnId)
{
    std::vector<uint8_t> pdu{ kReadCoils,
        static_cast<uint8_t>((startAddr >> 8) & 0xFF), static_cast<uint8_t>(startAddr & 0xFF),
        static_cast<uint8_t>((quantity >> 8) & 0xFF), static_cast<uint8_t>(quantity & 0xFF) };
    return m_buildRequest(unitId, pdu, pOutTxnId);
}

std::vector<uint8_t> ModbusProtocol::buildReadDiscreteInputs(uint8_t unitId, uint16_t startAddr, uint16_t quantity, uint16_t* pOutTxnId)
{
    std::vector<uint8_t> pdu{ kReadDiscreteInputs,
        static_cast<uint8_t>((startAddr >> 8) & 0xFF), static_cast<uint8_t>(startAddr & 0xFF),
        static_cast<uint8_t>((quantity >> 8) & 0xFF), static_cast<uint8_t>(quantity & 0xFF) };
    return m_buildRequest(unitId, pdu, pOutTxnId);
}

std::vector<uint8_t> ModbusProtocol::buildReadHoldingRegisters(uint8_t unitId, uint16_t startAddr, uint16_t quantity, uint16_t* pOutTxnId)
{
    std::vector<uint8_t> pdu{ kReadHoldingRegisters,
        static_cast<uint8_t>((startAddr >> 8) & 0xFF), static_cast<uint8_t>(startAddr & 0xFF),
        static_cast<uint8_t>((quantity >> 8) & 0xFF), static_cast<uint8_t>(quantity & 0xFF) };
    return m_buildRequest(unitId, pdu, pOutTxnId);
}

std::vector<uint8_t> ModbusProtocol::buildReadInputRegisters(uint8_t unitId, uint16_t startAddr, uint16_t quantity, uint16_t* pOutTxnId)
{
    std::vector<uint8_t> pdu{ kReadInputRegisters,
        static_cast<uint8_t>((startAddr >> 8) & 0xFF), static_cast<uint8_t>(startAddr & 0xFF),
        static_cast<uint8_t>((quantity >> 8) & 0xFF), static_cast<uint8_t>(quantity & 0xFF) };
    return m_buildRequest(unitId, pdu, pOutTxnId);
}

std::vector<uint8_t> ModbusProtocol::buildWriteSingleCoil(uint8_t unitId, uint16_t addr, bool value, uint16_t* pOutTxnId)
{
    const uint16_t wireValue = value ? 0xFF00 : 0x0000; // Modbus's own encoding for a single coil write
    std::vector<uint8_t> pdu{ kWriteSingleCoil,
        static_cast<uint8_t>((addr >> 8) & 0xFF), static_cast<uint8_t>(addr & 0xFF),
        static_cast<uint8_t>((wireValue >> 8) & 0xFF), static_cast<uint8_t>(wireValue & 0xFF) };
    return m_buildRequest(unitId, pdu, pOutTxnId);
}

std::vector<uint8_t> ModbusProtocol::buildWriteSingleRegister(uint8_t unitId, uint16_t addr, uint16_t value, uint16_t* pOutTxnId)
{
    std::vector<uint8_t> pdu{ kWriteSingleRegister,
        static_cast<uint8_t>((addr >> 8) & 0xFF), static_cast<uint8_t>(addr & 0xFF),
        static_cast<uint8_t>((value >> 8) & 0xFF), static_cast<uint8_t>(value & 0xFF) };
    return m_buildRequest(unitId, pdu, pOutTxnId);
}

std::vector<uint8_t> ModbusProtocol::buildWriteMultipleCoils(uint8_t unitId, uint16_t startAddr,
                                                               const std::vector<bool>& values, uint16_t* pOutTxnId)
{
    const uint16_t quantity = static_cast<uint16_t>(values.size());
    const uint8_t byteCount = static_cast<uint8_t>((values.size() + 7) / 8);

    std::vector<uint8_t> pdu;
    pdu.push_back(kWriteMultipleCoils);
    pdu.push_back(static_cast<uint8_t>((startAddr >> 8) & 0xFF));
    pdu.push_back(static_cast<uint8_t>(startAddr & 0xFF));
    pdu.push_back(static_cast<uint8_t>((quantity >> 8) & 0xFF));
    pdu.push_back(static_cast<uint8_t>(quantity & 0xFF));
    pdu.push_back(byteCount);

    std::vector<uint8_t> packed(byteCount, 0);
    for (size_t i = 0; i < values.size(); ++i) {
        if (values[i]) {
            packed[i / 8] |= static_cast<uint8_t>(1u << (i % 8));
        }
    }
    pdu.insert(pdu.end(), packed.begin(), packed.end());

    return m_buildRequest(unitId, pdu, pOutTxnId);
}

std::vector<uint8_t> ModbusProtocol::buildWriteMultipleRegisters(uint8_t unitId, uint16_t startAddr,
                                                                   const std::vector<uint16_t>& values, uint16_t* pOutTxnId)
{
    const uint16_t quantity = static_cast<uint16_t>(values.size());
    const uint8_t byteCount = static_cast<uint8_t>(values.size() * 2);

    std::vector<uint8_t> pdu;
    pdu.push_back(kWriteMultipleRegisters);
    pdu.push_back(static_cast<uint8_t>((startAddr >> 8) & 0xFF));
    pdu.push_back(static_cast<uint8_t>(startAddr & 0xFF));
    pdu.push_back(static_cast<uint8_t>((quantity >> 8) & 0xFF));
    pdu.push_back(static_cast<uint8_t>(quantity & 0xFF));
    pdu.push_back(byteCount);

    for (uint16_t v : values) {
        pdu.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        pdu.push_back(static_cast<uint8_t>(v & 0xFF));
    }

    return m_buildRequest(unitId, pdu, pOutTxnId);
}

// -----------------------------------------------------------------------
// Framing / decoders
// -----------------------------------------------------------------------

uint16_t ModbusProtocol::decodeFollowingLength(const uint8_t prefix[kMbapPrefixSize])
{
    return static_cast<uint16_t>((prefix[4] << 8) | prefix[5]);
}

uint16_t ModbusProtocol::decodeTransactionId(const std::vector<uint8_t>& adu)
{
    if (adu.size() < 2) return 0;
    return static_cast<uint16_t>((adu[0] << 8) | adu[1]);
}

uint8_t ModbusProtocol::decodeFunctionCode(const std::vector<uint8_t>& adu)
{
    // MBAP header is 7 bytes (Transaction Id(2) + Protocol Id(2) + Length(2) + Unit Id(1));
    // the function code is the first PDU byte, right after it.
    if (adu.size() < 8) return 0;
    return adu[7];
}

uint8_t ModbusProtocol::decodeExceptionCode(const std::vector<uint8_t>& adu)
{
    if (adu.size() < 9) return 0;
    return adu[8];
}

ModbusProtocol::ReadBitsResult ModbusProtocol::decodeReadBitsResponse(const std::vector<uint8_t>& adu, uint16_t quantity) const
{
    ReadBitsResult result;
    if (adu.size() < 9 || isException(adu)) {
        return result;
    }
    const uint8_t byteCount = adu[8];
    if (adu.size() < static_cast<size_t>(9 + byteCount)) {
        return result;
    }
    result.values.reserve(quantity);
    for (uint16_t i = 0; i < quantity; ++i) {
        const uint8_t byte = adu[9 + (i / 8)];
        result.values.push_back((byte & (1u << (i % 8))) != 0);
    }
    result.ok = true;
    return result;
}

ModbusProtocol::ReadRegsResult ModbusProtocol::decodeReadRegsResponse(const std::vector<uint8_t>& adu) const
{
    ReadRegsResult result;
    if (adu.size() < 9 || isException(adu)) {
        return result;
    }
    const uint8_t byteCount = adu[8];
    if (byteCount % 2 != 0 || adu.size() < static_cast<size_t>(9 + byteCount)) {
        return result;
    }
    const size_t regCount = byteCount / 2;
    result.values.reserve(regCount);
    for (size_t i = 0; i < regCount; ++i) {
        const uint16_t hi = adu[9 + i * 2];
        const uint16_t lo = adu[9 + i * 2 + 1];
        result.values.push_back(static_cast<uint16_t>((hi << 8) | lo));
    }
    result.ok = true;
    return result;
}

bool ModbusProtocol::isWriteAck(const std::vector<uint8_t>& adu, uint8_t expectedFunctionCode)
{
    return !isException(adu) && decodeFunctionCode(adu) == expectedFunctionCode;
}
