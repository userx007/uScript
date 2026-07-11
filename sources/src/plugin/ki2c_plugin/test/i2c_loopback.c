/*
 * i2c_loopback.c - Writes bytes to an I2C slave's registers and reads them
 *                   straight back, verifying round-trip integrity.
 *
 * Build:
 *   gcc -o i2c_loopback i2c_loopback.c
 *
 * Usage:
 *   ./i2c_loopback <bus-number> <i2c-address> [start-register]
 *
 *   bus-number     numeric suffix of /dev/i2c-<N>
 *   i2c-address    7-bit slave address, decimal or 0x.. (e.g. 0x50)
 *   start-register first register/offset written to (default: 0x00)
 *
 * Why this is NOT a byte-for-byte analog of vcan_mirror / uart_loopback:
 *
 *   CAN is a broadcast bus and UART is full-duplex point-to-point — in both
 *   cases a peer can push data at us any time, so a process can sit in a
 *   blocking read() and mirror whatever arrives. I2C is master-driven: a
 *   slave device is physically incapable of transmitting unless the master
 *   clocks out a read request first. There is no "listen for unsolicited
 *   bytes" mode to mirror. The only meaningful loopback on an I2C bus is
 *   therefore: WRITE data to a slave's registers, then READ it back and
 *   confirm it matches — which is what this tool does, once per line of
 *   input, using SMBus byte-data transactions (I2C_SMBUS_BYTE_DATA).
 *
 * Setup with i2c-stub (the kernel's virtual I2C device, no hardware needed
 * — the I2C equivalent of vcan/tty0tty):
 *
 *   sudo modprobe i2c-stub chip_addr=0x50
 *   dmesg | tail          # shows which /dev/i2c-N was created
 *
 *   i2c-stub emulates a chip with 256 bytes of register memory: whatever is
 *   written to a register is exactly what a subsequent read of that
 *   register returns, which is what makes it a genuine loopback target.
 *
 *   Run this tool against it:
 *     ./i2c_loopback 3 0x50            # adjust bus number to match dmesg
 *     55 aa 01 02 03                   # type a line of hex bytes, Enter
 *
 *   You can also pipe input:
 *     printf '01 02 03 04\ndead beef\n' | ./i2c_loopback 3 0x50
 *
 * Using it against real hardware:
 *
 *   Any simple register-addressed I2C device (many EEPROMs, sensors with
 *   scratch/config registers) will work the same way, as long as it
 *   implements plain SMBus byte-data reads/writes at consecutive register
 *   addresses. Devices needing 16-bit register addresses, page writes, or
 *   block transfers are out of scope here — extend smbus_write/read_byte
 *   below into raw I2C_RDWR combined transactions if you need that.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>

#include <fcntl.h>
#include <sys/ioctl.h>

#include <linux/i2c.h>
#include <linux/i2c-dev.h>

/* ------------------------------------------------------------------ */
/* Globals                                                             */
/* ------------------------------------------------------------------ */

static volatile int running = 1;

static void sig_handler(int sig)
{
    (void)sig;
    running = 0;
}

#define MAX_LINE_BYTES 64

/* ------------------------------------------------------------------ */
/* Minimal SMBus helpers (no libi2c-dev dependency, kernel headers only) */
/* ------------------------------------------------------------------ */

static __s32 i2c_smbus_access(int fd, char read_write, __u8 command,
                               int size, union i2c_smbus_data *data)
{
    struct i2c_smbus_ioctl_data args;
    args.read_write = read_write;
    args.command    = command;
    args.size       = size;
    args.data       = data;
    return ioctl(fd, I2C_SMBUS, &args);
}

static int smbus_write_byte(int fd, __u8 reg, __u8 value)
{
    union i2c_smbus_data data;
    data.byte = value;
    return i2c_smbus_access(fd, I2C_SMBUS_WRITE, reg,
                             I2C_SMBUS_BYTE_DATA, &data);
}

