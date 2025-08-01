#pragma once

#include <vector>
#include <string>
#include <algorithm>

enum class OperationType {
    Insert,
    Remove
};

class DeviceHandling
{

    private:

        struct DeviceEntry {
            std::string name;
            bool flag = false;
        };

        std::vector<DeviceEntry> deviceList;
        static constexpr std::size_t MaxListSize = 100;

        int findItemIndex(const std::string& item) const {
            for (size_t i = 0; i < deviceList.size(); ++i) {
                if (deviceList[i].name == item) {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }

        bool insertItem(const std::string& item) {
            if (findItemIndex(item) == -1 && deviceList.size() < MaxListSize) {
                deviceList.push_back({ item, false });
                return true;
            }
            return false;
        }

    public:

        void init() {
            deviceList.clear();
        }

        bool process(const std::string& input, std::string& output, OperationType opType) {
            bool updated = false;

            if (opType == OperationType::Insert) {
                if (insertItem(input)) {
                    output = input;
                    updated = true;
                }
            } else {
                int idx = findItemIndex(input);
                if (idx != -1) {
                    deviceList[idx].flag = true;
                }
            }

            return updated;
        }

        bool getRemoved(std::string& output) {
            auto it = std::find_if(deviceList.begin(), deviceList.end(),
                [](const DeviceEntry& entry) {
                    return !entry.name.empty() && !entry.flag;
                });

            if (it != deviceList.end()) {
                output = it->name;
                it->name.clear(); // mark as removed
                return true;
            }

            return false;
        }

        void resetAllFlags() {
            for (auto& entry : deviceList) {
                entry.flag = false;
            }
        }
};


#if 0

#include "DeviceHandling.hpp"
#include <iostream>

int main() {
    DeviceHandling handler;
    handler.init(); // Start fresh

    std::string output;

    // Add new devices
    if (handler.process("Camera", output, OperationType::Insert)) {
        std::cout << "Inserted: " << output << '\n';
    }

    if (handler.process("Microphone", output, OperationType::Insert)) {
        std::cout << "Inserted: " << output << '\n';
    }

    // Remove one device (mark for removal)
    handler.process("Camera", output, OperationType::Remove);

    // Get removed device (actually remove it and return its name)
    if (handler.getRemoved(output)) {
        std::cout << "Removed: " << output << '\n';
    }

    // Reset all flags
    handler.resetAllFlags();
}


#endif