#include "CP2112Base.hpp"
#include "uLogger.hpp"

// Windows HID API
// Link against: hid.lib (MSVC) / -lhid (MinGW-w64), setupapi.lib / -lsetupapi
//
// hidsdi.h already carries its own extern "C" guards on both MSVC and
// MinGW-w64 — do NOT wrap it again or MSVC will emit a C2732 error.
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <cstring>
#include <string>
#include <vector>


#define LT_HDR  "CP2112_BASE|"
#define LOG_HDR LOG_STRING(LT_HDR)


// ============================================================================
// Internal helper — device path enumeration
// ============================================================================

namespace {

/**
 * @brief Find the device path of the n-th CP2112 on the system
 *
 * Enumerates the HID device class via SetupAPI, verifies VID/PID via
 * HidD_GetAttributes, and returns the device path string for the
 * requested zero-based index.
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

    uint8_t matchCount = 0;
    bool    found      = false;

    for (DWORD memberIdx = 0;
         SetupDiEnumDeviceInterfaces(deviceInfoSet, nullptr, &hidGuid, memberIdx, &ifaceData);
         ++memberIdx)
    {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetail(
            deviceInfoSet, &ifaceData, nullptr, 0, &requiredSize, nullptr);

        if (requiredSize == 0) continue;

        // Use a plain byte vector to avoid UB from casting operator-new storage
        std::vector<uint8_t> detailBuf(requiredSize, 0);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA*>(detailBuf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

        if (!SetupDiGetDeviceInterfaceDetail(
                deviceInfoSet, &ifaceData, detail, requiredSize, nullptr, nullptr))
        {
            continue;
        }

        // Open temporarily to read attributes
        HANDLE hDev = CreateFile(
            detail->DevicePath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr);

        if (hDev != INVALID_HANDLE_VALUE) {
            HIDD_ATTRIBUTES attrs;
            attrs.Size = sizeof(HIDD_ATTRIBUTES);

            if (HidD_GetAttributes(hDev, &attrs) &&
                attrs.VendorID  == CP2112Base::CP2112_VID &&
                attrs.ProductID == CP2112Base::CP2112_PID)
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
// CP2112Base destructor
// ============================================================================

CP2112Base::~CP2112Base()
{
    CP2112Base::close();
}


// ============================================================================
// open_device
// ============================================================================

CP2112Base::Status CP2112Base::open_device(uint8_t u8DeviceIndex)
{
    std::string devicePath;
    if (!find_cp2112_path(u8DeviceIndex, devicePath)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("CP2112 not found at index"); LOG_UINT32(u8DeviceIndex);
                  LOG_STRING("(VID=0x10C4, PID=0xEA90)"));
        return Status::PORT_ACCESS;
    }

    // Opened with FILE_FLAG_OVERLAPPED so hid_interrupt_read can use a timeout
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

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("CP2112 handle opened, index="); LOG_UINT32(u8DeviceIndex));

    return Status::SUCCESS;
}


// ============================================================================
// close
// ============================================================================

CP2112Base::Status CP2112Base::close()
{
    if (m_hDevice != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hDevice);
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("CP2112 handle closed"));
        m_hDevice = INVALID_HANDLE_VALUE;
    }
    return Status::SUCCESS;
}


// ============================================================================
// is_open
// ============================================================================

bool CP2112Base::is_open() const
{
    if (m_hDevice == INVALID_HANDLE_VALUE) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("Device not open"));
        return false;
    }
    return true;
}


// ============================================================================
// HID primitives
// ============================================================================

CP2112Base::Status CP2112Base::hid_set_feature(const uint8_t* buf, size_t len) const
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


CP2112Base::Status CP2112Base::hid_get_feature(uint8_t* buf, size_t len) const
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


CP2112Base::Status CP2112Base::hid_interrupt_write(const uint8_t* buf, size_t len) const
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


CP2112Base::Status CP2112Base::hid_interrupt_read(uint8_t* buf, size_t len,
                                                   uint32_t timeoutMs,
                                                   size_t& bytesRead) const
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
