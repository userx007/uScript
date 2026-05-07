## Digispark ATtiny85 — USB→I2C Bridge

Great use case. Here's the full architecture:

```
Host App  ──USB HID──►  ATtiny85 Firmware  ──I2C──►  Slave Devices
(Python/C API)           (DigiUSB + TinyWireM)         (sensors, EEPROMs…)
```

**I2C Pins on Digispark:**
- `SDA` → Pin 0 (PB0)
- `SCL` → Pin 2 (PB2)
- Pins 3 & 4 are taken by USB (V-USB)

---

## Command Protocol (8-byte HID packets)

```
Host → Device:
 [CMD] [ADDR] [WLEN] [RLEN] [D0] [D1] [D2] [D3]

Device → Host:
 [CMD] [STATUS/LEN] [D0] [D1] [D2] [D3] [D4] [D5]
```

| CMD  | Value | Description              |
|------|-------|--------------------------|
| SCAN | 0x01  | Probe all 7-bit addresses |
| WRITE | 0x02 | Write bytes to slave     |
| READ  | 0x03 | Read bytes from slave    |
| WRITE_READ | 0x04 | Write then repeated-START read |

---

## Firmware — `i2c_bridge.ino`

```cpp
// Digispark ATtiny85 — USB→I2C Master Bridge
// Dependencies: DigiUSB, TinyWireM
// I2C: SDA=PB0 (pin 0), SCL=PB2 (pin 2)

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
```

---

## Host Library — `i2c_master.py`

