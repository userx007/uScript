/**
 * @file  uCandlelight.cpp
 * @brief Candlelight (gs_usb) driver implementation over libusb-1.0.
 *
 * See uCandlelight.hpp for the protocol reference this implements. This
 * file's structure mirrors uUcan.cpp's/uSlcan.cpp's section-by-section
 * layout (construction → port management → probe → channel config → open/
 * close → diagnostics → frame encode/decode → send_frame/receive_frame →
 * ICommDriver interface), with USB control/bulk transfers standing in for
 * UART reads/writes.
 */

#include "uCandlelight.hpp"
#include "uLogger.hpp"

#include <libusb-1.0/libusb.h>

#include <algorithm>
#include <cstring>


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR   "CANDLE_DRV  |"
#define LOG_HDR  LOG_STRING(LT_HDR)

namespace {

// USB control-transfer bmRequestType bytes for gs_usb's vendor/interface
// requests — see the "Protocol sequence" section of uCandlelight.hpp.
constexpr uint8_t USB_DIR_OUT_VENDOR_IFACE = 0x41; // OUT | VENDOR | RECIPIENT_INTERFACE
constexpr uint8_t USB_DIR_IN_VENDOR_IFACE  = 0xC1; // IN  | VENDOR | RECIPIENT_INTERFACE

ICommDriver::Status libusb_err_to_status(int rc)
{
    switch (rc) {
        case LIBUSB_SUCCESS:        return ICommDriver::Status::SUCCESS;
        case LIBUSB_ERROR_TIMEOUT:  return ICommDriver::Status::READ_TIMEOUT;
        case LIBUSB_ERROR_NO_DEVICE:
        case LIBUSB_ERROR_ACCESS:
        case LIBUSB_ERROR_NOT_FOUND: return ICommDriver::Status::PORT_ACCESS;
        case LIBUSB_ERROR_PIPE:      return ICommDriver::Status::NACK;
        case LIBUSB_ERROR_OVERFLOW:  return ICommDriver::Status::BUFFER_OVERFLOW;
        case LIBUSB_ERROR_NO_MEM:    return ICommDriver::Status::OUT_OF_MEMORY;
        case LIBUSB_ERROR_INVALID_PARAM: return ICommDriver::Status::INVALID_PARAM;
        default:                     return ICommDriver::Status::OPERATION_FAILED;
    }
}

// Little-endian field helpers (gs_usb wire values are always little-endian,
// regardless of host byte order — see GsHostConfig's doc comment).
inline void put_u32le(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

inline uint32_t get_u32le(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

/// Byte-serialise the little-endian control-transfer structs by hand rather
/// than relying on host struct layout matching the wire (portable across
/// compilers/ABIs even though every field here happens to be uint32_t).
void serialize_u32_struct(const uint32_t* fields, size_t count, uint8_t* out)
{
    for (size_t i = 0; i < count; ++i) {
        put_u32le(out + i * 4, fields[i]);
    }
}

void deserialize_u32_struct(const uint8_t* in, uint32_t* fields, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        fields[i] = get_u32le(in + i * 4);
    }
}

} // namespace


// ============================================================================
// Construction / destruction
// ============================================================================

Candlelight::Candlelight(uint16_t vendor_id, uint16_t product_id, unsigned device_index,
                         const std::string& strIdentityLabel)
    : m_strIdentityLabel(strIdentityLabel)
{
    open(vendor_id, product_id, device_index);
}

Candlelight::~Candlelight()
{
    if (m_channel_open) {
        (void)close_channel();
    }
    close();
}

// ============================================================================
// Port management
// ============================================================================

ICommDriver::Status Candlelight::open(uint16_t vendor_id, uint16_t product_id, unsigned device_index)
{
    close();

    int rc = libusb_init(&m_usbCtx);
    if (rc != LIBUSB_SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("libusb_init failed"));
        return libusb_err_to_status(rc);
    }

