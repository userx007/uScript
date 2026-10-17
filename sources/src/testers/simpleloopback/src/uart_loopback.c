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
 *             Windows: COM3, COM12, ... (names >= COM10 are mapped to \\.\COMn
 *             automatically); a null-modem pair such as com0com works like tty0tty
 *   baudrate  POSIX: one of the standard rates listed in BAUD_TABLE below
 *             Windows: any positive rate the driver accepts
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
#include <signal.h>
#include <errno.h>

#if defined(_WIN32)
#include <stddef.h>
#include <windows.h>
#if defined(_MSC_VER)
typedef ptrdiff_t ssize_t; /* MSVC has no ssize_t */
#endif
#else
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#endif

/* ------------------------------------------------------------------ */
/* Globals                                                             */
/* ------------------------------------------------------------------ */

static volatile int running = 1;

/** Signal handler: stops the main loop. */
static void sig_handler(int iSig)
{
    (void)iSig;
    running = 0;
}

/* ------------------------------------------------------------------ */
/* Baud rate lookup                                                    */
/* ------------------------------------------------------------------ */

#if !defined(_WIN32)

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

/** Maps a baud rate to the termios speed constant; returns -1 if unsupported. */
static int baud_to_flag(long baud, speed_t *pOut)
{
    for (size_t i = 0; i < sizeof(BAUD_TABLE) / sizeof(BAUD_TABLE[0]); i++) {
        if (BAUD_TABLE[i].value == baud) {
            *pOut = BAUD_TABLE[i].flag;
            return 0;
        }
    }
    return -1;
}

#endif /* !_WIN32 */

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Thin port abstraction: file descriptor + read/write/close on POSIX,
 * HANDLE + ReadFile/WriteFile/CloseHandle on Windows. */
#if defined(_WIN32)
typedef HANDLE port_t;
#define PORT_INVALID INVALID_HANDLE_VALUE

/** perror() replacement for Win32 errors. */
static void report_error(const char *pstrWhat)
{
    char  msg[256] = "";
    DWORD err      = GetLastError();
    DWORD len      = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0,
                                    msg, (DWORD)sizeof(msg), NULL);
    while (len > 0 && (msg[len - 1] == '\r' || msg[len - 1] == '\n' || msg[len - 1] == ' '))
        msg[--len] = '\0';
    fprintf(stderr, "%s: %s (error %lu)\n", pstrWhat, msg, (unsigned long)err);
}

/** Reads from the serial port. */
static ssize_t port_read(port_t h, void *buf, size_t len)
{
    DWORD got = 0;
    if (!ReadFile(h, buf, (DWORD)len, &got, NULL))
        return -1;
    return (ssize_t)got;
}

/** Writes to the serial port. */
static ssize_t port_write(port_t h, const void *buf, size_t len)
{
    DWORD put = 0;
    if (!WriteFile(h, buf, (DWORD)len, &put, NULL))
        return -1;
    return (ssize_t)put;
}

/** Closes the serial port. */
static void port_close(port_t h)
{
    CloseHandle(h);
}
#else
typedef int port_t;
#define PORT_INVALID (-1)

static void report_error(const char *pstrWhat)
{
    perror(pstrWhat);
}

/** Reads from the serial port. */
static ssize_t port_read(port_t h, void *buf, size_t len)
{
    return read(h, buf, len);
}

/** Writes to the serial port. */
static ssize_t port_write(port_t h, const void *buf, size_t len)
{
    return write(h, buf, len);
}

/** Closes the serial port. */
static void port_close(port_t h)
{
    close(h);
}
#endif

/** Print a chunk of bytes in candump-like hex format. */
static void print_chunk(const char *pstrPrefix, const unsigned char *buf, ssize_t len)
{
    printf("%s  [%zd] ", pstrPrefix, len);
    for (ssize_t i = 0; i < len; i++)
        printf("%02X ", buf[i]);
    printf("\n");
    fflush(stdout);
}

#if defined(_WIN32)

