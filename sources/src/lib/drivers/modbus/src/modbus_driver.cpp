#include "modbus_driver.hpp"
#include "uLogger.hpp"
#include "uGuiNotify.hpp"
#include "uString.hpp"
#include "uNumeric.hpp"

#include <cctype>
#include <algorithm>
#include <cstring>
#include <sstream>

#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LOG_HDR "MODBUS_DRV  |"

static constexpr const char* kPluginNameForDump = "MODBUS";

// -----------------------------------------------------------------------
// Pending-response handoff between send() and the following receive()
// call on the same thread. thread_local for the same reason as
// MqttDriver's equivalent (see mqtt_driver.cpp): a threaded MODBUS.CMD
// could in principle run concurrently with other MODBUS.CMD activity on
// this same, persistent driver instance.
//
// Unlike MQTT, there's no scenario where receive() is meaningfully called
// without a matching pending request (Modbus has no asynchronous,
// broker-initiated messages) — so unlike MqttDriver's receive(), this
// class's receive() simply fails if nothing is pending, rather than
// falling back to a "standalone receive" mode.
// -----------------------------------------------------------------------
namespace
{
    enum class PendingKind { None, ReadBits, ReadRegs, WriteAck };

    thread_local PendingKind tl_pendingKind         = PendingKind::None;
    thread_local uint16_t    tl_pendingTxnId        = 0;
    thread_local uint8_t     tl_pendingFunctionCode = 0;
    thread_local uint16_t    tl_pendingQuantity      = 0; // ReadBits only — see ModbusProtocol::decodeReadBitsResponse()
}

ModbusDriver::ModbusDriver(Config config)
    : m_config(std::move(config))
{
    if (m_config.strInstanceName.empty()) {
        m_config.strInstanceName = kPluginNameForDump;
    }
    m_mapCmds.insert({"READ_COILS",               &ModbusDriver::m_HandleReadCoils});
    m_mapCmds.insert({"READ_DISCRETE_INPUTS",      &ModbusDriver::m_HandleReadDiscreteInputs});
    m_mapCmds.insert({"READ_HOLDING_REGISTERS",    &ModbusDriver::m_HandleReadHoldingRegisters});
    m_mapCmds.insert({"READ_INPUT_REGISTERS",      &ModbusDriver::m_HandleReadInputRegisters});
    m_mapCmds.insert({"WRITE_SINGLE_COIL",         &ModbusDriver::m_HandleWriteSingleCoil});
    m_mapCmds.insert({"WRITE_SINGLE_REGISTER",     &ModbusDriver::m_HandleWriteSingleRegister});
    m_mapCmds.insert({"WRITE_MULTIPLE_COILS",      &ModbusDriver::m_HandleWriteMultipleCoils});
    m_mapCmds.insert({"WRITE_MULTIPLE_REGISTERS",  &ModbusDriver::m_HandleWriteMultipleRegisters});
}

ModbusDriver::~ModbusDriver()
{
    close();
}

bool ModbusDriver::open()
{
    if (m_config.host.empty()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Host not configured"));
        return false;
    }

    m_pTcpip = std::make_shared<TCPIP>(m_config.host, m_config.port, m_config.connectTimeoutMs, m_config.host);
    if (!m_pTcpip->is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("TCPIP open failed"));
        m_pTcpip.reset();
        return false;
    }

    m_protocol.resetTransactionIdSequence();
    return true;
}

void ModbusDriver::close()
{
    if (m_pTcpip) {
        m_pTcpip->close();
    }
    m_pTcpip.reset();
}

bool ModbusDriver::is_open() const
{
    return m_pTcpip && m_pTcpip->is_open();
}

CommDetails ModbusDriver::describeConnection(std::string_view xtra_params) const
{
    return m_pTcpip->describeConnection(xtra_params);
}

ICommDriver::WriteResult ModbusDriver::tout_write(uint32_t u32WriteTimeout, std::span<const uint8_t> buffer,
                                                    std::string_view xtra_params) const
{
    // Thin passthrough — see class doc comment. Never actually used by
    // ModbusPlugin, which always goes through send() instead.
    return m_pTcpip->tout_write(u32WriteTimeout, buffer, xtra_params);
}