    libusb_device** list = nullptr;
    ssize_t count = libusb_get_device_list(m_usbCtx, &list);
    if (count < 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("libusb_get_device_list failed"));
        libusb_exit(m_usbCtx);
        m_usbCtx = nullptr;
        return libusb_err_to_status(static_cast<int>(count));
    }

    libusb_device* match = nullptr;
    unsigned seen = 0;
    for (ssize_t i = 0; i < count; ++i) {
        libusb_device_descriptor desc{};
        if (libusb_get_device_descriptor(list[i], &desc) != LIBUSB_SUCCESS) continue;
        if (desc.idVendor == vendor_id && desc.idProduct == product_id) {
            if (seen == device_index) {
                match = list[i];
                break;
            }
            ++seen;
        }
    }

    if (!match) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("no matching gs_usb device found"));
        libusb_free_device_list(list, 1);
        libusb_exit(m_usbCtx);
        m_usbCtx = nullptr;
        return Status::PORT_ACCESS;
    }

    rc = libusb_open(match, &m_usbHandle);
    libusb_free_device_list(list, 1);
    if (rc != LIBUSB_SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("libusb_open failed"));
        libusb_exit(m_usbCtx);
        m_usbCtx = nullptr;
        return libusb_err_to_status(rc);
    }

    // gs_usb is a single-interface vendor device (interface 0), bulk IN/OUT
    // pair auto-discovered from its endpoint descriptors rather than
    // hard-coded, since the exact endpoint numbers vary by firmware/MCU.
    libusb_device* dev = libusb_get_device(m_usbHandle);
    libusb_config_descriptor* cfg = nullptr;
    rc = libusb_get_active_config_descriptor(dev, &cfg);
    if (rc != LIBUSB_SUCCESS || !cfg) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("libusb_get_active_config_descriptor failed"));
        libusb_close(m_usbHandle);
        m_usbHandle = nullptr;
        libusb_exit(m_usbCtx);
        m_usbCtx = nullptr;
        return libusb_err_to_status(rc);
    }

    m_epIn = 0;
    m_epOut = 0;
    m_interfaceNum = 0;
    if (cfg->bNumInterfaces > 0 && cfg->interface[0].num_altsetting > 0) {
        const libusb_interface_descriptor& iface = cfg->interface[0].altsetting[0];
        m_interfaceNum = iface.bInterfaceNumber;
        for (int e = 0; e < iface.bNumEndpoints; ++e) {
            const libusb_endpoint_descriptor& ep = iface.endpoint[e];
            const uint8_t xferType = ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK;
            if (xferType != LIBUSB_TRANSFER_TYPE_BULK) continue;
            if ((ep.bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN) {
                m_epIn = ep.bEndpointAddress;
            } else {
                m_epOut = ep.bEndpointAddress;
            }
        }
    }
    libusb_free_config_descriptor(cfg);

    if (m_epIn == 0 || m_epOut == 0) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("could not find bulk IN/OUT endpoint pair"));
        libusb_close(m_usbHandle);
        m_usbHandle = nullptr;
        libusb_exit(m_usbCtx);
        m_usbCtx = nullptr;
        return Status::PORT_ACCESS;
    }

    if (libusb_kernel_driver_active(m_usbHandle, m_interfaceNum) == 1) {
        libusb_detach_kernel_driver(m_usbHandle, m_interfaceNum);
    }
    rc = libusb_claim_interface(m_usbHandle, m_interfaceNum);
    if (rc != LIBUSB_SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("libusb_claim_interface failed"));
        libusb_close(m_usbHandle);
        m_usbHandle = nullptr;
        libusb_exit(m_usbCtx);
        m_usbCtx = nullptr;
        return libusb_err_to_status(rc);
    }

    Status s = probe();
    if (s != Status::SUCCESS) {
        close();
        return s;
    }

    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("opened gs_usb device, channels="); LOG_UINT8(static_cast<uint8_t>(m_devConfig.icount + 1)));
    return Status::SUCCESS;
}

ICommDriver::Status Candlelight::close()
{
    if (m_usbHandle) {
        libusb_release_interface(m_usbHandle, m_interfaceNum);
        libusb_close(m_usbHandle);
        m_usbHandle = nullptr;
    }
    if (m_usbCtx) {
        libusb_exit(m_usbCtx);
        m_usbCtx = nullptr;
    }
    m_channel_open = false;
    return Status::SUCCESS;
}

bool Candlelight::is_open() const
{
    return m_usbHandle != nullptr;
}

// ============================================================================
// Internal USB control-transfer helpers
// ============================================================================

