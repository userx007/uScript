// loopback.cpp
//
// Unified loopback/bridge tool. Replaces the five standalone testers
// (uart_loopback, kvcan_loopback/vcan_mirror, eth_loopback_server,
// udp_loopback_server, eth_raw_loopback_server) with one program that can
// mirror a channel to itself (their original behaviour, unchanged) or
// bridge any supported transport to any other.
//
// Usage:
//   loopback -i <input-spec> [-o <output-spec>] [-t <delay-ms>]
//
//   -i   input channel (required). Whatever arrives here is forwarded.
//   -o   output channel (optional). If omitted - or if it names the same
//        endpoint as -i (same CAN interface, same UART device, same TCP/
//        UDP listen port, same raw-Eth interface) - the tool mirrors on
//        the single input channel exactly like the original per-protocol
//        tools did.
//   -t   delay, in milliseconds, between receiving a message and sending
//        it back out (default: 0).
//
// Spec syntax (see channel_factory.hpp for the full grammar):
//   uart:<device>[/<baud>]                 e.g. uart:/dev/tnt0/115200
//   kvcan:<iface>[/<can_id>]                e.g. kvcan:vcan0/0x100
//   tcpip:[server/]<port>[/<bindaddr>]      e.g. tcpip:5000
//   tcpip:client/<host>/<port>              e.g. tcpip:client/10.0.0.5/5000
//   udp:[server/]<port>[/<bindaddr>]        e.g. udp:5000
//   udp:client/<host>/<port>                e.g. udp:client/10.0.0.5/5000
//   raweth:<ifname>[/<ethertype>][/promisc] e.g. raweth:eth0/0x88b5
//
// Examples:
//   loopback -i uart:/dev/tnt0/115200                       (UART -> UART, mirror)
//   loopback -i kvcan:vcan0                                  (CAN  -> CAN,  mirror)
//   loopback -i uart:/dev/tnt0/115200 -o kvcan:vcan0/0x100 -t 500
//   loopback -i kvcan:can0 -o tcpip:client/10.0.0.5/6000 -t 100
//
// Every RX and every TX is dumped to stdout, CAN frames in candump-like
// "ID [DLC] bytes" form, everything else as "[len] hex bytes" - matching
// the format the original standalone tools already used.

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "channel_factory.hpp"
#include "common.hpp"

namespace loopback
{
volatile sig_atomic_t g_stop = 0;

static void onSignal(int /*sig*/) { g_stop = 1; }

void install_signal_handlers()
{
    // SA_RESTART deliberately left off: without this, glibc's signal()
    // wrapper would transparently restart an interrupted blocking
    // read()/recv()/accept(), and Ctrl-C would appear to do nothing until
    // the next byte/frame/datagram arrived.
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = onSignal;
    sa.sa_flags   = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}
} // namespace loopback

namespace
{

void printUsage(const char *argv0)
{
    std::fprintf(stderr,
        "Usage: %s -i <input-spec> [-o <output-spec>] [-t <delay-ms>]\n"
        "\n"
        "  -i   input channel (required)\n"
        "  -o   output channel (default: mirror the input channel back to itself)\n"
        "  -t   delay in ms between RX and the mirrored/forwarded TX (default: 0)\n"
        "\n"
        "  spec := uart:<device>[/<baud>]\n"
        "        | kvcan:<iface>[/<can_id>]\n"
        "        | tcpip:[server/]<port>[/<bindaddr>] | tcpip:client/<host>/<port>\n"
        "        | udp:[server/]<port>[/<bindaddr>]   | udp:client/<host>/<port>\n"
        "        | raweth:<ifname>[/<ethertype>][/promisc]\n"
        "\n"
        "Examples:\n"
        "  %s -i uart:/dev/tnt0/115200\n"
        "  %s -i kvcan:vcan0\n"
        "  %s -i uart:/dev/tnt0/115200 -o kvcan:vcan0/0x100 -t 500\n",
        argv0, argv0, argv0, argv0);
}

void sleepInterruptible(int delay_ms)
{
    const int step_ms = 20;
    int remaining = delay_ms;
    while (remaining > 0 && !loopback::g_stop)
    {
        int chunk = std::min(remaining, step_ms);
        std::this_thread::sleep_for(std::chrono::milliseconds(chunk));
        remaining -= chunk;
    }
}

} // namespace