ICommDriver::ReadResult ModbusDriver::tout_read(uint32_t u32ReadTimeout, std::span<uint8_t> buffer,
                                                  const ICommDriver::ReadOptions& options, std::string_view xtra_params) const
{
    return m_pTcpip->tout_read(u32ReadTimeout, buffer, options, xtra_params);
}

// -----------------------------------------------------------------------
// ADU-level send/receive — every physical Modbus exchange goes through
// these two, which report the real, complete ADU bytes to the GUI
// comm-dump panel by hand (see class doc comment for why).
// -----------------------------------------------------------------------

ICommDriver::Status ModbusDriver::m_SendAdu(const std::vector<uint8_t>& adu, std::string_view xtra_params) const
{
    auto res = m_pTcpip->tout_write(5000, std::span<const uint8_t>(adu.data(), adu.size()));
    if (res.status == ICommDriver::Status::SUCCESS && gui_mode_active()) {
        gui_notify_comm_dump(m_config.strInstanceName, describeConnection(xtra_params),
                              CommDir::Tx, adu.data(), static_cast<uint32_t>(adu.size()));
    }
    return res.status;
}

ICommDriver::Status ModbusDriver::m_ReadAdu(std::vector<uint8_t>& aduOut, uint32_t timeoutMs, std::string_view xtra_params) const
{
    aduOut.clear();
    aduOut.resize(ModbusProtocol::kMbapPrefixSize);

    // 1. Fixed 6-byte MBAP prefix (Transaction Id + Protocol Id + Length) —
    // the only part of an ADU that can legitimately take a while to arrive
    // (nothing new to receive yet), so the only part bounded by timeoutMs.
    {
        size_t totalRead = 0;
        // 0 == infinite timeout: never expire this wait, and forward 0
        // straight through to tout_read() on each attempt (the underlying
        // TCP driver now blocks indefinitely on 0).
        const bool bInfinite = (timeoutMs == 0);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (totalRead < ModbusProtocol::kMbapPrefixSize) {
            uint32_t remainingMs = 0;
            if (!bInfinite) {
                const auto remaining = deadline - std::chrono::steady_clock::now();
                remainingMs = remaining > std::chrono::milliseconds(0)
                    ? static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count())
                    : 0;
                if (remainingMs == 0 && totalRead == 0) {
                    return ICommDriver::Status::READ_TIMEOUT;
                }
            }
            auto res = m_pTcpip->tout_read((!bInfinite && remainingMs == 0) ? 1 : remainingMs,
                std::span<uint8_t>(aduOut.data() + totalRead, ModbusProtocol::kMbapPrefixSize - totalRead),
                ICommDriver::ReadOptions{.mode = ICommDriver::ReadMode::Exact});
            if (res.status != ICommDriver::Status::SUCCESS || res.bytes_read == 0) {
                return ICommDriver::Status::READ_TIMEOUT;
            }
            totalRead += res.bytes_read;
        }
    }

    // 2. Exactly "Length" more bytes (Unit Id + PDU) — a short, fixed
    // continuation timeout, since a response that starts arriving but then
    // stalls is a broken-connection problem, not a "nothing yet" one.
    const uint16_t followingLength = ModbusProtocol::decodeFollowingLength(aduOut.data());
    if (followingLength == 0) {
        return ICommDriver::Status::PROTOCOL_ERROR; // Unit Id is always present — Length is never 0
    }
    const size_t prefixSize = aduOut.size();
    aduOut.resize(prefixSize + followingLength);
    size_t totalRead = 0;
    while (totalRead < followingLength) {
        auto res = m_pTcpip->tout_read(3000,
            std::span<uint8_t>(aduOut.data() + prefixSize + totalRead, followingLength - totalRead),
            ICommDriver::ReadOptions{.mode = ICommDriver::ReadMode::Exact});
        if (res.status != ICommDriver::Status::SUCCESS || res.bytes_read == 0) {
            return ICommDriver::Status::READ_TIMEOUT;
        }
        totalRead += res.bytes_read;
    }

    if (gui_mode_active()) {
        gui_notify_comm_dump(m_config.strInstanceName, describeConnection(xtra_params),
                              CommDir::Rx, aduOut.data(), static_cast<uint32_t>(aduOut.size()));
    }

    return ICommDriver::Status::SUCCESS;
}