ICommDriver::Status Candlelight::ctrl_out(GsUsbBreq req, uint16_t value, const void* data, uint16_t len, uint32_t timeout_ms)
{
    if (!is_open()) return Status::PORT_ACCESS;

    int rc = libusb_control_transfer(m_usbHandle, USB_DIR_OUT_VENDOR_IFACE,
                                      static_cast<uint8_t>(req), value,
                                      static_cast<uint16_t>(m_interfaceNum),
                                      const_cast<unsigned char*>(static_cast<const unsigned char*>(data)),
                                      len, timeout_ms);
    if (rc < 0 || static_cast<uint16_t>(rc) != len) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("ctrl_out failed, breq="); LOG_UINT8(static_cast<uint8_t>(req)));
        return (rc == LIBUSB_ERROR_TIMEOUT) ? Status::WRITE_TIMEOUT : libusb_err_to_status(rc < 0 ? rc : LIBUSB_ERROR_IO);
    }
    return Status::SUCCESS;
}

ICommDriver::Status Candlelight::ctrl_in(GsUsbBreq req, uint16_t value, void* data, uint16_t len, uint32_t timeout_ms)
{
    if (!is_open()) return Status::PORT_ACCESS;

    int rc = libusb_control_transfer(m_usbHandle, USB_DIR_IN_VENDOR_IFACE,
                                      static_cast<uint8_t>(req), value,
                                      static_cast<uint16_t>(m_interfaceNum),
                                      static_cast<unsigned char*>(data),
                                      len, timeout_ms);
    // A control IN transfer legitimately completing with fewer bytes than
    // wLength is normal USB behaviour (the device simply has less to say
    // than the host allowed room for) — NOT an error condition. This matters
    // in practice: some gs_usb-compatible firmwares reply to DEVICE_CONFIG
    // with only 8 of its 12 defined bytes (omitting hw_version) — see
    // probe()'s DEVICE_CONFIG handling. Only rc < 0 (a genuine transfer
    // failure) is treated as an error here; any 0 <= rc <= len is accepted
    // and the caller is responsible for validating/defaulting whatever
    // trailing bytes it didn't actually receive.
    if (rc < 0) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("ctrl_in failed, breq="); LOG_UINT8(static_cast<uint8_t>(req)));
        return (rc == LIBUSB_ERROR_TIMEOUT) ? Status::READ_TIMEOUT : libusb_err_to_status(rc);
    }
    return Status::SUCCESS;
}

// ============================================================================
// probe()  — HOST_FORMAT -> DEVICE_CONFIG -> BT_CONST [-> BT_CONST_EXT]
// ============================================================================