/** Opens and configures the serial port at @p baud. */
static port_t open_serial_port(const char *pstrPath, long baud)
{
    char full[300];
    /* COM10 and above (and, harmlessly, COM1-9) need the \\.\ device-namespace prefix. */
    if (strncmp(pstrPath, "\\\\.\\", 4) != 0 && (pstrPath[0] == 'C' || pstrPath[0] == 'c')) {
        snprintf(full, sizeof(full), "\\\\.\\%s", pstrPath);
    } else {
        snprintf(full, sizeof(full), "%s", pstrPath);
    }

    HANDLE h = CreateFileA(full, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        report_error(full);
        return PORT_INVALID;
    }

    DCB dcb;
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) {
        report_error("GetCommState");
        CloseHandle(h);
        return PORT_INVALID;
    }

    dcb.BaudRate        = (DWORD)baud;
    dcb.ByteSize        = 8;            /* 8N1 */
    dcb.Parity          = NOPARITY;
    dcb.StopBits        = ONESTOPBIT;
    dcb.fBinary         = TRUE;         /* raw: no EOF character handling */
    dcb.fParity         = FALSE;
    dcb.fOutxCtsFlow    = FALSE;        /* no hardware flow control */
    dcb.fOutxDsrFlow    = FALSE;
    dcb.fDtrControl     = DTR_CONTROL_ENABLE;
    dcb.fDsrSensitivity = FALSE;        /* ignore modem control lines (CLOCAL) */
    dcb.fOutX           = FALSE;        /* no XON/XOFF: payload must pass unmodified */
    dcb.fInX            = FALSE;
    dcb.fErrorChar      = FALSE;
    dcb.fNull           = FALSE;        /* do not strip NUL bytes */
    dcb.fRtsControl     = RTS_CONTROL_ENABLE;
    dcb.fAbortOnError   = FALSE;
    if (!SetCommState(h, &dcb)) {
        report_error("SetCommState");
        CloseHandle(h);
        return PORT_INVALID;
    }

    /* Return as soon as >= 1 byte is available, else give up after 100 ms with 0 bytes, so the
     * main loop's "nbytes == 0 -> try again" re-checks 'running' and Ctrl-C is honoured
     * (a blocking ReadFile would not be interrupted by it). */
    COMMTIMEOUTS to;
    to.ReadIntervalTimeout         = MAXDWORD;
    to.ReadTotalTimeoutMultiplier  = MAXDWORD;
    to.ReadTotalTimeoutConstant    = 100;
    to.WriteTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant   = 5000;
    if (!SetCommTimeouts(h, &to)) {
        report_error("SetCommTimeouts");
        CloseHandle(h);
        return PORT_INVALID;
    }

    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_TXABORT);
    return h;
}

#else /* POSIX */

/** Opens and configures the serial port at @p baud. */
static port_t open_serial_port(const char *pstrPath, long baud)
{
    speed_t speed;
    if (baud_to_flag(baud, &speed) < 0) {
        fprintf(stderr,
                "unsupported baud rate %ld (edit BAUD_TABLE to add it,\n"
                "or use Linux-specific BOTHER/termios2 for arbitrary rates)\n",
                baud);
        return PORT_INVALID;
    }

    /* O_NDELAY/O_NONBLOCK at open time avoids blocking on DCD for modem
     * lines; we clear it again right after so read() blocks normally. */
    int fd = open(pstrPath, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) {
        fprintf(stderr, "open '%s': %s\n", pstrPath, strerror(errno));
        return PORT_INVALID;
    }

    if (fcntl(fd, F_SETFL, 0) < 0) {
        perror("fcntl F_SETFL");
        close(fd);
        return PORT_INVALID;
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(fd, &tty) < 0) {
        perror("tcgetattr");
        close(fd);
        return PORT_INVALID;
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
        return PORT_INVALID;
    }

    tcflush(fd, TCIOFLUSH);
    return fd;
}

#endif /* _WIN32 */

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(int iArgc, char *argv[])
{
    if (iArgc < 2) {
        fprintf(stderr, "usage: %s <port> [baudrate]\n", argv[0]);
        fprintf(stderr, "  e.g.: %s /dev/tnt0 115200\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *port = argv[1];
    long baud = 115200;
    if (iArgc > 2) {
        char *endptr;
        baud = strtol(argv[2], &endptr, 10);
        if (*endptr != '\0' || baud <= 0) {
            fprintf(stderr, "invalid baudrate '%s'\n", argv[2]);
            return EXIT_FAILURE;
        }
    }

    port_t fd = open_serial_port(port, baud);
    if (fd == PORT_INVALID)
        return EXIT_FAILURE;

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    printf("uart_loopback: listening on %s @ %ld baud — press Ctrl-C to stop\n",
           port, baud);
    printf("%-10s  %s\n", "DIR", "DATA (hex)");
    printf("--------------------------------------------------\n");

    unsigned char buf[256];

    while (running) {
        ssize_t nbytes = port_read(fd, buf, sizeof(buf));

        if (nbytes < 0) {
#if !defined(_WIN32)
            if (errno == EINTR)
                break;          /* interrupted by signal */
#endif
            report_error("read");
            break;
        }
        if (nbytes == 0)
            continue;           /* nothing available, try again */

        print_chunk("RX", buf, nbytes);

        /* Echo the exact bytes straight back out the same port. */
        ssize_t total_written = 0;
        while (total_written < nbytes) {
            ssize_t w = port_write(fd, buf + total_written, (size_t)(nbytes - total_written));
            if (w < 0) {
#if !defined(_WIN32)
                if (errno == EINTR)
                    continue;
#endif
                report_error("write");
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
    port_close(fd);
    return EXIT_SUCCESS;
}
