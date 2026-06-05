/*
 * i2c_stub_test.c - Test I2C register read/write using the i2c-stub kernel module.
 *
 * Build:
 *   gcc -o i2c_stub_test i2c_stub_test.c
 *
 * Usage:
 *   ./i2c_stub_test <i2c-dev>  <slave-addr>  [reg] [value]
 *
 *   No reg/value  -> dump all 256 registers
 *   reg only      -> read single register
 *   reg + value   -> write then read-back to verify
 *
 * Setup (run as root):
 *   modprobe i2c-stub chip_addr=0x50
 *   # find the new adapter number:
 *   i2cdetect -l
 *   # use e.g. /dev/i2c-5  (the last adapter listed is usually the stub)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

/* ------------------------------------------------------------------ */
/* Low-level helpers                                                   */
/* ------------------------------------------------------------------ */

/** Open the I2C adapter and set the slave address. */
static int i2c_open(const char *dev, uint8_t addr)
{
    int fd = open(dev, O_RDWR);
    if (fd < 0) {
        perror("open i2c-dev");
        return -1;
    }
    if (ioctl(fd, I2C_SLAVE, addr) < 0) {
        perror("ioctl I2C_SLAVE");
        close(fd);
        return -1;
    }
    return fd;
}

/** Write one byte to a register. */
static int i2c_write_reg(int fd, uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    if (write(fd, buf, 2) != 2) {
        perror("i2c write");
        return -1;
    }
    return 0;
}

/** Read one byte from a register (write reg pointer, then read). */
static int i2c_read_reg(int fd, uint8_t reg, uint8_t *out)
{
    /* write the register address */
    if (write(fd, &reg, 1) != 1) {
        perror("i2c write reg pointer");
        return -1;
    }
    /* read the value */
    if (read(fd, out, 1) != 1) {
        perror("i2c read");
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* High-level operations                                               */
/* ------------------------------------------------------------------ */

/** Dump all 256 registers in a hex table. */
static void dump_registers(int fd)
{
    printf("\nRegister dump (addr  : +0  +1  +2  +3  +4  +5  +6  +7"
           "  +8  +9  +A  +B  +C  +D  +E  +F)\n");
    printf("--------------------------------------------------------------"
           "-----------------------------\n");

    for (int base = 0; base < 256; base += 16) {
        printf("  0x%02X : ", base);
        for (int col = 0; col < 16; col++) {
            uint8_t val = 0;
            if (i2c_read_reg(fd, (uint8_t)(base + col), &val) == 0)
                printf("%02X  ", val);
            else
                printf("??  ");
        }
        printf("\n");
    }
    printf("\n");
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage:\n"
        "  %s <dev> <addr>           -- dump all registers\n"
        "  %s <dev> <addr> <reg>     -- read register\n"
        "  %s <dev> <addr> <reg> <val> -- write then read-back\n"
        "\n"
        "  dev   : e.g. /dev/i2c-5\n"
        "  addr  : slave address in hex, e.g. 0x50\n"
        "  reg   : register offset in hex, e.g. 0x00\n"
        "  val   : byte value in hex, e.g. 0xAB\n",
        prog, prog, prog);
}

int main(int argc, char *argv[])
{
    if (argc < 3) { usage(argv[0]); return EXIT_FAILURE; }

    const char *dev   = argv[1];
    uint8_t     addr  = (uint8_t)strtoul(argv[2], NULL, 0);

    int fd = i2c_open(dev, addr);
    if (fd < 0) return EXIT_FAILURE;

    printf("I2C stub tester  |  adapter: %s  |  slave: 0x%02X\n", dev, addr);

    if (argc == 3) {
        /* ---- dump all registers ---- */
        dump_registers(fd);

    } else if (argc == 4) {
        /* ---- read single register ---- */
        uint8_t reg = (uint8_t)strtoul(argv[3], NULL, 0);
        uint8_t val = 0;
        if (i2c_read_reg(fd, reg, &val) == 0)
            printf("READ  reg 0x%02X -> 0x%02X (%u)\n", reg, val, val);
        else
            fprintf(stderr, "Read failed.\n");

    } else {
        /* ---- write + read-back ---- */
        uint8_t reg   = (uint8_t)strtoul(argv[3], NULL, 0);
        uint8_t wval  = (uint8_t)strtoul(argv[4], NULL, 0);

        printf("WRITE reg 0x%02X <- 0x%02X\n", reg, wval);
        if (i2c_write_reg(fd, reg, wval) != 0) {
            fprintf(stderr, "Write failed.\n");
            close(fd);
            return EXIT_FAILURE;
        }

        uint8_t rval = 0;
        if (i2c_read_reg(fd, reg, &rval) == 0) {
            printf("READ  reg 0x%02X -> 0x%02X\n", reg, rval);
            if (rval == wval)
                printf("OK  write/read-back matches.\n");
            else
                printf("MISMATCH  expected 0x%02X got 0x%02X\n", wval, rval);
        } else {
            fprintf(stderr, "Read-back failed.\n");
        }
    }

    close(fd);
    return EXIT_SUCCESS;
}
