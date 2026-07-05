#ifndef U_PCAN_DRIVER_H
#define U_PCAN_DRIVER_H

#include "ICommDriver.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <mutex>
#include <cstdint>

// PCAN-Basic API header — supplied by PEAK-System alongside the driver.
// On Linux:  /usr/include/PCAN-Basic/PCANBasic.h  (or pcan.h for the older ioctl API)
// On Windows: PCANBasic.h from the PCAN-Basic SDK
#if defined(_WIN32)
#  include <PCANBasic.h>
#else
#  include <PCANBasic.h>    // same SDK layout on Linux when installed via peak-system packages
#endif


/**
 * @brief PCAN-Basic driver wrapper implementing ICommDriver.
 *
 * Maps the byte-stream ICommDriver interface onto CAN frames.  Because CAN is
 * a message-oriented bus (not a byte stream) the following conventions apply:
 *
 *  Writing
 *  -------
 *  - The caller supplies raw payload bytes in `buffer`.
 *  - Bytes are packed into consecutive CAN frames, up to 8 bytes each (Classic
 *    CAN) or 64 bytes each (CAN FD, if m_bFD is true).
 *  - The CAN ID used for every outgoing frame is `m_u32DefaultTxId` unless the
 *    caller passes a non-empty `xtra_params` string whose first token is a
 *    decimal or 0x-prefixed hex CAN ID (e.g. "0x18FF50E5" or "123").
 *  - Extended frame format (29-bit) is used when the ID > 0x7FF, or when
 *    m_bExtendedId is forced to true.
 *
 *  Reading
 *  -------
 *  - ReadMode::Exact       — accumulate payload bytes from successive frames
 *    until `buffer` is full or the timeout expires.
 *  - ReadMode::UntilDelimiter — accumulate bytes from frames, searching for
 *    the delimiter byte; null-terminates on match.
 *  - ReadMode::UntilToken  — accumulate bytes from frames, applying KMP to
 *    find the token sequence.
 *  - An optional RX filter CAN ID may be supplied via `xtra_params`; if given
 *    it overrides `m_u32DefaultRxFilterId` for that call only.
 *    Pass an empty string to use the default (accept all / pre-configured).
 *
 *  Channel naming
 *  --------------
 *  `open()` accepts the PCAN channel handle as a decimal or 0x-hex string,
 *  e.g. "0x51" (PCAN_USBBUS1) or "81" (same value in decimal).
 *  Alternatively the convenience constants defined by PCANBasic.h may be used
 *  via the numeric overload.
 */
class PCAN : public ICommDriver
{

    public:

        // ------------------------------------------------------------------ //
        //  Constants                                                           //
        // ------------------------------------------------------------------ //

        static constexpr size_t   PCAN_MAX_PAYLOAD          = 8;     ///< Classic CAN max payload bytes per frame.
        static constexpr size_t   PCAN_FD_MAX_PAYLOAD       = 64;    ///< CAN FD max payload bytes per frame.
        static constexpr uint32_t PCAN_READ_DEFAULT_TIMEOUT  = 5000; ///< Default RX timeout in milliseconds.
        static constexpr uint32_t PCAN_WRITE_DEFAULT_TIMEOUT = 5000; ///< Default TX timeout in milliseconds.
        static constexpr uint32_t PCAN_DEFAULT_TX_ID         = 0x7FF; ///< Default TX CAN ID.
        static constexpr uint32_t PCAN_DEFAULT_RX_FILTER_ID  = 0x000; ///< 0 = accept all (open filter).

        /// SocketCAN canid_t convention used by every CAN plugin (KVCAN, SLCAN,
        /// PCAN) for TX ids, RX filter ids, and xtra_params overrides: bit 31
        /// set → 29-bit extended frame. PCANBasic's own TPCANMsg::ID field has
        /// no such flag bit (extended-ness lives in MSGTYPE instead), so any
        /// value in this convention must be normalised — flag stripped, data
        /// bits masked to the legal range — before it reaches TPCANMsg::ID or
        /// is compared against one. See sendFrame()/frameMatchesFilter().
        static constexpr uint32_t CAN_EFF_FLAG = 0x80000000U;
        static constexpr uint32_t CAN_EFF_MASK = 0x1FFFFFFFU;
        static constexpr uint32_t CAN_SFF_MASK = 0x000007FFU;

        // ------------------------------------------------------------------ //
        //  Construction / destruction                                          //
        // ------------------------------------------------------------------ //