int main(int argc, char *argv[])
{
    std::string in_spec, out_spec;
    int delay_ms = 0;

    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        if (arg == "-i" && i + 1 < argc)
            in_spec = argv[++i];
        else if (arg == "-o" && i + 1 < argc)
            out_spec = argv[++i];
        else if (arg == "-t" && i + 1 < argc)
            delay_ms = std::atoi(argv[++i]);
        else if (arg == "-h" || arg == "--help")
        {
            printUsage(argv[0]);
            return EXIT_SUCCESS;
        }
        else
        {
            std::fprintf(stderr, "unrecognized argument '%s'\n", arg.c_str());
            printUsage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (in_spec.empty())
    {
        std::fprintf(stderr, "-i <input-spec> is required\n");
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }
    if (delay_ms < 0)
    {
        std::fprintf(stderr, "-t delay must be >= 0\n");
        return EXIT_FAILURE;
    }

    loopback::ChannelPtr input_channel;
    loopback::ChannelPtr output_channel;
    bool mirror = out_spec.empty();

    try
    {
        input_channel = loopback::createChannel(in_spec);

        if (mirror)
        {
            output_channel = input_channel;
        }
        else
        {
            auto candidate_output = loopback::createChannel(out_spec);
            if (candidate_output->identity() == input_channel->identity())
            {
                // Same underlying endpoint (e.g. "-i kvcan:vcan0 -o
                // kvcan:vcan0"): collapse to one shared channel instead of
                // opening the resource twice. For CAN in particular this
                // also avoids an echo storm between two sockets on the
                // same interface (see can_channel.hpp).
                loopback::log_info("loopback",
                    "-o resolves to the same endpoint as -i (" + input_channel->identity() +
                        "); mirroring on a single channel instead of opening it twice");
                output_channel = input_channel;
                mirror = true;
            }
            else
            {
                output_channel = candidate_output;
            }
        }
    }
    catch (const loopback::SpecError &e)
    {
        std::fprintf(stderr, "%s\n", e.what());
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }

    // Only meaningful for CAN: enables the CAN_RAW_RECV_OWN_MSGS=0 single-
    // socket trick that lets one socket both receive and reply without an
    // echo storm. See can_channel.hpp.
    if (auto can = std::dynamic_pointer_cast<loopback::CanChannel>(input_channel))
        can->setMirrorMode(mirror);

    loopback::install_signal_handlers();

    if (!input_channel->open())
        return EXIT_FAILURE;
    if (!mirror && !output_channel->open())
        return EXIT_FAILURE;

    std::printf("loopback: %s -> %s (delay %dms) - press Ctrl-C to stop\n",
                input_channel->name().c_str(),
                mirror ? "(mirror)" : output_channel->name().c_str(),
                delay_ms);
    std::printf("--------------------------------------------------\n");

    while (!loopback::g_stop)
    {
        loopback::Message msg;
        if (!input_channel->readMessage(msg))
            break;

        input_channel->dump("RX", msg);

        sleepInterruptible(delay_ms);
        if (loopback::g_stop)
            break;

        if (!output_channel->writeMessage(msg))
        {
            loopback::log_warn("loopback", "failed to deliver message to " + output_channel->name() +
                                                ", dropping it and continuing");
            continue;
        }

        output_channel->dump("TX", msg);
    }

    std::printf("\nloopback: shutting down.\n");
    input_channel->close();
    if (output_channel && output_channel != input_channel)
        output_channel->close();

    return EXIT_SUCCESS;
}
