/*
 * uart_loopback.c - Mirrors bytes received on a UART/serial port back out
 *                    the same port.
 *
 * Build:
 *   gcc -o uart_loopback uart_loopback.c
 *
 * Usage:
 *   ./uart_loopback <port> [baudrate]
 *
 *   port      e.g. /dev/ttyUSB0, /dev/tnt0 (tty0tty), /dev/ttyS0
 *   baudrate  one of the standard POSIX rates listed in BAUD_TABLE below
 *             (default: 115200)
 *
 * Setup with tty0tty (a pair of cross-linked virtual serial ports):
 *   sudo modprobe tty0tty
 *   dmesg | tail            # shows the created pair, e.g. /dev/tnt0 <-> /dev/tnt1
 *
 *   Run this tool on one end:
 *     ./uart_loopback /dev/tnt0 115200
 *
 *   Then send data into the other end (e.g. with a terminal program or
 *   `minicom -D /dev/tnt1 -b 115200`) and it will be echoed straight back
 *   to you, because tty0tty forwards everything written on one side of the
 *   pair to the other side.
 *
 * Design:
 *
 *   Unlike the vcan_mirror case, a serial port has no kernel-level loopback
 *   or "own message" concept — whatever you write() only ever goes out the
 *   wire (or, for tty0tty, across to the *other* end of the pair). There is
 *   no risk of the mirror re-receiving its own transmission, so a single
 *   full-duplex file descriptor opened O_RDWR is sufficient: read a chunk,
 *   print it, write the same chunk straight back out.
 *
 *   The port is set to raw mode (no line discipline processing, no
 *   canonical mode, no echo, no signal-generating control characters) so
 *   that arbitrary binary payloads pass through unmodified — the same goal
 *   CAN_RAW pursues for CAN frames.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include <fcntl.h>
#include <termios.h>

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
/* Baud rate lookup                                                    */
/* ------------------------------------------------------------------ */

struct baud_entry {
    long     value;
    speed_t  flag;
};

/* Standard POSIX baud rates. Extend if your libc/kernel defines more
 * (e.g. B460800, B921600 on Linux). */
static const struct baud_entry BAUD_TABLE[] = {
    {     50,     B50 }, {     75,     B75 }, {    110,    B110 },
    {    134,    B134 }, {    150,    B150 }, {    200,    B200 },
    {    300,    B300 }, {    600,    B600 }, {   1200,   B1200 },
    {   1800,   B1800 }, {   2400,   B2400 }, {   4800,   B4800 },
    {   9600,   B9600 }, {  19200,  B19200 }, {  38400,  B38400 },
    {  57600,  B57600 }, { 115200, B115200 }, { 230400, B230400 },
#ifdef B460800
    { 460800, B460800 },
#endif
#ifdef B921600
    { 921600, B921600 },
#endif
};

static int baud_to_flag(long baud, speed_t *out)
{
    for (size_t i = 0; i < sizeof(BAUD_TABLE) / sizeof(BAUD_TABLE[0]); i++) {
        if (BAUD_TABLE[i].value == baud) {
            *out = BAUD_TABLE[i].flag;
            return 0;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/** Print a chunk of bytes in candump-like hex format. */
static void print_chunk(const char *prefix, const unsigned char *buf, ssize_t len)
{
    printf("%s  [%zd] ", prefix, len);
    for (ssize_t i = 0; i < len; i++)
        printf("%02X ", buf[i]);
    printf("\n");
    fflush(stdout);
}

static int open_serial_port(const char *path, long baud)
{
    speed_t speed;
    if (baud_to_flag(baud, &speed) < 0) {
        fprintf(stderr,
                "unsupported baud rate %ld (edit BAUD_TABLE to add it,\n"
                "or use Linux-specific BOTHER/termios2 for arbitrary rates)\n",
                baud);
        return -1;
    }

    /* O_NDELAY/O_NONBLOCK at open time avoids blocking on DCD for modem
     * lines; we clear it again right after so read() blocks normally. */
    int fd = open(path, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) {
        fprintf(stderr, "open '%s': %s\n", path, strerror(errno));
        return -1;
    }

    if (fcntl(fd, F_SETFL, 0) < 0) {
        perror("fcntl F_SETFL");
        close(fd);
        return -1;
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(fd, &tty) < 0) {
        perror("tcgetattr");
        close(fd);
        return -1;
    }

    cfmakeraw(&tty);               /* no canonical mode, no echo, no signals */
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    tty.c_cflag |= (CLOCAL | CREAD);   /* ignore modem control lines, enable RX */
    tty.c_cflag &= ~PARENB;            /* 8N1 */
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~CRTSCTS;           /* no hardware flow control */

    tty.c_cc[VMIN]  = 1;   /* block until at least 1 byte is available */
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) < 0) {
        perror("tcsetattr");
        close(fd);
        return -1;
    }

    tcflush(fd, TCIOFLUSH);
    return fd;
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <port> [baudrate]\n", argv[0]);
        fprintf(stderr, "  e.g.: %s /dev/tnt0 115200\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *port = argv[1];
    long baud = 115200;
    if (argc > 2) {
        char *endptr;
        baud = strtol(argv[2], &endptr, 10);
        if (*endptr != '\0' || baud <= 0) {
            fprintf(stderr, "invalid baudrate '%s'\n", argv[2]);
            return EXIT_FAILURE;
        }
    }

    int fd = open_serial_port(port, baud);
    if (fd < 0)
        return EXIT_FAILURE;

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    printf("uart_loopback: listening on %s @ %ld baud — press Ctrl-C to stop\n",
           port, baud);
    printf("%-10s  %s\n", "DIR", "DATA (hex)");
    printf("--------------------------------------------------\n");

    unsigned char buf[256];

    while (running) {
        ssize_t nbytes = read(fd, buf, sizeof(buf));

        if (nbytes < 0) {
            if (errno == EINTR)
                break;          /* interrupted by signal */
            perror("read");
            break;
        }
        if (nbytes == 0)
            continue;           /* nothing available, try again */

        print_chunk("RX", buf, nbytes);

        /* Echo the exact bytes straight back out the same port. */
        ssize_t total_written = 0;
        while (total_written < nbytes) {
            ssize_t w = write(fd, buf + total_written, nbytes - total_written);
            if (w < 0) {
                if (errno == EINTR)
                    continue;
                perror("write");
                running = 0;
                break;
            }
            total_written += w;
        }
        if (!running)
            break;

        print_chunk("TX", buf, nbytes);
        printf("\n");
    }

    printf("\nuart_loopback: shutting down.\n");
    close(fd);
    return EXIT_SUCCESS;
}
