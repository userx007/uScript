#ifndef U_CANDLELIGHT_DRIVER_HPP
#define U_CANDLELIGHT_DRIVER_HPP

/**
 * @file  uCandlelight.hpp
 * @brief Candlelight (gs_usb) driver — a native-USB CAN adapter protocol.
 *
 * "Candlelight" (a.k.a. "gs_usb", after the Linux kernel driver that speaks
 * it — drivers/net/can/usb/gs_usb.c) is a widely-deployed open protocol for
 * USB CAN/CAN-FD adapters (CANable, canable.io's stock firmware, the
 * candleLight-fw project, Elmue's CANable 2.5 firmware referenced by this
 * plugin's docs, and several commercial CAN-FD dongles). Unlike SLCAN/UCAN
 * (see uSlcan.hpp / uUcan.hpp), it is **not** a byte-stream-over-UART
 * protocol at all: the adapter enumerates as its own USB device (vendor
 * class), configuration is a set of USB *control* transfers, and CAN frames
 * travel over a bulk IN/OUT endpoint pair as fixed binary structures. This
 * driver talks to the device directly via libusb-1.0 (already a dependency
 * of this codebase's ftdi2xx driver — see its third_party/CMakeLists.txt).
 *
 * This is a straight C++/libusb port of the protocol exactly as specified
 * by the gs_usb Linux kernel driver and used by candleLight-fw / CANable's
 * Candlelight firmware (including Elmue's CANable 2.5 fork referenced by
 * this plugin's docs) — struct layouts, enum values and the USB
 * request/response sequence below are not this codebase's invention; see
 * https://github.com/torvalds/linux/blob/master/drivers/net/can/usb/gs_usb.c
 * and https://github.com/candle-usb/candleLight_fw for the canonical
 * reference, and https://github.com/Elmue/CANable-2.5-firmware-Slcan-and-Candlelight
 * for the specific firmware this plugin was written against.
 *
 * USB identification
 * -------------------
 * gs_usb devices are vendor-class (no standard USB class fits "raw CAN
 * bus"), so there's no OS-level enumeration by function — the caller opens
 * a specific VID:PID (+ optional Nth-matching-device index, since more than
 * one adapter can be plugged in at once). Common VID:PID pairs in the wild:
 *
 *     0x1D50:0x606F   "Geschwister Schneider CAN adapter" / canable.io's
 *                     candleLight-fw builds
 *     0x1209:0x2323   candleLight-fw's pid.codes-registered "candleLight" ID
 *                     (bytewerk.org), the default this driver's plugin uses
 *
 * open() takes both explicitly rather than hard-coding one — see the
 * plugin's CONFIG vid=/pid= tokens — since which pair a given adapter
 * actually enumerates under isn't otherwise knowable in advance; check
 * `lsusb`/Device Manager if unsure.
 *
 * Compatibility with Elmue's CANable 2.5 firmware
 * --------------------------------------------------
 * This driver was written against, and targets, the "legacy Geschwister
 * Schneider protocol" mode that
 * https://github.com/Elmue/CANable-2.5-firmware-Slcan-and-Candlelight's own
 * User & Developer Manual documents as staying available on USB interface 0
 * for backward compatibility, alongside that project's own extended
 * "ElmüSoft" protocol (per-frame USB overhead reduction, multi-frame USB
 * "blob" packing, host-side hardware filters, bus-load reporting, and more —
 * none of which this driver speaks; see that project's manual if any of
 * those are needed). Two real interop details this driver accounts for,
 * confirmed against that manual and the upstream Linux kernel gs_usb driver's
 * own history:
 *   - All multi-byte fields are little-endian on the wire, unconditionally —
 *     the HOST_FORMAT byte-order negotiation the original Geschwister
 *     Schneider firmware defined is not honoured by candleLight-derived
 *     firmware (including Elmue's), which always speaks little-endian
 *     regardless of what byte_order value the host sends. This driver still
 *     sends the HOST_FORMAT probe (matching every real host stack's
 *     behaviour) but never relies on the device actually switching modes.
 *   - DEVICE_CONFIG's reply can legitimately be shorter than its full
 *     12-byte definition (reserved1-3 + icount + sw_version + hw_version) —
 *     some firmwares only ever fill the first 8, omitting hw_version. A
 *     short USB control-IN completion is valid USB behaviour, not a transfer
 *     error, and probe() zero-initialises the buffer before reading so a
 *     short reply just leaves hw_version defaulted to 0 (see ctrl_in()'s doc
 *     comment).
 *
 * Protocol sequence
 * ------------------
 *   probe()        HOST_FORMAT (tells the device our byte order) →
 *                   DEVICE_CONFIG (channel count, versions) →
 *                   BT_CONST (clock rate + bit-timing limits + feature bits)
 *                   → if GS_CAN_FEATURE_FD is set, BT_CONST_EXT too (adds
 *                   the CAN-FD data-phase timing limits)
 *   set_bittiming() BITTIMING (nominal phase, always required)
 *   set_data_bittiming()  DATA_BITTIMING (data phase, CAN-FD only)
 *   open_channel()  MODE with mode=START (+ GS_CAN_MODE_* flags: listen-only,
 *                   loopback, triple-sample, one-shot, FD, pad-to-max,
 *                   berr-reporting)
 *   send_frame()/
 *   receive_frame() gs_host_frame structures over the bulk IN/OUT pair
 *                   (see below)
 *   close_channel() MODE with mode=RESET
 *
 * gs_host_frame and the echo_id TX-completion protocol
 * ------------------------------------------------------
 * Every frame — both directions — starts with the same 12-byte fixed
 * header (echo_id, can_id, can_dlc, channel, flags, reserved), followed by
 * a variable-length tail whose exact shape depends on two things this
 * driver negotiates once in probe()/open_channel() and then holds fixed
 * for the session: whether CAN-FD is in use (8-byte vs 64-byte data) and
 * whether the device tags frames with a hardware timestamp (data + 4 more
 * bytes). Because that tail shape is negotiated rather than fixed, this
 * driver serializes/deserializes gs_host_frame by hand (encode_frame() /
 * decode_frame() below) instead of overlaying a single C++ struct on the
 * wire bytes — see those functions' doc comments for the exact byte
 * layout used for each of the four tail shapes.
 *
 * echo_id has a dual role that has no equivalent in SLCAN/UCAN:
 *   - Host → device (TX): echo_id is a caller-chosen cookie (this driver
 *     uses a simple incrementing counter, wrapping before 0xFFFFFFFF).
 *   - Device → host: the device echoes that same frame back once the
 *     transmission completes ("TX-complete"), with echo_id unchanged, so
 *     the host can match completions to the frames it queued. A genuinely
 *     *received* bus frame is signalled with echo_id == 0xFFFFFFFF (all
 *     bits set, i.e. -1 as an unsigned 32-bit value) — there is no host
 *     cookie to echo for a frame the adapter didn't originate.
 *   send_frame() below folds this into the same synchronous, one-call-in
 *   one-call-out shape SLCAN/UCAN's send_frame() has: it writes the frame,
 *   then reads bulk-IN packets (silently absorbing any unrelated RX frames
 *   that happen to arrive first, exactly as candleLight's own driver does)
 *   until it either sees the matching TX-complete echo or times out.
 *   receive_frame() does the mirror image: it reads bulk-IN packets,
 *   silently absorbing any TX-complete echoes it sees, until it gets a
 *   frame with echo_id == 0xFFFFFFFF or times out.
 *
 * No on-device acceptance filtering
 * -----------------------------------
 * Unlike SLCAN/UCAN (one hardware standard + one hardware extended filter
 * slot each), gs_usb has **no filtering USB request at all** — every real
 * gs_usb host stack (the Linux kernel driver included) receives every
 * frame the bus carries and filters in software. This driver follows the
 * same model: there is no set_std_filter()/set_ext_filter() here at all
 * (compare UCAN, which has both). Acceptance filtering, if wanted, belongs
 * one layer up, in CandlelightFrameDriver — see candlelight_frame_driver.hpp.
 *
 * Class shape
 * -----------
 * Loosely mirrors SLCAN's/UCAN's public method surface (same send_frame()/
 * receive_frame()/open_channel()/close_channel() names and Status/CanFrame
 * semantics) so the plugin layer stays structurally close to slcan_plugin/
 * and ucan_plugin/ — but the configuration surface (set_bittiming(),
 * set_mode() taking a GS_CAN_MODE_* flag bitmask, no filter setters at all)
 * necessarily follows gs_usb's own shape rather than SLCAN/UCAN's, since
 * this driver is a faithful implementation of a protocol this codebase
 * doesn't own, not a new design of its own.
 */

