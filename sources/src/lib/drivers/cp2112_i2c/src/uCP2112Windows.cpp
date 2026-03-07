#include "uCP2112.hpp"
#include "uLogger.hpp"

// Windows HID API
// Link against: hid.lib (MSVC) / -lhid (MinGW), setupapi.lib / -lsetupapi
//
// hidsdi.h already carries its own extern "C" guards on both MSVC and
// MinGW-w64 — do NOT wrap it again or MSVC will emit a C2732 error.
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <cstring>
#include <string>
#include <vector>


#define LT_HDR  "CP2112_DRIVER|"
#define LOG_HDR LOG_STRING(LT_HDR)


// ============================================================================
// Internal helpers
// ============================================================================

namespace {

/**
 * @brief Find the device path of the n-th CP2112 on the system
 *
 * Enumerates the HID device class via SetupAPI, checks VID/PID via
 * HidD_GetAttributes, and returns the device path for the requested index.
 *
 * @param deviceIndex  Zero-based index among all connected CP2112 devices
 * @param pathOut      Receives the device path string on success
 * @return true if found
 */
static bool find_cp2112_path(uint8_t deviceIndex, std::string& pathOut)
{
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO deviceInfoSet = SetupDiGetClassDevs(
        &hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        return false;
    }

    SP_DEVICE_INTERFACE_DATA ifaceData;
    ifaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    uint8_t  matchCount = 0;
    bool     found      = false;

    for (DWORD memberIdx = 0;
         SetupDiEnumDeviceInterfaces(deviceInfoSet, nullptr, &hidGuid, memberIdx, &ifaceData);
         ++memberIdx)
    {
        // Get required buffer size
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetail(
            deviceInfoSet, &ifaceData, nullptr, 0, &requiredSize, nullptr);

        if (requiredSize == 0) continue;

        // Allocate detail buffer as a plain byte vector — avoids UB from
        // casting operator-new storage to an unrelated struct type.
        std::vector<uint8_t> detailBuf(requiredSize, 0);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA*>(detailBuf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

        if (!SetupDiGetDeviceInterfaceDetail(
                deviceInfoSet, &ifaceData, detail, requiredSize, nullptr, nullptr))
        {
            continue;
        }

        // Open device temporarily to read attributes
        HANDLE hDev = CreateFile(
            detail->DevicePath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);

        if (hDev != INVALID_HANDLE_VALUE) {
            HIDD_ATTRIBUTES attrs;
            attrs.Size = sizeof(HIDD_ATTRIBUTES);

            if (HidD_GetAttributes(hDev, &attrs) &&
                attrs.VendorID  == CP2112::CP2112_VID &&
                attrs.ProductID == CP2112::CP2112_PID)
            {
                if (matchCount == deviceIndex) {
                    pathOut = detail->DevicePath;
                    found   = true;
                    CloseHandle(hDev);
                    break;
                }
                ++matchCount;
            }

            CloseHandle(hDev);
        }
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    return found;
}

} // anonymous namespace


// ============================================================================
// open / close
// ============================================================================

CP2112::Status CP2112::open(uint8_t u8I2CAddress, uint32_t u32ClockHz, uint8_t u8DeviceIndex)
{
    if (u32ClockHz == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Invalid clock speed"));
        return Status::INVALID_PARAM;
    }

    // Locate the device path for the requested CP2112 index
    std::string devicePath;
    if (!find_cp2112_path(u8DeviceIndex, devicePath)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("CP2112 device index"); LOG_UINT32(u8DeviceIndex);
                  LOG_STRING("not found (VID=0x10C4, PID=0xEA90)"));
        return Status::PORT_ACCESS;
    }

    // Open the device with overlapped I/O so hid_interrupt_read can use a timeout
    m_hDevice = CreateFile(
        devicePath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        nullptr);

    if (m_hDevice == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("CreateFile failed for CP2112, error:"); LOG_UINT32(err));
        return Status::PORT_ACCESS;
    }

    m_u8I2CAddress = u8I2CAddress;

    // Configure I2C clock and SMBus parameters
    Status result = configure_smbus(u32ClockHz);
    if (result != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Failed to configure SMBus clock"); LOG_UINT32(u32ClockHz));
        CloseHandle(m_hDevice);
        m_hDevice = INVALID_HANDLE_VALUE;
        return result;
    }

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("CP2112 opened: index="); LOG_UINT32(u8DeviceIndex);
              LOG_STRING("I2C addr=0x"); LOG_HEX8(u8I2CAddress);
              LOG_STRING("clock="); LOG_UINT32(u32ClockHz));

    return Status::SUCCESS;
}


CP2112::Status CP2112::close()
{
    if (m_hDevice != INVALID_HANDLE_VALUE) {
        (void)cancel_transfer();
        CloseHandle(m_hDevice);
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("CP2112 closed"));
        m_hDevice = INVALID_HANDLE_VALUE;
    }
    return Status::SUCCESS;
}


// ============================================================================
// Low-level HID I/O
// ============================================================================

/**
 * @brief Send a HID Feature report to the CP2112
 *
 * Uses HidD_SetFeature.
 * buf[0] = report ID; total length = HID_REPORT_SIZE (64 bytes).
 */
