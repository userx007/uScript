#include <arpa/inet.h>
#include <iostream>
#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MCAST_PORT 11111
#define MCAST_ADDR "239.1.1.1" // Local Administration Multicast
#define TTL        1

int main()
{
    int sock;
    struct sockaddr_in multicastAddr;
    char message[1024];

    // 1. Create UDP Socket
    if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        perror("Socket creation failed");
        return 1;
    }

    // 2. Set Multicast TTL (Time To Live)
    // TTL=1 ensures packets don't leave the local subnet/loopback
    int ttl = TTL;
    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) == -1) {
        perror("setsockopt IP_MULTICAST_TTL");
        close(sock);
        return 1;
    }

    // 3. (Optional) Set Multicast Interface
    // For loopback, use INADDR_LOOPBACK (127.0.0.1)
    // For eth0, use INADDR_ANY (0.0.0.0)
    struct in_addr localInterface;
    inet_pton(AF_INET, "127.0.0.1", &localInterface); // Change to "0.0.0.0" for eth0
    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &localInterface, sizeof(localInterface)) == -1) {
        perror("setsockopt IP_MULTICAST_IF");
        close(sock);
        return 1;
    }

    // 4. Configure Multicast Address
    memset(&multicastAddr, 0, sizeof(multicastAddr));
    multicastAddr.sin_family = AF_INET;
    multicastAddr.sin_port   = htons(MCAST_PORT);
    if (inet_pton(AF_INET, MCAST_ADDR, &multicastAddr.sin_addr) <= 0) {
        std::cerr << "Invalid address/ Address not supported: " << MCAST_ADDR << std::endl;
        close(sock);
        return 1;
    }

    std::cout << "Sender started. Sending to " << MCAST_ADDR << ":" << MCAST_PORT << std::endl;

    // 5. Send Message
    const char *msg = "Hello from C++ Loopback/Network Multicast!";
    size_t len      = strlen(msg);

    if (sendto(sock, msg, len, 0, (struct sockaddr *)&multicastAddr, sizeof(multicastAddr)) == -1) {
        perror("sendto");
    } else {
        std::cout << "Message sent: " << msg << std::endl;
    }

    // Cleanup
    close(sock);
    return 0;
}
