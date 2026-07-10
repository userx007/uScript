#ifndef U_RAWETH_DRIVER_H
#define U_RAWETH_DRIVER_H

#include "ICommDriver.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <array>
#include <mutex>
#include <cstdint>


/**
 * @brief Raw (layer-2) Ethernet driver implementing the ICommDriver interface.
 *
 * Sends and receives raw Ethernet frames on a given interface (e.g. "eth0")
 * via an AF_PACKET/SOCK_RAW socket, bypassing the kernel's IP stack
 * entirely. Like CAN, and unlike TCP/UART, this is a FRAME based transport:
 * every read returns (at most) one frame's payload and every write emits
 * exactly one frame. There is no stream to reassemble at this layer.
 *
 * buffer semantics:
 *   The `buffer` passed to tout_read()/tout_write() is the Ethernet PAYLOAD
 *   only. The driver builds/strips the 14-byte L2 header (6-byte dest MAC +
 *   6-byte source MAC + 2-byte EtherType) itself, so callers never see MAC
 *   addresses in `buffer` — this keeps the call surface consistent with the
 *   byte-oriented drivers (UART/I2C/SPI/TCP) rather than leaking L2 framing
 *   into every call site.
 *
 *   tout_write()
 *     Builds one frame (dest MAC + own interface MAC + EtherType + payload)
 *     and issues a single sendto(2). This mirrors the CAN driver's "single
 *     frame enqueue" model rather than TCP's stream-loop: a raw Ethernet
 *     write is one atomic frame, not a byte range that can be short-written.
 *     poll(2) + retry-on-EAGAIN is still used to bound the wait against
 *     u32WriteTimeout, since a saturated NIC TX queue can transiently return
 *     EAGAIN even though the "write" itself is all-or-nothing.
 *
 *   tout_read() — ReadMode::Exact
 *     One poll(2) + recvfrom(2) pair. Receives exactly one frame, strips
 *     the L2 header, and copies up to buffer.size() bytes of the payload
 *     into buffer. If the frame's payload is larger than buffer.size(),
 *     the remainder is discarded (same trade-off as the CAN driver
 *     discarding excess frame bytes) — this is a "one frame per call"
 *     driver, not a "fill the buffer" one.
 *
 *   tout_read() — ReadMode::UntilDelimiter
 *     Receives frames one at a time, appending each frame's payload to
 *     buffer, until the delimiter byte is found within a frame's payload or
 *     the buffer is full. NOTE: any bytes received after the delimiter
 *     within the same frame are discarded, same as TCP/CAN.
 *
 *   tout_read() — ReadMode::UntilToken
 *     Streams frame payloads (frame by frame, byte by byte within each
 *     frame) through the KMP algorithm to detect the token sequence.
 *     bytes_read = 0 on return.
 *
 * xtra_params:
 *   Raw Ethernet has no notion of a "connection" — every write needs an
 *   explicit destination, the same way a CAN ID selects a frame or an I2C
 *   address selects a slave. open() configures a default destination MAC
 *   and EtherType; xtra_params, when non-empty, overrides them for that one
 *   call. Accepted formats:
 *     "AA:BB:CC:DD:EE:FF"          - override destination MAC only
 *     "AA:BB:CC:DD:EE:FF/0800"     - override destination MAC and EtherType
 *                                    (EtherType is hex, optional "0x" prefix)
 *   A malformed xtra_params value is logged at WARNING level and the
 *   configured default is used instead (the call is not failed outright).
 *   tout_read() ignores xtra_params entirely — the bound socket already
 *   filters to this interface + EtherType, and Ethernet has no equivalent
 *   of a CAN RX filter ID to further narrow incoming frames per call.
 *
 * Timeout handling:
 *   poll(2) on the socket fd before every recvfrom(2)/sendto(2).
 *   u32*Timeout = 0 selects the respective DEFAULT_TIMEOUT constant.
 *
 * Thread safety:
 *   All public methods are protected by an internal mutex. Unlike TCP,
 *   tout_read()/tout_write() do NOT release the lock around blocking I/O —
 *   each call is a single bounded frame operation (poll + one syscall, or a
 *   short bounded loop of frames for UntilDelimiter/UntilToken), not an
 *   indefinite stream read, so holding the lock for the duration is
 *   consistent with the CAN driver's approach.
 *
 * Requirements:
 *   AF_PACKET raw sockets require CAP_NET_RAW (or root). This driver is
 *   Linux-only; there is no portable BSD sockets equivalent of AF_PACKET,
 *   so — like uKVCan — it does not ship a macOS backend.
 */
