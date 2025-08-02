#ifndef UUART_MONITORING_HPP
#define UUART_MONITORING_HPP

#include "uDeviceHandling.hpp"

#include <vector>
#include <string>
#include <unordered_map>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <algorithm>
#include <sstream>
#include <optional>
#include <chrono>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <glob.h>
#endif


///////////////////////////////////////////////////////////////////
//         CLASS DECLARATION AND IMPLEMENTATION                  //
///////////////////////////////////////////////////////////////////


constexpr std::size_t TargetPathSize = 256;
constexpr int MaxPortCount = 255;
constexpr std::size_t MAX_ITEM_SIZE = 256;

#ifndef _WIN32
static const std::vector<std::string> PortPatterns{ "/dev/ttyACM*", "/dev/ttyUSB*" };

// POSIX glob helper
inline std::vector<std::string> glob(const std::string& pattern) {
    glob_t result;
    std::vector<std::string> matches;
    if (::glob(pattern.c_str(), 0, nullptr, &result) == 0) {
        for (size_t i = 0; i < result.gl_pathc; ++i) {
            matches.emplace_back(result.gl_pathv[i]);
        }
    }
    globfree(&result);
    return matches;
}
#endif

class UartMonitor {
public:
    UartMonitor() : pollingInterval(500), monitoringActive(false) {}

    ~UartMonitor() {
        stopMonitoring();
        if (monitorThread.joinable()) {
            monitorThread.join();
        }
    }

    void setPollingInterval(uint32_t intervalMs) const {
        pollingInterval = intervalMs;
    }

    std::string listPorts() const {
        const auto ports = scanSystemPorts();
        std::ostringstream oss;
        for (size_t i = 0; i < ports.size(); ++i) {
            oss << ports[i];
            if (i < ports.size() - 1)
                oss << ' ';
        }
        return oss.str();
    }

    void startMonitoring() const {
        monitoringActive.store(true);
        monitorThread = std::thread([this]() {
            monitorLoop();
        });
    }

    void stopMonitoring() const {
        monitoringActive.store(false);
        cvInsert.notify_all();
        cvRemove.notify_all();
    }

    std::string waitForInsert(std::optional<std::chrono::milliseconds> timeout = std::nullopt) const {
        std::unique_lock<std::mutex> lock(mutex);
        if (timeout.has_value()) {
            if (cvInsert.wait_for(lock, timeout.value(), [this] { return !insertedPorts.empty() || !monitoringActive.load(); })) {
                if (!insertedPorts.empty()) {
                    std::string port = insertedPorts.front();
                    insertedPorts.pop_front();
                    return port;
                }
            }
            return "";
        } else {
            cvInsert.wait(lock, [this] { return !insertedPorts.empty() || !monitoringActive.load(); });
            if (!insertedPorts.empty()) {
                std::string port = insertedPorts.front();
                insertedPorts.pop_front();
                return port;
            }
            return "";
        }
    }

    std::string waitForRemoval(std::optional<std::chrono::milliseconds> timeout = std::nullopt) const {
        std::unique_lock<std::mutex> lock(mutex);
        if (timeout.has_value()) {
            if (cvRemove.wait_for(lock, timeout.value(), [this] { return !removedPorts.empty() || !monitoringActive.load(); })) {
                if (!removedPorts.empty()) {
                    std::string port = removedPorts.front();
                    removedPorts.pop_front();
                    return port;
                }
            }
            return "";
        } else {
            cvRemove.wait(lock, [this] { return !removedPorts.empty() || !monitoringActive.load(); });
            if (!removedPorts.empty()) {
                std::string port = removedPorts.front();
                removedPorts.pop_front();
                return port;
            }
            return "";
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

    mutable uint32_t pollingInterval;
    mutable std::atomic<bool> monitoringActive;
    mutable std::thread monitorThread;

    mutable std::mutex mutex;
    mutable std::condition_variable cvInsert;
    mutable std::condition_variable cvRemove;
    mutable std::deque<std::string> insertedPorts;
    mutable std::deque<std::string> removedPorts;

    mutable std::mutex deviceMutex_;
    mutable DeviceHandling handler;

    void monitorLoop() const {
        handler.init();
        auto knownPorts = scanSystemPorts();
        for (const auto& port : knownPorts) {
            std::string inserted;
            handler.process(port, inserted, OperationType::Insert);
        }

        while (monitoringActive.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(pollingInterval));
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
            for (const auto& name : glob(pattern)) {
                ports.push_back(name);
            }
        }
#endif
        return ports;
    }


};

#endif // UUART_MONITORING_HPP
