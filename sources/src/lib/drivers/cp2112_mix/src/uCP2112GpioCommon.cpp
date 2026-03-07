#include "uCP2112Gpio.hpp"
#include "uLogger.hpp"

#include <cstring>

#define LT_HDR  "CP2112_GPIO|"
#define LOG_HDR LOG_STRING(LT_HDR)


// ============================================================================
// open
// ============================================================================

CP2112Gpio::Status CP2112Gpio::open(uint8_t u8DeviceIndex)
{
    // Delegate device enumeration and handle opening to the shared base.
    // No extra configuration is required for GPIO — pins are usable
    // immediately after open_device() succeeds.
    Status s = open_device(u8DeviceIndex);

    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("Failed to open CP2112 (GPIO) at index"); LOG_UINT32(u8DeviceIndex));
    } else {
        LOG_PRINT(LOG_DEBUG, LOG_HDR;
                  LOG_STRING("CP2112 GPIO opened, device index:"); LOG_UINT32(u8DeviceIndex));
    }

    return s;
}


// ============================================================================
// GPIO protocol  (AN495 §5.1 – §5.3)
// ============================================================================

/**
 * @brief Push GPIO configuration to the CP2112
 *
 * Report 0x02 — GPIO Configuration (Feature SET, 64 bytes):
 *   Byte 0  : Report ID = 0x02
 *   Byte 1  : Direction  mask  — 1 = output, 0 = input
 *   Byte 2  : Push-pull  mask  — 1 = push-pull, 0 = open-drain
 *   Byte 3  : Special function mask
 *               bit 0 → GPIO.0 = TX LED
 *               bit 1 → GPIO.1 = interrupt (active-low)
 *               bit 6 → GPIO.6 = clock output
 *               bit 7 → GPIO.7 = RX LED
 *   Byte 4  : Clock divider (used only when GPIO.6 = clock output)
 *   Bytes 5–63 : Reserved (zero)
 */
CP2112Gpio::Status CP2112Gpio::gpio_configure(const GpioConfig& config) const
{
    if (!is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("gpio_configure: device not open"));
        return Status::PORT_ACCESS;
    }

    uint8_t report[HID_REPORT_SIZE] = {0};
    report[0] = RPT_GPIO_CONFIG;
    report[1] = config.directionMask;
    report[2] = config.pushPullMask;
    report[3] = config.specialFuncMask;
    report[4] = config.clockDivider;

    Status s = hid_set_feature(report, HID_REPORT_SIZE);
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("gpio_configure: hid_set_feature failed"));
    }

    return s;
}


/**
 * @brief Drive output pins via Report 0x04 — Set GPIO Values (Feature SET):
 *   Byte 0  : Report ID = 0x04
 *   Byte 1  : Value mask — 1 = drive high, 0 = drive low
 *   Byte 2  : Apply mask — 1 = update this pin, 0 = leave unchanged
 *   Bytes 3–63 : Reserved (zero)
 *
 * The apply-mask allows atomic partial updates without a read-modify-write.
 */
CP2112Gpio::Status CP2112Gpio::gpio_write(uint8_t valueMask, uint8_t applyMask) const
{
    if (!is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("gpio_write: device not open"));
        return Status::PORT_ACCESS;
    }

    if (applyMask == 0x00) {
        // Nothing to do — mask explicitly says touch no pins
        return Status::SUCCESS;
    }

    uint8_t report[HID_REPORT_SIZE] = {0};
    report[0] = RPT_GPIO_SET;
    report[1] = valueMask;
    report[2] = applyMask;

    Status s = hid_set_feature(report, HID_REPORT_SIZE);
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR;
                  LOG_STRING("gpio_write: hid_set_feature failed");
                  LOG_STRING("value=0x"); LOG_HEX8(valueMask);
                  LOG_STRING("mask=0x");  LOG_HEX8(applyMask));
    }

    return s;
}


/**
 * @brief Read all pin levels via Report 0x03 — Get GPIO Values (Feature GET):
 *   Byte 0  : Report ID = 0x03 (set before calling hid_get_feature)
 *   Byte 1  : Pin levels — bit = 1 → high, 0 → low
 *   Bytes 2–63 : Reserved
 */
CP2112Gpio::Status CP2112Gpio::gpio_read(uint8_t& valueMask) const
{
    valueMask = 0x00;

    if (!is_open()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("gpio_read: device not open"));
        return Status::PORT_ACCESS;
    }

    uint8_t report[HID_REPORT_SIZE] = {0};
    report[0] = RPT_GPIO_GET;

    Status s = hid_get_feature(report, HID_REPORT_SIZE);
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("gpio_read: hid_get_feature failed"));
        return s;
    }

    valueMask = report[1];

    LOG_PRINT(LOG_VERBOSE, LOG_HDR;
              LOG_STRING("gpio_read: value=0x"); LOG_HEX8(valueMask));

    return Status::SUCCESS;
}