class RawEth : public ICommDriver
{
    public:

        static constexpr size_t   RAWETH_MAC_ADDR_LEN          = 6;      /**< Bytes in a MAC address.                    */
        static constexpr size_t   RAWETH_ETH_HEADER_LEN         = 14;    /**< dst(6) + src(6) + EtherType(2).            */
        static constexpr size_t   RAWETH_MAX_PAYLOAD            = 1500;  /**< Standard (non-jumbo) Ethernet MTU payload. */
        static constexpr size_t   RAWETH_MAX_FRAME_LEN          = RAWETH_ETH_HEADER_LEN + RAWETH_MAX_PAYLOAD;
        static constexpr size_t   RAWETH_MAX_BUFLENGTH          = 256;   /**< Max assembled buffer length (delimiter/token modes). */
        static constexpr uint16_t RAWETH_DEFAULT_ETHERTYPE      = 0x88B5;/**< IEEE 802 "Local Experimental Ethertype 1". */
        static constexpr uint32_t RAWETH_READ_DEFAULT_TIMEOUT   = 5000;  /**< Default read timeout in milliseconds.      */
        static constexpr uint32_t RAWETH_WRITE_DEFAULT_TIMEOUT  = 5000;  /**< Default write timeout in milliseconds.     */

        using MacAddr = std::array<uint8_t, RAWETH_MAC_ADDR_LEN>;

        static constexpr MacAddr RAWETH_BROADCAST_MAC = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

        RawEth() = default;

        /**
         * @brief Construct and immediately bind to strIfaceName.
         * @param strIfaceName      Interface name, e.g. "eth0".
         * @param defaultDestMac    Default destination MAC for writes (broadcast if omitted).
         * @param u16EtherType      EtherType to bind/send with (0 = use RAWETH_DEFAULT_ETHERTYPE).
         * @param bPromiscuous      Put the interface into promiscuous mode while open.
         */
        explicit RawEth(const std::string& strIfaceName,
                        const MacAddr& defaultDestMac = RAWETH_BROADCAST_MAC,
                        uint16_t u16EtherType = 0,
                        bool bPromiscuous = false)
        {
            open(strIfaceName, defaultDestMac, u16EtherType, bPromiscuous);
        }

        virtual ~RawEth()
        {
            close();
        }

        /**
         * @brief Resolve strIfaceName, open an AF_PACKET/SOCK_RAW socket, and
         *        bind it to that interface + EtherType.
         *
         * @param strIfaceName    Interface name, e.g. "eth0".
         * @param defaultDestMac  Default destination MAC used when xtra_params
         *                        does not override it (broadcast if omitted).
         * @param u16EtherType    EtherType to bind/send with (0 = use RAWETH_DEFAULT_ETHERTYPE).
         * @param bPromiscuous    Put the interface into promiscuous mode while open;
         *                        restored to its prior state on close().
         * @return Status::SUCCESS or an error code.
         */
        Status open(const std::string& strIfaceName,
                   const MacAddr& defaultDestMac = RAWETH_BROADCAST_MAC,
                   uint16_t u16EtherType = 0,
                   bool bPromiscuous = false);

        /**
         * @brief Close the socket, restoring promiscuous mode if this driver set it.
         * @return Status::SUCCESS.
         */
        Status close();

        /**
         * @brief Check whether the socket is open (bound).
         * @return true if the socket fd is valid.
         */
        bool is_open() const override;

        /**
         * @brief The interface's own MAC address, used as the source MAC on writes.
         *        Only valid while open (all-zero otherwise).
         */
        MacAddr local_mac() const;

        /**
         * @brief Unified read interface supporting multiple operation modes.
         *
         * @param u32ReadTimeout  Timeout in milliseconds (0 = use default).
         * @param buffer          Buffer to receive the frame PAYLOAD into (no L2 header).
         * @param options         Read operation configuration.
         * @param xtra_params     Unused by tout_read() — see class docs. Any
         *                        non-empty value is logged and otherwise ignored.
         * @return ReadResult containing status, bytes read, and terminator found flag.
         *
         * @details
         * - ReadMode::Exact:          One recvfrom(2); one frame's payload, up to buffer.size().
         * - ReadMode::UntilDelimiter: Accumulate payload bytes across frames until delimiter found.
         * - ReadMode::UntilToken:     KMP search across streamed frame payloads; bytes_read = 0.
         */
        ReadResult tout_read(uint32_t u32ReadTimeout,
                             std::span<uint8_t> buffer,
                             const ReadOptions& options,
                             std::string_view xtra_params = {}) const override;