// -----------------------------------------------------------------------
// Intermediary layer
// -----------------------------------------------------------------------

// Tokenizes on whitespace only, after stripping a trailing NUL — see
// mqtt_driver.cpp's identical fix for the full explanation: the
// interpreter's STRING_RAW conversion (ustring::stringToVector()) appends
// one by default, and it would otherwise land inside — and corrupt the
// length of — this driver's last argument token.
void ModbusDriver::m_TokenizeArgs(std::span<const uint8_t> dataSpan, std::vector<std::string>& outTokens)
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

ICommDriver::WriteResult ModbusDriver::send(uint32_t u32WriteTimeout, std::span<const uint8_t> dataSpan,
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
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("MODBUS.CMD > requires a function: READ_COILS, READ_DISCRETE_INPUTS, "
                  "READ_HOLDING_REGISTERS, READ_INPUT_REGISTERS, WRITE_SINGLE_COIL, WRITE_SINGLE_REGISTER, "
                  "WRITE_MULTIPLE_COILS or WRITE_MULTIPLE_REGISTERS"));
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }

    std::string cmdKeyword = tokens[0];
    std::transform(cmdKeyword.begin(), cmdKeyword.end(), cmdKeyword.begin(),
                    [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    auto it = m_mapCmds.find(cmdKeyword);
    if (it == m_mapCmds.end()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Unknown Modbus function:"); LOG_STRING(tokens[0]));
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

ICommDriver::ReadResult ModbusDriver::receive(uint32_t u32ReadTimeout, std::span<uint8_t> dataSpan,
                                               const ICommDriver::ReadOptions& options, std::string_view xtra_params) const
{
    (void)options;
    ICommDriver::ReadResult result;

    if (!is_open()) {
        result.status = ICommDriver::Status::PORT_ACCESS;
        return result;
    }

    if (tl_pendingKind == PendingKind::None) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("MODBUS.CMD <: no request outstanding — every receive answers the "
                  "request on its own MODBUS.CMD > ... line, there is no standalone receive"));
        result.status = ICommDriver::Status::INVALID_PARAM;
        return result;
    }

    const PendingKind kind         = tl_pendingKind;
    const uint16_t    txnId        = tl_pendingTxnId;
    const uint8_t     functionCode = tl_pendingFunctionCode;
    const uint16_t    quantity     = tl_pendingQuantity;
    tl_pendingKind = PendingKind::None; // consume-once

    std::vector<uint8_t> adu;
    // 0 == infinite timeout: never expire this wait, and forward 0 straight
    // through to m_ReadAdu() on each attempt.
    const bool bInfinite = (u32ReadTimeout == 0);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(u32ReadTimeout);
    while (true) {
        uint32_t remainingMs = 0;
        if (!bInfinite) {
            const auto remaining = deadline - std::chrono::steady_clock::now();
            if (remaining <= std::chrono::milliseconds(0)) {
                result.status = ICommDriver::Status::READ_TIMEOUT;
                return result;
            }
            remainingMs = static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count());
        }

        auto st = m_ReadAdu(adu, remainingMs, xtra_params);
        if (st != ICommDriver::Status::SUCCESS) {
            result.status = st;
            return result;
        }
        if (ModbusProtocol::decodeTransactionId(adu) == txnId) {
            break;
        }
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("Response transaction id mismatch, expected"); LOG_UINT32(txnId);
                  LOG_STRING("got"); LOG_UINT32(ModbusProtocol::decodeTransactionId(adu)); LOG_STRING("— still waiting"));
        // a late/stray response for an older transaction id — keep waiting for ours
    }

    std::string out;
    bool ok = true;

    if (ModbusProtocol::isException(adu)) {
        std::ostringstream oss;
        oss << "EXCEPTION:" << static_cast<unsigned>(ModbusProtocol::decodeExceptionCode(adu));
        out = oss.str();
        ok = false;
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Modbus exception response:"); LOG_STRING(out));
    } else {
        switch (kind) {
            case PendingKind::ReadBits: {
                auto decoded = m_protocol.decodeReadBitsResponse(adu, quantity);
                if (!decoded.ok) {
                    out = "MALFORMED_RESPONSE";
                    ok = false;
                    break;
                }
                std::ostringstream oss;
                for (size_t i = 0; i < decoded.values.size(); ++i) {
                    if (i) oss << ',';
                    oss << (decoded.values[i] ? '1' : '0');
                }
                out = oss.str();
                break;
            }
            case PendingKind::ReadRegs: {
                auto decoded = m_protocol.decodeReadRegsResponse(adu);
                if (!decoded.ok) {
                    out = "MALFORMED_RESPONSE";
                    ok = false;
                    break;
                }
                std::ostringstream oss;
                for (size_t i = 0; i < decoded.values.size(); ++i) {
                    if (i) oss << ',';
                    oss << decoded.values[i];
                }
                out = oss.str();
                break;
            }
            case PendingKind::WriteAck: {
                if (ModbusProtocol::isWriteAck(adu, functionCode)) {
                    out = "OK";
                } else {
                    out = "MALFORMED_RESPONSE";
                    ok = false;
                }
                break;
            }
            case PendingKind::None:
                out = "MALFORMED_RESPONSE";
                ok = false;
                break;
        }
    }

    const size_t len = std::min(dataSpan.size(), out.size());
    std::memcpy(dataSpan.data(), out.data(), len);
    result.status = ok ? ICommDriver::Status::SUCCESS : ICommDriver::Status::PROTOCOL_ERROR;
    result.bytes_read = len;
    return result;
}

