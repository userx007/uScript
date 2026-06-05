/*
 * spi_loopback_test.c - SPI loopback test using /dev/spidevX.Y
 *
 * In loopback mode MOSI is physically connected to MISO, so every byte
 * transmitted is received back unchanged.  This tool verifies that round-trip.
 *
 * Build:
 *   gcc -o spi_loopback_test spi_loopback_test.c
 *
 * Usage:
 *   ./spi_loopback_test [device] [speed_hz] [mode]
 *
 *   device   : SPI device node (default /dev/spidev0.0)
 *   speed_hz : clock speed in Hz  (default 500000 = 500 kHz)
 *   mode     : SPI mode 0-3        (default 0)
 *
 * Hardware setup (no kernel module needed beyond spidev):
 *   Connect MOSI (pin varies by board) to MISO with a jumper wire.
 *   Raspberry Pi: MOSI=GPIO10 (pin19) <-> MISO=GPIO9 (pin21)
 *
 * Enable spidev (Raspberry Pi example):
 *   sudo raspi-config  -> Interface Options -> SPI -> Enable
 *   ls /dev/spidev*
 *
 * For a pure-software stub (no hardware at all), load spi-stub:
 *   sudo modprobe spi-stub
 *   # Note: spi-stub does NOT echo data; it only probes the bus.
 *   # Real loopback requires the MOSI->MISO wire.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

#define TRANSFER_LEN    16      /* bytes per test transfer */
#define BITS_PER_WORD   8

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void print_buf(const char *label, const uint8_t *buf, size_t len)
{
    printf("  %-6s: ", label);
    for (size_t i = 0; i < len; i++)
        printf("%02X ", buf[i]);
    printf("\n");
}

/* ------------------------------------------------------------------ */
/* SPI transfer                                                        */
/* ------------------------------------------------------------------ */

/**
 * Full-duplex SPI transfer: transmit tx_buf, receive into rx_buf.
 * Returns 0 on success, -1 on error.
 */
static int spi_transfer(int fd,
                        const uint8_t *tx_buf,
                        uint8_t       *rx_buf,
                        size_t         len,
                        uint32_t       speed_hz)
{
    struct spi_ioc_transfer tr = {
        .tx_buf        = (unsigned long)tx_buf,
        .rx_buf        = (unsigned long)rx_buf,
        .len           = (uint32_t)len,
        .speed_hz      = speed_hz,
        .delay_usecs   = 0,
        .bits_per_word = BITS_PER_WORD,
        .cs_change     = 0,
    };

    if (ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
        perror("SPI_IOC_MESSAGE");
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Test patterns                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *name;
    uint8_t     data[TRANSFER_LEN];
} TestPattern;

static const TestPattern patterns[] = {
    { "Incrementing", { 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
                        0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F } },
    { "All-0xFF",     { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
                        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF } },
    { "All-0x00",     { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
                        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 } },
    { "Alternating",  { 0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55,
                        0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55 } },
    { "DEADBEEF",     { 0xDE,0xAD,0xBE,0xEF,0xDE,0xAD,0xBE,0xEF,
                        0xDE,0xAD,0xBE,0xEF,0xDE,0xAD,0xBE,0xEF } },
};

#define N_PATTERNS  (sizeof(patterns) / sizeof(patterns[0]))

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    const char *device   = (argc > 1) ? argv[1] : "/dev/spidev0.0";
    uint32_t    speed_hz = (argc > 2) ? (uint32_t)atol(argv[2]) : 500000;
    uint8_t     mode     = (argc > 3) ? (uint8_t)atoi(argv[3])  : 0;

    /* ---- open device ---- */
    int fd = open(device, O_RDWR);
    if (fd < 0) { perror("open spidev"); return EXIT_FAILURE; }

    /* ---- configure ---- */
    if (ioctl(fd, SPI_IOC_WR_MODE,          &mode)         < 0 ||
        ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &(uint8_t){BITS_PER_WORD}) < 0 ||
        ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ,  &speed_hz)     < 0)
    {
        perror("SPI ioctl configure");
        close(fd);
        return EXIT_FAILURE;
    }

    /* read back to confirm */
    uint8_t  act_bits  = 0;
    uint32_t act_speed = 0;
    uint8_t  act_mode  = 0;
    ioctl(fd, SPI_IOC_RD_MODE,          &act_mode);
    ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &act_bits);
    ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ,  &act_speed);

    printf("SPI loopback tester\n");
    printf("  Device   : %s\n", device);
    printf("  Mode     : SPI%u\n", act_mode);
    printf("  Bits     : %u\n", act_bits);
    printf("  Speed    : %u Hz (%.3f kHz)\n\n", act_speed, act_speed / 1000.0);

    /* ---- run test patterns ---- */
    int pass = 0, fail = 0;

    for (size_t p = 0; p < N_PATTERNS; p++) {
        uint8_t rx[TRANSFER_LEN] = {0};

        printf("[%zu/%zu] Pattern: %s\n", p + 1, N_PATTERNS, patterns[p].name);
        print_buf("TX", patterns[p].data, TRANSFER_LEN);

        if (spi_transfer(fd, patterns[p].data, rx, TRANSFER_LEN, speed_hz) < 0) {
            printf("  TRANSFER ERROR\n\n");
            fail++;
            continue;
        }

        print_buf("RX", rx, TRANSFER_LEN);

        if (memcmp(patterns[p].data, rx, TRANSFER_LEN) == 0) {
            printf("  PASS  TX == RX\n\n");
            pass++;
        } else {
            printf("  FAIL  mismatch detected\n");
            /* highlight differing bytes */
            printf("  DIFF  : ");
            for (int i = 0; i < TRANSFER_LEN; i++)
                printf(patterns[p].data[i] != rx[i] ? "^^ " : "   ");
            printf("\n\n");
            fail++;
        }
    }

    /* ---- summary ---- */
    printf("==============================================\n");
    printf("Results: %d/%zu passed, %d failed\n",
           pass, N_PATTERNS, fail);

    if (fail == 0 && pass > 0)
        printf("All tests PASSED. MOSI->MISO loopback confirmed.\n");
    else if (pass == 0)
        printf("All tests FAILED. Check MOSI->MISO wire and spidev setup.\n");
    else
        printf("Partial failure. Check wiring or try a lower speed.\n");

    close(fd);
    return (fail == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