ICommDriver::Status Candlelight::probe()
{
    // HOST_FORMAT: tell the device our byte order (0x0000BEEF, little-endian
    // on the wire — see GsHostConfig's doc comment).
    uint8_t hostFmt[4];
    put_u32le(hostFmt, 0x0000BEEFu);
    Status s = ctrl_out(GsUsbBreq::HOST_FORMAT, 0, hostFmt, sizeof(hostFmt), CANDLELIGHT_DEFAULT_TIMEOUT);
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("probe: HOST_FORMAT failed"));
        return s;
    }

    // DEVICE_CONFIG — 12 bytes on the wire (reserved1-3 + icount + sw_version +
    // hw_version), per the canonical gs_device_config layout. Zero-initialised
    // and requested in full even though some gs_usb-compatible firmwares only
    // ever fill the first 8 bytes (omitting hw_version) — see ctrl_in()'s doc
    // comment: a short USB control-IN completion is valid, not an error, and
    // leaves hw_version safely defaulted to 0 rather than reading uninitialised
    // memory.
    uint8_t devCfgBuf[12] = {0};
    s = ctrl_in(GsUsbBreq::DEVICE_CONFIG, 0, devCfgBuf, sizeof(devCfgBuf), CANDLELIGHT_DEFAULT_TIMEOUT);
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("probe: DEVICE_CONFIG failed"));
        return s;
    }
    m_devConfig.reserved1  = devCfgBuf[0];
    m_devConfig.reserved2  = devCfgBuf[1];
    m_devConfig.reserved3  = devCfgBuf[2];
    m_devConfig.icount     = devCfgBuf[3];
    m_devConfig.sw_version = get_u32le(devCfgBuf + 4);
    m_devConfig.hw_version = get_u32le(devCfgBuf + 8); // 0 if the firmware only sent 8 bytes

    // BT_CONST (10x u32 = 40 bytes)
    uint8_t btConstBuf[40] = {0};
    s = ctrl_in(GsUsbBreq::BT_CONST, 0, btConstBuf, sizeof(btConstBuf), CANDLELIGHT_DEFAULT_TIMEOUT);
    if (s != Status::SUCCESS) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("probe: BT_CONST failed"));
        return s;
    }
    {
        uint32_t fields[10];
        deserialize_u32_struct(btConstBuf, fields, 10);
        m_btConst.feature   = fields[0];
        m_btConst.fclk_can  = fields[1];
        m_btConst.tseg1_min = fields[2];
        m_btConst.tseg1_max = fields[3];
        m_btConst.tseg2_min = fields[4];
        m_btConst.tseg2_max = fields[5];
        m_btConst.sjw_max   = fields[6];
        m_btConst.brp_min   = fields[7];
        m_btConst.brp_max   = fields[8];
        m_btConst.brp_inc   = fields[9];
    }

    if (is_fd_supported()) {
        // BT_CONST_EXT (18x u32 = 72 bytes)
        uint8_t btConstExtBuf[72] = {0};
        s = ctrl_in(GsUsbBreq::BT_CONST_EXT, 0, btConstExtBuf, sizeof(btConstExtBuf), CANDLELIGHT_DEFAULT_TIMEOUT);
        if (s != Status::SUCCESS) {
            LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("probe: BT_CONST_EXT failed despite FD feature bit"));
            // Not fatal: proceed without confirmed FD data-phase limits.
        } else {
            uint32_t fields[18];
            deserialize_u32_struct(btConstExtBuf, fields, 18);
            m_btConstExt.feature     = fields[0];
            m_btConstExt.fclk_can    = fields[1];
            m_btConstExt.tseg1_min   = fields[2];
            m_btConstExt.tseg1_max   = fields[3];
            m_btConstExt.tseg2_min   = fields[4];
            m_btConstExt.tseg2_max   = fields[5];
            m_btConstExt.sjw_max     = fields[6];
            m_btConstExt.brp_min     = fields[7];
            m_btConstExt.brp_max     = fields[8];
            m_btConstExt.brp_inc     = fields[9];
            m_btConstExt.dtseg1_min  = fields[10];
            m_btConstExt.dtseg1_max  = fields[11];
            m_btConstExt.dtseg2_min  = fields[12];
            m_btConstExt.dtseg2_max  = fields[13];
            m_btConstExt.dsjw_max    = fields[14];
            m_btConstExt.dbrp_min    = fields[15];
            m_btConstExt.dbrp_max    = fields[16];
            m_btConstExt.dbrp_inc    = fields[17];
        }
    }

    return Status::SUCCESS;
}

// ============================================================================
// Channel configuration
// ============================================================================

ICommDriver::Status Candlelight::set_bittiming(uint32_t prop_seg, uint32_t phase_seg1, uint32_t phase_seg2,
                                                uint32_t sjw, uint32_t brp, uint32_t timeout_ms)
{
    if (m_channel_open) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("set_bittiming called with channel open"));
        return Status::INVALID_PARAM;
    }
    uint32_t fields[5] = { prop_seg, phase_seg1, phase_seg2, sjw, brp };
    uint8_t buf[20];
    serialize_u32_struct(fields, 5, buf);
    return ctrl_out(GsUsbBreq::BITTIMING, 0, buf, sizeof(buf), timeout_ms);
}

ICommDriver::Status Candlelight::set_data_bittiming(uint32_t prop_seg, uint32_t phase_seg1, uint32_t phase_seg2,
                                                     uint32_t sjw, uint32_t brp, uint32_t timeout_ms)
{
    if (m_channel_open) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("set_data_bittiming called with channel open"));
        return Status::INVALID_PARAM;
    }
    if (!is_fd_supported()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("set_data_bittiming: device has no GS_CAN_FEATURE_FD"));
        return Status::INVALID_PARAM;
    }
    uint32_t fields[5] = { prop_seg, phase_seg1, phase_seg2, sjw, brp };
    uint8_t buf[20];
    serialize_u32_struct(fields, 5, buf);
    return ctrl_out(GsUsbBreq::DATA_BITTIMING, 0, buf, sizeof(buf), timeout_ms);
}

