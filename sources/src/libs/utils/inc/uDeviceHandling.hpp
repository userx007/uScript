#ifndef DEVICE_HANDLING_HPP
#define DEVICE_HANDLING_HPP

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

enum class OperationType
{
    Insert,
    Remove
};

class DeviceHandling
{

    private:
        struct DeviceEntry
        {
                std::string name;
                bool isRemoved = false; // renamed for clarity
        };

        std::vector<DeviceEntry> deviceList;
        static constexpr std::size_t MaxListSize = 100;

        /** Returns the index of @p strItem in the device list, or -1 if absent. */
        int findItemIndex(const std::string &strItem) const
        {
            auto it = std::find_if(deviceList.begin(), deviceList.end(),
                                   [&strItem](const DeviceEntry &entry) {
                                       return entry.name == strItem;
                                   });

            if (it != deviceList.end()) {
                return static_cast<int>(std::distance(deviceList.begin(), it));
            }
            return -1;
        }

        /** Adds @p strItem unless already present or the list is full (MaxListSize); returns whether it was added. */
        bool insertItem(const std::string &strItem)
        {
            if (findItemIndex(strItem) == -1 && deviceList.size() < MaxListSize) {
                deviceList.push_back({strItem, false});
                return true;
            }
            return false;
        }

    public:
        /** Clears the device list. */
        void init()
        {
            deviceList.clear();
        }

        /** Applies an insert or remove event for @p strInput; returns true only if a new item was inserted (echoed in @p strOutput). */
        bool process(const std::string &strInput, std::string &strOutput, OperationType eOpType)
        {
            bool updated = false;

            if (eOpType == OperationType::Insert) {
                if (insertItem(strInput)) {
                    strOutput = strInput;
                    updated   = true;
                }
            } else {
                int idx = findItemIndex(strInput);
                if (idx != -1) {
                    deviceList[idx].isRemoved = true;
                }
            }

            return updated;
        }

        /** Pops the next removed device name into @p strOutput; returns false if none is pending. */
        bool getRemoved(std::string &strOutput)
        {
            auto it = std::find_if(deviceList.begin(), deviceList.end(),
                                   [](const DeviceEntry &entry) {
                                       return !entry.name.empty() && entry.isRemoved;
                                   });

            if (it != deviceList.end()) {
                strOutput = it->name;
                it->name.clear(); // Clear name to mark as processed
                return true;
            }

            return false;
        }

        /** Pops the next added device name into @p strOutput; returns false if none is pending. */
        bool getAdded(std::string &strOutput)
        {
            auto it = std::find_if(deviceList.begin(), deviceList.end(),
                                   [](const DeviceEntry &entry) {
                                       return !entry.name.empty() && !entry.isRemoved;
                                   });

            if (it != deviceList.end()) {
                strOutput = it->name;
                it->name.clear(); // Clear name to mark as processed
                return true;
            }

            return false;
        }

        /** Clears the removed flag on every entry. */
        void resetAllFlags()
        {
            for (auto &entry : deviceList) {
                entry.isRemoved = false;
            }
        }
};

#endif // DEVICE_HANDLING_HPP