        PCAN() = default;

        /**
         * @brief Convenience constructor — opens the channel immediately.
         * @param strChannel  PCAN channel handle, decimal or "0x" hex string (e.g. "0x51").
         * @param u32Bitrate  CAN bitrate in bps (e.g. 500000).
         * @param u32TxId     Default TX CAN ID.
         * @param bExtended   Force 29-bit extended frame format (auto-detected when false).
         * @param bFD         Enable CAN FD mode.
         */
        explicit PCAN(const std::string& strChannel,
                      uint32_t           u32Bitrate  = 500000,
                      uint32_t           u32TxId     = PCAN_DEFAULT_TX_ID,
                      bool               bExtended   = false,
                      bool               bFD         = false)
        {
            open(strChannel, u32Bitrate, u32TxId, bExtended, bFD);
        }

        virtual ~PCAN()
        {
            close();
        }

        // ------------------------------------------------------------------ //
        //  Lifecycle                                                           //
        // ------------------------------------------------------------------ //

        /**
         * @brief Open and initialise the PCAN channel.
         * @param strChannel  PCAN channel handle string (decimal or "0x" hex).
         * @param u32Bitrate  CAN bitrate in bps.
         * @param u32TxId     Default TX CAN ID used when xtra_params is empty.
         * @param bExtended   Force 29-bit extended frame format.
         * @param bFD         Enable CAN FD mode.
         * @return Status::SUCCESS on success, appropriate error code otherwise.
         */
        Status open(const std::string& strChannel,
                    uint32_t           u32Bitrate  = 500000,
                    uint32_t           u32TxId     = PCAN_DEFAULT_TX_ID,
                    bool               bExtended   = false,
                    bool               bFD         = false);

        /**
         * @brief Uninitialise and release the PCAN channel.
         * @return Status::SUCCESS always.
         */
        Status close();

        /**
         * @brief Check whether the channel is currently open.
         */
        bool is_open() const override;

        // ------------------------------------------------------------------ //
        //  ICommDriver interface                                               //
        // ------------------------------------------------------------------ //

        /**
         * @brief Unified read interface — accumulates CAN frame payloads.
         *
         * @param u32ReadTimeout  Timeout in milliseconds (0 = use default).
         * @param buffer          Destination byte buffer.
         * @param options         ReadMode / delimiter / token configuration.
         * @param xtra_params     Optional RX filter CAN ID override (decimal or "0x" hex).
         *                        Empty string → use m_u32DefaultRxFilterId (accept all).
         * @return ReadResult with status, bytes accumulated, and terminator flag.
         */
        ReadResult tout_read(uint32_t           u32ReadTimeout,
                             std::span<uint8_t> buffer,
                             const ReadOptions& options,
                             std::string_view   xtra_params = {}) const override;

        /**
         * @brief Unified write interface — fragments payload into CAN frames.
         *
         * @param u32WriteTimeout Timeout in milliseconds (0 = use default).
         * @param buffer          Payload bytes to transmit.
         * @param xtra_params     Optional TX CAN ID override (decimal or "0x" hex).
         *                        Empty string → use m_u32DefaultTxId.
         * @return WriteResult with status and total bytes written.
         */
        WriteResult tout_write(uint32_t                  u32WriteTimeout,
                               std::span<const uint8_t>  buffer,
                               std::string_view          xtra_params = {}) const override;

        // ------------------------------------------------------------------ //
        //  Configuration helpers                                               //
        // ------------------------------------------------------------------ //

        /** Set the default TX CAN ID used when xtra_params is empty. */
        void setDefaultTxId(uint32_t u32Id)          { m_u32DefaultTxId = u32Id; }

        /** Set the default RX acceptance-filter CAN ID (0 = accept all). */
        void setDefaultRxFilterId(uint32_t u32Id)    { m_u32DefaultRxFilterId = u32Id; }

        /** Force 29-bit extended frame format for all outgoing frames. */
        void setExtendedId(bool bExt)                { m_bExtendedId = bExt; }

        /** Query current default TX ID. */
        uint32_t getDefaultTxId()       const        { return m_u32DefaultTxId; }

        /** Query current default RX filter ID. */
        uint32_t getDefaultRxFilterId() const        { return m_u32DefaultRxFilterId; }

        /** Query PCAN channel handle (numeric). */
        TPCANHandle getChannel()        const        { return m_hChannel; }

    private:

        // ------------------------------------------------------------------ //
        //  State                                                               //
        // ------------------------------------------------------------------ //

        TPCANHandle        m_hChannel            = PCAN_NONEBUS; ///< PCAN channel handle.
        bool               m_bOpen               = false;         ///< True when the channel is initialised.
        bool               m_bFD                 = false;         ///< CAN FD mode flag.
        bool               m_bExtendedId         = false;         ///< Force 29-bit IDs.
        uint32_t           m_u32DefaultTxId      = PCAN_DEFAULT_TX_ID;
        uint32_t           m_u32DefaultRxFilterId = PCAN_DEFAULT_RX_FILTER_ID;
        mutable std::mutex m_mutex;                               ///< Protects concurrent access.

        // ------------------------------------------------------------------ //
        //  Internal helpers                                                    //
        // ------------------------------------------------------------------ //

        /** Parse a decimal or "0x"-prefixed hex string to uint32_t. Returns false on error. */
        static bool parseUint32(std::string_view sv, uint32_t& out);

        /** Resolve the TX CAN ID: xtra_params overrides the default when non-empty. */
        uint32_t resolveTxId(std::string_view xtra_params) const;

        /** Resolve the RX filter CAN ID from xtra_params (0 = accept-all default). */
        uint32_t resolveRxId(std::string_view xtra_params) const;

        /**
         * @brief Check whether a received frame matches an RX filter id expressed
         *        in the SocketCAN canid_t convention (bit 31 = CAN_EFF_FLAG).
         *
         *        u32RxFilterId == 0 means accept-all and always matches. Otherwise
         *        the flag bit is stripped and the frame's own extended/standard
         *        type (from msg.MSGTYPE) must agree with the filter's, in addition
         *        to the numeric id matching — a standard-frame filter for id 0x100
         *        must not accidentally match an extended frame whose 29-bit id
         *        also happens to equal 0x100.
         */
        bool frameMatchesFilter(const TPCANMsg& msg, uint32_t u32RxFilterId) const;

        /**
         * @brief Map a PCAN error code to ICommDriver::Status.
         */
        static Status mapPcanError(TPCANStatus sts);

        /**
         * @brief Map a numeric bitrate (bps) to a PCAN_BAUD_xxx constant.
         * @return PCAN_BAUD_500K etc., or 0 on unsupported value.
         */
        static TPCANBaudrate mapBitrate(uint32_t u32Bitrate);

        /**
         * @brief Receive one CAN frame within the given timeout.
         *
         * Blocks for up to u32TimeoutMs milliseconds using the PCAN event handle
         * (Windows) or a poll loop (Linux).  Fills `msg` and `ts` on success.
         *
         * @return Status::SUCCESS, Status::READ_TIMEOUT, or Status::READ_ERROR.
         */
        Status recvFrame(uint32_t u32TimeoutMs, TPCANMsg& msg, TPCANTimestamp& ts) const;

        /**
         * @brief Transmit one CAN frame with the given payload slice.
         *
         * @param u32Id         CAN ID for this frame.
         * @param bExtended     Use 29-bit extended frame format.
         * @param data          Up to PCAN_MAX_PAYLOAD bytes.
         * @return Status::SUCCESS or Status::WRITE_ERROR.
         */
        Status sendFrame(uint32_t u32Id, bool bExtended, std::span<const uint8_t> data) const;

        // ------------------------------------------------------------------ //
        //  Read-mode implementations                                           //
        // ------------------------------------------------------------------ //

        /** Accumulate exactly buffer.size() bytes from CAN frames. */
        Status readExact(uint32_t u32TimeoutMs, std::span<uint8_t> buffer,
                         size_t& szBytesRead, uint32_t u32RxFilterId) const;

        /** Accumulate bytes until delimiter byte found; null-terminates. */
        Status readUntilDelimiter(uint32_t u32TimeoutMs, std::span<uint8_t> buffer,
                                  uint8_t cDelimiter, size_t& szBytesRead,
                                  uint32_t u32RxFilterId) const;

        /** Accumulate bytes until KMP token match. */
        Status readUntilToken(uint32_t u32TimeoutMs,
                              std::span<const uint8_t> token,
                              uint32_t u32RxFilterId) const;

        /** Build KMP failure-function table. */
        static void buildKmpTable(std::span<const uint8_t> pattern, std::vector<int>& viLps);
};


#endif // U_PCAN_DRIVER_H
