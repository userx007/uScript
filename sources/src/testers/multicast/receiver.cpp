#include <iostream>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>

#define MCAST_PORT 11111
#define MCAST_ADDR "239.1.1.1"
#define BUFFER_SIZE 1024

int main() {
    int sock;
    struct sockaddr_in multicastAddr;
    socklen_t addr_len = sizeof(multicastAddr);
    char buffer[BUFFER_SIZE];
    int opt = 1;

    // 1. Create Socket
    if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        perror("socket");
        return 1;
    }

    // 2. Set SO_REUSEADDR
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 3. Bind to ALL interfaces (0.0.0.0)
    struct sockaddr_in localAddr;
    memset(&localAddr, 0, sizeof(localAddr));
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = htons(MCAST_PORT);
    localAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr*)&localAddr, sizeof(localAddr)) == -1) {
        perror("bind");
        close(sock);
        return 1;
    }

    // 4. Set TTL
    int ttl = 1;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    // 5. Join Group on ALL interfaces (INADDR_ANY)
    struct ip_mreq mreq_any;
    mreq_any.imr_multiaddr.s_addr = inet_addr(MCAST_ADDR);
    mreq_any.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq_any, sizeof(mreq_any)) == -1) {
        perror("IP_ADD_MEMBERSHIP (any)");
    }

    // 6. ✅ CRITICAL: Join Group on LOOPBACK interface (127.0.0.1) explicitly
    // This ensures the kernel delivers packets sent from 127.0.0.1 to the multicast group
    struct ip_mreq mreq_lo;
    mreq_lo.imr_multiaddr.s_addr = inet_addr(MCAST_ADDR);
    inet_pton(AF_INET, "127.0.0.1", &mreq_lo.imr_interface); // Explicitly set to lo
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq_lo, sizeof(mreq_lo)) == -1) {
        perror("IP_ADD_MEMBERSHIP (lo)");
    }

    // 7. ✅ CRITICAL: Enable Loopback Receipt
    int loopback = 1;
    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loopback, sizeof(loopback)) == -1) {
        perror("IP_MULTICAST_LOOP");
    }

    std::cout << "Receiver listening on 0.0.0.0:" << MCAST_PORT << std::endl;

    // 8. Receive Loop
    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytesReceived = recvfrom(sock, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&multicastAddr, &addr_len);

        if (bytesReceived > 0) {
            std::cout << "Received from " << inet_ntoa(multicastAddr.sin_addr)
                      << ":" << ntohs(multicastAddr.sin_port)
                      << " -> " << buffer << std::endl;
        } else {
            perror("recvfrom");
            break;
        }
    }

    setsockopt(sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq_any, sizeof(mreq_any));
    setsockopt(sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq_lo, sizeof(mreq_lo));

    close(sock);
    return 0;
}