        /**
         * @brief Unified write interface. Sends buffer as one frame's payload.
         *
         * @param u32WriteTimeout  Timeout in milliseconds (0 = use default).
         * @param buffer           Frame payload to send (max RAWETH_MAX_PAYLOAD bytes).
         * @param xtra_params      Optional per-call destination MAC / EtherType
         *                         override; see class docs for format.
         * @return WriteResult containing status and bytes written (payload bytes, not framed bytes).
         */
        WriteResult tout_write(uint32_t u32WriteTimeout,
                               std::span<const uint8_t> buffer,
                               std::string_view xtra_params = {}) const override;

    private:

        int                m_iHandle              = -1;    /**< AF_PACKET socket file descriptor.        */
        int                m_iIfIndex             = -1;    /**< Interface index (SIOCGIFINDEX).          */
        MacAddr            m_ownMac               = {};    /**< This interface's own MAC address.        */
        MacAddr            m_defaultDestMac       = RAWETH_BROADCAST_MAC; /**< Default destination MAC. */
        uint16_t           m_u16EtherType         = RAWETH_DEFAULT_ETHERTYPE; /**< Bound/default EtherType. */
        bool               m_bPromiscSetByUs      = false; /**< Whether open() enabled promiscuous mode. */
        mutable std::mutex m_mutex;                        /**< Protects concurrent access.               */

        // -----------------------------------------------------------------------
        // Internal transport primitives
        // -----------------------------------------------------------------------

        /**
         * @brief Receive exactly one frame in one poll(2) + recvfrom(2) pair,
         * strip the L2 header, and copy up to buffer.size() bytes of the
         * payload into buffer. szBytesRead is set to the number of payload
         * bytes actually copied (which may be less than the frame's full
         * payload if buffer is smaller).
         */
        Status timeout_read(uint32_t u32ReadTimeout,
                            std::span<uint8_t> buffer,
                            size_t& szBytesRead) const;

        /**
         * @brief Accumulate received frame payloads until cDelimiter is found
         * or the buffer is full. Null-terminates on Status::SUCCESS.
         */
        Status timeout_read_until(uint32_t u32ReadTimeout,
                                  std::span<uint8_t> buffer,
                                  uint8_t cDelimiter,
                                  size_t& szBytesRead) const;

        /**
         * @brief Stream frame payloads off the socket, applying the KMP
         * algorithm to detect the token sequence.
         */
        Status timeout_wait_for_token(uint32_t u32ReadTimeout,
                                      std::span<const uint8_t> token,
                                      bool useBuffer) const;

        /**
         * @brief Build one Ethernet frame (dest MAC + own MAC + EtherType +
         * payload) and send it as a single, poll(2)-bounded sendto(2),
         * retrying on EAGAIN until u32WriteTimeout elapses.
         */
        Status timeout_write(uint32_t u32WriteTimeout,
                             std::span<const uint8_t> buffer,
                             const MacAddr& destMac,
                             uint16_t u16EtherType,
                             size_t& szBytesWritten) const;

        /**
         * @brief Parse an xtra_params override string ("AA:BB:CC:DD:EE:FF" or
         * "AA:BB:CC:DD:EE:FF/0800") into a destination MAC and EtherType.
         * Falls back to the configured defaults (and logs a WARNING) on any
         * parse failure rather than failing the call.
         */
        void resolve_destination(std::string_view xtra_params,
                                 MacAddr& outDestMac,
                                 uint16_t& outEtherType) const;

        // -----------------------------------------------------------------------
        // KMP helpers (identical strategy to the UART / I2C / SPI / CAN / TCP drivers)
        // -----------------------------------------------------------------------

        /** @brief Run KMP stream matching over bytes received from successive frames. */
        Status kmp_stream_match(std::span<const uint8_t> token,
                                const std::vector<int>& viLps,
                                uint32_t u32Timeout,
                                bool bReturnOnTimeout,
                                bool useBuffer) const;

        /** @brief Build the KMP failure-function table for @p pattern. */
        void build_kmp_table(std::span<const uint8_t> pattern,
                             size_t szLength,
                             std::vector<int>& viLps) const;
};


#endif // U_RAWETH_DRIVER_H
