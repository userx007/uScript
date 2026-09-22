#include "SWD.hpp"

#include "Support.hpp"
#include "uLogger.hpp"

#include <array>
#include <bitset>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace HydraHAL {
    class Hydrabus;
} // namespace HydraHAL

/////////////////////////////////////////////////////////////////////////////////
//                            LOG DEFINITIONS                                  //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
#undef LT_HDR
#endif
#ifdef LOG_HDR
#undef LOG_HDR
#endif

#define LT_HDR  "HYDRA_SWD   |"
#define LOG_HDR LOG_STRING(LT_HDR)

/////////////////////////////////////////////////////////////////////////////////
//                         NAMESPACE IMPLEMENTATION                            //
/////////////////////////////////////////////////////////////////////////////////

namespace HydraHAL {

    // ---------------------------------------------------------------------------
    // Construction
    // ---------------------------------------------------------------------------

    SWD::SWD(std::shared_ptr<Hydrabus> hydrabus)
        : RawWire(std::move(hydrabus))
    {
        // SWD requires 3-Wire, Open-Drain, polarity 0  → config = 0b1010
        _config = 0x0A;
        _configure_port();
    }

    // ---------------------------------------------------------------------------
    // Private helpers
    // ---------------------------------------------------------------------------

    uint8_t SWD::_apply_dp_parity(uint8_t value) const
    {
        // The parity bit lives at bit 5; it covers bits [4:1] of the request
        uint8_t tmp = (value >> 1) & 0b00001111;
        if ((std::bitset<8>(tmp).count() % 2) == 1) {
            value = value | (1 << 5);
        }
        return value;
    }

    void SWD::_sync(std::stop_token stop_tok)
    {
        const std::array<uint8_t, 1> sync_byte{0x00};
        write(sync_byte, stop_tok);
    }

    // ---------------------------------------------------------------------------
    // Bus initialisation
    // ---------------------------------------------------------------------------

    void SWD::bus_init(std::stop_token stop_tok)
    {
        // JTAG-to-SWD magic sequence (50 HIGH clocks + 0x9E7B pattern + 50 HIGH + 2 idle)
        static const std::vector<uint8_t> jtag_to_swd = {
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0x7B, 0x9E,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0x0F};
        write(jtag_to_swd, stop_tok);
        _sync(stop_tok);
    }

    void SWD::multidrop_init(uint32_t addr, std::stop_token stop_tok)
    {
        bus_init(stop_tok);

        // ADIv6 dormant-to-active sequence
        static const std::vector<uint8_t> dormant_active = {
            0x92, 0xF3, 0x09, 0x62, 0x95, 0x2D, 0x85, 0x86,
            0xE9, 0xAF, 0xDD, 0xE3, 0xA2, 0x0E, 0xBC, 0x19};
        write(dormant_active, stop_tok);
        const std::array<uint8_t, 1> idle_bits{0x00};
        write_bits(idle_bits, 4, stop_tok); // 4 idle clocks

        // Protocol activation code = SWD (0x1A)
        const std::array<uint8_t, 1> activation{0x1A};
        write(activation, stop_tok);

        // Bus reset: 8 bytes of 0xFF = 64 HIGH clocks, then sync
        write(std::vector<uint8_t>(7, 0xFF), stop_tok);
        _sync(stop_tok);

        // Select the target DP
        write_dp(0x0C, addr, 0, /*ignore_status=*/true, stop_tok);
    }

    // ---------------------------------------------------------------------------
    // Debug Port (DP)
    // ---------------------------------------------------------------------------

    uint32_t SWD::read_dp(uint8_t addr, int to_ap, std::stop_token stop_tok)
    {
        // Build request byte: 0b10000101 | to_ap<<1 | addr_bits<<1
        uint8_t cmd = 0x85;
        cmd         = cmd | static_cast<uint8_t>(to_ap << 1);
        cmd         = cmd | static_cast<uint8_t>((addr & 0b1100) << 1);
        cmd         = _apply_dp_parity(cmd);

        const std::array<uint8_t, 1> req_rd{cmd};
        write(req_rd, stop_tok);

        // Read 3 ACK bits (LSB first)
        uint8_t status = 0;
        for (int i = 0; i < 3; ++i) {
            status += static_cast<uint8_t>(read_bit(stop_tok) << i);
        }

        if (status == 1) {
            // OK: read 32-bit data + 1 parity bit (parity captured in sync)
            auto raw        = read(4, stop_tok);
            uint32_t retval = from_le32(raw);
            _sync(stop_tok);
            return retval;
        } else if (status == 2) {
            // WAIT: abort and retry, unless cancellation has been requested —
            // otherwise a target stuck permanently in WAIT would recurse forever.
            _sync(stop_tok);
            if (stop_tok.stop_requested()) {
                throw std::runtime_error("[SWD] read_dp: cancelled while target WAIT-ing");
            }
            write_dp(0x00, 0x0000001F, 0, false, stop_tok); // ABORT — clear all fault flags
            return read_dp(addr, to_ap, stop_tok);
        } else {
            _sync(stop_tok);
            throw std::runtime_error(
                std::string("[SWD] read_dp: FAULT — status = ") +
                std::to_string(status));
        }
    }