namespace {

/**
 * @brief Search (brp, tseg1=prop_seg+phase_seg1, tseg2=phase_seg2) within
 *        [min,max] limits for the combination that reproduces bitrate_bps
 *        exactly from fclk_can and lands closest to the requested sample
 *        point. tseg1 is split prop_seg/phase_seg1 50/50 (both segments
 *        serve the same purpose in gs_usb's bit-timing model — the split
 *        is a host-side convention, not something the CAN clock cares
 *        about). Returns false if no exact-bitrate combination exists
 *        within the device's limits.
 */
bool calc_bittiming(uint32_t fclk_can, uint32_t bitrate_bps, double sample_point,
                    uint32_t tseg1_min, uint32_t tseg1_max,
                    uint32_t tseg2_min, uint32_t tseg2_max,
                    uint32_t sjw_max, uint32_t brp_min, uint32_t brp_max, uint32_t brp_inc,
                    uint32_t& out_prop_seg, uint32_t& out_phase_seg1, uint32_t& out_phase_seg2,
                    uint32_t& out_sjw, uint32_t& out_brp)
{
    if (bitrate_bps == 0 || fclk_can == 0) return false;

    bool found = false;
    double bestErr = 1e18;
    uint32_t bestBrp = 0, bestTseg1 = 0, bestTseg2 = 0;

    for (uint32_t brp = brp_min; brp <= brp_max; brp += (brp_inc ? brp_inc : 1)) {
        // Total time quanta per bit for this brp: tq_total = fclk / (brp * bitrate)
        // (1 sync quantum + tseg1 + tseg2 = tq_total; sync is fixed at 1 tq).
        const uint64_t denom = static_cast<uint64_t>(brp) * bitrate_bps;
        if (denom == 0) continue;
        if (fclk_can % denom != 0) continue; // must divide exactly - no bit-rate error tolerated
        const uint64_t tqTotal = fclk_can / denom;
        if (tqTotal < 3) continue; // need at least sync(1)+tseg1(1)+tseg2(1)

        const uint32_t segTotal = static_cast<uint32_t>(tqTotal - 1); // tseg1+tseg2

        // Try every tseg2 in range, derive tseg1 = segTotal - tseg2.
        for (uint32_t tseg2 = tseg2_min; tseg2 <= tseg2_max && tseg2 < segTotal; ++tseg2) {
            const uint32_t tseg1 = segTotal - tseg2;
            if (tseg1 < tseg1_min || tseg1 > tseg1_max) continue;

            const double samplePoint = static_cast<double>(1 + tseg1) / static_cast<double>(tqTotal);
            const double err = std::abs(samplePoint - sample_point);
            if (err < bestErr) {
                bestErr = err;
                bestBrp = brp;
                bestTseg1 = tseg1;
                bestTseg2 = tseg2;
                found = true;
            }
        }
    }

    if (!found) return false;

    out_prop_seg   = bestTseg1 / 2;
    out_phase_seg1 = bestTseg1 - out_prop_seg;
    out_phase_seg2 = bestTseg2;
    out_sjw        = std::min(sjw_max, out_phase_seg2);
    out_brp        = bestBrp;
    return true;
}

} // namespace

ICommDriver::Status Candlelight::set_bitrate(uint32_t bitrate_bps, double sample_point, uint32_t timeout_ms)
{
    uint32_t prop_seg = 0, phase_seg1 = 0, phase_seg2 = 0, sjw = 0, brp = 0;
    if (!calc_bittiming(m_btConst.fclk_can, bitrate_bps, sample_point,
                        m_btConst.tseg1_min, m_btConst.tseg1_max,
                        m_btConst.tseg2_min, m_btConst.tseg2_max,
                        m_btConst.sjw_max, m_btConst.brp_min, m_btConst.brp_max, m_btConst.brp_inc,
                        prop_seg, phase_seg1, phase_seg2, sjw, brp)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("set_bitrate: no exact bit-timing solution for requested rate"));
        return Status::INVALID_PARAM;
    }
    return set_bittiming(prop_seg, phase_seg1, phase_seg2, sjw, brp, timeout_ms);
}

