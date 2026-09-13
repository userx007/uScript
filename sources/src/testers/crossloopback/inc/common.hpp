// common.hpp
//
// Shared types/utilities used by every channel implementation and by
// loopback.cpp itself: the Message struct that flows between an input and
// an output channel, the global Ctrl-C/SIGTERM stop flag, and small
// logging/hex-dump helpers so every driver prints in the same style the
// original standalone tools used (candump-like "DIR  [len] XX XX XX").

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <signal.h>

namespace loopback
{

// Set by the SIGINT/SIGTERM handler installed in main(). Every blocking
// call in every channel (read/recv/accept/...) must check this after an
// EINTR so Ctrl-C actually stops the tool promptly instead of only after
// the next byte/frame/datagram arrives.
extern volatile sig_atomic_t g_stop;

void install_signal_handlers();

// A single logical unit of data moving from the input channel to the
// output channel: for byte-stream transports (UART, TCP, UDP, raw-Eth)
// this is exactly the chunk that arrived in one read/recv call; for CAN
// it is the payload of one CAN frame.
//
// can_id/has_can_id let a CAN *input* channel tell a CAN *output* channel
// which arbitration ID to resend with when the output channel wasn't
// given a fixed one of its own (e.g. bridging kvcan/vcan0 -> kvcan/vcan1
// with no explicit ID on the output spec just forwards the ID that was
// received). Non-CAN channels ignore this field entirely.
struct Message
{
    std::vector<uint8_t> data;
    bool     has_can_id = false;
    uint32_t can_id     = 0;
};

// ---- logging -------------------------------------------------------

inline void log_info(const std::string &tag, const std::string &msg)
{
    std::printf("[%s] %s\n", tag.c_str(), msg.c_str());
    std::fflush(stdout);
}

inline void log_warn(const std::string &tag, const std::string &msg)
{
    std::fprintf(stderr, "[%s] WARNING: %s\n", tag.c_str(), msg.c_str());
    std::fflush(stderr);
}

inline void log_err(const std::string &tag, const std::string &msg)
{
    std::fprintf(stderr, "[%s] ERROR: %s\n", tag.c_str(), msg.c_str());
    std::fflush(stderr);
}

// ---- dump helpers ----------------------------------------------------

// Generic "DIR  [len] XX XX XX ..." hex dump used by every non-CAN
// channel, matching the format the original uart/tcp/udp/raw-eth tools
// already printed.
inline void dump_bytes(const std::string &chan_tag, const char *dir,
                        const uint8_t *buf, size_t len)
{
    std::printf("%-10s %-8s [%zu] ", chan_tag.c_str(), dir, len);
    for (size_t i = 0; i < len; i++)
        std::printf("%02X ", buf[i]);
    std::printf("\n");
    std::fflush(stdout);
}

// candump-style "DIR  ID  [DLC] XX XX ..." dump used by the CAN channel.
inline void dump_can(const std::string &chan_tag, const char *dir,
                      uint32_t can_id, const uint8_t *buf, size_t len)
{
    std::printf("%-10s %-8s %03X  [%zu] ", chan_tag.c_str(), dir, can_id, len);
    for (size_t i = 0; i < len; i++)
        std::printf("%02X ", buf[i]);
    std::printf("\n");
    std::fflush(stdout);
}

} // namespace loopback
