#ifndef U_VECTOR_ETH_DRIVER_H
#define U_VECTOR_ETH_DRIVER_H

#include "ICommDriver.hpp"
#include "uVectorDriverHandle.hpp"
#include "uVector.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <span>
#include <mutex>
#include <cstdint>
#include <cstdio>
#include <stop_token>
#include <optional>

#if !defined(_WIN32)
#  error "uVectorEth: Vector Informatik's XL Driver Library (vxlapi) ships for Windows only. \
This driver cannot be built for this target platform."
#endif

#include <windows.h>
#include <vxlapi.h>


/**
 * @brief Vector XL-API Ethernet driver wrapper implementing ICommDriver.
 *
 * Talks to Vector Informatik's port-based Ethernet interfaces (VN5610(A),
 * VN7610, VN7570, VX1135, ...) through XL-API's direct Ethernet API
 * (xlEthTransmit()/xlEthReceive()/xlEthSetConfig()) — the same "open a port,
 * activate a channel, transmit/receive one frame at a time" model as CAN,
 * as opposed to XL-API's separate Network-based virtual-switch API
 * (xlNetEthOpenNetwork()/...) which models a whole simulated switched
 * network rather than one physical channel and doesn't fit this codebase's
 * one-driver-per-physical-channel ICommDriver contract.
 *
 * Sibling to uVector (CAN/CAN-FD): shares its device enumeration
 * (Vector::enumerateChannels()/Vector::hwTypeToString()/hwTypeFromString())
 * and its process-wide XL-API driver handle (VectorDriverHandle) so a
 * process can have CAN and Ethernet channels open through this codebase at
 * the same time without either side's xlOpenDriver()/xlCloseDriver()
 * clobbering the other's.
 *
 * Framing / addressing
 * ---------------------
 * Every tout_write() call sends its buffer as the payload of one or more
 * standard Ethernet II frames (EtherType + up to XL_ETH_PAYLOAD_SIZE_MAX
 * (1500) payload bytes each — this driver does not build 802.3 length-field
 * or VLAN-tagged frames). The destination MAC address and EtherType are
 * configured via setDefaultDestMac()/setDefaultEtherType() and can be
 * overridden per call through xtra_params (see resolveDest()'s doc comment
 * for the exact grammar) — the same per-call-override convention every CAN
 * driver in this codebase uses for its TX id. The source MAC address is
 * always left for the hardware to fill in (this driver never sets
 * XL_ETH_DATAFRAME_FLAGS_USE_SOURCE_MAC) — spoofing the source address
 * would need a real use case to justify the extra footgun.
 *
 * Reading
 * -------
 * Same ReadMode::Exact / UntilDelimiter / UntilToken semantics as every
 * other driver in this codebase, built once against the mode-agnostic
 * VectorEthRxFrame struct recvFrame() produces — see uVector.hpp's
 * VectorRxFrame for the equivalent CAN-side design this mirrors. A received
 * frame is accepted only if it matches the configured source-MAC and/or
 * EtherType filter (see setRxFilterSrcMac()/setRxFilterEtherType(); both
 * default to "accept all").
 *
 * No multi-frame transport protocol layer
 * -----------------------------------------
 * Unlike the CAN drivers, this class has no ISO-TP/J1939/CANopen equivalent
 * layered on top — Ethernet's own 1500-byte MTU is already far larger than
 * any single CAN frame, and this codebase's can_tp library is CAN-specific
 * (ISO 15765-2/SAE J1939-21 are both defined only in terms of CAN frames).
 * A payload larger than the MTU is fragmented the same naive way CAN's
 * writeFragmented_locked() fragments an oversized buffer — one frame per
 * up-to-MTU chunk — with no reassembly framing of its own; pair this with a
 * higher-level protocol (e.g. UDP via the codebase's own udp_plugin, running
 * over this channel's own IP stack if the peer has one) if you need one.
 */
class VectorEth : public ICommDriver
{
    public:

        // ------------------------------------------------------------------ //
        //  Constants                                                           //
        // ------------------------------------------------------------------ //

        static constexpr size_t   VECTOR_ETH_MAX_PAYLOAD        = XL_ETH_PAYLOAD_SIZE_MAX; ///< 1500 bytes.
        static constexpr uint32_t VECTOR_ETH_RX_QUEUE_SIZE       = 256;
        static constexpr uint16_t VECTOR_ETH_DEFAULT_ETHERTYPE   = 0x88B5; ///< IEEE Std 802 "Local Experimental Ethertype 1" - arbitrary but collision-safe default, mirrors Vector::VECTOR_DEFAULT_TX_ID's role for CAN.

        /** A MAC-48 address as 6 raw bytes, in transmission order. */
        using MacAddress = std::array<uint8_t, 6>;

