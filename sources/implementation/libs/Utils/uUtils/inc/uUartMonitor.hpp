#ifndef UUART_MONITORING_HPP
#define UUART_MONITORING_HPP

#include "uDeviceHandling.hpp"

#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <algorithm>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#endif

///////////////////////////////////////////////////////////////////
//                     LOG DEFINES                               //
///////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#define LT_HDR     "UARTMON    :"
#define LOG_HDR    LOG_STRING(LT_HDR)


///////////////////////////////////////////////////////////////////
//         CLASS DECLARATION AND IMPLEMENTATION                  //
///////////////////////////////////////////////////////////////////


constexpr std::size_t TargetPathSize = 256;
constexpr int MaxPortCount = 255;

#ifndef _WIN32
static const std::vector<std::string> PortPatterns{ "/dev/ttyACM*", "/dev/ttyUSB*" };
#endif

class UartMonitor
{

public:

    UartMonitor() : pollingInterval(500), monitoringActive(false) {}

    ~UartMonitor() {
        stopMonitoring();
        if (monitorThread.joinable()) {
            monitorThread.join();
        }
    }

    void setPollingInterval(uint32_t intervalMs) {
        pollingInterval = intervalMs;
    }

    std::string getPortString() const {
        std::lock_guard<std::mutex> lock(deviceMutex_);
        std::ostringstream oss;
        bool first = true;

        for (const auto& [port, device] : devices_) {
            if (!first) oss << " | ";
            oss << port;
            first = false;
        }

        return oss.str();
    }

    void startMonitoring() {
        monitoringActive.store(true);
        monitorThread = std::thread([this]() {
            monitorLoop();
        });
    }

    void stopMonitoring() {
        monitoringActive.store(false);
        cvInsert.notify_all();
        cvRemove.notify_all();
    }

    std::string waitForInsert(std::optional<std::chrono::milliseconds> timeout = std::nullopt) {
        std::unique_lock<std::mutex> lock(mutex);

        if (timeout.has_value()) {
            if (cvInsert.wait_for(lock, timeout.value(), [this] { return !insertedPorts.empty() || !monitoringActive.load(); })) {
                if (!insertedPorts.empty()) {
                    std::string port = insertedPorts.front();
                    insertedPorts.pop_front();
                    return port;
                }
            }
            return ""; // timed out or monitoring stopped
        } else {
            cvInsert.wait(lock, [this] { return !insertedPorts.empty() || !monitoringActive.load(); });
            if (!insertedPorts.empty()) {
                std::string port = insertedPorts.front();
                insertedPorts.pop_front();
                return port;
            }
            return ""; // monitoring stopped but no insertion
        }
    }

    std::string waitForRemoval(std::optional<std::chrono::milliseconds> timeout = std::nullopt) {
        std::unique_lock<std::mutex> lock(mutex);

        if (timeout.has_value()) {
            if (cvRemove.wait_for(lock, timeout.value(), [this] { return !removedPorts.empty() || !monitoringActive.load(); })) {
                if (!removedPorts.empty()) {
                    std::string port = removedPorts.front();
                    removedPorts.pop_front();
                    return port;
                }
            }
            return ""; // timed out or monitoring stopped
        } else {
            cvRemove.wait(lock, [this] { return !removedPorts.empty() || !monitoringActive.load(); });
            if (!removedPorts.empty()) {
                std::string port = removedPorts.front();
                removedPorts.pop_front();
                return port;
            }
            return ""; // monitoring stopped but no removal
        }
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

private:

    uint32_t pollingInterval;
    std::atomic<bool> monitoringActive;
    std::thread monitorThread;
    DeviceHandling handler;

    std::mutex mutex;
    std::condition_variable cvInsert;
    std::condition_variable cvRemove;
    std::deque<std::string> insertedPorts;
    std::deque<std::string> removedPorts;

    void monitorLoop() {
        handler.init();

        auto knownPorts = scanSystemPorts();
        for (const auto& port : knownPorts) {
            std::string inserted;
            handler.process(port, inserted, OperationType::Insert);
        }

        while (monitoringActive.load()) {
            time_sleep(pollingInterval);
            handler.resetAllFlags();

            auto currentPorts = scanSystemPorts();

            {
                std::lock_guard<std::mutex> lock(mutex);

                for (const auto& port : currentPorts) {
                    std::string inserted;
                    if (handler.process(port, inserted, OperationType::Insert)) {
                        insertedPorts.push_back(inserted);
                        cvInsert.notify_all();
                    }
                }

                std::string removed;
                while (handler.getRemoved(removed)) {
                    removedPorts.push_back(removed);
                    cvRemove.notify_all();
                }
            }
        }
    }

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

#endif //UUART_MONITORING_HPP



#if 0

#include "uUartMonitor.hpp"
#include <iostream>
#include <atomic>
#include <thread>

UartMonitor monitor;
monitor.setPollingInterval(1000);
monitor.startMonitoring();

std::thread waitInsertThread([&]() {
    std::string port = monitor.waitForInsert();
    if (!port.empty()) {
        std::cout << "Insert detected: " << port << '\n';
    }
});

std::thread waitRemoveThread([&]() {
    std::string port = monitor.waitForRemoval();
    if (!port.empty()) {
        std::cout << "Removal detected: " << port << '\n';
    }
});

// After some time…
monitor.stopMonitoring();

waitInsertThread.join();
waitRemoveThread.join();


#endif