#include "tester_platform.h" // POSIX sockets / Winsock portability (must come first on Windows)

#include <iostream>
#include <string.h>

#define MCAST_PORT  11111
#define MCAST_ADDR  "239.1.1.1"
#define BUFFER_SIZE 1024

int main()
{
    if (!tp_net_init()) {
        std::cerr << "Winsock initialisation failed" << std::endl;
        return 1;
    }

    tp_socket_t sock;
    struct sockaddr_in multicastAddr;
    socklen_t addr_len = sizeof(multicastAddr);
    char buffer[BUFFER_SIZE];
    int opt = 1;

    // 1. Create Socket
    if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == TP_INVALID_SOCKET) {
        tp_perror("socket");
        return 1;
    }

    // 2. Set SO_REUSEADDR (on both platforms this lets several receivers share the multicast port)
    tp_setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 3. Bind to ALL interfaces (0.0.0.0)
    struct sockaddr_in localAddr;
    memset(&localAddr, 0, sizeof(localAddr));
    localAddr.sin_family      = AF_INET;
    localAddr.sin_port        = htons(MCAST_PORT);
    localAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&localAddr, sizeof(localAddr)) == -1) {
        tp_perror("bind");
        tp_close(sock);
        return 1;
    }

    // 4. Set TTL
    int ttl = 1;
    tp_setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    // 5. Join Group on ALL interfaces (INADDR_ANY)
    struct ip_mreq mreq_any;
    inet_pton(AF_INET, MCAST_ADDR, &mreq_any.imr_multiaddr);
    mreq_any.imr_interface.s_addr = htonl(INADDR_ANY);
    if (tp_setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq_any, sizeof(mreq_any)) == -1) {
        tp_perror("IP_ADD_MEMBERSHIP (any)");
    }

    // 6. ✅ CRITICAL (Linux): Join Group on LOOPBACK interface (127.0.0.1) explicitly
    // This ensures the kernel delivers packets sent from 127.0.0.1 to the multicast group.
    // (On Windows the loopback adapter has no multicast support, so this join is expected to
    //  fail there; the message is harmless — membership on the default interface above is what counts.)
    struct ip_mreq mreq_lo;
    inet_pton(AF_INET, MCAST_ADDR, &mreq_lo.imr_multiaddr);
    inet_pton(AF_INET, "127.0.0.1", &mreq_lo.imr_interface); // Explicitly set to lo
    if (tp_setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq_lo, sizeof(mreq_lo)) == -1) {
        tp_perror("IP_ADD_MEMBERSHIP (lo)");
    }

    // 7. ✅ CRITICAL: Enable Loopback Receipt
    int loopback = 1;
    if (tp_setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loopback, sizeof(loopback)) == -1) {
        tp_perror("IP_MULTICAST_LOOP");
    }

    std::cout << "Receiver listening on 0.0.0.0:" << MCAST_PORT << std::endl;

    // 8. Receive Loop
    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        // BUFFER_SIZE - 1: keep the buffer NUL-terminated for the stream output below
        addr_len              = sizeof(multicastAddr);
        ssize_t bytesReceived = tp_recvfrom(sock, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr *)&multicastAddr, &addr_len);

        if (bytesReceived > 0) {
            char szFrom[INET_ADDRSTRLEN] = "?";
            inet_ntop(AF_INET, &multicastAddr.sin_addr, szFrom, sizeof(szFrom));
            std::cout << "Received from " << szFrom
                      << ":" << ntohs(multicastAddr.sin_port)
                      << " -> " << buffer << std::endl;
        } else {
            tp_perror("recvfrom");
            break;
        }
    }

    tp_setsockopt(sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq_any, sizeof(mreq_any));
    tp_setsockopt(sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq_lo, sizeof(mreq_lo));

    tp_close(sock);
    return 0;
}
