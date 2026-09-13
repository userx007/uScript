#include "TpFactory.hpp"
#include "TpConfig.hpp"
#include "ITransportProtocol.hpp"
#include "LoopbackCommDriver.hpp"
#include "SdoLoopbackServer.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
    constexpr const char *kIdAtoB = "A2B";
    constexpr const char *kIdBtoA = "B2A";

    // Deterministic test payload — byte i = (i*31 + seed) & 0xFF — so a
    // mismatch is trivially reproducible and diffable, not random noise.
    std::vector<uint8_t> make_payload(size_t len, uint8_t seed)
    {
        std::vector<uint8_t> data(len);
        for (size_t i = 0; i < len; ++i)
        {
            data[i] = static_cast<uint8_t>((i * 31 + seed) & 0xFF);
        }
        return data;
    }

    void print_usage(const char *argv0)
    {
        std::fprintf(stderr,
            "Usage: %s <protocol> [--size N] [--timeout MS] [--verbose]\n\n"
            "protocol (selects the transport protocol; it cannot be detected\n"
            "automatically at runtime — see the note below):\n"
            "  none         raw single frame, no TP framing (payload <= 8 bytes)\n"
            "  isotp        ISO 15765-2\n"
            "  j1939-bam    SAE J1939-21, broadcast (BAM)\n"
            "  j1939-rtscts SAE J1939-21, peer-to-peer (RTS/CTS)\n"
            "  canopen      CANopen SDO (expedited + segmented only, see README note)\n"
            "  nmea2000     NMEA 2000 Fast Packet\n\n"
            "options:\n"
            "  --size N      run one custom payload size instead of the built-in size suite\n"
            "  --timeout MS  per-frame-wait deadline in ms (default 5000)\n"
            "  --verbose     print every simulated CAN frame as it crosses the loopback bus\n\n"
            "Every argument set targets exactly one protocol on purpose: nothing about the\n"
            "bytes on the wire says which TP scheme framed them (ISO-TP's PCI nibble, J1939's\n"
            "control byte, CANopen's command specifier, and Fast Packet's sequence/frame\n"
            "counter all use the same bit positions for different things), so the two ends of\n"
            "a real link always agree on the protocol out of band (config, PGN, or CAN ID\n"
            "convention) rather than sniffing it from a frame.\n",
            argv0);
    }

    struct Args
    {
        std::string protocol;
        bool        verbose        = false;
        uint32_t    timeoutMs      = 5000;
        bool        customSizeSet  = false;
        size_t      customSize     = 0;
    };

    bool parse_args(int argc, char **argv, Args &out)
    {
        if (argc < 2) return false;
        out.protocol = argv[1];

        for (int i = 2; i < argc; ++i)
        {
            const std::string a = argv[i];
            if (a == "--verbose")
            {
                out.verbose = true;
            }
            else if (a == "--timeout" && i + 1 < argc)
            {
                out.timeoutMs = static_cast<uint32_t>(std::stoul(argv[++i]));
            }
            else if (a == "--size" && i + 1 < argc)
            {
                out.customSize = static_cast<size_t>(std::stoul(argv[++i]));
                out.customSizeSet = true;
            }
            else
            {
                std::fprintf(stderr, "Unknown argument: %s\n", a.c_str());
                return false;
            }
        }
        return true;
    }

    // ---- Generic symmetric test: one side send(), the other receive(). ----
    // Covers NONE, ISO-TP, J1939 (both modes), and NMEA 2000 Fast Packet —
    // every protocol here where send()/receive() are two ends of one
    // continuous handshake rather than distinct client/server roles.
    bool run_symmetric_case(ITransportProtocol *proto, LoopbackCommDriver &bus,
                             uint32_t timeoutMs, const std::vector<uint8_t> &payload)
    {
        std::vector<uint8_t> rxBuf(payload.size() + 64, 0);
        ICommDriver::ReadResult rr;

        std::thread receiver([&] {
            rr = proto->receive(bus, timeoutMs, std::span<uint8_t>(rxBuf), kIdAtoB, kIdBtoA);
        });
        ICommDriver::WriteResult wr = proto->send(bus, timeoutMs, payload, kIdAtoB, kIdBtoA);
        receiver.join();

        const bool ok = wr.status == ICommDriver::Status::SUCCESS &&
                         rr.status == ICommDriver::Status::SUCCESS &&
                         rr.bytes_read == payload.size() &&
                         std::equal(payload.begin(), payload.end(), rxBuf.begin());

        if (!ok)
        {
            std::fprintf(stderr,
                "    send status=%d written=%zu | receive status=%d read=%zu (expected %zu)\n",
                static_cast<int>(wr.status), wr.bytes_written,
                static_cast<int>(rr.status), rr.bytes_read, payload.size());
        }
        return ok;
    }

    // ---- "none": no ITransportProtocol at all, driver used directly — the
    // path TpFactory's doc comment says callers should take for TpProtocol::NONE. ----
    bool run_none_case(LoopbackCommDriver &bus, uint32_t timeoutMs, const std::vector<uint8_t> &payload)
    {
        if (payload.size() > 8)
        {
            std::fprintf(stderr, "    'none' has no segmentation; payload must be <= 8 bytes\n");
            return false;
        }
        std::vector<uint8_t> rxBuf(8, 0);
        ICommDriver::ReadResult rr;

        std::thread receiver([&] {
            ICommDriver::ReadOptions opts;
            opts.mode = ICommDriver::ReadMode::Exact;
            rr = bus.tout_read(timeoutMs, std::span<uint8_t>(rxBuf), opts, kIdAtoB);
        });
        auto wr = bus.tout_write(timeoutMs, payload, kIdAtoB);
        receiver.join();

        return wr.status == ICommDriver::Status::SUCCESS &&
               rr.status == ICommDriver::Status::SUCCESS &&
               rr.bytes_read == payload.size() &&
               std::equal(payload.begin(), payload.end(), rxBuf.begin());
    }

    // ---- CANopen SDO: client (this library) vs. SdoLoopbackServer (this app). ----
    bool run_canopen_case(ITransportProtocol *proto, LoopbackCommDriver &bus,
                          uint32_t timeoutMs, const std::vector<uint8_t> &payload)
    {
        // Download: client send()s payload to our server; verify the server
        // reconstructed it byte-for-byte.
        std::vector<uint8_t> serverReceived;
        bool downloadServerOk = false;
        std::thread server([&] {
            downloadServerOk = SdoLoopbackServer::serve_download(bus, kIdAtoB, kIdBtoA, timeoutMs, serverReceived);
        });
        auto wr = proto->send(bus, timeoutMs, payload, kIdAtoB, kIdBtoA);
        server.join();

        const bool downloadOk = wr.status == ICommDriver::Status::SUCCESS && downloadServerOk &&
                                 serverReceived.size() == payload.size() &&
                                 std::equal(payload.begin(), payload.end(), serverReceived.begin());
        if (!downloadOk)
        {
            std::fprintf(stderr, "    download: client status=%d | server_ok=%d server_len=%zu (expected %zu)\n",
                          static_cast<int>(wr.status), downloadServerOk, serverReceived.size(), payload.size());
        }

        // Upload: our server serves `payload` back; client receive()s it.
        std::vector<uint8_t> rxBuf(payload.size() + 64, 0);
        ICommDriver::ReadResult rr;
        bool uploadServerOk = false;
        std::thread server2([&] {
            uploadServerOk = SdoLoopbackServer::serve_upload(bus, kIdAtoB, kIdBtoA, timeoutMs, payload);
        });
        rr = proto->receive(bus, timeoutMs, std::span<uint8_t>(rxBuf), kIdBtoA, kIdAtoB);
        server2.join();

        const bool uploadOk = rr.status == ICommDriver::Status::SUCCESS && uploadServerOk &&
                               rr.bytes_read == payload.size() &&
                               std::equal(payload.begin(), payload.end(), rxBuf.begin());
        if (!uploadOk)
        {
            std::fprintf(stderr, "    upload: server_ok=%d | client status=%d read=%zu (expected %zu)\n",
                          uploadServerOk, static_cast<int>(rr.status), rr.bytes_read, payload.size());
        }

        return downloadOk && uploadOk;
    }

    // Note on upload's txId/rxId: the client's receive() signature is
    // (driver, timeout, buffer, rxId, txId) — rxId is where the client
    // expects OUR data/handshake frames, txId is where it sends its own
    // requests. So from the client's point of view rxId=kIdBtoA (our
    // replies) and txId=kIdAtoB (its requests) — the mirror image of the
    // download call, which is why the two run_canopen_case() calls above
    // pass kIdAtoB/kIdBtoA in opposite argument positions.

    struct ProtocolPlan
    {
        TpProtocol         proto;
        std::vector<size_t> sizes;
        std::string        note;
    };

    ProtocolPlan plan_for(const std::string &name, TpConfig &cfg)
    {
        if (name == "isotp")
            return { TpProtocol::ISO_TP, {1, 7, 8, 50, 500, 4000}, "" };
        if (name == "j1939-bam")
        {
            cfg.j1939UseBam = true;
            // BAM paces consecutive frames with a fixed 50ms inter-packet
            // gap and has no flow control to speed that up, so sizes here
            // are kept modest to keep the test's runtime reasonable
            // (200 bytes / 7 per frame * 50ms is already ~1.4s).
            return { TpProtocol::J1939_TP, {1, 8, 9, 50, 200}, "BAM paces frames at 50ms/frame — larger sizes take proportionally longer" };
        }
        if (name == "j1939-rtscts")
        {
            cfg.j1939UseBam = false;
            return { TpProtocol::J1939_TP, {1, 8, 9, 50, 500, 1785}, "" };
        }
        if (name == "canopen")
        {
            cfg.canOpenUseBlock = false; // see SdoLoopbackServer.hpp
            return { TpProtocol::CANOPEN_SDO, {1, 4, 5, 50, 500},
                     "block transfer is intentionally not exercised — see SdoLoopbackServer.hpp" };
        }
        if (name == "nmea2000")
            return { TpProtocol::NMEA2000_FAST_PACKET, {1, 6, 7, 50, 223}, "" };
        return { TpProtocol::NONE, {}, "" };
    }
}

