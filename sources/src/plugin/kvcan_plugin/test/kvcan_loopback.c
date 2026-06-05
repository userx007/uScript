/*
 * vcan_mirror.c - Mirrors CAN frames received on a vcan interface back to the same interface.
 *
 * Build:
 *   gcc -o vcan_mirror vcan_mirror.c
 *
 * Usage:
 *   ./vcan_mirror [interface]   (default: vcan0)
 *
 * Setup:
 *   sudo modprobe vcan
 *   sudo ip link add dev vcan0 type vcan
 *   sudo ip link set up vcan0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <linux/can.h>
#include <linux/can/raw.h>

/* ------------------------------------------------------------------ */
/* Globals                                                             */
/* ------------------------------------------------------------------ */

static volatile int running = 1;

static void sig_handler(int sig)
{
    (void)sig;
    running = 0;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/** Print a CAN frame in candump-like format. */
static void print_frame(const char *prefix, const struct can_frame *f)
{
    printf("%s  %03X  [%u] ", prefix, f->can_id & CAN_EFF_MASK, f->can_dlc);
    for (int i = 0; i < f->can_dlc; i++)
        printf("%02X ", f->data[i]);
    printf("\n");
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    const char *ifname = (argc > 1) ? argv[1] : "vcan0";

    /* ---- open raw CAN socket ---- */
    int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    /* ---- resolve interface index ---- */
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        fprintf(stderr, "ioctl SIOCGIFINDEX for '%s': %s\n",
                ifname, strerror(errno));
        close(sock);
        return EXIT_FAILURE;
    }

    /* ---- bind to the interface ---- */
    struct sockaddr_can addr = {
        .can_family  = AF_CAN,
        .can_ifindex = ifr.ifr_ifindex,
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sock);
        return EXIT_FAILURE;
    }

    /*
     * On a real interface we would enable loopback so our echo does
     * not come back a second time.  On vcan it is on by default but
     * we disable receiving our own sent frames to avoid an echo storm.
     */
    int recv_own = 0;
    setsockopt(sock, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS,
               &recv_own, sizeof(recv_own));

    /* ---- signals ---- */
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    printf("vcan_mirror: listening on %s — press Ctrl-C to stop\n", ifname);
    printf("%-10s  %-6s  %-4s  %s\n", "DIR", "ID", "DLC", "DATA");
    printf("--------------------------------------------------\n");

    /* ---- main loop ---- */
    while (running) {
        struct can_frame frame;
        ssize_t nbytes = read(sock, &frame, sizeof(frame));

        if (nbytes < 0) {
            if (errno == EINTR)
                break;          /* interrupted by signal */
            perror("read");
            break;
        }

        if (nbytes < (ssize_t)sizeof(struct can_frame)) {
            fprintf(stderr, "short read (%zd bytes)\n", nbytes);
            continue;
        }

        /* ignore remote-transmission-request frames */
        if (frame.can_id & CAN_RTR_FLAG) {
            printf("RX (RTR, skipped)\n");
            continue;
        }

        print_frame("RX", &frame);

        /* echo the frame back */
        ssize_t sent = write(sock, &frame, sizeof(frame));
        if (sent < 0) {
            perror("write");
            break;
        }

        print_frame("TX", &frame);
        printf("\n");
    }

    printf("\nvcan_mirror: shutting down.\n");
    close(sock);
    return EXIT_SUCCESS;
}
