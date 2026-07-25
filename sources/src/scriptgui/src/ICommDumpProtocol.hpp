#ifndef COMM_DUMP_PROTOCOL_HPP
#define COMM_DUMP_PROTOCOL_HPP

/**
 * @file ICommDumpProtocol.hpp
 * @brief Wire format for GUI:COMM_DUMP: events (plugin Rx/Tx traffic dump).
 *
 * This header has NO Qt dependency so it can be included from plugin .so/.dll
 * translation units exactly like uGuiNotify.hpp — and from ICommDriver.hpp /
 * every concrete driver, since CommDetails is also the return type of
 * ICommDriver::describeConnection(). It defines:
 *
 *   - CommFamily / CommDir     tag enums
 *   - CommDetails              { family, label } describing the "Details"
 *                               column content — label is driver-rendered
 *                               free text, not a fixed per-protocol layout
 *   - commdump_details()       builds a CommDetails from a family + string,
 *                               truncating safely to the wire label size
 *   - commdump_pack()          plugin name + details + dir + buffer
 *                               -> flat byte vector
 *   - commdump_base64_encode() flat bytes -> base64 std::string
 *
 * WHY family+label INSTEAD OF A PER-PROTOCOL UNION
 * ---------------------------------------------------------------------------
 * A tagged union with one fixed-layout struct per protocol (UART/TCP/UDP/
 * RAWETH/I2C/SPI/CAN) would assume a handful of physical transports. In
 * practice there are ~30 concrete ICommDriver implementations, several
 * sharing one logical protocol over completely different hardware (I2C
 * alone: a kernel i2c-dev node, a CP2112 HID bridge, three different FTDI
 * chips, a CH347, an Arduino bridge — each with its own idea of "identity").
 * A fixed union would need editing every time a driver is added, and still
 * couldn't express what's actually distinctive about each one. A small,
 * stable family tag (six values, meant to basically never change) plus a
 * short driver-rendered label string covers all of them, and adding driver
 * #30 never touches this file again.
 *
 * WHY A FLAT PACK INSTEAD OF A RAW STRUCT CAST
 * ---------------------------------------------------------------------------
 * gui_notify_comm_dump() below serializes the record into ONE LF-terminated
 * base64 line, consistent with every other GUI: message (see uGuiNotify.hpp's
 * protocol doc comment) — raw bytes cannot go through that channel unmodified
 * because an embedded 0x0A would desync the line-based parser on the Qt side.
 *
 * We explicitly pack fields into a byte buffer (fixed-width, no struct
 * padding assumptions) rather than base64-ing a memcpy of the C++ struct.
 * Plugin .so files and the interpreter executable are separate DSOs; relying
 * on identical padding/alignment for a struct crossing that boundary is the
 * same class of fragility uGuiNotify.hpp already had to work around once for
 * g_gui_mode / thread-local tid (see the comments in that file). Explicit
 * pack/unpack sidesteps it entirely and is cheap at these payload sizes.
 *
 * WIRE LAYOUT (all integers little-endian, which is native on every platform
 * this project targets; add an endianness swap here if that ever changes):
 *
 *   [1]  uint8_t   pluginNameLen
 *   [N]  char      pluginName[pluginNameLen]      (not NUL-terminated)
 *   [1]  uint8_t   family                         (CommFamily)
 *   [k_labelSize] char label                      (NUL-padded, always emitted
 *                                                   in full regardless of the
 *                                                   real string length)
 *   [1]  uint8_t   direction                      (CommDir)
 *   [4]  uint32_t  dataLen
 *   [dataLen] uint8_t data[dataLen]
 */

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

// ---------------------------------------------------------------------------
// Family / direction tags
// ---------------------------------------------------------------------------

// Deliberately small and stable — this is a *category* for e.g. future
// icon/colour choices in the GUI, not a full protocol descriptor. The actual
// identifying information (port path, IP:port, bus+address, CAN id, ...)
// lives entirely in CommDetails::label, rendered by the driver itself.
enum class CommFamily : uint8_t
{
    SERIAL = 0,   // UART and UART-like point-to-point byte streams
    I2C    = 1,
    SPI    = 2,
    CAN    = 3,
    NET    = 4,   // TCP/UDP/raw-Ethernet and SPI/MII-attached MAC chips alike
    OTHER  = 5    // GPIO, JTAG, or anything that doesn't fit the above
};