#include "ICommDriver.hpp"

// Reused as-is — CanFrame describes a CAN frame's content, not how it
// travels on the wire, so it is exactly as valid for gs_usb's native-USB
// framing as it is for SLCAN's ASCII lines or UCAN's UART packets. See
// this file's header comment.
#include "uSlcan.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct libusb_context;
struct libusb_device_handle;


// ============================================================================
// gs_usb wire protocol — verbatim from the Linux kernel gs_usb driver /
// candleLight-fw. See uCandlelight.hpp's header comment for the source.
// ============================================================================

/// USB control-transfer bRequest values (bmRequestType: vendor, interface,
/// direction per-request — see the encode/probe functions below).
enum class GsUsbBreq : uint8_t {
    HOST_FORMAT       = 0,
    BITTIMING         = 1,
    MODE              = 2,
    BERR              = 3,
    BT_CONST          = 4,
    DEVICE_CONFIG     = 5,
    TIMESTAMP         = 6,
    IDENTIFY          = 7,
    GET_USER_ID       = 8,
    SET_USER_ID       = 9,
    DATA_BITTIMING    = 10,
    BT_CONST_EXT      = 11,
    SET_TERMINATION   = 12,
    GET_TERMINATION   = 13,
    GET_STATE         = 14,
};

enum class GsCanMode : uint32_t {
    RESET = 0,
    START = 1,
};

