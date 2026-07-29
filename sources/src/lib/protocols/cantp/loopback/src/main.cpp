// Real (SocketCAN) CAN-TP loopback echo application.
//
// Sits on a real Linux CAN interface (typically a vcan virtual bus set up
// for bench/CI testing) and behaves like a minimal ECU/UDS-server stand-in:
// it listens for a transport-protocol message on RxId, stores it, waits a
// configurable delay, then sends the exact same message back out on TxId.
// This gives the other side of the link — the real component under test —
// something concrete to exchange multi-frame CAN-TP traffic with over an
// actual SocketCAN socket, as opposed to main_sim's in-memory two-thread
// simulation of both ends of a link.
//
// Build: part of the cantp_loopback target (see CMakeLists.txt).
// Run:   ip link add dev vcan0 type vcan && ip link set up vcan0
//        ./cantp_loopback isotp vcan0 7E0 7E8 50
#include "TpFactory.hpp"
#include "TpConfig.hpp"
#include "ITransportProtocol.hpp"
#include "RealCommDriver.hpp"
#include "SdoLoopbackServer.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
    std::atomic<bool> g_running{true};

    void on_signal(int /*sig*/)
    {
        g_running = false;
    }

    void print_usage(const char *argv0)
    {
        std::fprintf(stderr,
            "Real CAN-TP loopback echo server (Linux SocketCAN / vcan)\n\n"
            "Usage: %s <protocol> <can-if> <rxId> <txId> <delay-ms> [options]\n\n"
            "  protocol   none | isotp | j1939-bam | j1939-rtscts | canopen | nmea2000\n"
            "  can-if     SocketCAN interface name, e.g. vcan0\n"
            "  rxId       CAN id this app listens for messages on (hex, e.g. 7E0 or 0x7E0)\n"
            "  txId       CAN id this app echoes messages back on (hex, e.g. 7E8 or 0x7E8)\n"
            "  delay-ms   milliseconds to wait, after a message has been fully received\n"
            "             and stored, before sending it back out unmodified\n\n"
            "options:\n"
            "  --timeout MS   per-frame / per-operation wait deadline, ms (default 5000)\n"
            "  --maxlen N     largest reassembled message this app will accept (default 4095)\n"
            "  --once         handle exactly one message then exit (default: run forever)\n"
            "  --verbose      print every message as it is received / echoed\n\n"
            "protocol notes:\n"
            "  isotp / j1939-* / nmea2000 / none:\n"
            "    every full message received on rxId is echoed back on txId, unprompted,\n"
            "    after the configured delay.\n"
            "  canopen:\n"
            "    CANopen SDO is strictly client-request/server-response in both\n"
            "    directions, so this app cannot push data back unprompted. It stores\n"
            "    whatever the peer Downloads, and serves that same data back the next\n"
            "    time the peer Uploads (reads) it -- applying the configured delay to\n"
            "    that Upload response. Block transfer is not supported (see\n"
            "    SdoLoopbackServer.hpp); configure the peer for segmented/expedited SDO.\n\n"
            "example (physical addressing on vcan0):\n"
            "  ip link add dev vcan0 type vcan && ip link set up vcan0\n"
            "  %s isotp vcan0 7E0 7E8 50\n",
            argv0, argv0);
    }

    struct Args
    {
        std::string protocol;
        std::string iface;
        std::string rxId;
        std::string txId;
        uint32_t    delayMs   = 0;
        uint32_t    timeoutMs = 5000;
        size_t      maxLen    = 4095;
        bool        once      = false;
        bool        verbose   = false;
    };

    bool parse_uint(const char *s, uint32_t &out)
    {
        try { out = static_cast<uint32_t>(std::stoul(s)); return true; }
        catch (...) { return false; }
    }

    bool parse_args(int argc, char **argv, Args &out)
    {
        if (argc < 6) return false;

        out.protocol = argv[1];
        out.iface    = argv[2];
        out.rxId     = argv[3];
        out.txId     = argv[4];

        if (!parse_uint(argv[5], out.delayMs))
        {
            std::fprintf(stderr, "Invalid delay-ms: %s\n", argv[5]);
            return false;
        }

        for (int i = 6; i < argc; ++i)
        {
            const std::string a = argv[i];
            if (a == "--verbose")
            {
                out.verbose = true;
            }
            else if (a == "--once")
            {
                out.once = true;
            }
            else if (a == "--timeout" && i + 1 < argc)
            {
                if (!parse_uint(argv[++i], out.timeoutMs))
                {
                    std::fprintf(stderr, "Invalid --timeout: %s\n", argv[i]);
                    return false;
                }
            }
            else if (a == "--maxlen" && i + 1 < argc)
            {
                uint32_t v = 0;
                if (!parse_uint(argv[++i], v))
                {
                    std::fprintf(stderr, "Invalid --maxlen: %s\n", argv[i]);
                    return false;
                }
                out.maxLen = v;
            }
            else
            {
                std::fprintf(stderr, "Unknown argument: %s\n", a.c_str());
                return false;
            }
        }
        return true;
    }

    void log_frame(bool verbose, const char *dir, const std::string &rxId, const std::string &txId,
                    const std::vector<uint8_t> &data)
    {
        if (!verbose) return;
        std::fprintf(stderr, "  [%s] rxId=%-8s txId=%-8s len=%3zu  ", dir, rxId.c_str(), txId.c_str(), data.size());
        for (uint8_t b : data) std::fprintf(stderr, "%02X ", b);
        std::fprintf(stderr, "\n");
    }

    void sleep_delay(uint32_t delayMs)
    {
        if (delayMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }

    // ---- "none": no ITransportProtocol at all, driver used directly (single frame, <= 8 bytes). ----
    // status: true = message handled (success or hard failure), false = nothing arrived (timeout, keep looping).
    bool run_none_iteration(RealCommDriver &drv, const Args &args, bool &ok)
    {
        std::vector<uint8_t> buf(8, 0);
        ICommDriver::ReadOptions opts;
        opts.mode = ICommDriver::ReadMode::Exact;

        auto rr = drv.tout_read(args.timeoutMs, std::span<uint8_t>(buf), opts, args.rxId);
        if (rr.status == ICommDriver::Status::READ_TIMEOUT) return false;

        if (rr.status != ICommDriver::Status::SUCCESS)
        {
            std::fprintf(stderr, "  receive failed, status=%d\n", static_cast<int>(rr.status));
            ok = false;
            return true;
        }
        buf.resize(rr.bytes_read);
        log_frame(args.verbose, "RX", args.rxId, args.txId, buf);

        sleep_delay(args.delayMs);

        auto wr = drv.tout_write(args.timeoutMs, std::span<const uint8_t>(buf), args.txId);
        log_frame(args.verbose, "TX", args.rxId, args.txId, buf);

        ok = (wr.status == ICommDriver::Status::SUCCESS);
        if (!ok) std::fprintf(stderr, "  echo send failed, status=%d\n", static_cast<int>(wr.status));
        return true;
    }

    // ---- ISO-TP / J1939 / NMEA2000 Fast Packet: symmetric ITransportProtocol send()/receive(). ----
    bool run_tp_iteration(ITransportProtocol &tp, RealCommDriver &drv, const Args &args, bool &ok)
    {
        std::vector<uint8_t> buf(args.maxLen, 0);

        auto rr = tp.receive(drv, args.timeoutMs, std::span<uint8_t>(buf), args.rxId, args.txId);
        if (rr.status == ICommDriver::Status::READ_TIMEOUT) return false;

        if (rr.status != ICommDriver::Status::SUCCESS)
        {
            std::fprintf(stderr, "  receive failed, status=%d\n", static_cast<int>(rr.status));
            ok = false;
            return true;
        }
        buf.resize(rr.bytes_read);
        log_frame(args.verbose, "RX", args.rxId, args.txId, buf);

        sleep_delay(args.delayMs);

        // Roles are mirrored on the way back: what we received on rxId (with
        // handshake replies stamped txId) we now transmit on txId (with any
        // handshake we still need from the peer expected back on rxId).
        auto wr = tp.send(drv, args.timeoutMs, buf, args.txId, args.rxId);
        log_frame(args.verbose, "TX", args.rxId, args.txId, buf);

        ok = (wr.status == ICommDriver::Status::SUCCESS);
        if (!ok) std::fprintf(stderr, "  echo send failed, status=%d\n", static_cast<int>(wr.status));
        return true;
    }

    // ---- CANopen SDO: store on Download, echo back on the peer's next Upload. ----
    // See SdoLoopbackServer.hpp / the usage note in print_usage() for why this
    // can't be a simple "receive, then push back" loop the way the other
    // protocols are.
    bool run_canopen_iteration(RealCommDriver &drv, const Args &args, std::vector<uint8_t> &stored,
                                bool &haveStored, bool &ok)
    {
        bool isDownload = false;
        bool sawFirstFrame = false;

        auto onDirectionKnown = [&](bool download)
        {
            isDownload = download;
            sawFirstFrame = true;
            // Only an Upload echoes previously-stored data back to the peer;
            // that's the response this app's configurable delay applies to.
            if (!download) sleep_delay(args.delayMs);
        };

        bool transacted = SdoLoopbackServer::serve_one(drv, args.rxId, args.txId, args.timeoutMs, stored, onDirectionKnown);

        if (!sawFirstFrame)
        {
            // Nothing arrived at all within the timeout -- not an error, just keep polling.
            return false;
        }

        if (!transacted)
        {
            std::fprintf(stderr, "  SDO %s transaction failed\n", isDownload ? "download" : "upload");
            ok = false;
            return true;
        }

        if (isDownload) haveStored = true;
        log_frame(args.verbose, isDownload ? "RX(download)" : "TX(upload)", args.rxId, args.txId, stored);
        ok = true;
        return true;
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
    TpProtocol protoEnum = TpProtocol::NONE;
    bool isNone = (args.protocol == "none");

    if (!isNone)
    {
        std::string lookup = args.protocol;
        if (lookup == "j1939-bam")       { cfg.j1939UseBam = true;  lookup = "J1939"; }
        else if (lookup == "j1939-rtscts") { cfg.j1939UseBam = false; lookup = "J1939"; }
        else if (lookup == "canopen")    { cfg.canOpenUseBlock = false; lookup = "CANOPEN"; } // block xfer unsupported, see SdoLoopbackServer.hpp

        if (!tp_protocol_from_string(lookup, protoEnum))
        {
            std::fprintf(stderr, "Unknown protocol: %s\n\n", args.protocol.c_str());
            print_usage(argv[0]);
            return 2;
        }
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    RealCommDriver drv(args.iface);
    if (!drv.is_open())
    {
        std::fprintf(stderr,
            "Failed to open CAN interface '%s'. Is it up? e.g.:\n"
            "  ip link add dev %s type vcan && ip link set up %s\n",
            args.iface.c_str(), args.iface.c_str(), args.iface.c_str());
        return 1;
    }

    std::unique_ptr<ITransportProtocol> tp = isNone ? nullptr : make_transport_protocol(protoEnum, cfg);

    std::printf("=== CAN-TP loopback echo server ===\n");
    std::printf("  protocol : %s\n", args.protocol.c_str());
    std::printf("  if       : %s\n", args.iface.c_str());
    std::printf("  rxId     : %s\n", args.rxId.c_str());
    std::printf("  txId     : %s\n", args.txId.c_str());
    std::printf("  delay    : %u ms\n", args.delayMs);
    std::printf("  timeout  : %u ms\n", args.timeoutMs);
    std::printf("Waiting for messages (Ctrl+C to stop)...\n");

    std::vector<uint8_t> canOpenStored;
    bool canOpenHaveStored = false;
    uint64_t handled = 0;

    while (g_running)
    {
        bool gotSomething;
        bool ok = false;

        if (isNone)
        {
            gotSomething = run_none_iteration(drv, args, ok);
        }
        else if (protoEnum == TpProtocol::CANOPEN_SDO)
        {
            gotSomething = run_canopen_iteration(drv, args, canOpenStored, canOpenHaveStored, ok);
        }
        else
        {
            gotSomething = run_tp_iteration(*tp, drv, args, ok);
        }

        if (!gotSomething)
        {
            continue; // plain timeout waiting for the next message; keep polling
        }

        ++handled;
        std::printf("[%llu] %s\n", static_cast<unsigned long long>(handled), ok ? "echoed" : "FAILED");

        if (args.once)
        {
            return ok ? 0 : 1;
        }
    }

    std::printf("Stopped after %llu message(s).\n", static_cast<unsigned long long>(handled));
    return 0;
}
