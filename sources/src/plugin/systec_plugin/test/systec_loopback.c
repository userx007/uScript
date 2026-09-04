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
 *
 * Design — single socket, own-message reception disabled:
 *
 *   One socket is used for both RX and TX.  CAN_RAW_LOOPBACK is left at its
 *   default (enabled), so frames we write are looped back by the kernel to
 *   every OTHER socket on the same host — including the application that sent
 *   the original frame and is waiting for the reply.
 *
 *   CAN_RAW_RECV_OWN_MSGS is set to 0, so this socket does NOT receive the
 *   frames it wrote itself.  That breaks the echo storm without hiding our
 *   reply from the application.
 *
 *   Why not two sockets?
 *     If a TX socket (sock_tx) writes a frame, the kernel delivers it to ALL
 *     other local sockets — including an RX socket (sock_rx) on the same
 *     interface.  There is no kernel knob to say "deliver to the app socket
 *     but not to sock_rx".  CAN_RAW_LOOPBACK = 0 on sock_tx would suppress
 *     delivery to every local socket, including the app — breaking the use
 *     case.  A single socket with CAN_RAW_RECV_OWN_MSGS = 0 is the only
 *     approach that satisfies both requirements.
 *
 *   Why not CAN_RAW_LOOPBACK = 0?
 *     That would prevent the app from ever receiving our echoed reply, since
 *     the kernel would not deliver sock_tx's frames to any local socket at all.
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

    /* ---- resolve interface index ---- */
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

    int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) { perror("socket"); return EXIT_FAILURE; }

    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        fprintf(stderr, "ioctl SIOCGIFINDEX for '%s': %s\n",
                ifname, strerror(errno));
        close(sock);
        return EXIT_FAILURE;
    }

    struct sockaddr_can addr = {
        .can_family  = AF_CAN,
        .can_ifindex = ifr.ifr_ifindex,
    };

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(sock); return EXIT_FAILURE;
    }

    /*
     * Do NOT receive frames we wrote ourselves.
     *
     * CAN_RAW_LOOPBACK remains ON (default): the kernel still delivers our
     * writes to every other socket on this host, so the app waiting for the
     * reply will receive it.
     *
     * CAN_RAW_RECV_OWN_MSGS = 0: this socket is excluded from that delivery
     * for its own transmissions, preventing the echo storm.
     */
    int recv_own = 0;
    if (setsockopt(sock, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS,
                   &recv_own, sizeof(recv_own)) < 0) {
        perror("setsockopt CAN_RAW_RECV_OWN_MSGS");
        close(sock);
        return EXIT_FAILURE;
    }

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

        /*
         * Echo the frame back.
         * CAN_RAW_RECV_OWN_MSGS = 0 ensures this write is NOT looped back
         * into our own receive queue, while CAN_RAW_LOOPBACK (ON by default)
         * ensures the originating app socket DOES receive it.
         */
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