enum class GsCanState : uint32_t {
    ERROR_ACTIVE  = 0,
    ERROR_WARNING = 1,
    ERROR_PASSIVE = 2,
    BUS_OFF       = 3,
    STOPPED       = 4,
    SLEEPING      = 5,
};

/// struct gs_device_mode::flags and struct gs_device_config feature bits
/// (GS_CAN_MODE_* when writing MODE, GS_CAN_FEATURE_* when reading BT_CONST).
enum GsCanFeature : uint32_t {
    GS_CAN_MODE_NORMAL                   = 0,
    GS_CAN_MODE_LISTEN_ONLY               = 1u << 0,
    GS_CAN_MODE_LOOP_BACK                 = 1u << 1,
    GS_CAN_MODE_TRIPLE_SAMPLE             = 1u << 2,
    GS_CAN_MODE_ONE_SHOT                  = 1u << 3,
    GS_CAN_MODE_HW_TIMESTAMP              = 1u << 4,
    GS_CAN_MODE_PAD_PKTS_TO_MAX_PKT_SIZE  = 1u << 7,
    GS_CAN_MODE_FD                        = 1u << 8,
    GS_CAN_MODE_BERR_REPORTING            = 1u << 12,

    GS_CAN_FEATURE_LISTEN_ONLY             = 1u << 0,
    GS_CAN_FEATURE_LOOP_BACK               = 1u << 1,
    GS_CAN_FEATURE_TRIPLE_SAMPLE           = 1u << 2,
    GS_CAN_FEATURE_ONE_SHOT                = 1u << 3,
    GS_CAN_FEATURE_HW_TIMESTAMP            = 1u << 4,
    GS_CAN_FEATURE_IDENTIFY                = 1u << 5,
    GS_CAN_FEATURE_USER_ID                 = 1u << 6,
    GS_CAN_FEATURE_PAD_PKTS_TO_MAX_PKT_SIZE = 1u << 7,
    GS_CAN_FEATURE_FD                      = 1u << 8,
    GS_CAN_FEATURE_BT_CONST_EXT            = 1u << 10,
    GS_CAN_FEATURE_TERMINATION             = 1u << 11,
    GS_CAN_FEATURE_BERR_REPORTING          = 1u << 12,
    GS_CAN_FEATURE_GET_STATE               = 1u << 13,
};

