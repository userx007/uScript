// modbus_server.cpp — a small, self-contained Modbus TCP server, inspired
// by pymodbus's architecture (a datastore + a server that decodes
// requests, dispatches by function code, and encodes responses) but
// written as a standalone C++ tool with no Python/pymodbus dependency, so
// modbus_plugin can be tested against something running entirely in this
// codebase's own toolchain.
//
// Supports the same eight function codes modbus_plugin's ModbusDriver
// implements: READ_COILS, READ_DISCRETE_INPUTS, READ_HOLDING_REGISTERS,
// READ_INPUT_REGISTERS, WRITE_SINGLE_COIL, WRITE_SINGLE_REGISTER,
// WRITE_MULTIPLE_COILS, WRITE_MULTIPLE_REGISTERS — plus proper Modbus
// exception responses (illegal function / illegal data address / illegal
// data value) for anything malformed or out of range.
//
// One thread per client connection (simplicity over scalability — this is
// a test tool, not a production PLC simulator); all four data tables are
// shared across connections through ModbusDataStore's internal mutex, so
// a write from one client is immediately visible to a read from another,
// same as a real Modbus TCP gateway multiplexing several masters onto one
// slave.

#include "modbus_datastore.hpp"

#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <csignal>

namespace
{
    // Function codes — same eight as modbus_plugin's ModbusProtocol.
    constexpr uint8_t kReadCoils             = 0x01;
    constexpr uint8_t kReadDiscreteInputs    = 0x02;
    constexpr uint8_t kReadHoldingRegisters  = 0x03;
    constexpr uint8_t kReadInputRegisters    = 0x04;
    constexpr uint8_t kWriteSingleCoil       = 0x05;
    constexpr uint8_t kWriteSingleRegister   = 0x06;
    constexpr uint8_t kWriteMultipleCoils    = 0x0F;
    constexpr uint8_t kWriteMultipleRegisters = 0x10;
    constexpr uint8_t kExceptionFlag         = 0x80;

    constexpr uint8_t kExceptionIllegalFunction = 0x01;

    constexpr size_t kMbapPrefixSize = 6; // Transaction Id(2) + Protocol Id(2) + Length(2)

    bool g_verbose = false;
    std::atomic<bool> g_running{true};

    void hexdump(const char* label, const std::vector<uint8_t>& data)
    {
        if (!g_verbose) return;
        std::printf("  %s (%zu bytes):", label, data.size());
        for (auto b : data) std::printf(" %02X", b);
        std::printf("\n");
    }

    // ---- Full-read/write helpers (a single recv()/send() can return less
    // than requested even on a blocking socket) ----

    bool readFull(int fd, uint8_t* buf, size_t len)
    {
        size_t got = 0;
        while (got < len) {
            ssize_t n = ::recv(fd, buf + got, len - got, 0);
            if (n <= 0) return false; // peer closed or error
            got += static_cast<size_t>(n);
        }
        return true;
    }

    bool writeFull(int fd, const uint8_t* buf, size_t len)
    {
        size_t sent = 0;
        while (sent < len) {
            ssize_t n = ::send(fd, buf + sent, len - sent, 0);
            if (n <= 0) return false;
            sent += static_cast<size_t>(n);
        }
        return true;
    }

    uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
    void putBe16(std::vector<uint8_t>& out, uint16_t v)
    {
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(v & 0xFF));
    }

    // Builds a complete response ADU (MBAP header + PDU) given the
    // request's transaction id and unit id, echoed back as required.
    std::vector<uint8_t> buildResponseAdu(uint16_t txnId, uint8_t unitId, const std::vector<uint8_t>& pdu)
    {
        std::vector<uint8_t> adu;
        const uint16_t followingLength = static_cast<uint16_t>(1 + pdu.size());
        putBe16(adu, txnId);
        putBe16(adu, 0x0000); // Protocol Identifier — always 0 for Modbus
        putBe16(adu, followingLength);
        adu.push_back(unitId);
        adu.insert(adu.end(), pdu.begin(), pdu.end());
        return adu;
    }

    std::vector<uint8_t> buildExceptionPdu(uint8_t functionCode, uint8_t exceptionCode)
    {
        return { static_cast<uint8_t>(functionCode | kExceptionFlag), exceptionCode };
    }

    /**
     * @brief Decodes one request PDU, calls into the datastore, and
     * returns the response PDU (which may itself be an exception PDU —
     * the caller doesn't need to special-case that, it's just bytes to
     * wrap in an MBAP header and send back).
     */
    std::vector<uint8_t> handlePdu(ModbusDataStore& store, const std::vector<uint8_t>& pdu)
    {
        if (pdu.empty()) {
            return buildExceptionPdu(0, kExceptionIllegalFunction);
        }
        const uint8_t fc = pdu[0];

        switch (fc) {
            case kReadCoils:
            case kReadDiscreteInputs: {
                if (pdu.size() != 5) return buildExceptionPdu(fc, ModbusDataStore::kExceptionIllegalValue);
                const uint16_t addr = be16(&pdu[1]);
                const uint16_t qty  = be16(&pdu[3]);
                if (qty == 0 || qty > 2000) return buildExceptionPdu(fc, ModbusDataStore::kExceptionIllegalValue);

                std::vector<bool> bits;
                const uint8_t exc = (fc == kReadCoils) ? store.readCoils(addr, qty, bits)
                                                        : store.readDiscreteInputs(addr, qty, bits);
                if (exc != ModbusDataStore::kExceptionNone) return buildExceptionPdu(fc, exc);

                const uint8_t byteCount = static_cast<uint8_t>((bits.size() + 7) / 8);
                std::vector<uint8_t> resp{ fc, byteCount };
                resp.resize(2 + byteCount, 0);
                for (size_t i = 0; i < bits.size(); ++i) {
                    if (bits[i]) resp[2 + i / 8] |= static_cast<uint8_t>(1u << (i % 8));
                }
                return resp;
            }

            case kReadHoldingRegisters:
            case kReadInputRegisters: {
                if (pdu.size() != 5) return buildExceptionPdu(fc, ModbusDataStore::kExceptionIllegalValue);
                const uint16_t addr = be16(&pdu[1]);
                const uint16_t qty  = be16(&pdu[3]);
                if (qty == 0 || qty > 125) return buildExceptionPdu(fc, ModbusDataStore::kExceptionIllegalValue);

                std::vector<uint16_t> regs;
                const uint8_t exc = (fc == kReadHoldingRegisters) ? store.readHoldingRegisters(addr, qty, regs)
                                                                   : store.readInputRegisters(addr, qty, regs);
                if (exc != ModbusDataStore::kExceptionNone) return buildExceptionPdu(fc, exc);

                std::vector<uint8_t> resp{ fc, static_cast<uint8_t>(regs.size() * 2) };
                for (auto r : regs) putBe16(resp, r);
                return resp;
            }

            case kWriteSingleCoil: {
                if (pdu.size() != 5) return buildExceptionPdu(fc, ModbusDataStore::kExceptionIllegalValue);
                const uint16_t addr  = be16(&pdu[1]);
                const uint16_t wire  = be16(&pdu[3]);
                if (wire != 0xFF00 && wire != 0x0000) return buildExceptionPdu(fc, ModbusDataStore::kExceptionIllegalValue);

                const uint8_t exc = store.writeSingleCoil(addr, wire == 0xFF00);
                if (exc != ModbusDataStore::kExceptionNone) return buildExceptionPdu(fc, exc);

                return std::vector<uint8_t>(pdu); // echo the request verbatim, per spec
            }

            case kWriteSingleRegister: {
                if (pdu.size() != 5) return buildExceptionPdu(fc, ModbusDataStore::kExceptionIllegalValue);
                const uint16_t addr  = be16(&pdu[1]);
                const uint16_t value = be16(&pdu[3]);

                const uint8_t exc = store.writeSingleRegister(addr, value);
                if (exc != ModbusDataStore::kExceptionNone) return buildExceptionPdu(fc, exc);

                return std::vector<uint8_t>(pdu); // echo the request verbatim, per spec
            }

            case kWriteMultipleCoils: {
                if (pdu.size() < 6) return buildExceptionPdu(fc, ModbusDataStore::kExceptionIllegalValue);
                const uint16_t addr      = be16(&pdu[1]);
                const uint16_t qty       = be16(&pdu[3]);
                const uint8_t byteCount  = pdu[5];
                if (qty == 0 || qty > 1968 || byteCount != (qty + 7) / 8 || pdu.size() != 6 + byteCount) {
                    return buildExceptionPdu(fc, ModbusDataStore::kExceptionIllegalValue);
                }
                std::vector<bool> values(qty);
                for (uint16_t i = 0; i < qty; ++i) {
                    values[i] = (pdu[6 + i / 8] & (1u << (i % 8))) != 0;
                }
                const uint8_t exc = store.writeMultipleCoils(addr, values);
                if (exc != ModbusDataStore::kExceptionNone) return buildExceptionPdu(fc, exc);

                std::vector<uint8_t> resp{ fc };
                putBe16(resp, addr);
                putBe16(resp, qty);
                return resp;
            }

            case kWriteMultipleRegisters: {
                if (pdu.size() < 6) return buildExceptionPdu(fc, ModbusDataStore::kExceptionIllegalValue);
                const uint16_t addr      = be16(&pdu[1]);
                const uint16_t qty       = be16(&pdu[3]);
                const uint8_t byteCount  = pdu[5];
                if (qty == 0 || qty > 123 || byteCount != qty * 2 || pdu.size() != 6 + byteCount) {
                    return buildExceptionPdu(fc, ModbusDataStore::kExceptionIllegalValue);
                }
                std::vector<uint16_t> values(qty);
                for (uint16_t i = 0; i < qty; ++i) {
                    values[i] = be16(&pdu[6 + i * 2]);
                }
                const uint8_t exc = store.writeMultipleRegisters(addr, values);
                if (exc != ModbusDataStore::kExceptionNone) return buildExceptionPdu(fc, exc);

                std::vector<uint8_t> resp{ fc };
                putBe16(resp, addr);
                putBe16(resp, qty);
                return resp;
            }

            default:
                return buildExceptionPdu(fc, kExceptionIllegalFunction);
        }
    }

    void handleClient(int clientFd, std::string peerLabel, ModbusDataStore* store)
    {
        std::printf("[+] client connected: %s\n", peerLabel.c_str());

        while (g_running) {
            uint8_t prefix[kMbapPrefixSize];
            if (!readFull(clientFd, prefix, kMbapPrefixSize)) break;

            const uint16_t txnId          = be16(&prefix[0]);
            const uint16_t protocolId     = be16(&prefix[2]);
            const uint16_t followingLength = be16(&prefix[4]);

            if (protocolId != 0x0000 || followingLength == 0 || followingLength > 253) {
                std::printf("[!] %s: malformed MBAP header, closing\n", peerLabel.c_str());
                break;
            }

            std::vector<uint8_t> rest(followingLength);
            if (!readFull(clientFd, rest.data(), rest.size())) break;

            const uint8_t unitId = rest[0];
            std::vector<uint8_t> pdu(rest.begin() + 1, rest.end());

            std::vector<uint8_t> reqAdu(prefix, prefix + kMbapPrefixSize);
            reqAdu.insert(reqAdu.end(), rest.begin(), rest.end());
            if (g_verbose) {
                std::printf("[>] %s txn=%u unit=%u fc=0x%02X\n", peerLabel.c_str(), txnId, unitId, pdu.empty() ? 0 : pdu[0]);
            }
            hexdump("REQ", reqAdu);

            std::vector<uint8_t> respPdu = handlePdu(*store, pdu);
            std::vector<uint8_t> respAdu = buildResponseAdu(txnId, unitId, respPdu);
            hexdump("RESP", respAdu);

            if (!writeFull(clientFd, respAdu.data(), respAdu.size())) break;
        }

        std::printf("[-] client disconnected: %s\n", peerLabel.c_str());
        ::close(clientFd);
    }

    void onSignal(int) { g_running = false; }
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0); // line-buffered even when redirected to a file/pipe

    uint16_t port = 5020; // non-privileged default; pass --port 502 (as root) for the standard port
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-v" || arg == "--verbose") {
            g_verbose = true;
        } else if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (arg == "-h" || arg == "--help") {
            std::printf("Usage: %s [-p|--port <port>] [-v|--verbose]\n", argv[0]);
            std::printf("  Default port: 5020 (use --port 502 as root for the standard Modbus port)\n");
            return 0;
        }
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    std::signal(SIGPIPE, SIG_IGN); // a client closing mid-write must not kill the server

    ModbusDataStore store;
    // A little seed data, so a fresh server has something interesting to
    // read right away — mirrors the values used in modbus_plugin's own
    // empirical test during development.
    store.seedCoils(0, {true, false, true, true, false, false, false, true});
    store.seedHoldingRegisters(100, {12, 34, 56, 78});

    const int listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0) {
        std::perror("socket");
        return 1;
    }
    int reuse = 1;
    ::setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (::bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::perror("bind");
        ::close(listenFd);
        return 1;
    }
    if (::listen(listenFd, 16) != 0) {
        std::perror("listen");
        ::close(listenFd);
        return 1;
    }

    std::printf("modbus_server: listening on 0.0.0.0:%u%s\n", port, g_verbose ? " (verbose)" : "");
    std::printf("  seeded: coils 0-7 = 1,0,1,1,0,0,0,1 ; holding registers 100-103 = 12,34,56,78\n");

    std::vector<std::thread> clientThreads;
    while (g_running) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        const int clientFd = ::accept(listenFd, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
        if (clientFd < 0) {
            if (!g_running) break;
            continue;
        }

        char ipStr[INET_ADDRSTRLEN];
        ::inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
        std::string peerLabel = std::string(ipStr) + ":" + std::to_string(ntohs(clientAddr.sin_port));

        int nodelay = 1;
        ::setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

        clientThreads.emplace_back(handleClient, clientFd, peerLabel, &store);
        clientThreads.back().detach();
    }

    ::close(listenFd);
    std::printf("modbus_server: shutting down\n");
    return 0;
}