        static constexpr MacAddress BROADCAST_MAC = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

        /** Parse "AA:BB:CC:DD:EE:FF" (colon or dash separated, case-insensitive) into out. */
        static bool parseMac(std::string_view sv, MacAddress& out);

        /** Format a MAC address as "AA:BB:CC:DD:EE:FF". */
        static std::string formatMac(const MacAddress& mac);

        // ------------------------------------------------------------------ //
        //  Device enumeration / direct selection                               //
        // ------------------------------------------------------------------ //

        // Reuses Vector::ChannelInfo/Vector::DeviceSelector/Vector::enumerateChannels()/
        // Vector::hwTypeToString()/Vector::hwTypeFromString() directly - see uVector.hpp.
        // Only the CAN-vs-Ethernet capability filter in matchChannels() below differs.

        /**
         * @brief Enumerate channels and keep only those matching every set field of sel
         *        AND advertising Ethernet support (ChannelInfo::bSupportsEthernet).
         * @return Zero, one, or many matches — callers needing exactly one (openDirect())
         *         must check size() themselves and report ambiguity/absence distinctly.
         */
        static std::vector<Vector::ChannelInfo> matchChannels(const Vector::DeviceSelector& sel);

        // ------------------------------------------------------------------ //
        //  Construction / destruction                                          //
        // ------------------------------------------------------------------ //

        VectorEth() = default;

        explicit VectorEth(const std::string& strAppName,
                           uint32_t           u32AppChannel    = 0,
                           const std::string& strIdentityLabel = {},
                           const std::string& strInstanceName  = {})
            : m_strIdentityLabel(strIdentityLabel)
            , m_strInstanceName(strInstanceName.empty() ? "VectorEth" : strInstanceName)
        {
            open(strAppName, u32AppChannel);
        }

        explicit VectorEth(const Vector::DeviceSelector& sel,
                           const std::string&            strIdentityLabel = {},
                           const std::string&            strInstanceName  = {})
            : m_strIdentityLabel(strIdentityLabel)
            , m_strInstanceName(strInstanceName.empty() ? "VectorEth" : strInstanceName)
        {
            openDirect(sel);
        }

        virtual ~VectorEth()
        {
            close();
        }

        // ------------------------------------------------------------------ //
        //  Lifecycle                                                           //
        // ------------------------------------------------------------------ //

        /**
         * @brief Open an Ethernet channel via Vector Hardware Config, same
         *        application-name/index indirection as Vector::open().
         */
        Status open(const std::string& strAppName, uint32_t u32AppChannel = 0);

        /**
         * @brief Open an Ethernet channel by resolving sel directly, same
         *        semantics as Vector::openDirect() (must match exactly one
         *        Ethernet-capable channel).
         */
        Status openDirect(const Vector::DeviceSelector& sel);

        Status close();
        bool   is_open() const override;

        CommDetails describeConnection(std::string_view xtra_params = {}) const override
        {
            MacAddress mac; uint16_t etherType;
            resolveDest(xtra_params, mac, etherType);
            char label[k_labelSize];
            std::snprintf(label, sizeof(label), "%s dst=%s type=0x%04X",
                          m_strIdentityLabel.empty() ? "VectorEth" : m_strIdentityLabel.c_str(),
                          formatMac(mac).c_str(), etherType);
            return commdump_details(CommFamily::NET, label);
        }

        // ------------------------------------------------------------------ //
        //  ICommDriver interface                                               //
        // ------------------------------------------------------------------ //

        ReadResult tout_read(uint32_t           u32ReadTimeout,
                             std::span<uint8_t> buffer,
                             const ReadOptions& options,
                             std::string_view   xtra_params = {},
                             std::stop_token    stop_tok = {}) const override;

        WriteResult tout_write(uint32_t                  u32WriteTimeout,
                               std::span<const uint8_t>  buffer,
                               std::string_view          xtra_params = {},
                               std::stop_token           stop_tok = {}) const override;

        // ------------------------------------------------------------------ //
        //  Configuration helpers                                               //
        // ------------------------------------------------------------------ //

        void setDefaultDestMac(const MacAddress& mac) { m_defaultDestMac = mac; }
        void setDefaultEtherType(uint16_t u16Val)      { m_u16DefaultEtherType = u16Val; }

        /** Display text for the GUI comm-dump panel / log lines. Must be called before open()/openDirect(). */
        void setIdentityLabel(const std::string& strLabel) { m_strIdentityLabel = strLabel; }
        void setInstanceName(const std::string& strName)   { m_strInstanceName  = strName.empty() ? "VectorEth" : strName; }

        MacAddress getDefaultDestMac()    const { return m_defaultDestMac; }
        uint16_t   getDefaultEtherType()  const { return m_u16DefaultEtherType; }