enum class CommDir : uint8_t { Rx = 0, Tx = 1 };

// ---------------------------------------------------------------------------
// CommDetails — the "Details" column content.
//
// label is fixed-size (not std::string) so CommDetails stays POD-ish and its
// wire size is stable across compilers/DSOs, matching the constraint the old
// union had — just with one field instead of seven.
// ---------------------------------------------------------------------------
inline constexpr int k_labelSize = 64;   // includes the NUL terminator

struct CommDetails
{
    CommFamily family = CommFamily::OTHER;
    char       label[k_labelSize] = {};   // e.g. "/dev/ttyUSB0", "192.168.1.5:502",
                                           // "PCAN-USB ch0 id=0x123", "i2c-1 addr=0x50"
};

// Builds a CommDetails from a family + arbitrary string, truncating safely
// (with a trailing '~') if the label doesn't fit k_labelSize - 1 characters.
// This is the ONLY way driver code should construct a CommDetails — it keeps
// the truncation/NUL-termination logic in one place.
inline CommDetails commdump_details(CommFamily family, std::string_view label)
{
    CommDetails d;
    d.family = family;
    const size_t maxLen = sizeof(d.label) - 1;
    if (label.size() <= maxLen) {
        std::memcpy(d.label, label.data(), label.size());
    } else {
        std::memcpy(d.label, label.data(), maxLen - 1);
        d.label[maxLen - 1] = '~';   // truncation marker
    }
    return d;
}

// ---------------------------------------------------------------------------
// commdump_pack — serialize one record into a flat byte buffer
// ---------------------------------------------------------------------------
inline std::vector<uint8_t> commdump_pack(const std::string   &pluginName,
                                           const CommDetails   &details,
                                           CommDir              dir,
                                           const uint8_t       *data,
                                           uint32_t             dataLen)
{
    std::vector<uint8_t> buf;
    const uint8_t nameLen = static_cast<uint8_t>(
        pluginName.size() > 255 ? 255 : pluginName.size());

    buf.reserve(1 + nameLen + 1 + k_labelSize + 1 + 4 + dataLen);

    buf.push_back(nameLen);
    buf.insert(buf.end(), pluginName.begin(), pluginName.begin() + nameLen);

    buf.push_back(static_cast<uint8_t>(details.family));
    buf.insert(buf.end(),
               reinterpret_cast<const uint8_t *>(details.label),
               reinterpret_cast<const uint8_t *>(details.label) + k_labelSize);

    buf.push_back(static_cast<uint8_t>(dir));

    for (int i = 0; i < 4; ++i)
        buf.push_back(static_cast<uint8_t>((dataLen >> (8 * i)) & 0xFF));

    if (dataLen && data)
        buf.insert(buf.end(), data, data + dataLen);

    return buf;
}

// ---------------------------------------------------------------------------
// commdump_base64_encode — plain C++ base64, no Qt dependency
// (the Qt side decodes with QByteArray::fromBase64(), which is a compatible
// standard-alphabet, padded encoder/decoder pair)
// ---------------------------------------------------------------------------
inline std::string commdump_base64_encode(const std::vector<uint8_t> &in)
{
    static const char *tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);

    size_t i = 0;
    while (i + 3 <= in.size()) {
        const uint32_t n = (uint32_t(in[i]) << 16) | (uint32_t(in[i + 1]) << 8) | in[i + 2];
        out.push_back(tbl[(n >> 18) & 0x3F]);
        out.push_back(tbl[(n >> 12) & 0x3F]);
        out.push_back(tbl[(n >> 6) & 0x3F]);
        out.push_back(tbl[n & 0x3F]);
        i += 3;
    }
    const size_t rem = in.size() - i;
    if (rem == 1) {
        const uint32_t n = uint32_t(in[i]) << 16;
        out.push_back(tbl[(n >> 18) & 0x3F]);
        out.push_back(tbl[(n >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2) {
        const uint32_t n = (uint32_t(in[i]) << 16) | (uint32_t(in[i + 1]) << 8);
        out.push_back(tbl[(n >> 18) & 0x3F]);
        out.push_back(tbl[(n >> 12) & 0x3F]);
        out.push_back(tbl[(n >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

#endif // COMM_DUMP_PROTOCOL_HPP
