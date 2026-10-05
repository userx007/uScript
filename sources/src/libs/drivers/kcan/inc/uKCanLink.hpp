#ifndef U_KCAN_LINK_H
#define U_KCAN_LINK_H

#include <cstdint>
#include <string>

/**
 * @file uKCanLink.hpp
 * @brief SocketCAN *network-interface* control (bitrate, link up/down, state).
 *
 * The KCAN frame driver (uKCan.hpp) only moves frames through a raw PF_CAN
 * socket. A real CAN adapter (PEAK PCAN-USB, Kvaser, gs_usb/candleLight,
 * on-board controllers ...) additionally has to be told its bus speed
 * before it is brought up - exactly what
 *
 *     ip link set can0 type can bitrate 500000
 *     ip link set can0 up
 *
 * does. This module does the same thing directly over an rtnetlink socket
 * (NETLINK_ROUTE), so it needs neither the `ip` binary nor libsocketcan.
 *
 * Privileges:
 *   Changing bit-timing / link state needs CAP_NET_ADMIN (root, or
 *   `setcap cap_net_admin+ep <binary>`). *Reading* the state does not.
 *   All functions below return an errno value (0 = success), so callers can
 *   tell EPERM ("run as root / grant CAP_NET_ADMIN") from EBUSY ("interface
 *   is up, bring it down first") from ENODEV ("no such interface") etc.
 *
 * Virtual interfaces:
 *   vcan0 & co. have no bit-timing. link_apply() detects a non-"can" link
 *   kind, skips the bit-timing part and only makes sure the link is up, so
 *   the same configuration works against both real and virtual interfaces.
 */
namespace kcan {

    /** @brief CAN controller mode flags (identical values to linux/can/netlink.h CAN_CTRLMODE_*). */
    enum CtrlMode : uint32_t {
        CTRLMODE_LOOPBACK        = 0x01, ///< controller-internal loopback (no bus needed - handy for self tests)
        CTRLMODE_LISTEN_ONLY     = 0x02, ///< silent mode: never transmits, never ACKs
        CTRLMODE_TRIPLE_SAMPLING = 0x04, ///< sample every bit three times
        CTRLMODE_ONE_SHOT        = 0x08, ///< no automatic retransmission
        CTRLMODE_BERR_REPORTING  = 0x10, ///< report bus errors as error frames
        CTRLMODE_FD              = 0x20, ///< CAN FD mode
        CTRLMODE_PRESUME_ACK     = 0x40, ///< ignore missing ACK
        CTRLMODE_FD_NON_ISO      = 0x80, ///< non-ISO CAN FD
    };

    /** @brief What the caller wants the interface to look like. 0 / empty = "leave as is". */
    struct LinkConfig {
            uint32_t bitrate         = 0; ///< nominal bitrate in bit/s (e.g. 125000, 250000, 500000, 1000000); 0 = untouched
            uint32_t samplePoint     = 0; ///< nominal sample point in permille (875 = 87.5 %); 0 = kernel default
            uint32_t dataBitrate     = 0; ///< CAN FD data-phase bitrate in bit/s; 0 = untouched
            uint32_t dataSamplePoint = 0; ///< CAN FD data-phase sample point in permille; 0 = kernel default
            bool restartMsSet        = false;
            uint32_t restartMs       = 0; ///< automatic bus-off recovery delay in ms (0 = off); only used if restartMsSet
            uint32_t ctrlModeMask    = 0; ///< which CtrlMode bits the caller cares about
            uint32_t ctrlModeFlags   = 0; ///< value of those bits (bit set in mask+flags = on, set in mask only = off)

            /** @brief true when nothing is requested at all */
            bool empty() const
            {
                return bitrate == 0 && dataBitrate == 0 && !restartMsSet && ctrlModeMask == 0;
            }
    };

    /** @brief Snapshot of an interface as reported by the kernel. */
    struct LinkStatus {
            bool exists = false;
            std::string kind;             ///< "can", "vcan", "" (e.g. a slcan tty netdev) ...
            bool isCan           = false; ///< kind == "can" (bit-timing settable)
            bool up              = false; ///< IFF_UP
            bool lowerUp         = false; ///< IFF_LOWER_UP (carrier: controller is active on the bus)
            uint32_t mtu         = 0;     ///< 16 = classic CAN, 72 = CAN FD
            uint32_t state       = 0;     ///< CAN_STATE_* (0 = ERROR-ACTIVE ... 4 = BUS-OFF, 5 = STOPPED, 6 = SLEEPING)
            bool hasState        = false;

            uint32_t bitrate     = 0;
            uint32_t samplePoint = 0; ///< permille
            uint32_t tq = 0, propSeg = 0, phaseSeg1 = 0, phaseSeg2 = 0, sjw = 0, brp = 0;

            uint32_t dataBitrate     = 0;
            uint32_t dataSamplePoint = 0;

            uint32_t ctrlModeMask    = 0; ///< modes the driver *supports*
            uint32_t ctrlModeFlags   = 0; ///< modes currently *enabled*
            uint32_t restartMs       = 0;
            uint32_t clockHz         = 0;
            bool hasBerr             = false;
            uint16_t txErr = 0, rxErr = 0;
    };

    /** @brief Read the current state of @p strIface. No privileges needed. */
    int link_get_status(const std::string &strIface, LinkStatus &sStatus);

    /** @brief Set IFF_UP on/off. Same as `ip link set <iface> up|down`. */
    int link_set_up(const std::string &strIface, bool bUp);

    /**
     * @brief Write bit-timing / controller-mode settings. Interface must be DOWN (EBUSY otherwise).
     *        Same as `ip link set <iface> type can bitrate .. sample-point .. dbitrate .. fd on ..`.
     *        The kernel derives brp/prop_seg/phase_seg/sjw from bitrate + sample point itself.
     */
    int link_configure(const std::string &strIface, const LinkConfig &sCfg);

    /**
     * @brief High-level "make it so" call used by the plugin.
     *
     *   - reads the interface; if it already is up and already matches @p sCfg,
     *     nothing is touched (bChanged = false) - safe to call before every use, and
     *     it works without privileges when the interface was preconfigured elsewhere
     *   - otherwise: down -> link_configure() -> up
     *   - for non-"can" kinds (vcan) the bit-timing part is skipped, only "up" is ensured
     *
     * @param[out] bChanged  true if the interface was actually reconfigured / brought up
     * @return 0 or errno
     */
    int link_apply(const std::string &strIface, const LinkConfig &sCfg, bool &bChanged);

    /** @brief Restart a bus-off controller (`ip link set <iface> type can restart`). Needs restart-ms == 0 & iface up. */
    int link_restart(const std::string &strIface);

    /** @brief true if the interface already satisfies @p sCfg (used by link_apply(), exposed for tests). */
    bool link_matches(const LinkStatus &sStatus, const LinkConfig &sCfg);

    /** @brief "ERROR-ACTIVE", "ERROR-WARNING", "ERROR-PASSIVE", "BUS-OFF", "STOPPED", "SLEEPING" */
    const char *state_name(uint32_t u32State);

    /** @brief One-line, human readable (and script friendly, key=value) rendering of @p sStatus. */
    std::string format_status(const std::string &strIface, const LinkStatus &sStatus);

    /** @brief strerror() with a hint for the common failures (EPERM, EBUSY, ENODEV, EOPNOTSUPP ...). */
    std::string describe_errno(int iErrno);

} // namespace kcan

#endif // U_KCAN_LINK_H
