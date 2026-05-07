// Digispark ATtiny85 — USB→SPI Master Bridge
// ─────────────────────────────────────────────────────────────────────────────
// Core    : TinyCore (MCUdude) — replaces Digistump + TinySPI
// Library : SPI.h (built into TinyCore), DigiUSB (standalone, see notes)
//
// ── Pin mapping (TinyCore USI — DIFFERENT from TinySPI!) ─────────────────────
//   MOSI → USI DO  → PB1 (pin 1)   ← was PB0 on TinySPI
//   MISO → USI DI  → PB0 (pin 0)   ← was PB1 on TinySPI
//   SCK  → USI SCK → PB2 (pin 2)   unchanged
//   CS   → hardwired GND (no free GPIO — PB3/PB4 used by V-USB)
//
// ── Clock frequencies @ 16.5 MHz internal PLL ────────────────────────────────
//   DIV_8MHz  (div 0) → SPISettings(8000000) → TinyCore DIV2  ≈ 8.25 MHz
//   DIV_4MHz  (div 1) → SPISettings(4000000) → TinyCore DIV4  ≈ 4.1  MHz (default)
//   DIV_2MHz  (div 2) → SPISettings(2000000) → TinyCore DIV8  ≈ 2.0  MHz
//   DIV_1MHz  (div 3) → SPISettings(1000000) → TinyCore ≥DIV14 ≈ 1.1 MHz (approx.)
//
// ── TinyCore board install URL ────────────────────────────────────────────────
//   https://mcudude.github.io/TinyCore/package_MCUdude_TinyCore_index.json
//   Board: ATtiny25/45/85 — Clock: 16 MHz (PLL)
//
// ── DigiUSB with TinyCore ─────────────────────────────────────────────────────
//   Since TinyCore replaces the Digistump board package, DigiUSB is no longer
//   bundled. Copy the DigisparkUSB folder from the Digistump package into your
//   Arduino libraries directory and rename it DigiUSB:
//     ~/.arduino15/packages/digistump/hardware/avr/<ver>/libraries/DigisparkUSB/
//     → ~/Arduino/libraries/DigiUSB/
//   TinyCore's pin-mapping is compatible with V-USB on PB3/PB4.
// ─────────────────────────────────────────────────────────────────────────────

// ------------------------------------------
// SPI Pins on Digispark:
// ------------------------------------------
// - MOSI → Pin 0 (PB0)
// - MISO → Pin 1 (PB1)
// - SCK  → Pin 2 (PB2)
// ------------------------------------------

#include <DigiUSB.h>
#include <SPI.h>              // TinyCore built-in USI SPI — replaces TinySPI.h
#include <avr/interrupt.h>    // for cli()/sei() used in ATOMIC transfers

// ── Commands (must match host SPIBridge constants) ────────────────────────────
#define CMD_SPI_TRANSFER  0x10
#define CMD_SPI_WRITE     0x11
#define CMD_SPI_READ      0x12
#define CMD_SPI_CONFIG    0x13

// ── Status ────────────────────────────────────────────────────────────────────
#define STATUS_OK         0x00
#define STATUS_ERR        0xFF

#define PKT_SIZE          8

// ── CS pin ───────────────────────────────────────────────────────────────────
// No free GPIO for CS — tie CS to GND for single-slave use.
// If you sacrifice MISO (PB0) for CS on a write-only slave, define CS_PIN 0.
#define CS_PIN            255   // 255 = hardwired, no GPIO toggle

// ── SPI state (mode + clock) ──────────────────────────────────────────────────
// Stored globally so CMD_SPI_CONFIG can update them without re-opening SPI.
static uint8_t  s_u8SpiMode = SPI_MODE0;   // CPOL=0 CPHA=0

// Clock lookup: divider index 0-3 → Hz value for SPISettings
// TinyCore maps Hz to its software DIV2/4/8/≥14 routines automatically.
static const uint32_t k_au32ClockHz[4] = {
    8000000UL,   // 0 → DIV2  ≈ 8.25 MHz
    4000000UL,   // 1 → DIV4  ≈ 4.1  MHz  (default)
    2000000UL,   // 2 → DIV8  ≈ 2.0  MHz
    1000000UL,   // 3 → ≥DIV14 ≈ 1.1 MHz  (approximate — see TinyCore docs)
};
static uint8_t s_u8ClkIdx = 1;   // default: 4 MHz

static uint8_t rxBuf[PKT_SIZE];
static uint8_t txBuf[PKT_SIZE];

// ── Helpers ───────────────────────────────────────────────────────────────────

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