    void SWD::write_dp(uint8_t addr, uint32_t value,
                       int to_ap,
                       bool ignore_status,
                       std::stop_token stop_tok)
    {
        uint8_t cmd = 0x81;
        cmd         = cmd | static_cast<uint8_t>(to_ap << 1);
        cmd         = cmd | static_cast<uint8_t>((addr & 0b1100) << 1);
        cmd         = _apply_dp_parity(cmd);

        const std::array<uint8_t, 1> req_wr{cmd};
        write(req_wr, stop_tok);

        uint8_t status = 0;
        for (int i = 0; i < 3; ++i) {
            status += static_cast<uint8_t>(read_bit(stop_tok) << i);
        }
        clocks(2, stop_tok); // turnaround clocks

        if (!ignore_status) {
            if (status == 2) {
                // WAIT — abort and retry, unless cancellation has been requested.
                _sync(stop_tok);
                if (stop_tok.stop_requested()) {
                    throw std::runtime_error("[SWD] write_dp: cancelled while target WAIT-ing");
                }
                write_dp(0x00, 0x0000001F, 0, false, stop_tok);
                write_dp(addr, value, to_ap, false, stop_tok);
                return;
            }
            if (status != 1) {
                _sync(stop_tok);
                throw std::runtime_error(
                    std::string("[SWD] write_dp: FAULT — status = ") +
                    std::to_string(status));
            }
        }

        // Send 32-bit data (LE)
        auto payload = u32_le(value);
        write(std::vector<uint8_t>(payload.begin(), payload.end()), stop_tok);

        // Parity bit: 1 if odd number of set bits in value, else 0
        uint8_t parity = static_cast<uint8_t>(
            std::bitset<32>(value).count() % 2);
        const std::array<uint8_t, 1> par_byte{parity};
        write(par_byte, stop_tok);
    }

    // ---------------------------------------------------------------------------
    // Access Port (AP)
    // ---------------------------------------------------------------------------

    uint32_t SWD::read_ap(uint8_t ap_address, uint8_t bank, std::stop_token stop_tok)
    {
        // Build SELECT register:
        //   bits [31:24] = AP address
        //   bits [7:4]   = bank select
        uint32_t select_reg = (static_cast<uint32_t>(ap_address) << 24) | (static_cast<uint32_t>(bank) & 0xF0u);

        write_dp(0x08, select_reg, 0, false, stop_tok); // DP SELECT register

        // Trigger AP read (result goes into RDBUFF)
        read_dp(static_cast<uint8_t>(bank & 0b1100), 1, stop_tok);

        // Read buffered result from RDBUFF (DP address 0x0C)
        return read_dp(0x0C, 0, stop_tok);
    }

    void SWD::write_ap(uint8_t ap_address, uint8_t bank, uint32_t value, std::stop_token stop_tok)
    {
        uint32_t select_reg = (static_cast<uint32_t>(ap_address) << 24) | (static_cast<uint32_t>(bank) & 0xF0u);

        write_dp(0x08, select_reg, 0, false, stop_tok); // DP SELECT register
        write_dp(static_cast<uint8_t>(bank & 0b1100), value, 1, false, stop_tok);
    }

    // ---------------------------------------------------------------------------
    // Utilities
    // ---------------------------------------------------------------------------

    void SWD::scan_bus(std::stop_token stop_tok)
    {
        LOG_PRINT(LOG_DEBUG, LOG_HDR; LOG_STRING("Scanning APs..."));
        for (int ap = 0; ap < 256; ++ap) {
            if (stop_tok.stop_requested()) {
                break;
            }
            uint32_t idr = read_ap(static_cast<uint8_t>(ap), 0xFC, stop_tok);
            if (idr != 0x00000000 && idr != 0xFFFFFFFF) {
                LOG_PRINT(LOG_DEBUG, LOG_HDR;
                          LOG_STRING("AP"); LOG_HEX8(static_cast<uint8_t>(ap));
                          LOG_STRING("IDR ="); LOG_HEX32(idr));
            }
        }
    }

    void SWD::abort(uint8_t flags, std::stop_token stop_tok)
    {
        write_dp(0x00, flags, 0, false, stop_tok);
    }

} // namespace HydraHAL