        /** RX filter: only accept frames from this source MAC. Pass std::nullopt (default) to accept any source. */
        void setRxFilterSrcMac(std::optional<MacAddress> mac) { m_rxFilterSrcMac = mac; }

        /** RX filter: only accept frames with this EtherType. Pass std::nullopt (default) to accept any type. */
        void setRxFilterEtherType(std::optional<uint16_t> u16Val) { m_rxFilterEtherType = u16Val; }

        /** Physical-layer config, applied at open() via xlEthSetConfig(). Defaults: auto-negotiate everything. */
        struct PhyConfig
        {
            unsigned int u32Speed     = XL_ETH_MODE_SPEED_AUTO_100_1000;
            unsigned int u32Duplex    = XL_ETH_MODE_DUPLEX_AUTO;
            unsigned int u32Connector = XL_ETH_MODE_CONNECTOR_DONT_CARE;
            unsigned int u32Phy       = XL_ETH_MODE_PHY_DONT_CARE;
            unsigned int u32ClockMode = XL_ETH_MODE_CLOCK_AUTO;
            unsigned int u32MdiMode   = XL_ETH_MODE_MDI_AUTO;
            unsigned int u32BrPairs   = XL_ETH_MODE_BR_PAIR_DONT_CARE;
        };
        void setPhyConfig(const PhyConfig& cfg) { m_phyConfig = cfg; }

    private:

        struct VectorEthRxFrame
        {
            MacAddress destMac{};
            MacAddress srcMac{};
            uint16_t   u16EtherType = 0;
            uint16_t   u16Len       = 0;
            std::array<uint8_t, VECTOR_ETH_MAX_PAYLOAD> data{};
        };

        XLportHandle       m_xlPort       = XL_INVALID_PORTHANDLE;
        XLaccess           m_xlAccessMask = 0;
        HANDLE             m_hRxEvent     = nullptr;
        bool               m_bOpen        = false;
        mutable std::mutex m_mutex;
        std::string        m_strIdentityLabel;
        std::string        m_strInstanceName{"VectorEth"};

        MacAddress                m_defaultDestMac      = BROADCAST_MAC;
        uint16_t                  m_u16DefaultEtherType = VECTOR_ETH_DEFAULT_ETHERTYPE;
        std::optional<MacAddress> m_rxFilterSrcMac;
        std::optional<uint16_t>   m_rxFilterEtherType;
        PhyConfig                 m_phyConfig;
        mutable uint32_t          m_u32TxFrameId = 0; ///< Monotonic XL-API frameIdentifier for outgoing frames.

        /**
         * @brief Resolve the destination MAC/EtherType for one tout_write() call.
         *
         * xtra_params grammar (all parts optional, "/"-separated, evaluated
         * left to right): "<mac>", "<mac>/<ethertype>", or "/<ethertype>".
         * <mac> is "AA:BB:CC:DD:EE:FF" (or dash-separated); <ethertype> is
         * decimal or 0x-hex. An empty xtra_params falls back entirely to
         * setDefaultDestMac()/setDefaultEtherType(). A part that fails to
         * parse falls back to its own default individually (matching how
         * Vector::resolveTxId() degrades on unparsable xtra_params) — this
         * function always succeeds.
         */
        void resolveDest(std::string_view xtra_params, MacAddress& outMac, uint16_t& outEtherType) const;

        Status m_OpenWithMask_locked(XLaccess accessMask);

        Status recvFrame(uint32_t u32TimeoutMs, VectorEthRxFrame& out, std::stop_token stop_tok = {}) const;
        Status sendFrame(const MacAddress& destMac, uint16_t u16EtherType, std::span<const uint8_t> data) const;

        bool frameMatchesFilter(const VectorEthRxFrame& frame) const;
        void dumpFrame(CommDir dir, const MacAddress& peerMac, uint16_t u16EtherType, std::span<const uint8_t> data) const;

        static Status mapXlError(XLstatus sts);

        Status readExact(uint32_t u32TimeoutMs, std::span<uint8_t> buffer,
                         size_t& szBytesRead, std::stop_token stop_tok = {}) const;

        Status readUntilDelimiter(uint32_t u32TimeoutMs, std::span<uint8_t> buffer,
                                  uint8_t cDelimiter, size_t& szBytesRead,
                                  std::stop_token stop_tok = {}) const;

        Status readUntilToken(uint32_t u32TimeoutMs, std::span<const uint8_t> token,
                              std::stop_token stop_tok = {}) const;

        static void buildKmpTable(std::span<const uint8_t> pattern, std::vector<int>& viLps);
};

#endif // U_VECTOR_ETH_DRIVER_H