/// struct gs_host_frame::flags
enum GsCanFlag : uint8_t {
    GS_CAN_FLAG_OVERFLOW = 1u << 0,
    GS_CAN_FLAG_FD       = 1u << 1,
    GS_CAN_FLAG_BRS      = 1u << 2,
    GS_CAN_FLAG_ESI      = 1u << 3,
};

/// SocketCAN-convention flag bits inside struct gs_host_frame::can_id
/// (same convention Linux's <linux/can.h> uses — gs_usb reuses it verbatim
/// rather than defining its own).
static constexpr uint32_t GS_CAN_EFF_FLAG = 0x80000000u; ///< extended (29-bit) id
static constexpr uint32_t GS_CAN_RTR_FLAG = 0x40000000u; ///< remote frame
static constexpr uint32_t GS_CAN_ERR_FLAG = 0x20000000u; ///< error frame (RX only)
static constexpr uint32_t GS_CAN_SFF_MASK = 0x000007FFu;
static constexpr uint32_t GS_CAN_EFF_MASK = 0x1FFFFFFFu;

/// echo_id value meaning "this is a genuinely received bus frame", not a
/// TX-completion echo — see uCandlelight.hpp's "echo_id" section.
static constexpr uint32_t GS_CAN_ECHO_ID_RX = 0xFFFFFFFFu;

#pragma pack(push, 1)

/// USB_DIR_OUT control transfer payload for GsUsbBreq::HOST_FORMAT.
struct GsHostConfig {
    uint32_t byte_order; ///< little-endian on the wire; 0x0000BEEF tells the
                          ///< device "host is little-endian" (candleLight-fw
                          ///< only ever speaks little-endian regardless, but
                          ///< every real host stack still sends this probe).
};

/// USB_DIR_IN control transfer response for GsUsbBreq::DEVICE_CONFIG.
struct GsDeviceConfig {
    uint8_t  reserved1;
    uint8_t  reserved2;
    uint8_t  reserved3;
    uint8_t  icount;      ///< number of CAN channels minus 1
    uint32_t sw_version;
    uint32_t hw_version;
};

/// USB_DIR_OUT control transfer payload for GsUsbBreq::MODE.
struct GsDeviceMode {
    uint32_t mode;   ///< GsCanMode
    uint32_t flags;  ///< GS_CAN_MODE_* bitmask
};

/// USB_DIR_IN control transfer response for GsUsbBreq::GET_STATE
/// (only if GsDeviceBtConst::feature has GS_CAN_FEATURE_GET_STATE).
struct GsDeviceState {
    uint32_t state;  ///< GsCanState
    uint32_t rxerr;
    uint32_t txerr;
};

/// USB_DIR_OUT control transfer payload for GsUsbBreq::BITTIMING /
/// GsUsbBreq::DATA_BITTIMING.
struct GsDeviceBittiming {
    uint32_t prop_seg;
    uint32_t phase_seg1;
    uint32_t phase_seg2;
    uint32_t sjw;
    uint32_t brp;
};

