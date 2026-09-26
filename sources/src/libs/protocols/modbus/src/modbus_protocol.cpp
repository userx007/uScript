#include "modbus_protocol.hpp"

std::vector<uint8_t> ModbusProtocol::m_buildRequest(uint8_t u8UnitId, const std::vector<uint8_t> &vPdu, uint16_t *pu16OutTxnId)
{
    const uint16_t txnId = m_allocateTransactionId();
    if (pu16OutTxnId) {
        *pu16OutTxnId = txnId;
    }

    const uint16_t followingLength = static_cast<uint16_t>(1 + vPdu.size()); // Unit Id + PDU

    std::vector<uint8_t> adu;
    adu.reserve(7 + vPdu.size());
    adu.push_back(static_cast<uint8_t>((txnId >> 8) & 0xFF));
    adu.push_back(static_cast<uint8_t>(txnId & 0xFF));
    adu.push_back(0x00); // Protocol Identifier hi — always 0 for Modbus
    adu.push_back(0x00); // Protocol Identifier lo
    adu.push_back(static_cast<uint8_t>((followingLength >> 8) & 0xFF));
    adu.push_back(static_cast<uint8_t>(followingLength & 0xFF));
    adu.push_back(u8UnitId);
    adu.insert(adu.end(), vPdu.begin(), vPdu.end());
    return adu;
}

std::vector<uint8_t> ModbusProtocol::buildReadCoils(uint8_t u8UnitId, uint16_t u16StartAddr, uint16_t u16Quantity, uint16_t *pu16OutTxnId)
{
    std::vector<uint8_t> pdu{kReadCoils,
                             static_cast<uint8_t>((u16StartAddr >> 8) & 0xFF), static_cast<uint8_t>(u16StartAddr & 0xFF),
                             static_cast<uint8_t>((u16Quantity >> 8) & 0xFF), static_cast<uint8_t>(u16Quantity & 0xFF)};
    return m_buildRequest(u8UnitId, pdu, pu16OutTxnId);
}

std::vector<uint8_t> ModbusProtocol::buildReadDiscreteInputs(uint8_t u8UnitId, uint16_t u16StartAddr, uint16_t u16Quantity, uint16_t *pu16OutTxnId)
{
    std::vector<uint8_t> pdu{kReadDiscreteInputs,
                             static_cast<uint8_t>((u16StartAddr >> 8) & 0xFF), static_cast<uint8_t>(u16StartAddr & 0xFF),
                             static_cast<uint8_t>((u16Quantity >> 8) & 0xFF), static_cast<uint8_t>(u16Quantity & 0xFF)};
    return m_buildRequest(u8UnitId, pdu, pu16OutTxnId);
}

std::vector<uint8_t> ModbusProtocol::buildReadHoldingRegisters(uint8_t u8UnitId, uint16_t u16StartAddr, uint16_t u16Quantity, uint16_t *pu16OutTxnId)
{
    std::vector<uint8_t> pdu{kReadHoldingRegisters,
                             static_cast<uint8_t>((u16StartAddr >> 8) & 0xFF), static_cast<uint8_t>(u16StartAddr & 0xFF),
                             static_cast<uint8_t>((u16Quantity >> 8) & 0xFF), static_cast<uint8_t>(u16Quantity & 0xFF)};
    return m_buildRequest(u8UnitId, pdu, pu16OutTxnId);
}

std::vector<uint8_t> ModbusProtocol::buildReadInputRegisters(uint8_t u8UnitId, uint16_t u16StartAddr, uint16_t u16Quantity, uint16_t *pu16OutTxnId)
{
    std::vector<uint8_t> pdu{kReadInputRegisters,
                             static_cast<uint8_t>((u16StartAddr >> 8) & 0xFF), static_cast<uint8_t>(u16StartAddr & 0xFF),
                             static_cast<uint8_t>((u16Quantity >> 8) & 0xFF), static_cast<uint8_t>(u16Quantity & 0xFF)};
    return m_buildRequest(u8UnitId, pdu, pu16OutTxnId);
}

std::vector<uint8_t> ModbusProtocol::buildWriteSingleCoil(uint8_t u8UnitId, uint16_t u16Addr, bool bValue, uint16_t *pu16OutTxnId)
{
    const uint16_t wireValue = bValue ? 0xFF00 : 0x0000; // Modbus's own encoding for a single coil write
    std::vector<uint8_t> pdu{kWriteSingleCoil,
                             static_cast<uint8_t>((u16Addr >> 8) & 0xFF), static_cast<uint8_t>(u16Addr & 0xFF),
                             static_cast<uint8_t>((wireValue >> 8) & 0xFF), static_cast<uint8_t>(wireValue & 0xFF)};
    return m_buildRequest(u8UnitId, pdu, pu16OutTxnId);
}

std::vector<uint8_t> ModbusProtocol::buildWriteSingleRegister(uint8_t u8UnitId, uint16_t u16Addr, uint16_t u16Value, uint16_t *pu16OutTxnId)
{
    std::vector<uint8_t> pdu{kWriteSingleRegister,
                             static_cast<uint8_t>((u16Addr >> 8) & 0xFF), static_cast<uint8_t>(u16Addr & 0xFF),
                             static_cast<uint8_t>((u16Value >> 8) & 0xFF), static_cast<uint8_t>(u16Value & 0xFF)};
    return m_buildRequest(u8UnitId, pdu, pu16OutTxnId);
}