int main(int argc, char **argv)
{
    Args args;
    if (!parse_args(argc, argv, args))
    {
        print_usage(argv[0]);
        return 2;
    }

    TpConfig cfg;
    const bool isNone = (args.protocol == "none");
    ProtocolPlan plan = isNone ? ProtocolPlan{ TpProtocol::NONE, {1, 4, 8}, "" }
                               : plan_for(args.protocol, cfg);

    if (!isNone && plan.sizes.empty() && args.protocol != "none")
    {
        std::fprintf(stderr, "Unknown protocol: %s\n\n", args.protocol.c_str());
        print_usage(argv[0]);
        return 2;
    }

    std::unique_ptr<ITransportProtocol> tp = isNone ? nullptr : make_transport_protocol(plan.proto, cfg);

    std::vector<size_t> sizes = args.customSizeSet ? std::vector<size_t>{ args.customSize } : plan.sizes;

    std::printf("=== CAN-TP loopback: %s ===\n", args.protocol.c_str());
    if (!plan.note.empty())
    {
        std::printf("note: %s\n", plan.note.c_str());
    }

    LoopbackCommDriver bus(args.verbose);
    int failures = 0;

    for (size_t sz : sizes)
    {
        bus.reset();
        const auto payload = make_payload(sz, static_cast<uint8_t>(sz));

        bool ok;
        if (isNone)
            ok = run_none_case(bus, args.timeoutMs, payload);
        else if (plan.proto == TpProtocol::CANOPEN_SDO)
            ok = run_canopen_case(tp.get(), bus, args.timeoutMs, payload);
        else
            ok = run_symmetric_case(tp.get(), bus, args.timeoutMs, payload);

        std::printf("  [%s] size=%-5zu\n", ok ? "PASS" : "FAIL", sz);
        if (!ok) ++failures;
    }

    std::printf("=== %zu/%zu passed ===\n", sizes.size() - static_cast<size_t>(failures), sizes.size());
    return failures == 0 ? 0 : 1;
}