/// USB_DIR_IN control transfer response for GsUsbBreq::BT_CONST.
struct GsDeviceBtConst {
    uint32_t feature;    ///< GS_CAN_FEATURE_* bitmask
    uint32_t fclk_can;   ///< CAN clock in Hz — the basis for every bit-timing calculation
    uint32_t tseg1_min;
    uint32_t tseg1_max;
    uint32_t tseg2_min;
    uint32_t tseg2_max;
    uint32_t sjw_max;
    uint32_t brp_min;
    uint32_t brp_max;
    uint32_t brp_inc;
};

/// USB_DIR_IN control transfer response for GsUsbBreq::BT_CONST_EXT
/// (only if GsDeviceBtConst::feature has GS_CAN_FEATURE_FD).
struct GsDeviceBtConstExtended {
    uint32_t feature;
    uint32_t fclk_can;
    uint32_t tseg1_min;
    uint32_t tseg1_max;
    uint32_t tseg2_min;
    uint32_t tseg2_max;
    uint32_t sjw_max;
    uint32_t brp_min;
    uint32_t brp_max;
    uint32_t brp_inc;
    // Data-phase (CAN-FD) limits:
    uint32_t dtseg1_min;
    uint32_t dtseg1_max;
    uint32_t dtseg2_min;
    uint32_t dtseg2_max;
    uint32_t dsjw_max;
    uint32_t dbrp_min;
    uint32_t dbrp_max;
    uint32_t dbrp_inc;
};

#pragma pack(pop)


// ============================================================================
// Candlelight driver class
// ============================================================================

/**
 * @brief Candlelight (gs_usb) driver — see uCandlelight.hpp's header comment
 *        for the protocol this implements.
 */
class Candlelight : public ICommDriver
{
public:

    static constexpr uint32_t CANDLELIGHT_DEFAULT_TIMEOUT = 1000; ///< ms

    Candlelight() = default;

    /**
     * @brief Construct and immediately open+probe the device.
     * @param vendor_id   USB VID (e.g. 0x1D50 or 0x1209 — see header comment)
     * @param product_id  USB PID (e.g. 0x606F or 0x2323)
     * @param device_index  which matching device to open, if more than one
     *                      gs_usb-compatible adapter is plugged in (0 = first)
     * @param strIdentityLabel  Display text for the GUI comm-dump panel
     */
    Candlelight(uint16_t vendor_id, uint16_t product_id, unsigned device_index,
                const std::string& strIdentityLabel = {});

    virtual ~Candlelight();

    // ------------------------------------------------------------------
    // Port management
    // ------------------------------------------------------------------

    /// Opens the USB device and runs the probe() sequence (HOST_FORMAT →
    /// DEVICE_CONFIG → BT_CONST [→ BT_CONST_EXT if FD-capable]).
    Status open(uint16_t vendor_id, uint16_t product_id, unsigned device_index);
    Status close();
    bool is_open() const override;

    CommDetails describeConnection(std::string_view /*xtra_params*/ = {}) const override
    {
        return commdump_details(CommFamily::CAN,
                                 m_strIdentityLabel.empty() ? "Candlelight" : m_strIdentityLabel);
    }

    /// Populated by probe(); zero-initialised (all-0 feature bits, so every
    /// is_*_supported() below reads false) until a successful open().
    const GsDeviceConfig&          device_config() const  { return m_devConfig; }
    const GsDeviceBtConst&         bt_const() const        { return m_btConst; }
    const GsDeviceBtConstExtended& bt_const_ext() const    { return m_btConstExt; }

    bool is_fd_supported() const          { return (m_btConst.feature & GS_CAN_FEATURE_FD) != 0; }
    bool is_get_state_supported() const   { return (m_btConst.feature & GS_CAN_FEATURE_GET_STATE) != 0; }
    bool is_termination_supported() const { return (m_btConst.feature & GS_CAN_FEATURE_TERMINATION) != 0; }

    // ------------------------------------------------------------------
    // Channel configuration  (must be called before open_channel)
    // ------------------------------------------------------------------

