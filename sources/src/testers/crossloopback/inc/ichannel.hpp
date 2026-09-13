// ichannel.hpp
//
// Common interface every transport driver (UART, CAN, TCP/IP, UDP,
// raw-Ethernet) implements. loopback.cpp only ever talks to channels
// through this interface, which is what makes "any combination of input
// and output" possible: the main loop does not know or care whether it is
// bridging UART to CAN or CAN to TCP, it just calls readMessage() on the
// input channel and writeMessage() on the output channel.

#pragma once

#include <memory>
#include <string>

#include "common.hpp"

namespace loopback
{

class IChannel
{
public:
    virtual ~IChannel() = default;

    // Open/bind/connect/listen as appropriate for this transport. Returns
    // false (after logging the reason) on failure.
    virtual bool open() = 0;

    virtual void close() = 0;

    // Block until one message (a CAN frame's payload, or one chunk of
    // bytes for stream/datagram transports) is available, or until
    // g_stop is set / a fatal I/O error occurs.
    //
    // Implementations remember whatever "reply-to" context they need
    // (accepted TCP client fd, UDP sender address, raw-Ethernet source
    // MAC, ...) internally, so that a subsequent writeMessage() on the
    // *same* channel object automatically replies to the right place.
    // This is what lets "-i X" with no "-o" reproduce the exact
    // single-socket mirror behaviour the original standalone tools had.
    //
    // Returns false when no further messages will come (stop requested,
    // fatal error, or - for connection oriented transports - the peer is
    // gone and cannot be replaced).
    virtual bool readMessage(Message &msg) = 0;

    // Send a message out this channel. May mutate msg (the CAN channel
    // truncates oversized payloads to 8 bytes here, logging a warning,
    // so that the TX dump printed by the caller reflects what was
    // actually put on the wire/bus).
    //
    // Returns false on failure; the main loop logs and continues reading
    // more input rather than aborting the whole bridge, since a single
    // failed delivery (e.g. no TCP client connected yet) does not mean
    // the input side is done.
    virtual bool writeMessage(Message &msg) = 0;

    // Short human-readable identity used in banners and dump lines, e.g.
    // "uart:/dev/tnt0@115200" or "kvcan:vcan0".
    virtual std::string name() const = 0;

    // A coarser identity used only to detect "the -i and -o spec refer to
    // the same underlying endpoint" (e.g. same CAN interface, same UART
    // device, same TCP listen port) so loopback.cpp can collapse them
    // into a single shared channel object instead of opening the same
    // resource twice - which for CAN would otherwise create an infinite
    // echo storm between the two sockets, and for UART/TCP would just
    // fail or fight over the same fd/port.
    virtual std::string identity() const = 0;

    virtual bool isCan() const { return false; }

    // Print an RX/TX dump line for msg, in this channel's native format.
    virtual void dump(const char *dir, const Message &msg) const = 0;
};

using ChannelPtr = std::shared_ptr<IChannel>;

} // namespace loopback