ICommDriver::Status Candlelight::set_fd_data_bitrate(uint32_t bitrate_bps, double sample_point, uint32_t timeout_ms)
{
    if (!is_fd_supported()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("set_fd_data_bitrate: device has no GS_CAN_FEATURE_FD"));
        return Status::INVALID_PARAM;
    }
    uint32_t prop_seg = 0, phase_seg1 = 0, phase_seg2 = 0, sjw = 0, brp = 0;
    if (!calc_bittiming(m_btConstExt.fclk_can, bitrate_bps, sample_point,
                        m_btConstExt.dtseg1_min, m_btConstExt.dtseg1_max,
                        m_btConstExt.dtseg2_min, m_btConstExt.dtseg2_max,
                        m_btConstExt.dsjw_max, m_btConstExt.dbrp_min, m_btConstExt.dbrp_max, m_btConstExt.dbrp_inc,
                        prop_seg, phase_seg1, phase_seg2, sjw, brp)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("set_fd_data_bitrate: no exact bit-timing solution for requested rate"));
        return Status::INVALID_PARAM;
    }
    return set_data_bittiming(prop_seg, phase_seg1, phase_seg2, sjw, brp, timeout_ms);
}

// ============================================================================
// Channel open / close
// ============================================================================

ICommDriver::Status Candlelight::open_channel(uint32_t mode_flags, uint32_t timeout_ms)
{
    uint32_t fields[2] = { static_cast<uint32_t>(GsCanMode::START), mode_flags };
    uint8_t buf[8];
    serialize_u32_struct(fields, 2, buf);

    Status s = ctrl_out(GsUsbBreq::MODE, 0, buf, sizeof(buf), timeout_ms);
    if (s == Status::SUCCESS) {
        m_channel_open = true;
        m_fd_negotiated = (mode_flags & GS_CAN_MODE_FD) != 0;
        LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("CAN channel opened, fd="); LOG_UINT8(m_fd_negotiated ? 1 : 0));
    }
    return s;
}

ICommDriver::Status Candlelight::close_channel(uint32_t timeout_ms)
{
    uint32_t fields[2] = { static_cast<uint32_t>(GsCanMode::RESET), 0 };
    uint8_t buf[8];
    serialize_u32_struct(fields, 2, buf);

    Status s = ctrl_out(GsUsbBreq::MODE, 0, buf, sizeof(buf), timeout_ms);
    m_channel_open = false; // mark closed even on error to avoid loops
    LOG_PRINT(LOG_INFO, LOG_HDR; LOG_STRING("CAN channel closed"));
    return s;
}

// ============================================================================
// Diagnostic queries
// ============================================================================

ICommDriver::Status Candlelight::get_state(GsDeviceState& state, uint32_t timeout_ms)
{
    if (!is_get_state_supported()) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("get_state: device has no GS_CAN_FEATURE_GET_STATE"));
        return Status::OPERATION_FAILED;
    }
    uint8_t buf[12] = {0};
    Status s = ctrl_in(GsUsbBreq::GET_STATE, 0, buf, sizeof(buf), timeout_ms);
    if (s != Status::SUCCESS) return s;

    uint32_t fields[3];
    deserialize_u32_struct(buf, fields, 3);
    state.state = fields[0];
    state.rxerr = fields[1];
    state.txerr = fields[2];
    return Status::SUCCESS;
}

// ============================================================================
// Frame encoding (session-dependent tail shape)
// ============================================================================

size_t Candlelight::encode_frame(uint32_t echo_id, const CanFrame& frame, std::span<uint8_t> out) const
{
    if (out.size() < max_packet_len()) return 0;
    if (frame.is_canfd && !m_fd_negotiated) return 0;

    const uint8_t dataLen = frame.is_canfd ? frame.len : (frame.is_remote ? 0 : std::min<uint8_t>(frame.len, 8));

    uint32_t can_id = frame.id;
    if (frame.is_extended) can_id = (can_id & GS_CAN_EFF_MASK) | GS_CAN_EFF_FLAG;
    else                    can_id = can_id & GS_CAN_SFF_MASK;
    if (frame.is_remote) can_id |= GS_CAN_RTR_FLAG;

    uint8_t flags = 0;
    if (frame.is_canfd) {
        flags |= GS_CAN_FLAG_FD;
        if (frame.brs) flags |= GS_CAN_FLAG_BRS;
    }

    uint8_t* p = out.data();
    put_u32le(p + 0, echo_id);
    put_u32le(p + 4, can_id);
    p[8]  = frame.is_canfd ? SLCAN::len_to_dlc(frame.len) : dataLen; // can_dlc: DLC code for FD, byte count for classic
    p[9]  = 0; // channel — single-channel adapters only (see class doc comment)
    p[10] = flags;
    p[11] = 0; // reserved

    const uint8_t tailCap = m_fd_negotiated ? 64 : 8;
    std::fill(p + GS_HOST_FRAME_HDR_LEN, p + GS_HOST_FRAME_HDR_LEN + tailCap, 0);
    if (!frame.is_remote) {
        std::copy(frame.data.begin(), frame.data.begin() + dataLen, p + GS_HOST_FRAME_HDR_LEN);
    }

    return GS_HOST_FRAME_HDR_LEN + tailCap; // no hw timestamp requested (see class doc comment)
}