// -----------------------------------------------------------------------
// Modbus function handlers
// -----------------------------------------------------------------------

bool ModbusDriver::m_ParseReadArgs(const std::vector<std::string>& args, uint16_t maxQuantity,
                                    uint8_t& outUnitId, uint16_t& outAddr, uint16_t& outQuantity) const
{
    if (args.size() != 3) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Usage: <unit_id> <start_addr> <quantity>"));
        return false;
    }
    uint32_t unitId = 0, addr = 0, quantity = 0;
    if (!numeric::str2uint32(args[0], unitId) || unitId > 255) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid unit id (0-255):"); LOG_STRING(args[0]));
        return false;
    }
    if (!numeric::str2uint32(args[1], addr) || addr > 0xFFFF) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid address (0-65535):"); LOG_STRING(args[1]));
        return false;
    }
    if (!numeric::str2uint32(args[2], quantity) || quantity == 0 || quantity > maxQuantity) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid quantity (1-"); LOG_UINT32(maxQuantity); LOG_STRING("):"); LOG_STRING(args[2]));
        return false;
    }
    outUnitId = static_cast<uint8_t>(unitId);
    outAddr = static_cast<uint16_t>(addr);
    outQuantity = static_cast<uint16_t>(quantity);
    return true;
}

bool ModbusDriver::m_HandleReadCoils(const std::vector<std::string>& args, std::string_view xtra_params) const
{
    uint8_t unitId; uint16_t addr, quantity;
    if (!m_ParseReadArgs(args, ModbusProtocol::kMaxReadBits, unitId, addr, quantity)) return false;

    uint16_t txnId = 0;
    auto adu = m_protocol.buildReadCoils(unitId, addr, quantity, &txnId);
    if (m_SendAdu(adu, xtra_params) != ICommDriver::Status::SUCCESS) return false;

    tl_pendingKind         = PendingKind::ReadBits;
    tl_pendingTxnId        = txnId;
    tl_pendingFunctionCode = ModbusProtocol::kReadCoils;
    tl_pendingQuantity     = quantity;
    return true;
}

