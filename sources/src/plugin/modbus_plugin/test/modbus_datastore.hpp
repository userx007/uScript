#ifndef MODBUS_DATASTORE_HPP
#define MODBUS_DATASTORE_HPP

#include <cstdint>
#include <vector>
#include <mutex>

/**
 * @brief The four Modbus data tables (coils, discrete inputs, holding
 * registers, input registers), bounds-checked and thread-safe.
 *
 * Mirrors the shape of pymodbus's `ModbusSlaveContext` (four independently
 * addressed tables, each 0-based) — this is the "datastore" half of a
 * pymodbus-style server; ModbusTcpServer (modbus_server.cpp) is the
 * "context/server" half that decodes requests, calls into this class, and
 * encodes responses.
 *
 * Every accessor returns a Modbus exception code (0 = success, 2 = illegal
 * data address, 3 = illegal data value) rather than throwing, since that's
 * exactly what the caller needs to build a Modbus exception response.
 */
class ModbusDataStore
{
public:
    static constexpr uint8_t kExceptionNone          = 0x00;
    static constexpr uint8_t kExceptionIllegalAddress = 0x02;
    static constexpr uint8_t kExceptionIllegalValue   = 0x03;

    explicit ModbusDataStore(size_t tableSize = 65536)
        : m_coils(tableSize, false)
        , m_discreteInputs(tableSize, false)
        , m_holdingRegisters(tableSize, 0)
        , m_inputRegisters(tableSize, 0)
    {
    }

    uint8_t readCoils(uint16_t addr, uint16_t qty, std::vector<bool>& out) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_readBits(m_coils, addr, qty, out);
    }

    uint8_t readDiscreteInputs(uint16_t addr, uint16_t qty, std::vector<bool>& out) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_readBits(m_discreteInputs, addr, qty, out);
    }

    uint8_t readHoldingRegisters(uint16_t addr, uint16_t qty, std::vector<uint16_t>& out) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_readRegs(m_holdingRegisters, addr, qty, out);
    }

    uint8_t readInputRegisters(uint16_t addr, uint16_t qty, std::vector<uint16_t>& out) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_readRegs(m_inputRegisters, addr, qty, out);
    }

    uint8_t writeSingleCoil(uint16_t addr, bool value)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (addr >= m_coils.size()) return kExceptionIllegalAddress;
        m_coils[addr] = value;
        return kExceptionNone;
    }

    uint8_t writeSingleRegister(uint16_t addr, uint16_t value)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (addr >= m_holdingRegisters.size()) return kExceptionIllegalAddress;
        m_holdingRegisters[addr] = value;
        return kExceptionNone;
    }

    uint8_t writeMultipleCoils(uint16_t addr, const std::vector<bool>& values)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (values.empty() || static_cast<size_t>(addr) + values.size() > m_coils.size()) {
            return kExceptionIllegalAddress;
        }
        for (size_t i = 0; i < values.size(); ++i) {
            m_coils[addr + i] = values[i];
        }
        return kExceptionNone;
    }

    uint8_t writeMultipleRegisters(uint16_t addr, const std::vector<uint16_t>& values)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (values.empty() || static_cast<size_t>(addr) + values.size() > m_holdingRegisters.size()) {
            return kExceptionIllegalAddress;
        }
        for (size_t i = 0; i < values.size(); ++i) {
            m_holdingRegisters[addr + i] = values[i];
        }
        return kExceptionNone;
    }

    // Direct seeding for test setup, mirroring pymodbus's ModbusSequentialDataBlock.setValues().
    void seedHoldingRegisters(uint16_t addr, const std::vector<uint16_t>& values)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (size_t i = 0; i < values.size() && addr + i < m_holdingRegisters.size(); ++i) {
            m_holdingRegisters[addr + i] = values[i];
        }
    }

    void seedCoils(uint16_t addr, const std::vector<bool>& values)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (size_t i = 0; i < values.size() && addr + i < m_coils.size(); ++i) {
            m_coils[addr + i] = values[i];
        }
    }

private:
    static uint8_t m_readBits(const std::vector<bool>& table, uint16_t addr, uint16_t qty, std::vector<bool>& out)
    {
        if (qty == 0 || static_cast<size_t>(addr) + qty > table.size()) {
            return kExceptionIllegalAddress;
        }
        out.assign(table.begin() + addr, table.begin() + addr + qty);
        return kExceptionNone;
    }

    static uint8_t m_readRegs(const std::vector<uint16_t>& table, uint16_t addr, uint16_t qty, std::vector<uint16_t>& out)
    {
        if (qty == 0 || static_cast<size_t>(addr) + qty > table.size()) {
            return kExceptionIllegalAddress;
        }
        out.assign(table.begin() + addr, table.begin() + addr + qty);
        return kExceptionNone;
    }

    mutable std::mutex m_mutex; // guards all four tables against concurrent client-handler threads
    std::vector<bool> m_coils;
    std::vector<bool> m_discreteInputs;
    std::vector<uint16_t> m_holdingRegisters;
    std::vector<uint16_t> m_inputRegisters;
};

#endif // MODBUS_DATASTORE_HPP