// ============================================================================
// Frame decoding (session-dependent tail shape)
// ============================================================================

bool Candlelight::decode_frame(const uint8_t* pkt, size_t len, uint32_t& echo_id, CanFrame& frame) const
{
    if (!pkt || len < GS_HOST_FRAME_HDR_LEN) return false;

    echo_id = get_u32le(pkt + 0);
    const uint32_t can_id_raw = get_u32le(pkt + 4);
    const uint8_t  can_dlc    = pkt[8];
    // pkt[9] is channel — ignored (single-channel adapters only, see class doc comment)
    const uint8_t  flags      = pkt[10];

    const bool is_fd = (flags & GS_CAN_FLAG_FD) != 0;
    const uint8_t tailCap = is_fd ? 64 : 8;
    if (len < GS_HOST_FRAME_HDR_LEN + tailCap) return false;

    frame.is_extended = (can_id_raw & GS_CAN_EFF_FLAG) != 0;
    frame.is_remote   = (can_id_raw & GS_CAN_RTR_FLAG) != 0;
    frame.is_canfd    = is_fd;
    frame.brs         = is_fd && (flags & GS_CAN_FLAG_BRS) != 0;
    frame.id          = frame.is_extended ? (can_id_raw & GS_CAN_EFF_MASK) : (can_id_raw & GS_CAN_SFF_MASK);
    frame.dlc         = can_dlc;
    frame.len          = is_fd ? SLCAN::dlc_to_len(can_dlc) : std::min<uint8_t>(can_dlc, 8);

    frame.data.fill(0);
    if (!frame.is_remote) {
        std::copy(pkt + GS_HOST_FRAME_HDR_LEN, pkt + GS_HOST_FRAME_HDR_LEN + frame.len, frame.data.begin());
    }

    return true;
}

// ============================================================================
// Internal bulk helpers
// ============================================================================

ICommDriver::Status Candlelight::bulk_write_frame(uint32_t echo_id, const CanFrame& frame, uint32_t timeout_ms)
{
    std::vector<uint8_t> buf(max_packet_len());
    size_t n = encode_frame(echo_id, frame, std::span<uint8_t>(buf));
    if (n == 0) return Status::INVALID_PARAM;

    int transferred = 0;
    int rc = libusb_bulk_transfer(m_usbHandle, m_epOut, buf.data(), static_cast<int>(n), &transferred, timeout_ms);
    if (rc != LIBUSB_SUCCESS || static_cast<size_t>(transferred) != n) {
        LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("bulk OUT failed"));
        return (rc == LIBUSB_ERROR_TIMEOUT) ? Status::WRITE_TIMEOUT : libusb_err_to_status(rc);
    }
    return Status::SUCCESS;
}

ICommDriver::Status Candlelight::bulk_read_one(uint32_t& echo_id, CanFrame& frame, uint32_t timeout_ms)
{
    std::vector<uint8_t> buf(max_packet_len());
    int transferred = 0;
    int rc = libusb_bulk_transfer(m_usbHandle, m_epIn, buf.data(), static_cast<int>(buf.size()), &transferred, timeout_ms);
    if (rc != LIBUSB_SUCCESS) {
        return (rc == LIBUSB_ERROR_TIMEOUT) ? Status::READ_TIMEOUT : libusb_err_to_status(rc);
    }
    if (!decode_frame(buf.data(), static_cast<size_t>(transferred), echo_id, frame)) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("bulk_read_one: decode failed"));
        return Status::READ_ERROR;
    }
    return Status::SUCCESS;
}

// ============================================================================
// send_frame  — write, then wait for the matching TX-complete echo
// ============================================================================