bool ModbusDriver::m_HandleReadDiscreteInputs(const std::vector<std::string>& args, std::string_view xtra_params) const
{
    uint8_t unitId; uint16_t addr, quantity;
    if (!m_ParseReadArgs(args, ModbusProtocol::kMaxReadBits, unitId, addr, quantity)) return false;

    uint16_t txnId = 0;
    auto adu = m_protocol.buildReadDiscreteInputs(unitId, addr, quantity, &txnId);
    if (m_SendAdu(adu, xtra_params) != ICommDriver::Status::SUCCESS) return false;

    tl_pendingKind         = PendingKind::ReadBits;
    tl_pendingTxnId        = txnId;
    tl_pendingFunctionCode = ModbusProtocol::kReadDiscreteInputs;
    tl_pendingQuantity     = quantity;
    return true;
}

bool ModbusDriver::m_HandleReadHoldingRegisters(const std::vector<std::string>& args, std::string_view xtra_params) const
{
    uint8_t unitId; uint16_t addr, quantity;
    if (!m_ParseReadArgs(args, ModbusProtocol::kMaxReadRegisters, unitId, addr, quantity)) return false;

    uint16_t txnId = 0;
    auto adu = m_protocol.buildReadHoldingRegisters(unitId, addr, quantity, &txnId);
    if (m_SendAdu(adu, xtra_params) != ICommDriver::Status::SUCCESS) return false;

    tl_pendingKind         = PendingKind::ReadRegs;
    tl_pendingTxnId        = txnId;
    tl_pendingFunctionCode = ModbusProtocol::kReadHoldingRegisters;
    return true;
}

bool ModbusDriver::m_HandleReadInputRegisters(const std::vector<std::string>& args, std::string_view xtra_params) const
{
    uint8_t unitId; uint16_t addr, quantity;
    if (!m_ParseReadArgs(args, ModbusProtocol::kMaxReadRegisters, unitId, addr, quantity)) return false;

    uint16_t txnId = 0;
    auto adu = m_protocol.buildReadInputRegisters(unitId, addr, quantity, &txnId);
    if (m_SendAdu(adu, xtra_params) != ICommDriver::Status::SUCCESS) return false;

    tl_pendingKind         = PendingKind::ReadRegs;
    tl_pendingTxnId        = txnId;
    tl_pendingFunctionCode = ModbusProtocol::kReadInputRegisters;
    return true;
}

bool ModbusDriver::m_HandleWriteSingleCoil(const std::vector<std::string>& args, std::string_view xtra_params) const
{
    if (args.size() != 3) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Usage: WRITE_SINGLE_COIL <unit_id> <addr> <0|1>"));
        return false;
    }
    uint32_t unitId = 0, addr = 0;
    if (!numeric::str2uint32(args[0], unitId) || unitId > 255) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid unit id (0-255):"); LOG_STRING(args[0]));
        return false;
    }
    if (!numeric::str2uint32(args[1], addr) || addr > 0xFFFF) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid address (0-65535):"); LOG_STRING(args[1]));
        return false;
    }
    if (args[2] != "0" && args[2] != "1") {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid coil value (must be 0 or 1):"); LOG_STRING(args[2]));
        return false;
    }
    const bool value = (args[2] == "1");

    uint16_t txnId = 0;
    auto adu = m_protocol.buildWriteSingleCoil(static_cast<uint8_t>(unitId), static_cast<uint16_t>(addr), value, &txnId);
    if (m_SendAdu(adu, xtra_params) != ICommDriver::Status::SUCCESS) return false;

    tl_pendingKind         = PendingKind::WriteAck;
    tl_pendingTxnId        = txnId;
    tl_pendingFunctionCode = ModbusProtocol::kWriteSingleCoil;
    return true;
}

bool ModbusDriver::m_HandleWriteSingleRegister(const std::vector<std::string>& args, std::string_view xtra_params) const
{
    if (args.size() != 3) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Usage: WRITE_SINGLE_REGISTER <unit_id> <addr> <value 0-65535>"));
        return false;
    }
    uint32_t unitId = 0, addr = 0, value = 0;
    if (!numeric::str2uint32(args[0], unitId) || unitId > 255) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid unit id (0-255):"); LOG_STRING(args[0]));
        return false;
    }
    if (!numeric::str2uint32(args[1], addr) || addr > 0xFFFF) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid address (0-65535):"); LOG_STRING(args[1]));
        return false;
    }
    if (!numeric::str2uint32(args[2], value) || value > 0xFFFF) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid register value (0-65535):"); LOG_STRING(args[2]));
        return false;
    }

    uint16_t txnId = 0;
    auto adu = m_protocol.buildWriteSingleRegister(static_cast<uint8_t>(unitId), static_cast<uint16_t>(addr), static_cast<uint16_t>(value), &txnId);
    if (m_SendAdu(adu, xtra_params) != ICommDriver::Status::SUCCESS) return false;

    tl_pendingKind         = PendingKind::WriteAck;
    tl_pendingTxnId        = txnId;
    tl_pendingFunctionCode = ModbusProtocol::kWriteSingleRegister;
    return true;
}