```python
"""
i2c_master.py — Host-side API for the Digispark USB→I2C bridge.

Requires:  pip install hid
Usage:
    with I2CMaster() as bus:
        devices = bus.scan()
        bus.write(0x3C, [0x00, 0xAF])
        data = bus.read(0x3C, 6)
        reg  = bus.write_read(0x68, [0x3B], 6)   # MPU-6050 accel regs
"""

import hid
import time
from typing import Optional

# ── Digispark DigiUSB VID/PID ──────────────────────────────────────────────
DIGISPARK_VID = 0x16C0
DIGISPARK_PID = 0x05DF   # DigiUSB / V-USB raw HID

# ── Protocol constants (must match firmware) ───────────────────────────────
CMD_SCAN        = 0x01
CMD_WRITE       = 0x02
CMD_READ        = 0x03
CMD_WRITE_READ  = 0x04

STATUS_OK   = 0x00
STATUS_NACK = 0x01
STATUS_ERR  = 0xFF

PKT_SIZE    = 8          # bytes per HID report (payload, no report-ID)
TIMEOUT_MS  = 2000       # default read timeout
SCAN_TIMEOUT_MS = 5000   # scan touches 127 addresses → needs longer


class I2CError(Exception):
    pass


class I2CMaster:
    """
    USB→I2C bridge backed by a Digispark ATtiny85.

    Context-manager aware:
        with I2CMaster() as bus: ...

    Or manual lifecycle:
        bus = I2CMaster()
        bus.open()
        ...
        bus.close()
    """

    def __init__(self, vid: int = DIGISPARK_VID, pid: int = DIGISPARK_PID):
        self._vid = vid
        self._pid = pid
        self._dev: Optional[hid.device] = None

    # ── Lifecycle ──────────────────────────────────────────────────────────

    def open(self) -> None:
        self._dev = hid.device()
        self._dev.open(self._vid, self._pid)
        self._dev.set_nonblocking(False)

    def close(self) -> None:
        if self._dev:
            self._dev.close()
            self._dev = None

    def __enter__(self) -> "I2CMaster":
        self.open()
        return self

    def __exit__(self, *_) -> None:
        self.close()

    # ── Low-level transport ────────────────────────────────────────────────

    def _send(self, payload: list[int]) -> None:
        """Send an 8-byte HID report.  hidapi needs a leading 0x00 report-ID."""
        assert len(payload) <= PKT_SIZE
        packet = [0x00] + payload + [0x00] * (PKT_SIZE - len(payload))
        self._dev.write(packet)

    def _recv(self, timeout_ms: int = TIMEOUT_MS) -> bytes:
        """Receive an 8-byte HID report with timeout."""
        data = self._dev.read(PKT_SIZE, timeout_ms)
        if not data:
            raise I2CError("Read timeout — no response from device")
        return bytes(data[:PKT_SIZE])

    # ── Public API ─────────────────────────────────────────────────────────

    def scan(self) -> list[int]:
        """
        Probe all 7-bit I2C addresses (1–126).

        Returns a list of addresses that ACKed.
        Typical duration: 1–2 s (USB round-trips per address).
        """
        self._send([CMD_SCAN])
        found: list[int] = []
        while True:
            pkt = self._recv(SCAN_TIMEOUT_MS)
            if pkt[0] != CMD_SCAN:
                raise I2CError(f"Unexpected response to SCAN: 0x{pkt[0]:02X}")
            addr = pkt[1]
            if addr == 0x00:        # sentinel → scan complete
                break
            found.append(addr)
        return found

    def write(self, addr: int, data: bytes | list[int]) -> None:
        """
        Write bytes to an I2C slave.

        Args:
            addr:  7-bit slave address
            data:  up to 5 bytes

        Raises I2CError on NACK or error.
        """
        data = list(data)
        if len(data) > 5:
            raise ValueError("Maximum 5 bytes per write packet")
        self._send([CMD_WRITE, addr, len(data)] + data)
        pkt = self._recv()
        if pkt[0] != CMD_WRITE:
            raise I2CError("Unexpected response to WRITE")
        if pkt[1] == STATUS_NACK:
            raise I2CError(f"NACK from address 0x{addr:02X}")
        if pkt[1] != STATUS_OK:
            raise I2CError(f"Write error: 0x{pkt[1]:02X}")

    def read(self, addr: int, length: int) -> bytes:
        """
        Read bytes from an I2C slave.

        Args:
            addr:   7-bit slave address
            length: number of bytes to read (max 6)

        Returns bytes received (may be fewer than requested if slave NACKs early).
        """
        if length > 6:
            raise ValueError("Maximum 6 bytes per read packet")
        self._send([CMD_READ, addr, length])
        pkt = self._recv()
        if pkt[0] != CMD_READ:
            raise I2CError("Unexpected response to READ")
        n = pkt[1]
        return bytes(pkt[2 : 2 + n])

    def write_read(self, addr: int,
                   write_data: bytes | list[int],
                   read_len: int) -> bytes:
        """
        Combined write + repeated-START read (register read pattern).

        Args:
            addr:       7-bit slave address
            write_data: register/pointer bytes (max 4)
            read_len:   bytes to read back    (max 5)

        Returns the read bytes.
        """
        write_data = list(write_data)
        if len(write_data) > 4:
            raise ValueError("Maximum 4 write bytes for write_read")
        if read_len > 5:
            raise ValueError("Maximum 5 read bytes for write_read")

        self._send([CMD_WRITE_READ, addr,
                    len(write_data), read_len] + write_data)
        pkt = self._recv()

        if pkt[0] != CMD_WRITE_READ:
            raise I2CError("Unexpected response to WRITE_READ")
        if pkt[1] == STATUS_NACK:
            raise I2CError(f"NACK from address 0x{addr:02X}")
        if pkt[1] != STATUS_OK:
            raise I2CError(f"Write-read error: 0x{pkt[1]:02X}")

        n = pkt[2]
        return bytes(pkt[3 : 3 + n])

    # ── Convenience helpers ────────────────────────────────────────────────

    def write_reg(self, addr: int, reg: int, value: int) -> None:
        """Write a single byte to a register."""
        self.write(addr, [reg, value])

    def read_reg(self, addr: int, reg: int, length: int = 1) -> bytes:
        """Read one or more bytes starting at a register address."""
        return self.write_read(addr, [reg], length)
```

---

## Usage Examples

