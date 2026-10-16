#include "tester_platform.h" // POSIX sockets / Winsock portability (must come first on Windows)

#include <iostream>
#include <string.h>

#define MCAST_PORT 11111
#define MCAST_ADDR "239.1.1.1" // Local Administration Multicast
#define TTL        1

// Interface used for outgoing multicast (IP_MULTICAST_IF).
//   Linux  : 127.0.0.1 so the loopback interface is used (see build.sh: "ip link set lo multicast on")
//   Windows: the loopback adapter does not carry multicast, so use 0.0.0.0 = system default interface.
// Override with the first command line argument, e.g. `sender 192.168.1.10`.
#if defined(_WIN32)
#define DEFAULT_MCAST_IF "0.0.0.0"
#else
#define DEFAULT_MCAST_IF "127.0.0.1"
#endif

int main(int argc, char **argv)
{
    if (!tp_net_init()) {
        std::cerr << "Winsock initialisation failed" << std::endl;
        return 1;
    }
    const char *pszInterface = (argc > 1) ? argv[1] : DEFAULT_MCAST_IF;

    tp_socket_t sock;
    struct sockaddr_in multicastAddr;

    // 1. Create UDP Socket
    if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == TP_INVALID_SOCKET) {
        tp_perror("Socket creation failed");
        return 1;
    }

    // 2. Set Multicast TTL (Time To Live)
    // TTL=1 ensures packets don't leave the local subnet/loopback
    int ttl = TTL;
    if (tp_setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) == -1) {
        tp_perror("setsockopt IP_MULTICAST_TTL");
        tp_close(sock);
        return 1;
    }

    // 3. (Optional) Set Multicast Interface
    // For loopback, use INADDR_LOOPBACK (127.0.0.1)
    // For eth0, use INADDR_ANY (0.0.0.0)
    struct in_addr localInterface;
    if (inet_pton(AF_INET, pszInterface, &localInterface) != 1) {
        std::cerr << "Invalid interface address: " << pszInterface << std::endl;
        tp_close(sock);
        return 1;
    }
    if (tp_setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &localInterface, sizeof(localInterface)) == -1) {
        tp_perror("setsockopt IP_MULTICAST_IF");
        tp_close(sock);
        return 1;
    }

    // 4. Configure Multicast Address
    memset(&multicastAddr, 0, sizeof(multicastAddr));
    multicastAddr.sin_family = AF_INET;
    multicastAddr.sin_port   = htons(MCAST_PORT);
    if (inet_pton(AF_INET, MCAST_ADDR, &multicastAddr.sin_addr) <= 0) {
        std::cerr << "Invalid address/ Address not supported: " << MCAST_ADDR << std::endl;
        tp_close(sock);
        return 1;
    }

    std::cout << "Sender started. Sending to " << MCAST_ADDR << ":" << MCAST_PORT << std::endl;

    // 5. Send Message
    const char *msg = "Hello from C++ Loopback/Network Multicast!";
    size_t len      = strlen(msg);

    if (tp_sendto(sock, msg, len, 0, (struct sockaddr *)&multicastAddr, sizeof(multicastAddr)) == -1) {
        tp_perror("sendto");
    } else {
        std::cout << "Message sent: " << msg << std::endl;
    }

    // Cleanup
    tp_close(sock);
    return 0;
}
