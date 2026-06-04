#include "uDigisparkI2C.hpp"
#include "uLogger.hpp"

#include <hidapi/hidapi.h>
#include <cstring>
#include <cerrno>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR   "I2C_BRIDGE  |"
#define LOG_HDR  LOG_STRING(LT_HDR)

/** hidapi write needs a leading Report-ID byte (0x00 for single-report devices). */
static constexpr size_t HID_REPORT_ID_SIZE = 1;
static constexpr size_t HID_WRITE_SIZE     = I2CBridge::I2C_PKT_SIZE + HID_REPORT_ID_SIZE;


// ============================================================================
// LIFECYCLE  (Linux / hidapi)
// ============================================================================

I2CBridge::Status I2CBridge::open(uint16_t u16Vid, uint16_t u16Pid)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_pDevice != nullptr)
    {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("open: device already open"));
        return Status::SUCCESS;
    }

    if (hid_init() != 0)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("open: hid_init() failed"));
        return Status::PORT_ACCESS;
    }

    m_pDevice = hid_open(u16Vid, u16Pid, nullptr);
    if (m_pDevice == nullptr)
    {
        const wchar_t* pErrMsg = hid_error(nullptr);
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("open: hid_open() failed VID="); LOG_HEX16(u16Vid);
                  LOG_STRING("PID="); LOG_HEX16(u16Pid));
        (void)pErrMsg;
        hid_exit();
        return Status::PORT_ACCESS;
    }

    // Use blocking I/O; timeouts are handled per-packet via hid_read_timeout()
    hid_set_nonblocking(m_pDevice, 0);

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("open: HID device opened VID="); LOG_HEX16(u16Vid);
              LOG_STRING("PID="); LOG_HEX16(u16Pid));

    return Status::SUCCESS;
}


I2CBridge::Status I2CBridge::close()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_pDevice != nullptr)
    {
        hid_close(m_pDevice);
        m_pDevice = nullptr;
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("close: HID device closed"));
    }

    hid_exit();
    return Status::SUCCESS;
}


// ============================================================================
// LOW-LEVEL HID TRANSPORT  (Linux / hidapi)
// ============================================================================

/**
 * @brief Send one 8-byte HID packet to the firmware.
 *
 * hidapi requires a leading 0x00 Report-ID byte for devices that use
 * a single un-numbered report, making the actual write buffer 9 bytes.
 *
 * @param payload  Exactly I2C_PKT_SIZE bytes of command data
 * @return Status::SUCCESS or Status::WRITE_ERROR
 */
I2CBridge::Status I2CBridge::hid_pkt_send(std::span<const uint8_t> payload) const
{
    if (payload.size() != I2C_PKT_SIZE)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("hid_pkt_send: wrong payload size"); LOG_UINT32(payload.size()));
        return Status::INVALID_PARAM;
    }

    uint8_t writeBuf[HID_WRITE_SIZE] = {};
    writeBuf[0] = 0x00;  // Report-ID
    std::memcpy(&writeBuf[1], payload.data(), I2C_PKT_SIZE);

    int iRet = hid_write(m_pDevice, writeBuf, HID_WRITE_SIZE);
    if (iRet < 0)
    {
        const wchar_t* pErrMsg = hid_error(m_pDevice);
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("hid_pkt_send: hid_write() failed ret="); LOG_INT(iRet));
        (void)pErrMsg;
        return Status::WRITE_ERROR;
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("hid_pkt_send: CMD="); LOG_HEX8(payload[0]));

    return Status::SUCCESS;
}


/**
 * @brief Receive one 8-byte HID response packet from the firmware.
 *
 * Uses hid_read_timeout() so individual packet waits are bounded.
 * This mirrors UART's poll()-based timeout pattern.
 *
 * @param packet      Output buffer of exactly I2C_PKT_SIZE bytes
 * @param u32Timeout  Milliseconds to wait; 0 = return immediately if no data
 * @return Status::SUCCESS, Status::READ_TIMEOUT or Status::READ_ERROR
 */
I2CBridge::Status I2CBridge::hid_pkt_recv(std::span<uint8_t> packet, uint32_t u32Timeout) const
{
    if (packet.size() < I2C_PKT_SIZE)
    {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("hid_pkt_recv: packet buffer too small"); LOG_UINT32(packet.size()));
        return Status::INVALID_PARAM;
    }

    int iRet = hid_read_timeout(m_pDevice,
                                packet.data(),
                                I2C_PKT_SIZE,
                                static_cast<int>(u32Timeout));

    if (iRet == 0)
    {
        LOG_PRINT(LOG_WARNING, LOG_HDR;
                  LOG_STRING("hid_pkt_recv: timeout after"); LOG_UINT32(u32Timeout); LOG_STRING("ms"));
        return Status::READ_TIMEOUT;
    }

    if (iRet < 0)
    {
        const wchar_t* pErrMsg = hid_error(m_pDevice);
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("hid_pkt_recv: hid_read_timeout() failed ret="); LOG_INT(iRet));
        (void)pErrMsg;
        return Status::READ_ERROR;
    }

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("hid_pkt_recv: CMD="); LOG_HEX8(packet[0]);
              LOG_STRING("bytes="); LOG_INT(iRet));

    return Status::SUCCESS;
}