```python
from i2c_master import I2CMaster, I2CError

with I2CMaster() as bus:

    # ── Scan ───────────────────────────────────────────────
    print("Scanning...")
    devices = bus.scan()
    print(f"Found: {[hex(a) for a in devices]}")
    # → Found: ['0x3c', '0x68', '0x76']

    # ── SSD1306 OLED (0x3C) ────────────────────────────────
    bus.write(0x3C, [0x00, 0xAE])          # display off
    bus.write(0x3C, [0x00, 0xAF])          # display on

    # ── MPU-6050 (0x68): read 6 accel bytes from 0x3B ──────
    raw = bus.write_read(0x68, [0x3B], 6)
    ax = int.from_bytes(raw[0:2], 'big', signed=True)
    ay = int.from_bytes(raw[2:4], 'big', signed=True)
    az = int.from_bytes(raw[4:6], 'big', signed=True)
    print(f"Accel: {ax}, {ay}, {az}")

    # ── BMP280 (0x76): chip-ID register ────────────────────
    chip_id = bus.read_reg(0x76, 0xD0, 1)
    print(f"BMP280 chip ID: 0x{chip_id[0]:02X}")   # → 0x60
```

---

## Key Limitations & Notes

| Topic | Detail |
|---|---|
| **Packet size** | 8-byte HID reports → max ~5 bytes payload per transfer. For longer I2C bursts, implement multi-packet framing at the app layer. |
| **Speed** | V-USB polling is ~1 ms/packet. Scan takes ~1–2 s. Not suitable for high-bandwidth sensors. |
| **Repeated START** | Requires `TinyWireM ≥ 1.0`. If your version lacks `endTransmission(false)`, `write_read` will fall back to a STOP+START instead. |
| **Pull-ups** | Add **4.7 kΩ** pull-ups on SDA (pin 0) and SCL (pin 2) to 5 V. |
| **Clock stretch** | ATtiny85 USI doesn't support clock stretching as a master — fast slaves are fine. |
| **Arduino IDE** | Board: *Digispark (Default - 16.5 MHz)*; add DigiUSB and TinyWireM via Library Manager. |

## Installing DigiUSB & TinyWireM in Arduino IDE

### Step 0 — Add Digispark Board Support First

Without this, neither library will work correctly.

1. Open **File → Preferences**
2. Paste this URL into *"Additional Boards Manager URLs"*:
```
https://raw.githubusercontent.com/digistump/arduino-boards-index/master/package_digistump_index.json
```
3. Go to **Tools → Board → Boards Manager**
4. Search `Digistump` → Install **"Digistump AVR Boards"**
5. Select **Tools → Board → Digistump AVR Boards → Digispark (Default - 16.5 MHz)**

---

### DigiUSB — Comes bundled with the board package

After installing the board support above, `DigiUSB` is **already included automatically**.

Verify it's there:
- **Sketch → Include Library** → scroll down to *"Contributed Libraries"*
- You should see **DigiUSB** listed

If it's missing, manually copy it:
```
# Its location after board install (Windows):
C:\Users\<you>\AppData\Local\Arduino15\packages\digistump\
    hardware\avr\<version>\libraries\DigisparkUSB\

# Copy that folder into your Arduino libraries folder:
Documents\Arduino\libraries\DigiUSB\
```

---

### TinyWireM — Install via Library Manager

1. Go to **Sketch → Include Library → Manage Libraries**
2. Search: `TinyWireM`
3. Install **"TinyWireM" by Adafruit** (or by kehribar — both work)

> ⚠️ If you see two results, prefer the **Adafruit** one — it has the `endTransmission(false)` repeated-START support needed for `CMD_WRITE_READ`.

---

### Verify Installation

Create a new sketch and paste:

```cpp
#include <DigiUSB.h>
#include <TinyWireM.h>

void setup() {}
void loop() {}
```

Hit **Verify (Ctrl+R)** — if it compiles without errors, you're good.

---

### Common Problems

