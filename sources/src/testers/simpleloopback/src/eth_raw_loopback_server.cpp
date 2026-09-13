// eth_raw_loopback_server.cpp
//
// Minimal raw-Ethernet loopback server: listens on a network interface at
// the link layer (AF_PACKET / SOCK_RAW) and echoes every frame it sees
// straight back onto the wire, with source/destination MAC addresses
// swapped so the reply actually routes back to the sender. This is the
// raw-Ethernet analog of eth_loopback_server.cpp (which loops back over
// TCP/IP) — use this one when the thing under test talks directly to L2
// (custom EtherTypes, non-IP protocols, driver code that builds its own
// Ethernet headers) instead of over a TCP socket.
//
// It is a standalone test tool: no ICommDriver, no project headers, single
// translation unit.
//
//   g++ -std=c++17 -O2 -Wall -Wextra -o eth_raw_loopback_server eth_raw_loopback_server.cpp
//
// Usage:
//   sudo ./eth_raw_loopback_server <ifname> [ethertype_hex] [--promisc]
//
//   ifname         Interface to listen/echo on (e.g. "eth0", "veth0", or
//                   "lo" for local testing — "lo" does carry a (fake)
//                   Ethernet header under AF_PACKET, so it works fine for
//                   exercising a driver's frame-parsing code).
//   ethertype_hex  Only capture/echo this EtherType, e.g. "0x88b5". If
//                   omitted, captures ETH_P_ALL (every EtherType).
//   --promisc      Put the interface into promiscuous mode so frames not
//                   addressed to this host's own MAC are captured too.
//                   Needed if the peer under test sends to some MAC other
//                   than this box's real NIC address.
//
// Requires CAP_NET_RAW (typically: run as root, or
//   sudo setcap cap_net_raw+ep ./eth_raw_loopback_server
// once, then run unprivileged).
//
// Behaviour:
//   - Opens an AF_PACKET/SOCK_RAW socket bound to the given interface, so
//     each read/write includes the full 14-byte Ethernet header — this
//     tool builds and rewrites that header itself, unlike SOCK_DGRAM which
//     would strip it.
//   - There is no "connection": every frame that arrives is a standalone
//     unit. For each one, the destination and source MAC addresses are
//     swapped and the frame (EtherType + payload, unchanged) is sent
//     straight back out the same interface — the raw-Ethernet equivalent
//     of "echo whatever you receive".
//   - Frames the socket sees because it *sent* them (PACKET_OUTGOING, or
//     source MAC == our own interface MAC) are skipped, otherwise the
//     server would echo its own echoes forever.
//   - Dumps every frame, both RX (as received) and TX (as echoed back, with
//     addresses already swapped) in a candump-like table — see
//     print_frame() — same DIR/.../DATA layout kvcan_loopback.c uses for
//     CAN frames, with SRC MAC/DST MAC/ETHERTYPE in place of CAN's ID/DLC.
//   - Ctrl+C (SIGINT) or SIGTERM stops the server after the current
//     recvfrom() call returns.

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{
    constexpr size_t FRAME_BUF_SIZE = 65536; // generous; covers jumbo frames too
    constexpr size_t ETH_HDR_LEN    = 14;    // dst(6) + src(6) + ethertype(2)
    constexpr size_t MAC_LEN        = 6;
    // Ethernet payloads can run into the tens of KB (jumbo frames) — unlike
    // a CAN frame's 8 bytes, printing every byte would flood the terminal.
    // Cap the console dump and note how much was left out, same idea as
    // CommDumpModel's preview truncation in the GUI.
    constexpr size_t DUMP_MAX_BYTES = 64;

    volatile sig_atomic_t g_stop = 0;

    void on_signal(int /*sig*/)
    {
        g_stop = 1;
    }

    std::string mac_to_string(const uint8_t* mac)
    {
        char sz[18];
        std::snprintf(sz, sizeof(sz), "%02x:%02x:%02x:%02x:%02x:%02x",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return std::string(sz);
    }

    bool mac_equal(const uint8_t* a, const uint8_t* b)
    {
        return std::memcmp(a, b, MAC_LEN) == 0;
    }

    /** Print one Ethernet frame in a candump-like table row: DIR, SRC MAC,
     *  DST MAC, ETHERTYPE, LEN, and a hex dump of the payload (the bytes
     *  after the 14-byte header) — the raw-Ethernet analogue of
     *  kvcan_loopback.c's print_frame(), with CAN's ID/DLC swapped out for
     *  the fields that actually identify an Ethernet frame. Reads the
     *  frame straight out of the wire buffer (`frame`/`frameLen`) rather
     *  than taking the header fields as separate arguments, so the exact
     *  same call works for both the as-received RX frame and the
     *  address-swapped TX frame that gets echoed back — same as kvcan's
     *  print_frame(prefix, &frame) being called before AND after the frame
     *  is reused for the echoed reply.
     */
    void print_frame(const char* prefix, const uint8_t* frame, size_t frameLen)
    {
        if (frameLen < ETH_HDR_LEN)
            return; // caller already rejects runts before this would be called

        const uint8_t* dst      = frame;
        const uint8_t* src      = frame + MAC_LEN;
        const uint16_t ethertype = ntohs(*reinterpret_cast<const uint16_t*>(frame + 2 * MAC_LEN));
        const uint8_t* payload   = frame + ETH_HDR_LEN;
        const size_t   payloadLen = frameLen - ETH_HDR_LEN;

        std::printf("%-4s  %-17s  %-17s  0x%04x  %-6zu ",
                    prefix, mac_to_string(src).c_str(), mac_to_string(dst).c_str(),
                    ethertype, payloadLen);

        const size_t shown = std::min(payloadLen, DUMP_MAX_BYTES);
        for (size_t i = 0; i < shown; ++i)
            std::printf("%02X ", payload[i]);
        if (payloadLen > shown)
            std::printf("... (+%zu more bytes)", payloadLen - shown);
        std::printf("\n");
        std::fflush(stdout);
    }

    // Look up an interface's index and MAC address via ioctl(). Returns
    // false on failure (e.g. unknown interface name, insufficient perms).
    bool resolve_interface(int fd, const std::string& ifname, int& ifindex, uint8_t ownMac[MAC_LEN])
    {
        struct ifreq sIfr = {};
        std::strncpy(sIfr.ifr_name, ifname.c_str(), IFNAMSIZ - 1);

        if (::ioctl(fd, SIOCGIFINDEX, &sIfr) < 0)
        {
            std::fprintf(stderr, "ioctl(SIOCGIFINDEX, %s) failed, errno=%d (%s)\n",
                         ifname.c_str(), errno, std::strerror(errno));
            return false;
        }
        ifindex = sIfr.ifr_ifindex;

        std::memset(&sIfr, 0, sizeof(sIfr));
        std::strncpy(sIfr.ifr_name, ifname.c_str(), IFNAMSIZ - 1);
        if (::ioctl(fd, SIOCGIFHWADDR, &sIfr) < 0)
        {
            std::fprintf(stderr, "ioctl(SIOCGIFHWADDR, %s) failed, errno=%d (%s)\n",
                         ifname.c_str(), errno, std::strerror(errno));
            return false;
        }
        std::memcpy(ownMac, sIfr.ifr_hwaddr.sa_data, MAC_LEN);
        return true;
    }

    bool enable_promiscuous(int fd, int ifindex)
    {
        struct packet_mreq sMreq = {};
        sMreq.mr_ifindex = ifindex;
        sMreq.mr_type    = PACKET_MR_PROMISC;

        if (::setsockopt(fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &sMreq, sizeof(sMreq)) < 0)
        {
            std::fprintf(stderr, "setsockopt(PACKET_ADD_MEMBERSHIP) failed, errno=%d (%s)\n",
                         errno, std::strerror(errno));
            return false;
        }
        return true;
    }
} // namespace


int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "Usage: %s <ifname> [ethertype_hex] [--promisc]\n", argv[0]);
        return 1;
    }

    const std::string strIfName = argv[1];
    uint16_t          u16EtherTypeFilter = ETH_P_ALL; // capture everything by default
    bool               bPromisc          = false;

    for (int i = 2; i < argc; ++i)
    {
        const std::string strArg = argv[i];
        if (strArg == "--promisc")
        {
            bPromisc = true;
        }
        else
        {
            u16EtherTypeFilter = static_cast<uint16_t>(std::strtoul(strArg.c_str(), nullptr, 0));
        }
    }

    // Same rationale as the TCP loopback server: install with SA_RESTART
    // off so a blocking recvfrom() actually returns EINTR and g_stop gets
    // observed promptly instead of only after the next frame arrives.
    struct sigaction sSigAction = {};
    sSigAction.sa_handler = on_signal;
    sSigAction.sa_flags   = 0; // no SA_RESTART
    ::sigemptyset(&sSigAction.sa_mask);
    ::sigaction(SIGINT, &sSigAction, nullptr);
    ::sigaction(SIGTERM, &sSigAction, nullptr);

    // AF_PACKET/SOCK_RAW gives us the full Ethernet frame, header included,
    // on both read and write — required since we rewrite the header
    // in-place to bounce the frame back to its sender.
    const int sockFd = ::socket(AF_PACKET, SOCK_RAW, htons(u16EtherTypeFilter));
    if (sockFd < 0)
    {
        std::fprintf(stderr, "socket(AF_PACKET, SOCK_RAW) failed, errno=%d (%s)\n"
                     "(this usually means missing CAP_NET_RAW — try running as root, or:\n"
                     " sudo setcap cap_net_raw+ep %s)\n",
                     errno, std::strerror(errno), argv[0]);
        return 1;
    }

    int     ifindex = -1;
    uint8_t ownMac[MAC_LEN] = {};
    if (!resolve_interface(sockFd, strIfName, ifindex, ownMac))
    {
        ::close(sockFd);
        return 1;
    }

    struct sockaddr_ll sBindAddr = {};
    sBindAddr.sll_family   = AF_PACKET;
    sBindAddr.sll_protocol = htons(u16EtherTypeFilter);
    sBindAddr.sll_ifindex  = ifindex;

    if (::bind(sockFd, reinterpret_cast<struct sockaddr*>(&sBindAddr), sizeof(sBindAddr)) < 0)
    {
        std::fprintf(stderr, "bind() to %s failed, errno=%d (%s)\n",
                     strIfName.c_str(), errno, std::strerror(errno));
        ::close(sockFd);
        return 1;
    }

    if (bPromisc && !enable_promiscuous(sockFd, ifindex))
    {
        ::close(sockFd);
        return 1;
    }

    std::printf("eth_raw_loopback_server listening on %s (mac %s), ethertype=0x%04x%s (Ctrl+C to stop)\n",
               strIfName.c_str(), mac_to_string(ownMac).c_str(), u16EtherTypeFilter,
               bPromisc ? ", promiscuous" : "");
    std::printf("%-4s  %-17s  %-17s  %-6s  %-6s  %s\n", "DIR", "SRC MAC", "DST MAC", "ETHTYPE", "LEN", "DATA");
    std::printf("--------------------------------------------------------------------------------\n");

    uint8_t buffer[FRAME_BUF_SIZE];
    size_t  totalFrames = 0;
    size_t  totalBytes  = 0;

    while (!g_stop)
    {
        struct sockaddr_ll sSrcAddr = {};
        socklen_t          szAddrLen = sizeof(sSrcAddr);

        const ssize_t n = ::recvfrom(sockFd, buffer, sizeof(buffer), 0,
                                     reinterpret_cast<struct sockaddr*>(&sSrcAddr), &szAddrLen);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            std::fprintf(stderr, "recvfrom() failed, errno=%d (%s)\n", errno, std::strerror(errno));
            break;
        }

        // Frames the kernel hands us because we transmitted them ourselves
        // (PACKET_OUTGOING) must be ignored, or every echo would loop back
        // in as a "new" frame to echo again, forever.
        if (sSrcAddr.sll_pkttype == PACKET_OUTGOING)
        {
            continue;
        }

        if (static_cast<size_t>(n) < ETH_HDR_LEN)
        {
            std::fprintf(stderr, "runt frame (%zd bytes), dropping\n", n);
            continue;
        }

        uint8_t* pDst       = buffer;           // bytes 0..5
        uint8_t* pSrc       = buffer + MAC_LEN;  // bytes 6..11
        uint8_t  origSrc[MAC_LEN];
        std::memcpy(origSrc, pSrc, MAC_LEN);

        // Defensive second check: skip anything that already carries our
        // own MAC as its source (covers edge cases the pkttype filter
        // might miss, e.g. some virtual interfaces).
        if (mac_equal(origSrc, ownMac))
        {
            continue;
        }

        const size_t szLen = static_cast<size_t>(n);

        print_frame("RX", buffer, szLen);

        // Swap addresses in place: reply goes back to whoever sent it,
        // "from" us. Payload and EtherType are left untouched.
        std::memcpy(pDst, origSrc, MAC_LEN);
        std::memcpy(pSrc, ownMac, MAC_LEN);

        struct sockaddr_ll sDstAddr = sSrcAddr;
        sDstAddr.sll_ifindex = ifindex;
        sDstAddr.sll_halen   = MAC_LEN;
        std::memcpy(sDstAddr.sll_addr, origSrc, MAC_LEN);

        const ssize_t sent = ::sendto(sockFd, buffer, szLen, 0,
                                      reinterpret_cast<struct sockaddr*>(&sDstAddr), sizeof(sDstAddr));
        if (sent < 0)
        {
            std::fprintf(stderr, "sendto() failed, errno=%d (%s)\n", errno, std::strerror(errno));
            continue;
        }
        if (static_cast<size_t>(sent) != szLen)
        {
            std::fprintf(stderr, "short write echoing frame (%zd of %zu bytes)\n", sent, szLen);
        }

        print_frame("TX", buffer, szLen);   // buffer now holds the swapped (echoed) header

        ++totalFrames;
        totalBytes += szLen;
    }

    std::printf("eth_raw_loopback_server shutting down (%zu frames / %zu bytes echoed)\n",
               totalFrames, totalBytes);
    ::close(sockFd);
    return 0;
}
