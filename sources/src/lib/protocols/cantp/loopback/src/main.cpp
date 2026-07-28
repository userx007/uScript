#include "TpFactory.hpp"
#include "TpConfig.hpp"
#include "ITransportProtocol.hpp"
#include "RealCommDriver.hpp"
#include "LoopbackCommDriver.hpp"

#include <cstdio>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <csignal>

constexpr uint32_t DEFAULT_RX_ID = 0x100;
constexpr uint32_t DEFAULT_TX_ID = DEFAULT_RX_ID + 1;

std::atomic<bool> g_running{true};

void signal_handler(int) {
    g_running = false;
}

void print_hex(const char* label, const std::vector<uint8_t>& data, size_t max_len = 8) {
    std::printf("%s: ", label);
    size_t limit = std::min(data.size(), max_len);
    for (size_t i = 0; i < limit; ++i) {
        std::printf("%02X ", data[i]);
    }
    if (data.size() > max_len) {
        std::printf("... (%zu bytes total)", data.size());
    }
    std::printf("\n");
}

int main(int argc, char **argv)
{
    std::string protocol = "isotp";
    std::string iface = "vcan0";
    bool verbose = false;
    bool isSim = false;
    uint32_t rxId = DEFAULT_RX_ID;
    uint32_t txId = DEFAULT_TX_ID;

    if (argc > 1) {
        protocol = argv[1];
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--sim") isSim = true;
            else if (arg == "--iface" && i+1 < argc) iface = argv[++i];
            else if (arg == "--verbose") verbose = true;
            else if (arg == "--rx" && i+1 < argc) rxId = std::stoul(argv[++i]);
        }
    }

    printf("=== CAN-TP Sequential Echo Server ===\n");
    printf("Protocol : %s\n", protocol.c_str());
    printf("Mode     : %s\n", isSim ? "SIMULATION" : ("REAL (IFACE: " + iface + ")").c_str());
    printf("RX/TX ID : 0x%03X\n", rxId);
    printf("Listening...\n\n");

    std::unique_ptr<ICommDriver> driver;
    if (isSim) {
        driver = std::make_unique<LoopbackCommDriver>(verbose);
    } else {
        driver = std::make_unique<RealCommDriver>(iface);
        if (!driver->is_open()) {
            fprintf(stderr, "Failed to open CAN interface: %s\n", iface.c_str());
            return 1;
        }
    }

    TpConfig cfg;
    if (protocol == "j1939-bam") cfg.j1939UseBam = true;
    else if (protocol == "j1939-rtscts") cfg.j1939UseBam = false;
    else if (protocol == "canopen") cfg.canOpenUseBlock = false;

    std::unique_ptr<ITransportProtocol> tp;
    bool useRawDriver = false;

    if (protocol == "none") {
        useRawDriver = true;
    } else {
        TpProtocol p;
        if (protocol == "isotp") p = TpProtocol::ISO_TP;
        else if (protocol == "j1939-bam" || protocol == "j1939-rtscts") p = TpProtocol::J1939_TP;
        else if (protocol == "canopen") p = TpProtocol::CANOPEN_SDO;
        else if (protocol == "nmea2000") p = TpProtocol::NMEA2000_FAST_PACKET;
        else {
            fprintf(stderr, "Unknown protocol: %s\n", protocol.c_str());
            return 1;
        }
        tp = make_transport_protocol(p, cfg);
    }

    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);

    const uint32_t TIMEOUT_MS = 5000;

    std::vector<uint8_t> rx_buffer(1500, 0);
    std::vector<uint8_t> tx_buffer(1500, 0);

    while (g_running) {
        ICommDriver::ReadResult rr;

        // 1. Wait for PDU from External Tester (on RX_ID)
        printf("[RX] Waiting for PDU on 0x%03X...\n", rxId);

        rr = tp->receive(
            *driver,
            TIMEOUT_MS,
            std::span<uint8_t>(rx_buffer),
            std::to_string(rxId), // Filter for RX
            std::to_string(txId)  // TX ID used by TP
        );

        if (rr.status != ICommDriver::Status::SUCCESS) {
            if (rr.status == ICommDriver::Status::READ_TIMEOUT) {
                printf("[RX] Timeout.\n");
                continue;
            } else {
                fprintf(stderr, "[RX] Error: %s\n", ICommDriver::to_string(rr.status).c_str());
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
        }

        size_t payload_len = rr.bytes_read;
        printf("[RX] Received PDU: %zu bytes\n", payload_len);
        print_hex("  Payload", rx_buffer, payload_len > 8 ? 8 : payload_len);

        // 2. Echo back to External Tester (on TX_ID)
        printf("[TX] Sending PDU to 0x%03X...\n", txId);

        tp->send(
            *driver,
            TIMEOUT_MS,
            std::span<const uint8_t>(tx_buffer.begin(), tx_buffer.begin() + payload_len),
            std::to_string(txId), // Send to TX
            std::to_string(rxId)  // RX ID used by TP
        );

        printf("[TX] Sent.\n");
    }

    printf("Stopped.\n");
    return 0;
}