std::vector<uint8_t> ModbusProtocol::buildWriteMultipleCoils(uint8_t u8UnitId, uint16_t u16StartAddr,
                                                             const std::vector<bool> &vValues, uint16_t *pu16OutTxnId)
{
    const uint16_t quantity = static_cast<uint16_t>(vValues.size());
    const uint8_t byteCount = static_cast<uint8_t>((vValues.size() + 7) / 8);

    std::vector<uint8_t> pdu;
    pdu.push_back(kWriteMultipleCoils);
    pdu.push_back(static_cast<uint8_t>((u16StartAddr >> 8) & 0xFF));
    pdu.push_back(static_cast<uint8_t>(u16StartAddr & 0xFF));
    pdu.push_back(static_cast<uint8_t>((quantity >> 8) & 0xFF));
    pdu.push_back(static_cast<uint8_t>(quantity & 0xFF));
    pdu.push_back(byteCount);

    std::vector<uint8_t> packed(byteCount, 0);
    for (size_t i = 0; i < vValues.size(); ++i) {
        if (vValues[i]) {
            packed[i / 8] |= static_cast<uint8_t>(1u << (i % 8));
        }
    }
    pdu.insert(pdu.end(), packed.begin(), packed.end());

    return m_buildRequest(u8UnitId, pdu, pu16OutTxnId);
}

std::vector<uint8_t> ModbusProtocol::buildWriteMultipleRegisters(uint8_t u8UnitId, uint16_t u16StartAddr,
                                                                 const std::vector<uint16_t> &vValues, uint16_t *pu16OutTxnId)
{
    const uint16_t quantity = static_cast<uint16_t>(vValues.size());
    const uint8_t byteCount = static_cast<uint8_t>(vValues.size() * 2);

    std::vector<uint8_t> pdu;
    pdu.push_back(kWriteMultipleRegisters);
    pdu.push_back(static_cast<uint8_t>((u16StartAddr >> 8) & 0xFF));
    pdu.push_back(static_cast<uint8_t>(u16StartAddr & 0xFF));
    pdu.push_back(static_cast<uint8_t>((quantity >> 8) & 0xFF));
    pdu.push_back(static_cast<uint8_t>(quantity & 0xFF));
    pdu.push_back(byteCount);

    for (uint16_t v : vValues) {
        pdu.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        pdu.push_back(static_cast<uint8_t>(v & 0xFF));
    }

    return m_buildRequest(u8UnitId, pdu, pu16OutTxnId);
}

// -----------------------------------------------------------------------
// Framing / decoders
// -----------------------------------------------------------------------

uint16_t ModbusProtocol::decodeFollowingLength(const uint8_t prefix[kMbapPrefixSize])
{
    return static_cast<uint16_t>((prefix[4] << 8) | prefix[5]);
}

uint16_t ModbusProtocol::decodeTransactionId(const std::vector<uint8_t> &vAdu)
{
    if (vAdu.size() < 2) {
        return 0;
    }
    return static_cast<uint16_t>((vAdu[0] << 8) | vAdu[1]);
}

uint8_t ModbusProtocol::decodeFunctionCode(const std::vector<uint8_t> &vAdu)
{
    // MBAP header is 7 bytes (Transaction Id(2) + Protocol Id(2) + Length(2) + Unit Id(1));
    // the function code is the first PDU byte, right after it.
    if (vAdu.size() < 8) {
        return 0;
    }
    return vAdu[7];
}

uint8_t ModbusProtocol::decodeExceptionCode(const std::vector<uint8_t> &vAdu)
{
    if (vAdu.size() < 9) {
        return 0;
    }
    return vAdu[8];
}

ModbusProtocol::ReadBitsResult ModbusProtocol::decodeReadBitsResponse(const std::vector<uint8_t> &vAdu, uint16_t u16Quantity) const
{
    ReadBitsResult result;
    if (vAdu.size() < 9 || isException(vAdu)) {
        return result;
    }
    const uint8_t byteCount = vAdu[8];
    if (vAdu.size() < static_cast<size_t>(9 + byteCount)) {
        return result;
    }
    result.values.reserve(u16Quantity);
    for (uint16_t i = 0; i < u16Quantity; ++i) {
        const uint8_t byte = vAdu[9 + (i / 8)];
        result.values.push_back((byte & (1u << (i % 8))) != 0);
    }
    result.ok = true;
    return result;
}

ModbusProtocol::ReadRegsResult ModbusProtocol::decodeReadRegsResponse(const std::vector<uint8_t> &vAdu) const
{
    ReadRegsResult result;
    if (vAdu.size() < 9 || isException(vAdu)) {
        return result;
    }
    const uint8_t byteCount = vAdu[8];
    if (byteCount % 2 != 0 || vAdu.size() < static_cast<size_t>(9 + byteCount)) {
        return result;
    }
    const size_t regCount = byteCount / 2;
    result.values.reserve(regCount);
    for (size_t i = 0; i < regCount; ++i) {
        const uint16_t hi = vAdu[9 + i * 2];
        const uint16_t lo = vAdu[9 + i * 2 + 1];
        result.values.push_back(static_cast<uint16_t>((hi << 8) | lo));
    }
    result.ok = true;
    return result;
}

bool ModbusProtocol::isWriteAck(const std::vector<uint8_t> &vAdu, uint8_t u8ExpectedFunctionCode)
{
    return !isException(vAdu) && decodeFunctionCode(vAdu) == u8ExpectedFunctionCode;
}
