#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

#include "DeviceHandling.hpp"         // STL-based device manager
#include "string_handling.hpp"        // For string_print_vector_content()
#include "time_handling.hpp"          // For time_sleep()
#include "dlt.h"                      // Logging

namespace uart {

constexpr std::size_t TargetPathSize = 256;
constexpr int MaxPortCount = 255;

#ifndef _WIN32
static const std::vector<std::string> PortPatterns{ "/dev/ttyACM*", "/dev/ttyUSB*" };
#endif

class UartMonitor {
public:
    UartMonitor(uint32_t pollingIntervalMs)
        : pollingInterval(pollingIntervalMs) {}

    void listPorts(const std::string& caption = "Ports") const {
        std::vector<std::string> ports = scanSystemPorts();
        string_print_vector_content(caption.c_str(), LT_HDR, DLT_LOG_INFO, ports, " | ");
    }

    uint32_t countAvailablePorts() const {
#ifdef _WIN32
        char targetPath[TargetPathSize] = {};
        char portName[MAX_ITEM_SIZE] = {};
        uint32_t count = 0;
        for (int i = 1; i <= MaxPortCount; ++i) {
            std::snprintf(portName, sizeof(portName), "COM%d", i);
            if (QueryDosDevice(portName, targetPath, TargetPathSize)) {
                ++count;
            }
        }
        return count;
#else
        return static_cast<uint32_t>(scanSystemPorts().size());
#endif
    }

    void startMonitoring(std::atomic<bool>& runFlag) {
        if (pollingInterval == 0) {
            DLT_LOG(UartPortCtx, DLT_LOG_ERROR, DLT_HDR; DLT_STRING("Polling interval cannot be zero"));
            return;
        }

        handler.init();

        // Initial scan
        auto initialPorts = scanSystemPorts();
        for (const auto& port : initialPorts) {
            std::string inserted;
            handler.process(port, inserted, OperationType::Insert);
        }

        // Monitoring loop
        while (runFlag.load()) {
            time_sleep(pollingInterval);
            handler.resetAllFlags();

            auto currentPorts = scanSystemPorts();

            // Detect insertions
            for (const auto& port : currentPorts) {
                std::string inserted;
                if (handler.process(port, inserted, OperationType::Insert)) {
                    std::string caption = "(T) Inserted [" + inserted + "] =>";
                    listPorts(caption);
                }
            }

            // Detect removals
            std::string removed;
            while (handler.getRemoved(removed)) {
                std::string caption = "(T) Removed [" + removed + "] =>";
                listPorts(caption);
            }
        }
    }

private:
    uint32_t pollingInterval;
    DeviceHandling handler;

    std::vector<std::string> scanSystemPorts() const {
        std::vector<std::string> ports;

#ifdef _WIN32
        char targetPath[TargetPathSize] = {};
        char portName[MAX_ITEM_SIZE] = {};

        for (int i = 1; i <= MaxPortCount; ++i) {
            std::snprintf(portName, sizeof(portName), "COM%d", i);
            if (QueryDosDevice(portName, targetPath, TargetPathSize)) {
                ports.push_back(portName);
            }
        }
#else
        for (const auto& pattern : PortPatterns) {
            for (const auto& name : glob({pattern})) {
                ports.push_back(name);
            }
        }
#endif
        return ports;
    }
};

} // namespace uart


#if 0

#include "UartMonitor.hpp"
#include <iostream>
#include <atomic>
#include <thread>

int main() {
    // Create an atomic flag to control the monitoring loop
    std::atomic<bool> runMonitor{true};

    // Define polling interval in milliseconds
    constexpr uint32_t pollingIntervalMs = 1000;

    // Create an instance of UartMonitor
    uart::UartMonitor monitor(pollingIntervalMs);

    // List all currently available ports
    monitor.listPorts("Initial UART Ports");

    // Start monitoring in a separate thread
    std::thread monitorThread([&]() {
        monitor.startMonitoring(runMonitor);
    });

    // Let it monitor for 10 seconds
    std::this_thread::sleep_for(std::chrono::seconds(10));

    // Stop monitoring
    runMonitor = false;
    monitorThread.join();

    std::cout << "Monitoring stopped.\n";
    return 0;
}


#endif