    Status set_bittiming(uint32_t prop_seg, uint32_t phase_seg1, uint32_t phase_seg2,
                         uint32_t sjw, uint32_t brp, uint32_t timeout_ms = CANDLELIGHT_DEFAULT_TIMEOUT);
    Status set_data_bittiming(uint32_t prop_seg, uint32_t phase_seg1, uint32_t phase_seg2,
                              uint32_t sjw, uint32_t brp, uint32_t timeout_ms = CANDLELIGHT_DEFAULT_TIMEOUT);

    /**
     * @brief Convenience wrapper: derive prop_seg/phase_seg1/phase_seg2/brp
     *        for a target nominal bit rate from this device's queried
     *        bt_const() (fclk_can + tseg/brp limits) and call
     *        set_bittiming(). sjw is set to min(phase_seg2, bt_const().sjw_max).
     * @param bitrate_bps    Target nominal bit rate, e.g. 500000
     * @param sample_point   Target sample point, 0.0-1.0 (0.875 = 87.5%, the
     *                       usual CAN default and this function's default)
     * @return false if no (brp, tseg1, tseg2) combination within this
     *         device's limits reproduces bitrate_bps exactly
     */
    Status set_bitrate(uint32_t bitrate_bps, double sample_point = 0.875,
                       uint32_t timeout_ms = CANDLELIGHT_DEFAULT_TIMEOUT);

    /// Same as set_bitrate(), but for the CAN-FD data phase via bt_const_ext().
    Status set_fd_data_bitrate(uint32_t bitrate_bps, double sample_point = 0.75,
                               uint32_t timeout_ms = CANDLELIGHT_DEFAULT_TIMEOUT);

    // ------------------------------------------------------------------
    // Channel open / close
    // ------------------------------------------------------------------

    /// @param mode_flags  GS_CAN_MODE_* bitmask (listen-only, loopback,
    ///                    triple-sample, one-shot, FD, pad-to-max, berr-reporting)
    Status open_channel(uint32_t mode_flags = GS_CAN_MODE_NORMAL,
                        uint32_t timeout_ms = CANDLELIGHT_DEFAULT_TIMEOUT);
    Status close_channel(uint32_t timeout_ms = CANDLELIGHT_DEFAULT_TIMEOUT);

    // ------------------------------------------------------------------
    // Diagnostic queries
    // ------------------------------------------------------------------

    /// Requires is_get_state_supported(); Status::OPERATION_FAILED otherwise.
    Status get_state(GsDeviceState& state, uint32_t timeout_ms = CANDLELIGHT_DEFAULT_TIMEOUT);

    // ------------------------------------------------------------------
    // Frame TX / RX  (typed, preferred API)
    // ------------------------------------------------------------------

    /**
     * @brief Send one frame and wait for its TX-complete echo — see
     *        uCandlelight.hpp's "echo_id" section for why this needs to
     *        both write and then read.
     */
    Status send_frame(const CanFrame& frame, uint32_t timeout_ms = CANDLELIGHT_DEFAULT_TIMEOUT);

    /**
     * @brief Wait for the next genuinely-received bus frame (echo_id ==
     *        GS_CAN_ECHO_ID_RX), silently absorbing any TX-complete echoes
     *        seen along the way.
     */
    Status receive_frame(CanFrame& frame, uint32_t timeout_ms = CANDLELIGHT_DEFAULT_TIMEOUT);

    // ------------------------------------------------------------------
    // ICommDriver generic interface (raw bulk passthrough)
    // ------------------------------------------------------------------

    /// Raw bulk-IN read of one gs_host_frame packet, whatever its echo_id
    /// (i.e. this can hand back either a TX-complete echo or an RX frame —
    /// see receive_frame() for the typed, echo-filtering version).
    ReadResult tout_read(uint32_t u32ReadTimeout,
                         std::span<uint8_t> buffer,
                         const ReadOptions& options,
                         std::string_view xtra_params = {}) const override;

    /// Raw bulk-OUT write of one already-encoded gs_host_frame packet.
    WriteResult tout_write(uint32_t u32WriteTimeout,
                           std::span<const uint8_t> buffer,
                           std::string_view xtra_params = {}) const override;

