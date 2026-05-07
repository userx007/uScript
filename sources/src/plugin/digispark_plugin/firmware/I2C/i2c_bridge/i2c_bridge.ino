// ------------------------------------------
// Board: Digispark (Default 16.5MHz)
// Digispark ATtiny85 — USB→I2C Master Bridge
// Dependencies: DigiUSB, TinyWireM
// I2C: SDA=PB0 (pin 0), SCL=PB2 (pin 2)
// ------------------------------------------

// ------------------------------------------
// I2C Pins on Digispark:
// ------------------------------------------
// - SDA → Pin 0 (PB0)
// - SCL → Pin 2 (PB2)
// - Pins 3 & 4 are taken by USB (V-USB)
// ------------------------------------------


#include <DigiUSB.h>
#include <TinyWireM.h>

// ── Commands ────────────────────────────────────────────────
#define CMD_SCAN        0x01
#define CMD_WRITE       0x02
#define CMD_READ        0x03
#define CMD_WRITE_READ  0x04

// ── Status codes ────────────────────────────────────────────
#define STATUS_OK       0x00
#define STATUS_NACK     0x01
#define STATUS_ERR      0xFF

#define PKT_SIZE        8

static uint8_t rxBuf[PKT_SIZE];
static uint8_t txBuf[PKT_SIZE];

// ── Helpers ─────────────────────────────────────────────────

static void pkt_send(void) {
    for (uint8_t i = 0; i < PKT_SIZE; i++)
        DigiUSB.write(txBuf[i]);
    DigiUSB.refresh();
}

static bool pkt_recv(void) {
    uint32_t t = millis();
    while (DigiUSB.available() < PKT_SIZE) {
        DigiUSB.refresh();
        if (millis() - t > 200) return false;  // 200 ms timeout
    }
    for (uint8_t i = 0; i < PKT_SIZE; i++)
        rxBuf[i] = DigiUSB.read();
    return true;
}

// ── Command handlers ────────────────────────────────────────

// Scan: returns one packet per found device
// Response: [CMD_SCAN][addr] repeated, then [CMD_SCAN][0x00] as sentinel
static void cmd_scan(void) {
    for (uint8_t addr = 1; addr < 127; addr++) {
        TinyWireM.beginTransmission(addr);
        uint8_t err = TinyWireM.endTransmission();
        if (err == 0) {
            txBuf[0] = CMD_SCAN;
            txBuf[1] = addr;
            memset(&txBuf[2], 0, PKT_SIZE - 2);
            pkt_send();
        }
        DigiUSB.refresh();   // keep USB alive during slow scan
    }
    // Sentinel: addr=0 means "scan done"
    txBuf[0] = CMD_SCAN;
    txBuf[1] = 0x00;
    memset(&txBuf[2], 0, PKT_SIZE - 2);
    pkt_send();
}

// Write: [CMD_WRITE][addr][len][d0..d4]
// Response: [CMD_WRITE][STATUS]
static void cmd_write(void) {
    uint8_t addr = rxBuf[1];
    uint8_t len  = rxBuf[2] > 5 ? 5 : rxBuf[2];

    TinyWireM.beginTransmission(addr);
    for (uint8_t i = 0; i < len; i++)
        TinyWireM.write(rxBuf[3 + i]);
    uint8_t err = TinyWireM.endTransmission();

    memset(txBuf, 0, PKT_SIZE);
    txBuf[0] = CMD_WRITE;
    txBuf[1] = (err == 0) ? STATUS_OK : STATUS_NACK;
    pkt_send();
}

// Read: [CMD_READ][addr][len][0...]
// Response: [CMD_READ][n_received][d0..d5]
static void cmd_read(void) {
    uint8_t addr = rxBuf[1];
    uint8_t len  = rxBuf[2] > 6 ? 6 : rxBuf[2];

    uint8_t n = TinyWireM.requestFrom(addr, len);

    memset(txBuf, 0, PKT_SIZE);
    txBuf[0] = CMD_READ;
    txBuf[1] = n;
    for (uint8_t i = 0; i < n; i++)
        txBuf[2 + i] = TinyWireM.read();
    pkt_send();
}

// Write-Read: [CMD_WRITE_READ][addr][wlen][rlen][d0..d3]
// Issues a write followed by a repeated START then read.
// Response: [CMD_WRITE_READ][STATUS][n_read][d0..d4]
static void cmd_write_read(void) {
    uint8_t addr = rxBuf[1];
    uint8_t wlen = rxBuf[2] > 4 ? 4 : rxBuf[2];
    uint8_t rlen = rxBuf[3] > 5 ? 5 : rxBuf[3];

    TinyWireM.beginTransmission(addr);
    for (uint8_t i = 0; i < wlen; i++)
        TinyWireM.write(rxBuf[4 + i]);
    // sendStop=false → repeated START (TinyWireM ≥1.0 supports this)
    uint8_t err = TinyWireM.endTransmission(false);

    memset(txBuf, 0, PKT_SIZE);
    txBuf[0] = CMD_WRITE_READ;

    if (err != 0) {
        txBuf[1] = STATUS_NACK;
        pkt_send();
        return;
    }

    uint8_t n = TinyWireM.requestFrom(addr, rlen);
    txBuf[1] = STATUS_OK;
    txBuf[2] = n;
    for (uint8_t i = 0; i < n; i++)
        txBuf[3 + i] = TinyWireM.read();
    pkt_send();
}

// ── Main ────────────────────────────────────────────────────

void setup() {
    DigiUSB.begin();
    TinyWireM.begin();
}

void loop() {
    DigiUSB.refresh();
    if (pkt_recv()) {
        switch (rxBuf[0]) {
            case CMD_SCAN:       cmd_scan();       break;
            case CMD_WRITE:      cmd_write();      break;
            case CMD_READ:       cmd_read();       break;
            case CMD_WRITE_READ: cmd_write_read(); break;
            default:
                memset(txBuf, 0, PKT_SIZE);
                txBuf[0] = rxBuf[0];
                txBuf[1] = STATUS_ERR;
                pkt_send();
                break;
        }
    }
}