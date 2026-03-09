#include "SWD.hpp"
#include "common.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <bitset>

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

void SWD::_sync()
{
    const std::array<uint8_t, 1> sync_byte{0x00};
    write(sync_byte);
}

// ---------------------------------------------------------------------------
// Bus initialisation
// ---------------------------------------------------------------------------

void SWD::bus_init()
{
    // JTAG-to-SWD magic sequence (50 HIGH clocks + 0x9E7B pattern + 50 HIGH + 2 idle)
    static const std::vector<uint8_t> jtag_to_swd = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x7B, 0x9E,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x0F
    };
    write(jtag_to_swd);
    _sync();
}

void SWD::multidrop_init(uint32_t addr)
{
    bus_init();

    // ADIv6 dormant-to-active sequence
    static const std::vector<uint8_t> dormant_active = {
        0x92, 0xF3, 0x09, 0x62, 0x95, 0x2D, 0x85, 0x86,
        0xE9, 0xAF, 0xDD, 0xE3, 0xA2, 0x0E, 0xBC, 0x19
    };
    write(dormant_active);
    const std::array<uint8_t, 1> idle_bits{0x00};
    write_bits(idle_bits, 4);  // 4 idle clocks

    // Protocol activation code = SWD (0x1A)
    const std::array<uint8_t, 1> activation{0x1A};
    write(activation);

    // Bus reset: 8 bytes of 0xFF = 64 HIGH clocks, then sync
    write(std::vector<uint8_t>(7, 0xFF));
    _sync();

    // Select the target DP
    write_dp(0x0C, addr, 0, /*ignore_status=*/true);
}

// ---------------------------------------------------------------------------
// Debug Port (DP)
// ---------------------------------------------------------------------------

uint32_t SWD::read_dp(uint8_t addr, int to_ap)
{
    // Build request byte: 0b10000101 | to_ap<<1 | addr_bits<<1
    uint8_t cmd = 0x85;
    cmd = cmd | static_cast<uint8_t>(to_ap << 1);
    cmd = cmd | static_cast<uint8_t>((addr & 0b1100) << 1);
    cmd = _apply_dp_parity(cmd);

    const std::array<uint8_t, 1> req_rd{cmd};
    write(req_rd);

    // Read 3 ACK bits (LSB first)
    uint8_t status = 0;
    for (int i = 0; i < 3; ++i) {
        status += static_cast<uint8_t>(read_bit() << i);
    }

    if (status == 1) {
        // OK: read 32-bit data + 1 parity bit (parity captured in sync)
        auto raw = read(4);
        uint32_t retval = from_le32(raw);
        _sync();
        return retval;
    }
    else if (status == 2) {
        // WAIT: abort and retry
        _sync();
        write_dp(0x00, 0x0000001F);   // ABORT — clear all fault flags
        return read_dp(addr, to_ap);
    }
    else {
        _sync();
        throw std::runtime_error(
            std::string("[SWD] read_dp: FAULT — status = ") +
            std::to_string(status));
    }
}

void SWD::write_dp(uint8_t addr, uint32_t value,
                   int  to_ap,
                   bool ignore_status)
{
    uint8_t cmd = 0x81;
    cmd = cmd | static_cast<uint8_t>(to_ap << 1);
    cmd = cmd | static_cast<uint8_t>((addr & 0b1100) << 1);
    cmd = _apply_dp_parity(cmd);

    const std::array<uint8_t, 1> req_wr{cmd};
    write(req_wr);

    uint8_t status = 0;
    for (int i = 0; i < 3; ++i) {
        status += static_cast<uint8_t>(read_bit() << i);
    }
    clocks(2);   // turnaround clocks

    if (!ignore_status) {
        if (status == 2) {
            // WAIT
            _sync();
            write_dp(0x00, 0x0000001F);
            write_dp(addr, value, to_ap);
            return;
        }
        if (status != 1) {
            _sync();
            throw std::runtime_error(
                std::string("[SWD] write_dp: FAULT — status = ") +
                std::to_string(status));
        }
    }

    // Send 32-bit data (LE)
    auto payload = u32_le(value);
    write(std::vector<uint8_t>(payload.begin(), payload.end()));

    // Parity bit: 1 if odd number of set bits in value, else 0
    uint8_t parity = static_cast<uint8_t>(
        std::bitset<32>(value).count() % 2);
    const std::array<uint8_t, 1> par_byte{parity};
    write(par_byte);
}

// ---------------------------------------------------------------------------
// Access Port (AP)
// ---------------------------------------------------------------------------

uint32_t SWD::read_ap(uint8_t ap_address, uint8_t bank)
{
    // Build SELECT register:
    //   bits [31:24] = AP address
    //   bits [7:4]   = bank select
    uint32_t select_reg = (static_cast<uint32_t>(ap_address) << 24)
                        | (static_cast<uint32_t>(bank) & 0xF0u);

    write_dp(0x08, select_reg);            // DP SELECT register

    // Trigger AP read (result goes into RDBUFF)
    read_dp(static_cast<uint8_t>(bank & 0b1100), 1);

    // Read buffered result from RDBUFF (DP address 0x0C)
    return read_dp(0x0C);
}

void SWD::write_ap(uint8_t ap_address, uint8_t bank, uint32_t value)
{
    uint32_t select_reg = (static_cast<uint32_t>(ap_address) << 24)
                        | (static_cast<uint32_t>(bank) & 0xF0u);

    write_dp(0x08, select_reg);            // DP SELECT register
    write_dp(static_cast<uint8_t>(bank & 0b1100), value, 1);
}

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

void SWD::scan_bus()
{
    std::cout << "[SWD] Scanning APs...\n";
    for (int ap = 0; ap < 256; ++ap) {
        uint32_t idr = read_ap(static_cast<uint8_t>(ap), 0xFC);
        if (idr != 0x00000000 && idr != 0xFFFFFFFF) {
            std::cout << "  AP 0x" << std::hex << ap
                      << ": IDR = 0x" << idr << std::dec << '\n';
        }
    }
}

void SWD::abort(uint8_t flags)
{
    write_dp(0x00, flags);
}

} // namespace HydraHAL
