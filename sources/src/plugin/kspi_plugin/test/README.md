# spi_loopback_test

A Linux userspace utility that validates a SPI bus by sending known byte
patterns and verifying the receive buffer matches — confirming the MOSI→MISO
path is intact.

---

## How It Works

SPI is full-duplex: the controller drives MOSI while simultaneously sampling
MISO.  If MOSI is wired to MISO, every transmitted byte is received
unchanged.  The tool runs five fixed patterns through the bus and reports
pass/fail per pattern.

```
[spi_loopback_test]
       │
       ▼
  /dev/spidevX.Y  (spidev kernel driver)
       │
       ▼
  SPI controller
     MOSI ──┐
            │ jumper wire
     MISO ◀─┘
```

---

## Prerequisites

| Requirement | Notes |
|------------|-------|
| Linux kernel with `spidev` | Built into most distros |
| A board with SPI pins | Raspberry Pi, BeagleBone, etc. |
| MOSI wired to MISO | One jumper wire |
| `gcc` | Build the binary |

> **Pure software testing:** `spi-stub` (`sudo modprobe spi-stub`) creates a
> fake SPI adapter but does **not** loop data back — it only lets you probe
> the bus.  Real data echo requires the physical MOSI→MISO jumper, or a
> dedicated loopback SPI slave device.

---

## Hardware Setup

### Raspberry Pi (any model with 40-pin header)

| Signal | GPIO | Physical pin |
|--------|------|--------------|
| MOSI   | GPIO10 | Pin 19 |
| MISO   | GPIO9  | Pin 21 |

Connect pin 19 to pin 21 with a jumper wire.

```
Pin 19 (MOSI) ────┐
                  │  <── jumper wire
Pin 21 (MISO) ────┘
```

### Enable spidev on Raspberry Pi

```bash
sudo raspi-config
# Interface Options → SPI → Enable → Finish → Reboot

ls /dev/spidev*
# /dev/spidev0.0  /dev/spidev0.1
```

### BeagleBone Black

```bash
# SPI1: MOSI = P9.30 (pin 30), MISO = P9.29 (pin 29)
# Load the spidev overlay:
sudo config-pin P9.29 spi
sudo config-pin P9.30 spi
sudo config-pin P9.28 spi_cs
sudo config-pin P9.31 spi_sclk
ls /dev/spidev*
# /dev/spidev1.0
```

### Generic board

Consult your board's pinout.  Find `SPI_MOSI` and `SPI_MISO` on the same SPI
bus and connect them with a jumper.  Enable `spidev` via your board's
configuration tool or device tree overlay.

---

## Build

```bash
gcc -o spi_loopback_test spi_loopback_test.c
```

---

## Usage

```
./spi_loopback_test [device] [speed_hz] [mode]
```

| Argument | Default | Description |
|----------|---------|-------------|
| `device` | `/dev/spidev0.0` | spidev node |
| `speed_hz` | `500000` | SPI clock in Hz (500 kHz) |
| `mode` | `0` | SPI mode: 0, 1, 2, or 3 |

---

## Examples

### 1. Basic loopback test (all defaults)

```bash
./spi_loopback_test
```

```
SPI loopback tester
  Device   : /dev/spidev0.0
  Mode     : SPI0
  Bits     : 8
  Speed    : 500000 Hz (500.000 kHz)

[1/5] Pattern: Incrementing
  TX    : 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F
  RX    : 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F
  PASS  TX == RX

[2/5] Pattern: All-0xFF
  TX    : FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF
  RX    : FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF
  PASS  TX == RX

[3/5] Pattern: All-0x00
  TX    : 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  RX    : 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  PASS  TX == RX

[4/5] Pattern: Alternating
  TX    : AA 55 AA 55 AA 55 AA 55 AA 55 AA 55 AA 55 AA 55
  RX    : AA 55 AA 55 AA 55 AA 55 AA 55 AA 55 AA 55 AA 55
  PASS  TX == RX

[5/5] Pattern: DEADBEEF
  TX    : DE AD BE EF DE AD BE EF DE AD BE EF DE AD BE EF
  RX    : DE AD BE EF DE AD BE EF DE AD BE EF DE AD BE EF
  PASS  TX == RX

==============================================
Results: 5/5 passed, 0 failed
All tests PASSED. MOSI->MISO loopback confirmed.
```

---

### 2. Custom device and speed

```bash
# Use spidev1.0 at 1 MHz
./spi_loopback_test /dev/spidev1.0 1000000
```

