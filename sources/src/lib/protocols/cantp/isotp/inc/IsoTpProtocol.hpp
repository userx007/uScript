#ifndef CAN_TP_ISO_TP_PROTOCOL_HPP
#define CAN_TP_ISO_TP_PROTOCOL_HPP

#include "ITransportProtocol.hpp"
#include "TpConfig.hpp"

/**
 * @file IsoTpProtocol.hpp
 * @brief ISO 15765-2 (ISO-TP) transport protocol over classic 8-byte CAN frames.
 *
 * Implements the standard Single Frame / First Frame / Consecutive Frame /
 * Flow Control state machine:
 *
 *   PCI nibble 0x0  Single Frame        SF_DL in low nibble of byte 0
 *   PCI nibble 0x1  First Frame         12-bit length across bytes 0-1
 *   PCI nibble 0x2  Consecutive Frame   4-bit sequence number in byte 0
 *   PCI nibble 0x3  Flow Control        FS (byte0 low nibble) / BS (byte1) / STmin (byte2)
 *
 * Addressing: this implementation uses "normal addressing" — the full CAN ID
 * is the message identifier, no extra address-extension byte is consumed
 * from the payload. txId/rxId map directly to the driver's xtra_params CAN
 * ID hint (see ITransportProtocol.hpp).
 *
 * Known limitations:
 *   - Classic 8-byte frames only (7 usable data bytes for SF/CF). CAN-FD
 *     ISO-TP (ISO 15765-2:2016, single frame up to 62 bytes) would need the
 *     frame-length constants in the .cpp made configurable per driver.
 *   - Flow-Control WAIT (FS=1) is retried but not bounded by a dedicated
 *     wait-frame counter; N_Bs / u32WriteTimeout still bound the operation.
 *   - N_Ar/N_As (per-frame TX confirmation timing) are not modelled
 *     separately; the caller-supplied overall timeout is used for every
 *     blocking receive instead.
 */
class IsoTpProtocol final : public ITransportProtocol
{
    public:

        explicit IsoTpProtocol(const TpConfig& cfg = {}) : m_cfg(cfg) {}

        ICommDriver::WriteResult send(
            const ICommDriver& driver,
            uint32_t u32WriteTimeout,
            std::span<const uint8_t> data,
            std::string_view txId,
            std::string_view rxId = {}) const override;

        ICommDriver::ReadResult receive(
            const ICommDriver& driver,
            uint32_t u32ReadTimeout,
            std::span<uint8_t> buffer,
            std::string_view rxId,
            std::string_view txId = {}) const override;

        TpProtocol id() const override { return TpProtocol::ISO_TP; }

    private:

        TpConfig m_cfg;

        /** @brief Blocks for the duration encoded by an ISO-TP STmin byte. */
        static void sleep_st_min(uint8_t stMin);
};

#endif // CAN_TP_ISO_TP_PROTOCOL_HPP