bool ModbusDriver::m_HandleWriteMultipleCoils(const std::vector<std::string>& args, std::string_view xtra_params) const
{
    if (args.size() < 3) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Usage: WRITE_MULTIPLE_COILS <unit_id> <start_addr> <v1> [v2] ..."));
        return false;
    }
    uint32_t unitId = 0, addr = 0;
    if (!numeric::str2uint32(args[0], unitId) || unitId > 255) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid unit id (0-255):"); LOG_STRING(args[0]));
        return false;
    }
    if (!numeric::str2uint32(args[1], addr) || addr > 0xFFFF) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid address (0-65535):"); LOG_STRING(args[1]));
        return false;
    }
    const size_t count = args.size() - 2;
    if (count > ModbusProtocol::kMaxWriteBits) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Too many coil values, max"); LOG_UINT32(ModbusProtocol::kMaxWriteBits));
        return false;
    }
    std::vector<bool> values;
    values.reserve(count);
    for (size_t i = 2; i < args.size(); ++i) {
        if (args[i] != "0" && args[i] != "1") {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid coil value (must be 0 or 1):"); LOG_STRING(args[i]));
            return false;
        }
        values.push_back(args[i] == "1");
    }

    uint16_t txnId = 0;
    auto adu = m_protocol.buildWriteMultipleCoils(static_cast<uint8_t>(unitId), static_cast<uint16_t>(addr), values, &txnId);
    if (m_SendAdu(adu, xtra_params) != ICommDriver::Status::SUCCESS) return false;

    tl_pendingKind         = PendingKind::WriteAck;
    tl_pendingTxnId        = txnId;
    tl_pendingFunctionCode = ModbusProtocol::kWriteMultipleCoils;
    return true;
}

bool ModbusDriver::m_HandleWriteMultipleRegisters(const std::vector<std::string>& args, std::string_view xtra_params) const
{
    if (args.size() < 3) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Usage: WRITE_MULTIPLE_REGISTERS <unit_id> <start_addr> <v1> [v2] ..."));
        return false;
    }
    uint32_t unitId = 0, addr = 0;
    if (!numeric::str2uint32(args[0], unitId) || unitId > 255) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid unit id (0-255):"); LOG_STRING(args[0]));
        return false;
    }
    if (!numeric::str2uint32(args[1], addr) || addr > 0xFFFF) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid address (0-65535):"); LOG_STRING(args[1]));
        return false;
    }
    const size_t count = args.size() - 2;
    if (count > ModbusProtocol::kMaxWriteRegisters) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Too many register values, max"); LOG_UINT32(ModbusProtocol::kMaxWriteRegisters));
        return false;
    }
    std::vector<uint16_t> values;
    values.reserve(count);
    for (size_t i = 2; i < args.size(); ++i) {
        uint32_t v = 0;
        if (!numeric::str2uint32(args[i], v) || v > 0xFFFF) {
            LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid register value (0-65535):"); LOG_STRING(args[i]));
            return false;
        }
        values.push_back(static_cast<uint16_t>(v));
    }

    uint16_t txnId = 0;
    auto adu = m_protocol.buildWriteMultipleRegisters(static_cast<uint8_t>(unitId), static_cast<uint16_t>(addr), values, &txnId);
    if (m_SendAdu(adu, xtra_params) != ICommDriver::Status::SUCCESS) return false;

    tl_pendingKind         = PendingKind::WriteAck;
    tl_pendingTxnId        = txnId;
    tl_pendingFunctionCode = ModbusProtocol::kWriteMultipleRegisters;
    return true;
}