CP2112::Status CP2112::hid_set_feature(const uint8_t* buf, size_t len) const
{
    if (!buf || len != HID_REPORT_SIZE) {
        return Status::INVALID_PARAM;
    }

    if (!HidD_SetFeature(m_hDevice,
                         const_cast<PVOID>(reinterpret_cast<const void*>(buf)),
                         static_cast<ULONG>(len)))
    {
        DWORD err = GetLastError();
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("HidD_SetFeature failed, report=0x"); LOG_HEX8(buf[0]);
                  LOG_STRING("error:"); LOG_UINT32(err));
        return Status::WRITE_ERROR;
    }

    return Status::SUCCESS;
}


/**
 * @brief Receive a HID Feature report from the CP2112
 *
 * Uses HidD_GetFeature.
 * buf[0] must be set to the desired report ID on entry.
 * On return, buf contains the full 64-byte response (buf[0] = report ID).
 */
CP2112::Status CP2112::hid_get_feature(uint8_t* buf, size_t len) const
{
    if (!buf || len != HID_REPORT_SIZE) {
        return Status::INVALID_PARAM;
    }

    if (!HidD_GetFeature(m_hDevice,
                         reinterpret_cast<PVOID>(buf),
                         static_cast<ULONG>(len)))
    {
        DWORD err = GetLastError();
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("HidD_GetFeature failed, report=0x"); LOG_HEX8(buf[0]);
                  LOG_STRING("error:"); LOG_UINT32(err));
        return Status::READ_ERROR;
    }

    return Status::SUCCESS;
}


/**
 * @brief Write a HID Interrupt OUT report to the CP2112
 *
 * Uses WriteFile with overlapped I/O (device was opened with FILE_FLAG_OVERLAPPED).
 * buf[0] = report ID (0x0D for Data Write); length = 64 bytes.
 */
CP2112::Status CP2112::hid_interrupt_write(const uint8_t* buf, size_t len) const
{
    if (!buf || len != HID_REPORT_SIZE) {
        return Status::INVALID_PARAM;
    }

    OVERLAPPED ov;
    std::memset(&ov, 0, sizeof(ov));
    ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) {
        return Status::WRITE_ERROR;
    }

    DWORD written = 0;
    BOOL  ok = WriteFile(m_hDevice, buf, static_cast<DWORD>(len), &written, &ov);

    if (!ok && GetLastError() != ERROR_IO_PENDING) {
        DWORD err = GetLastError();
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("WriteFile failed, error:"); LOG_UINT32(err));
        CloseHandle(ov.hEvent);
        return Status::WRITE_ERROR;
    }

    // Wait for the write to complete (no timeout — writes to HID are fast)
    if (WaitForSingleObject(ov.hEvent, CP2112_WRITE_DEFAULT_TIMEOUT) == WAIT_TIMEOUT) {
        CancelIo(m_hDevice);
        CloseHandle(ov.hEvent);
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("WriteFile overlapped timed out"));
        return Status::WRITE_TIMEOUT;
    }

    GetOverlappedResult(m_hDevice, &ov, &written, FALSE);
    CloseHandle(ov.hEvent);

    if (written != static_cast<DWORD>(len)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("WriteFile short write:"); LOG_UINT32(written));
        return Status::WRITE_ERROR;
    }

    return Status::SUCCESS;
}


/**
 * @brief Read one HID Interrupt IN report from the CP2112 with a timeout
 *
 * Uses ReadFile with overlapped I/O + WaitForSingleObject for the timeout.
 * On success, buf[0] = report ID (0x0C for Data Read Response).
 *
 * @param timeoutMs  How long to wait for data; returns READ_TIMEOUT if exceeded.
 */
CP2112::Status CP2112::hid_interrupt_read(uint8_t* buf, size_t len,
                                           uint32_t timeoutMs, size_t& bytesRead) const
{
    if (!buf || len != HID_REPORT_SIZE) {
        return Status::INVALID_PARAM;
    }

    bytesRead = 0;

    OVERLAPPED ov;
    std::memset(&ov, 0, sizeof(ov));
    ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!ov.hEvent) {
        return Status::READ_ERROR;
    }

    DWORD dwRead = 0;
    BOOL  ok = ReadFile(m_hDevice, buf, static_cast<DWORD>(len), &dwRead, &ov);

    if (!ok && GetLastError() != ERROR_IO_PENDING) {
        DWORD err = GetLastError();
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("ReadFile failed, error:"); LOG_UINT32(err));
        CloseHandle(ov.hEvent);
        return Status::READ_ERROR;
    }

    DWORD waitResult = WaitForSingleObject(ov.hEvent, static_cast<DWORD>(timeoutMs));

    if (waitResult == WAIT_TIMEOUT) {
        CancelIo(m_hDevice);
        CloseHandle(ov.hEvent);
        return Status::READ_TIMEOUT;
    }

    if (waitResult != WAIT_OBJECT_0) {
        DWORD err = GetLastError();
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("WaitForSingleObject failed, error:"); LOG_UINT32(err));
        CloseHandle(ov.hEvent);
        return Status::READ_ERROR;
    }

    if (!GetOverlappedResult(m_hDevice, &ov, &dwRead, FALSE)) {
        DWORD err = GetLastError();
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("GetOverlappedResult failed, error:"); LOG_UINT32(err));
        CloseHandle(ov.hEvent);
        return Status::READ_ERROR;
    }

    CloseHandle(ov.hEvent);
    bytesRead = static_cast<size_t>(dwRead);

    return Status::SUCCESS;
}