---

### 3. SPI mode 3

```bash
# CPOL=1 CPHA=1
./spi_loopback_test /dev/spidev0.0 500000 3
```

---

### 4. Testing at different speeds

Useful for finding the maximum reliable clock for your wiring:

```bash
for speed in 100000 500000 1000000 4000000 8000000; do
    echo "--- Testing at ${speed} Hz ---"
    ./spi_loopback_test /dev/spidev0.0 $speed
done
```

At high speeds (> 4 MHz) longer jumper wires may introduce reflections and
cause failures — this is expected behaviour and a useful diagnostic.

---

### 5. Failure output (broken wire or no loopback)

If MOSI is not connected to MISO, MISO is usually pulled low (0x00):

```
[1/5] Pattern: Incrementing
  TX    : 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F
  RX    : 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  FAIL  mismatch detected
  DIFF  :    ^^ ^^ ^^ ^^ ^^ ^^ ^^ ^^ ^^ ^^ ^^ ^^ ^^ ^^ ^^
```

---

## SPI Modes Reference

| Mode | CPOL | CPHA | Clock idle | Sample edge |
|------|------|------|-----------|-------------|
| 0    | 0    | 0    | Low        | Rising      |
| 1    | 0    | 1    | Low        | Falling     |
| 2    | 1    | 0    | High       | Falling     |
| 3    | 1    | 1    | High       | Rising      |

When testing in loopback, the mode does not affect whether data echoes — any
mode will pass if the wire is intact.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| `open spidev: No such file or directory` | spidev not enabled | Enable SPI in board config and reboot |
| `open spidev: Permission denied` | User lacks access | `sudo usermod -aG spi $USER` or run with `sudo` |
| `SPI_IOC_MESSAGE: Invalid argument` | Mode or speed out of range | Use mode 0–3 and check board's max SPI clock |
| All patterns FAIL, RX is all `0x00` | MOSI not connected to MISO | Check the jumper wire |
| Intermittent failures at high speed | Signal integrity issue | Shorten the jumper wire or reduce speed |
| `spi-stub` loaded but no data echoed | spi-stub is a probe-only stub | Use the physical MOSI→MISO wire for data tests |

---

## Limitations

| Limitation | Notes |
|------------|-------|
| Physical wire required for data | `spi-stub` does not echo data bytes |
| Fixed 16-byte transfer length | Modify `TRANSFER_LEN` in source to change |
| No CS/multi-slave testing | Tests only the active chip select |
| No DMA stress test | Single ioctl transfer per pattern |


---

# SPI on Linux picture:

## Software-only stub (no data echo)

```bash
sudo modprobe spi-stub
```
This just creates a fake SPI master — it **won't echo data**, only useful for probing the bus.

---

## spidev — the userspace driver (what you actually need)

`spidev` is not loaded standalone; it attaches to an existing SPI controller:

```bash
sudo modprobe spidev
```

But **spidev alone does nothing** — it needs a device tree entry or a platform device that says "expose this SPI bus as /dev/spidevX.Y".

---

## Practical approach by board

### Raspberry Pi
```bash
# Enable via raspi-config (recommended)
sudo raspi-config
# → Interface Options → SPI → Enable → Reboot

# Or load manually:
sudo modprobe spi-bcm2835   # the actual controller driver
sudo modprobe spidev
```

### Generic x86 / PC (no SPI pins exposed)
```bash
# Use a USB-SPI adapter (e.g. CH341)
sudo modprobe spi-ch341-usb
# then spidev attaches automatically
```

### BeagleBone
```bash
sudo modprobe spi-omap2-mcspi
sudo modprobe spidev
```

---

## Check if it worked

```bash
# List SPI-related modules currently loaded
lsmod | grep spi

# Check if spidev nodes appeared
ls /dev/spidev*

# Check kernel log for SPI activity
dmesg | grep -i spi
```

---

## Summary

| Module | What it does |
|--------|-------------|
| `spi-stub` | Fake master, no data echo |
| `spidev` | Userspace access driver (needs controller) |
| `spi-bcm2835` | Real controller driver (Raspberry Pi) |
| `spi-omap2-mcspi` | Real controller driver (BeagleBone) |
| `spi-ch341-usb` | USB→SPI bridge |

The key point: **`modprobe spidev` only works if a SPI controller driver is already loaded and has a device registered for spidev** — otherwise no `/dev/spidevX.Y` node appears.