// ── Transfer one byte with interrupt guard ────────────────────────────────────
// TinyCore's USI SPI does NOT disable interrupts during a transfer.
// A PCINT or USB interrupt mid-byte can stretch the clock by one bit period.
// Wrapping each byte in cli/sei keeps the clock clean for timing-sensitive
// devices.  Remove the guards if your slave is tolerant of stretched SCK.
static uint8_t spi_transfer_safe(uint8_t u8Byte) {
    uint8_t u8Ret;
    cli();
    u8Ret = SPI.transfer(u8Byte);
    sei();
    return u8Ret;
}

// ── Command handlers ──────────────────────────────────────────────────────────

// CONFIG: [CMD_SPI_CONFIG][mode 0-3][divider 0-3][0 0 0 0 0]
// Response: [CMD_SPI_CONFIG][STATUS_OK]
static void cmd_config() {
    uint8_t u8Mode = rxBuf[1] & 0x03;
    uint8_t u8Div  = rxBuf[2];
    if (u8Div > 3) u8Div = 1;   // clamp to valid range

    // Map mode index to Arduino SPI mode constant
    const uint8_t k_au8Modes[4] = {
        SPI_MODE0, SPI_MODE1, SPI_MODE2, SPI_MODE3
    };
    s_u8SpiMode = k_au8Modes[u8Mode];
    s_u8ClkIdx  = u8Div;

    // Re-initialise SPI with the new settings.
    // beginTransaction/endTransaction handle the register writes.
    SPISettings newSettings(k_au32ClockHz[s_u8ClkIdx], MSBFIRST, s_u8SpiMode);
    SPI.beginTransaction(newSettings);
    SPI.endTransaction();

    memset(txBuf, 0, PKT_SIZE);
    txBuf[0] = CMD_SPI_CONFIG;
    txBuf[1] = STATUS_OK;
    pkt_send();
}

// TRANSFER (full-duplex): [CMD_SPI_TRANSFER][len][d0..d5]
// Response:               [CMD_SPI_TRANSFER][len][d0..d5]
static void cmd_transfer() {
    uint8_t len = rxBuf[1];
    if (len > 6) len = 6;

    memset(txBuf, 0, PKT_SIZE);
    txBuf[0] = CMD_SPI_TRANSFER;
    txBuf[1] = len;

    SPISettings settings(k_au32ClockHz[s_u8ClkIdx], MSBFIRST, s_u8SpiMode);

    cs_assert();
    SPI.beginTransaction(settings);
    for (uint8_t i = 0; i < len; i++)
        txBuf[2 + i] = spi_transfer_safe(rxBuf[2 + i]);
    SPI.endTransaction();
    cs_deassert();

    pkt_send();
}

// WRITE: [CMD_SPI_WRITE][len][d0..d5]
// Response: [CMD_SPI_WRITE][STATUS_OK]
static void cmd_write() {
    uint8_t len = rxBuf[1];
    if (len > 6) len = 6;

    SPISettings settings(k_au32ClockHz[s_u8ClkIdx], MSBFIRST, s_u8SpiMode);

    cs_assert();
    SPI.beginTransaction(settings);
    for (uint8_t i = 0; i < len; i++)
        spi_transfer_safe(rxBuf[2 + i]);
    SPI.endTransaction();
    cs_deassert();

    memset(txBuf, 0, PKT_SIZE);
    txBuf[0] = CMD_SPI_WRITE;
    txBuf[1] = STATUS_OK;
    pkt_send();
}

// READ: [CMD_SPI_READ][len][0 0 0 0 0 0]
// Sends 0x00 on MOSI, captures MISO
// Response: [CMD_SPI_READ][len][d0..d5]
static void cmd_read() {
    uint8_t len = rxBuf[1];
    if (len > 6) len = 6;

    memset(txBuf, 0, PKT_SIZE);
    txBuf[0] = CMD_SPI_READ;
    txBuf[1] = len;

    SPISettings settings(k_au32ClockHz[s_u8ClkIdx], MSBFIRST, s_u8SpiMode);

    cs_assert();
    SPI.beginTransaction(settings);
    for (uint8_t i = 0; i < len; i++)
        txBuf[2 + i] = spi_transfer_safe(0x00);
    SPI.endTransaction();
    cs_deassert();

    pkt_send();
}

// ── Main ──────────────────────────────────────────────────────────────────────

void setup() {
    if (CS_PIN != 255) {
        pinMode(CS_PIN, OUTPUT);
        digitalWrite(CS_PIN, HIGH);
    }

    // SPI.begin() sets:  MOSI=PB1(pin1)  MISO=PB0(pin0)  SCK=PB2(pin2)
    // ⚠ This is SWAPPED vs TinySPI (which had MOSI=PB0, MISO=PB1).
    // Update your wiring accordingly.
    SPI.begin();

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