    // ------------------------------------------------------------------
    // Encoding / decoding helpers (static where they don't depend on the
    // negotiated FD/hw-timestamp tail shape; member where they do)
    // ------------------------------------------------------------------

    /**
     * @brief Encode a CAN frame to a gs_host_frame packet using this
     *        session's negotiated tail shape (FD vs classic; hw-timestamp
     *        is never requested by this driver, so the timestamp field is
     *        never written — see open_channel()).
     * @param echo_id  Caller-chosen TX cookie (send_frame() supplies its
     *                 own internal counter; exposed here for tout_write()
     *                 callers building a packet by hand)
     * @param frame    Frame to encode
     * @param[out] out Output buffer; must be at least max_packet_len() bytes
     * @return Number of bytes written, or 0 on error (payload too long for
     *         the negotiated mode, or channel not open)
     */
    size_t encode_frame(uint32_t echo_id, const CanFrame& frame, std::span<uint8_t> out) const;

    /**
     * @brief Decode a gs_host_frame packet (either a TX-complete echo or an
     *        RX frame — check echo_id) using this session's negotiated tail
     *        shape.
     * @param[out] echo_id  GS_CAN_ECHO_ID_RX for a genuine RX frame, or the
     *                      original TX echo_id for a TX-complete echo
     * @param[out] frame    Decoded frame
     * @return true on success
     */
    bool decode_frame(const uint8_t* pkt, size_t len, uint32_t& echo_id, CanFrame& frame) const;

    /// Fixed header size common to every gs_host_frame, any tail shape.
    static constexpr size_t GS_HOST_FRAME_HDR_LEN = 12; // echo_id(4)+can_id(4)+can_dlc(1)+channel(1)+flags(1)+reserved(1)

    /// Largest possible packet this session can produce/consume: header +
    /// 64-byte CAN-FD payload + 4-byte hw timestamp (never both FD-off and
    /// timestamp-on differ in a way that exceeds this).
    size_t max_packet_len() const { return GS_HOST_FRAME_HDR_LEN + 64 + 4; }

private:

    // ------------------------------------------------------------------
    // Internal helpers
    // ------------------------------------------------------------------

    Status probe();

    Status ctrl_out(GsUsbBreq req, uint16_t value, const void* data, uint16_t len, uint32_t timeout_ms);
    Status ctrl_in(GsUsbBreq req, uint16_t value, void* data, uint16_t len, uint32_t timeout_ms);

    Status bulk_write_frame(uint32_t echo_id, const CanFrame& frame, uint32_t timeout_ms);
    /// Reads exactly one bulk-IN packet, decodes it, and reports whether it
    /// was an RX frame or a TX-complete echo — the shared core of both
    /// send_frame()'s echo-wait loop and receive_frame()'s RX-wait loop.
    Status bulk_read_one(uint32_t& echo_id, CanFrame& frame, uint32_t timeout_ms);

    // ------------------------------------------------------------------
    // Members
    // ------------------------------------------------------------------

    libusb_context*       m_usbCtx    = nullptr;
    libusb_device_handle* m_usbHandle = nullptr;
    uint8_t                m_epIn      = 0; ///< bulk IN endpoint address (with USB_DIR_IN bit set), discovered from the device's descriptors
    uint8_t                m_epOut     = 0; ///< bulk OUT endpoint address
    int                    m_interfaceNum = 0;

    GsDeviceConfig          m_devConfig{};
    GsDeviceBtConst         m_btConst{};
    GsDeviceBtConstExtended m_btConstExt{};

    bool     m_channel_open = false;
    bool     m_fd_negotiated = false;    ///< set by open_channel() from mode_flags & GS_CAN_MODE_FD
    uint32_t m_next_echo_id  = 0;
    std::string m_strIdentityLabel;
};

#endif // U_CANDLELIGHT_DRIVER_HPP
