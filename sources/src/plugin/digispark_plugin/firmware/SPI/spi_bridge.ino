// Digispark ATtiny85 — USB→SPI Master Bridge
// MOSI=PB0(pin0)  MISO=PB1(pin1)  SCK=PB2(pin2)  CS=hardwired or PB3(pin3)
// NOTE: PB3 is USB D-, use CS pin only between USB transactions carefully.

#include <DigiUSB.h>
#include <TinySPI.h>

// ── Commands ─────────────────────────────────────────────────
#define CMD_SPI_TRANSFER  0x10   // full-duplex: write N bytes, read N bytes
#define CMD_SPI_WRITE     0x11   // write only
#define CMD_SPI_READ      0x12   // read only (sends 0x00 as MOSI)
#define CMD_SPI_CONFIG    0x13   // set mode and speed divider

// ── Status ───────────────────────────────────────────────────
#define STATUS_OK         0x00
#define STATUS_ERR        0xFF

#define PKT_SIZE          8

// CS pin — use PB1 if MISO not needed (write-only devices)
// For full-duplex single slave: hardwire CS to GND, define as 255 (unused)
#define CS_PIN            255    // 255 = hardwired, no GPIO toggling

static uint8_t rxBuf[PKT_SIZE];
static uint8_t txBuf[PKT_SIZE];

// ── Helpers ──────────────────────────────────────────────────

static void cs_assert()   { if (CS_PIN != 255) digitalWrite(CS_PIN, LOW);  }
static void cs_deassert() { if (CS_PIN != 255) digitalWrite(CS_PIN, HIGH); }

static void pkt_send() {
    for (uint8_t i = 0; i < PKT_SIZE; i++)
        DigiUSB.write(txBuf[i]);
    DigiUSB.refresh();
}

static bool pkt_recv() {
    uint32_t t = millis();
    while (DigiUSB.available() < PKT_SIZE) {
        DigiUSB.refresh();
        if (millis() - t > 200) return false;
    }
    for (uint8_t i = 0; i < PKT_SIZE; i++)
        rxBuf[i] = DigiUSB.read();
    return true;
}

// ── Command handlers ─────────────────────────────────────────

// CONFIG: [CMD_SPI_CONFIG][mode 0-3][divider][0...]
// divider: 0=SPI_CLOCK_DIV2, 1=DIV4, 2=DIV8, 3=DIV16
static void cmd_config() {
    uint8_t mode = rxBuf[1] & 0x03;
    uint8_t div  = rxBuf[2];

    SPI.setDataMode(mode);   // SPI_MODE0..3

    const uint8_t dividers[] = {
        SPI_CLOCK_DIV2, SPI_CLOCK_DIV4,
        SPI_CLOCK_DIV8, SPI_CLOCK_DIV16
    };
    SPI.setClockDivider(dividers[div < 4 ? div : 1]);

    memset(txBuf, 0, PKT_SIZE);
    txBuf[0] = CMD_SPI_CONFIG;
    txBuf[1] = STATUS_OK;
    pkt_send();
}

// TRANSFER (full-duplex): [CMD_SPI_TRANSFER][len][d0..d5]
// Response:               [CMD_SPI_TRANSFER][len][d0..d5]
static void cmd_transfer() {
    uint8_t len = rxBuf[1] > 6 ? 6 : rxBuf[1];

    memset(txBuf, 0, PKT_SIZE);
    txBuf[0] = CMD_SPI_TRANSFER;
    txBuf[1] = len;

    cs_assert();
    for (uint8_t i = 0; i < len; i++)
        txBuf[2 + i] = SPI.transfer(rxBuf[2 + i]);
    cs_deassert();

    pkt_send();
}

// WRITE: [CMD_SPI_WRITE][len][d0..d5]
// Response: [CMD_SPI_WRITE][STATUS]
static void cmd_write() {
    uint8_t len = rxBuf[1] > 6 ? 6 : rxBuf[1];

    cs_assert();
    for (uint8_t i = 0; i < len; i++)
        SPI.transfer(rxBuf[2 + i]);
    cs_deassert();

    memset(txBuf, 0, PKT_SIZE);
    txBuf[0] = CMD_SPI_WRITE;
    txBuf[1] = STATUS_OK;
    pkt_send();
}

// READ: [CMD_SPI_READ][len][0...]
// Sends 0x00 bytes as MOSI, captures MISO
// Response: [CMD_SPI_READ][len][d0..d5]
static void cmd_read() {
    uint8_t len = rxBuf[1] > 6 ? 6 : rxBuf[1];

    memset(txBuf, 0, PKT_SIZE);
    txBuf[0] = CMD_SPI_READ;
    txBuf[1] = len;

    cs_assert();
    for (uint8_t i = 0; i < len; i++)
        txBuf[2 + i] = SPI.transfer(0x00);
    cs_deassert();

    pkt_send();
}

// ── Main ─────────────────────────────────────────────────────

void setup() {
    if (CS_PIN != 255) {
        pinMode(CS_PIN, OUTPUT);
        digitalWrite(CS_PIN, HIGH);
    }
    SPI.begin();
    SPI.setDataMode(SPI_MODE0);
    SPI.setClockDivider(SPI_CLOCK_DIV4);
    DigiUSB.begin();
}

void loop() {
    DigiUSB.refresh();
    if (pkt_recv()) {
        switch (rxBuf[0]) {
            case CMD_SPI_CONFIG:   cmd_config();   break;
            case CMD_SPI_WRITE:    cmd_write();    break;
            case CMD_SPI_READ:     cmd_read();     break;
            case CMD_SPI_TRANSFER: cmd_transfer(); break;
            default:
                memset(txBuf, 0, PKT_SIZE);
                txBuf[0] = rxBuf[0];
                txBuf[1] = STATUS_ERR;
                pkt_send();
                break;
        }
    }
}