static int smbus_read_byte(int fd, __u8 reg, __u8 *value)
{
    union i2c_smbus_data data;
    int res = i2c_smbus_access(fd, I2C_SMBUS_READ, reg,
                                I2C_SMBUS_BYTE_DATA, &data);
    if (res < 0)
        return res;
    *value = data.byte & 0xFF;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void print_chunk(const char *prefix, const unsigned char *buf, int len)
{
    printf("%s  [%d] ", prefix, len);
    for (int i = 0; i < len; i++)
        printf("%02X ", buf[i]);
    printf("\n");
    fflush(stdout);
}

/** Parse whitespace-separated hex byte pairs from a line, e.g. "55 aa 01". */
static int parse_hex_line(char *line, unsigned char *out, int max_out)
{
    int count = 0;
    char *tok = strtok(line, " \t\r\n");
    while (tok && count < max_out) {
        char *endptr;
        long v = strtol(tok, &endptr, 16);
        if (*endptr != '\0' || v < 0 || v > 0xFF) {
            fprintf(stderr, "skipping invalid hex byte '%s'\n", tok);
        } else {
            out[count++] = (unsigned char)v;
        }
        tok = strtok(NULL, " \t\r\n");
    }
    return count;
}

static int open_i2c_bus(int bus_num, int address)
{
    char path[32];
    snprintf(path, sizeof(path), "/dev/i2c-%d", bus_num);

    int fd = open(path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "open '%s': %s\n", path, strerror(errno));
        return -1;
    }

    if (ioctl(fd, I2C_SLAVE, address) < 0) {
        fprintf(stderr, "ioctl I2C_SLAVE (addr 0x%02x): %s\n",
                address, strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <bus-number> <i2c-address> [start-register]\n",
                argv[0]);
        fprintf(stderr, "  e.g.: %s 3 0x50\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *endptr;
    long bus_num = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0' || bus_num < 0) {
        fprintf(stderr, "invalid bus number '%s'\n", argv[1]);
        return EXIT_FAILURE;
    }

    long address = strtol(argv[2], &endptr, 0);   /* base 0 => accepts 0x.. */
    if (*endptr != '\0' || address < 0 || address > 0x7F) {
        fprintf(stderr, "invalid 7-bit i2c address '%s'\n", argv[2]);
        return EXIT_FAILURE;
    }

    long start_reg = 0;
    if (argc > 3) {
        start_reg = strtol(argv[3], &endptr, 0);
        if (*endptr != '\0' || start_reg < 0 || start_reg > 0xFF) {
            fprintf(stderr, "invalid start register '%s'\n", argv[3]);
            return EXIT_FAILURE;
        }
    }

    int fd = open_i2c_bus((int)bus_num, (int)address);
    if (fd < 0)
        return EXIT_FAILURE;

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    int interactive = isatty(STDIN_FILENO);

    printf("i2c_loopback: bus /dev/i2c-%ld, addr 0x%02lx, start reg 0x%02lx"
           " - press Ctrl-C / Ctrl-D to stop\n", bus_num, address, start_reg);
    if (interactive)
        printf("Enter whitespace-separated hex bytes per line (e.g. \"55 aa 01\"):\n");
    printf("%-10s  %s\n", "DIR", "DATA (hex)");
    printf("--------------------------------------------------\n");

    char line[512];

    while (running) {
        if (interactive) {
            printf("> ");
            fflush(stdout);
        }

        if (!fgets(line, sizeof(line), stdin))
            break;   /* EOF (Ctrl-D) or interrupted */

        unsigned char tx[MAX_LINE_BYTES];
        int n = parse_hex_line(line, tx, MAX_LINE_BYTES);
        if (n == 0)
            continue;

        /* Write each byte to a consecutive register. */
        for (int i = 0; i < n; i++) {
            if (smbus_write_byte(fd, (__u8)(start_reg + i), tx[i]) < 0) {
                fprintf(stderr, "write to reg 0x%02lx failed: %s\n",
                        start_reg + i, strerror(errno));
                running = 0;
                break;
            }
        }
        if (!running)
            break;
        print_chunk("TX", tx, n);

        /* Read the same registers back. */
        unsigned char rx[MAX_LINE_BYTES];
        int ok = 1;
        for (int i = 0; i < n; i++) {
            if (smbus_read_byte(fd, (__u8)(start_reg + i), &rx[i]) < 0) {
                fprintf(stderr, "read from reg 0x%02lx failed: %s\n",
                        start_reg + i, strerror(errno));
                ok = 0;
                running = 0;
                break;
            }
        }
        if (!ok)
            break;
        print_chunk("RX", rx, n);

        if (memcmp(tx, rx, n) == 0) {
            printf("MATCH  (%d bytes verified)\n\n", n);
        } else {
            printf("MISMATCH!\n\n");
        }
    }

    printf("\ni2c_loopback: shutting down.\n");
    close(fd);
    return EXIT_SUCCESS;
}