ICommDriver::Status Candlelight::send_frame(const CanFrame& frame, uint32_t timeout_ms)
{
    if (!m_channel_open) {
        LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("send_frame: channel not open"));
        return Status::PORT_ACCESS;
    }

    const uint32_t echoId = m_next_echo_id++;
    if (m_next_echo_id == GS_CAN_ECHO_ID_RX) m_next_echo_id = 0; // never collide with the RX marker

    LOG_PRINT(LOG_DEBUG, LOG_HDR;
              LOG_STRING("TX id="); LOG_HEX32(frame.id);
              LOG_STRING(" len="); LOG_UINT8(frame.len);
              LOG_STRING(frame.is_extended ? " EXT" : " STD");
              LOG_STRING(frame.is_canfd    ? " CANFD" : " CAN"));

    Status s = bulk_write_frame(echoId, frame, timeout_ms);
    if (s != Status::SUCCESS) return s;

    // Wait for the matching TX-complete echo, silently absorbing any RX
    // frames that happen to arrive first — see uCandlelight.hpp's "echo_id".
    for (;;) {
        uint32_t gotEcho = 0;
        CanFrame dummy{};
        s = bulk_read_one(gotEcho, dummy, timeout_ms);
        if (s != Status::SUCCESS) {
            LOG_PRINT(LOG_WARNING, LOG_HDR; LOG_STRING("send_frame: no TX-complete echo"));
            return (s == Status::READ_TIMEOUT) ? Status::WRITE_TIMEOUT : Status::WRITE_ERROR;
        }
        if (gotEcho == echoId) {
            return Status::SUCCESS;
        }
        // else: an RX frame, or (in principle) another in-flight TX's echo —
        // keep waiting for ours, bounded by the same overall timeout_ms
        // each bulk_read_one() call already enforces.
    }
}

// ============================================================================
// receive_frame  — wait for the next genuine RX frame
// ============================================================================

ICommDriver::Status Candlelight::receive_frame(CanFrame& frame, uint32_t timeout_ms)
{
    for (;;) {
        uint32_t echoId = 0;
        Status s = bulk_read_one(echoId, frame, timeout_ms);
        if (s != Status::SUCCESS) {
            if (s != Status::READ_TIMEOUT) {
                LOG_PRINT(LOG_ERROR, LOG_HDR; LOG_STRING("receive_frame: read error"));
            }
            return s;
        }
        if (echoId == GS_CAN_ECHO_ID_RX) {
            LOG_PRINT(LOG_DEBUG, LOG_HDR;
                      LOG_STRING("RX id="); LOG_HEX32(frame.id);
                      LOG_STRING(" len="); LOG_UINT8(frame.len);
                      LOG_STRING(frame.is_extended ? " EXT" : " STD");
                      LOG_STRING(frame.is_canfd    ? " CANFD" : " CAN"));
            return Status::SUCCESS;
        }
        // else: a TX-complete echo for some earlier send_frame() call —
        // absorb it and keep waiting, bounded by the same timeout_ms each
        // bulk_read_one() call enforces.
    }
}

// ============================================================================
// ICommDriver generic interface
// ============================================================================

Candlelight::ReadResult Candlelight::tout_read(uint32_t u32ReadTimeout,
                                               std::span<uint8_t> buffer,
                                               const ReadOptions& /*options*/,
                                               std::string_view   /*xtra_params*/) const
{
    ReadResult result;
    if (!is_open()) {
        result.status = Status::PORT_ACCESS;
        return result;
    }

    int transferred = 0;
    int rc = libusb_bulk_transfer(m_usbHandle, m_epIn, buffer.data(),
                                   static_cast<int>(buffer.size()), &transferred, u32ReadTimeout);
    result.status           = libusb_err_to_status(rc);
    result.bytes_read       = (rc == LIBUSB_SUCCESS) ? static_cast<size_t>(transferred) : 0;
    result.found_terminator = (rc == LIBUSB_SUCCESS);
    return result;
}

Candlelight::WriteResult Candlelight::tout_write(uint32_t u32WriteTimeout,
                                                 std::span<const uint8_t> buffer,
                                                 std::string_view /*xtra_params*/) const
{
    WriteResult result;
    if (!is_open()) {
        result.status = Status::PORT_ACCESS;
        return result;
    }

    int transferred = 0;
    int rc = libusb_bulk_transfer(m_usbHandle, m_epOut,
                                   const_cast<uint8_t*>(buffer.data()),
                                   static_cast<int>(buffer.size()), &transferred, u32WriteTimeout);
    result.status        = libusb_err_to_status(rc);
    result.bytes_written = (rc == LIBUSB_SUCCESS) ? static_cast<size_t>(transferred) : 0;
    return result;
}