| Error | Fix |
|---|---|
| `DigiUSB.h: No such file` | Board package not installed, or wrong board selected |
| `TinyWireM.h: No such file` | Library Manager install didn't complete — try restarting IDE |
| `'endTransmission' takes no arguments` | Old TinyWireM version — update to Adafruit's version |
| Board not detected on USB | Install **Digispark USB driver** (Zadig on Windows, libusb) |

---

### Windows Extra Step — USB Driver (Zadig)

Digispark uses V-USB and needs a driver on Windows:

1. Download **[Zadig](https://zadig.akeo.ie/)**
2. Plug in Digispark **within 5 seconds of power-on** (bootloader window)
3. In Zadig: select *"Digispark Bootloader"* → Install **libusb-win32**
4. Repeat for the *"DigiUSB"* device (runtime)

Linux/macOS don't need this — it works out of the box.

## Linux Setup for Digispark — Arduino IDE

### 1 — udev Rules (Most Important Step)

By default Linux won't give user-level access to the Digispark USB device. Fix it with a udev rule:

```bash
sudo nano /etc/udev/rules.d/49-digispark.rules
```

Paste this content:

```
# Digispark bootloader
SUBSYSTEMS=="usb", ATTRS{idVendor}=="16d0", ATTRS{idProduct}=="0753", MODE:="0666"

# DigiUSB runtime (your firmware's VID/PID)
SUBSYSTEMS=="usb", ATTRS{idVendor}=="16c0", ATTRS{idProduct}=="05df", MODE:="0666"

# DigiUSB (alternate)
SUBSYSTEMS=="usb", ATTRS{idVendor}=="16c0", ATTRS{idProduct}=="27db", MODE:="0666"
```

Then reload:

```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
```

> You only need to do this **once**. No drivers, no Zadig.

---

### 2 — Add Yourself to the `dialout` Group

```bash
sudo usermod -aG dialout $USER
```

Log out and back in (or `newgrp dialout`) for it to take effect.

---

### 3 — Arduino IDE Setup

```bash
# If you don't have Arduino IDE yet:
# Download the .tar.xz from https://www.arduino.cc/en/software
# Extract and run:
cd ~/arduino-ide   # or wherever you extracted it
./arduino
```

Then inside the IDE — same as before:

**File → Preferences → Additional Boards Manager URLs:**
```
https://raw.githubusercontent.com/digistump/arduino-boards-index/master/package_digistump_index.json
```

**Tools → Board → Boards Manager** → search `Digistump` → Install.

**Sketch → Include Library → Manage Libraries** → search `TinyWireM` → Install Adafruit's version.

---

### 4 — Flashing the Firmware

Digispark has a **5-second bootloader window** on plug-in. The workflow is:

1. **Don't plug in** the Digispark yet
2. Click **Upload** in Arduino IDE
3. Wait until you see:
```
Running Digispark Uploader...
Plug in device now... (will timeout in 60 seconds)
```
4. **Then plug in** the Digispark → flashing starts automatically

---

### 5 — Verify Everything Works

Check the device is seen by Linux:

```bash
# Plug in Digispark, then:
lsusb | grep -i "16c0\|16d0\|digispark"

# Should show something like:
# Bus 001 Device 012: ID 16c0:05df Van Ooijen Technische Informatica Free shared USB VID/PID pair for HID devices
```

Check udev picked it up:

```bash
dmesg | tail -20
# Look for: usb ... New USB device found, idVendor=16c0
```

Test the Python host library:

```bash
pip install hid
python3 -c "import hid; print(hid.enumerate(0x16C0, 0x05DF))"
# Should print device info dict, not an empty list
```

---

### Quick Troubleshooting on Linux

| Symptom | Fix |
|---|---|
| `lsusb` shows device but Python gets empty list | udev rules not reloaded — repeat step 1 |
| Upload fails / times out | Didn't plug in during the bootloader window — unplug, retry |
| `permission denied` on `/dev/bus/usb/...` | `usermod -aG dialout` not applied yet — re-login |
| `hid.HIDException: unable to open device` | Run once with `sudo` to confirm it's a permissions issue, then fix udev |
| Arduino IDE doesn't list Digispark board | Board URL wrong or Boards Manager install failed — check internet, retry |

## Yes — but with Important Pin Conflicts

The same USB bridge pattern works perfectly for SPI. However there are hardware constraints you need to know first.

---

### Pin Conflict: SPI vs I2C

The ATtiny85 USI hardware is **shared** between I2C and SPI — same physical pins:

| Function | I2C | SPI | Pin |
|---|---|---|---|
| Data | SDA | MOSI | **PB0 (pin 0)** |
| Clock | SCL | SCK | **PB2 (pin 2)** |
| Data in | — | MISO | **PB1 (pin 1)** |
| CS | — | CS | ⚠️ **No free pin** |

USB occupies PB3 & PB4, reset is PB5. So **you cannot run I2C and SPI simultaneously**, and there's **no free pin for CS**.

**Practical solutions for CS:**
- **Single slave** → hardwire CS to GND permanently
- **Multiple slaves** → use an external 74HC138 decoder driven by SCK/MOSI patterns (complex)
- **Pick one protocol** → I2C or SPI per firmware build

---

### Firmware — `spi_bridge.ino`

```cpp
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
```

---

### Host Library — `spi_master.py`

```python
"""
spi_master.py — Host-side API for the Digispark USB→SPI bridge.

Requires:  pip install hid
"""

import hid
from typing import Optional

DIGISPARK_VID = 0x16C0
DIGISPARK_PID = 0x05DF

CMD_SPI_TRANSFER = 0x10
CMD_SPI_WRITE    = 0x11
CMD_SPI_READ     = 0x12
CMD_SPI_CONFIG   = 0x13

STATUS_OK  = 0x00
STATUS_ERR = 0xFF

PKT_SIZE   = 8

class SPIError(Exception):
    pass

class SPIMaster:
    """
    USB→SPI bridge backed by a Digispark ATtiny85.

    Modes 0–3 match standard SPI CPOL/CPHA:
        MODE0: CPOL=0, CPHA=0  (most common)
        MODE1: CPOL=0, CPHA=1
        MODE2: CPOL=1, CPHA=0
        MODE3: CPOL=1, CPHA=1

    Clock dividers (base 16.5 MHz):
        0 → DIV2  ≈ 8.25 MHz
        1 → DIV4  ≈ 4.1  MHz  (default)
        2 → DIV8  ≈ 2.0  MHz
        3 → DIV16 ≈ 1.0  MHz
    """

    def __init__(self, vid: int = DIGISPARK_VID, pid: int = DIGISPARK_PID):
        self._vid = vid
        self._pid = pid
        self._dev: Optional[hid.device] = None

    # ── Lifecycle ─────────────────────────────────────────────────────────

    def open(self) -> None:
        self._dev = hid.device()
        self._dev.open(self._vid, self._pid)
        self._dev.set_nonblocking(False)

    def close(self) -> None:
        if self._dev:
            self._dev.close()
            self._dev = None

    def __enter__(self) -> "SPIMaster":
        self.open()
        return self

    def __exit__(self, *_) -> None:
        self.close()

    # ── Low-level ─────────────────────────────────────────────────────────

    def _send(self, payload: list[int]) -> None:
        packet = [0x00] + payload + [0x00] * (PKT_SIZE - len(payload))
        self._dev.write(packet)

    def _recv(self, timeout_ms: int = 1000) -> bytes:
        data = self._dev.read(PKT_SIZE, timeout_ms)
        if not data:
            raise SPIError("Read timeout")
        return bytes(data[:PKT_SIZE])

    # ── Public API ────────────────────────────────────────────────────────

    def configure(self, mode: int = 0, divider: int = 1) -> None:
        """Set SPI mode (0-3) and clock divider (0-3)."""
        if not 0 <= mode <= 3:
            raise ValueError("mode must be 0–3")
        if not 0 <= divider <= 3:
            raise ValueError("divider must be 0–3")
        self._send([CMD_SPI_CONFIG, mode, divider])
        pkt = self._recv()
        if pkt[1] != STATUS_OK:
            raise SPIError("Config failed")

    def transfer(self, data: bytes | list[int]) -> bytes:
        """
        Full-duplex transfer: send bytes, simultaneously receive bytes.
        Max 6 bytes per call.
        Returns received bytes (same length as sent).
        """
        data = list(data)
        if len(data) > 6:
            raise ValueError("Maximum 6 bytes per transfer")
        self._send([CMD_SPI_TRANSFER, len(data)] + data)
        pkt = self._recv()
        if pkt[0] != CMD_SPI_TRANSFER:
            raise SPIError("Unexpected response")
        n = pkt[1]
        return bytes(pkt[2 : 2 + n])

    def write(self, data: bytes | list[int]) -> None:
        """Write-only transfer. Max 6 bytes."""
        data = list(data)
        if len(data) > 6:
            raise ValueError("Maximum 6 bytes per write")
        self._send([CMD_SPI_WRITE, len(data)] + data)
        pkt = self._recv()
        if pkt[1] != STATUS_OK:
            raise SPIError("Write failed")

    def read(self, length: int) -> bytes:
        """Read-only (sends 0x00 on MOSI). Max 6 bytes."""
        if length > 6:
            raise ValueError("Maximum 6 bytes per read")
        self._send([CMD_SPI_READ, length])
        pkt = self._recv()
        if pkt[0] != CMD_SPI_READ:
            raise SPIError("Unexpected response")
        n = pkt[1]
        return bytes(pkt[2 : 2 + n])

    # ── Convenience ───────────────────────────────────────────────────────

    def write_reg(self, reg: int, value: int) -> None:
        """Write a value to a register (reg | 0x00 for write bit)."""
        self.write([reg & 0x7F, value])   # MSB=0 → write (common convention)

    def read_reg(self, reg: int, length: int = 1) -> bytes:
        """Read from a register address."""
        return self.transfer([reg | 0x80] + [0x00] * length)[1:]
```

---

### Usage Examples

```python
from spi_master import SPIMaster, SPIError

with SPIMaster() as bus:

    # Configure: MODE0, DIV4 (~4 MHz)
    bus.configure(mode=0, divider=1)

    # ── MCP3204 ADC — 12-bit, 4ch ─────────────────────────
    # Single-ended CH0: start=1, SGL=1, D2=0, D1=0, D0=0
    raw = bus.transfer([0x06, 0x00, 0x00])   # 3-byte frame
    adc = ((raw[1] & 0x0F) << 8) | raw[2]
    print(f"ADC CH0: {adc} ({adc * 3.3 / 4095:.3f} V)")

    # ── MAX31855 Thermocouple (read-only, 4 bytes) ─────────
    bus.configure(mode=0, divider=3)         # max 5 MHz
    data = bus.read(4)
    raw14 = (data[0] << 6) | (data[1] >> 2)
    temp_c = raw14 * 0.25
    print(f"Temperature: {temp_c:.2f} °C")

    # ── Generic register read (e.g. BME280) ───────────────
    chip_id = bus.read_reg(0xD0)             # reg 0xD0, MSB set = read
    print(f"Chip ID: 0x{chip_id[0]:02X}")
```

---

### I2C vs SPI Bridge — Summary

| | I2C Bridge | SPI Bridge |
|---|---|---|
| Pins used | PB0 (SDA), PB2 (SCL) | PB0 (MOSI), PB1 (MISO), PB2 (SCK) |
| CS pin | Not needed | Hardwire or sacrifice MISO |
| Multi-device | Yes (addresses) | Needs external CS logic |
| Library | `TinyWireM` | `TinySPI` |
| Max payload/pkt | 5 bytes write / 6 read | 6 bytes |
| Simultaneous with other protocol | ❌ USI is shared | ❌ USI is shared |

