#pragma once

#include <vector>
#include <string>
#include <algorithm>
#include <cstddef>

enum class OperationType {
    Insert,
    Remove
};

class DeviceHandling
{
private:
    struct DeviceEntry {
        std::string name;
        bool isRemoved = false; // renamed for clarity
    };

    std::vector<DeviceEntry> deviceList;
    static constexpr std::size_t MaxListSize = 100;

    int findItemIndex(const std::string& item) const {
        auto it = std::find_if(deviceList.begin(), deviceList.end(),
            [&item](const DeviceEntry& entry) {
                return entry.name == item;
            });
        
        if (it != deviceList.end()) {
            return static_cast<int>(std::distance(deviceList.begin(), it));
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
                deviceList[idx].isRemoved = true;
            }
        }

        return updated;
    }

    bool getRemoved(std::string& output) {
        auto it = std::find_if(deviceList.begin(), deviceList.end(),
            [](const DeviceEntry& entry) {
                return !entry.name.empty() && entry.isRemoved;
            });

        if (it != deviceList.end()) {
            output = it->name;
            it->name.clear(); // Clear name to mark as processed
            return true;
        }

        return false;
    }

    bool getAdded(std::string& output) {
        auto it = std::find_if(deviceList.begin(), deviceList.end(),
            [](const DeviceEntry& entry) {
                return !entry.name.empty() && !entry.isRemoved;
            });

        if (it != deviceList.end()) {
            output = it->name;
            it->name.clear(); // Clear name to mark as processed
            return true;
        }

        return false;
    }

    void resetAllFlags() {
        for (auto& entry : deviceList) {
            entry.isRemoved = false;
        }
    }
};
