# i2c_stub_test

A Linux userspace utility to test I2C register access without physical hardware,
using the `i2c-stub` kernel module.

---

## How It Works

The `i2c-stub` module creates a fake I2C adapter in RAM.  It simulates a slave
device at a configurable address and stores register values in memory.  You can
write and read any of the 256 registers as if talking to a real chip — no
oscilloscope, no logic analyser, no wiring.

```
[your app] ──ioctl──▶ /dev/i2c-X ──▶ i2c-stub (kernel) ──▶ RAM registers
```

---

## Prerequisites

| Package | Purpose |
|---------|---------|
| `i2c-tools` | `i2cdetect`, `i2cget`, `i2cset` CLI helpers |
| Linux kernel ≥ 3.x | `i2c-stub` and `i2c-dev` modules |
| `gcc` | Build the test binary |

```bash
# Debian / Ubuntu
sudo apt install i2c-tools gcc

# Fedora / RHEL
sudo dnf install i2c-tools gcc
```

---

## Setup

```bash
# 1. Load the stub adapter with one or more simulated slave addresses
sudo modprobe i2c-stub chip_addr=0x50

# For multiple addresses:
sudo modprobe i2c-stub chip_addr=0x50,0x68,0x76

# 2. Load the userspace I2C device driver
sudo modprobe i2c-dev

# 3. Find which adapter number was assigned
i2cdetect -l
# Example output:
# i2c-0   i2c       Synopsys DesignWare I2C adapter   I2C adapter
# i2c-5   smbus     SMBus stub driver                 SMBus adapter

# 4. Probe the stub adapter (use the number from the line with "stub")
sudo i2cdetect -y 5
# Addresses given to chip_addr will show as "50", "68", etc.
```

> **Tip:** The stub adapter is usually the highest-numbered one listed.

---

## Build

```bash
gcc -o i2c_stub_test i2c_stub_test.c
```

---

## Usage

```
./i2c_stub_test <device> <slave-addr> [reg] [value]
```

| Argument | Description | Example |
|----------|-------------|---------|
| `device` | I2C adapter device node | `/dev/i2c-5` |
| `slave-addr` | Slave address (hex) | `0x50` |
| `reg` | Register offset (hex, optional) | `0x10` |
| `value` | Byte to write (hex, optional) | `0xAB` |

---

## Examples

### 1. Dump all 256 registers

```bash
./i2c_stub_test /dev/i2c-5 0x50
```

```
I2C stub tester  |  adapter: /dev/i2c-5  |  slave: 0x50

Register dump (addr  : +0  +1  +2  +3  +4  +5  +6  +7  +8  +9  +A  +B  +C  +D  +E  +F)
------------------------------------------------------------------------------------------
  0x00 : 00  00  00  00  00  00  00  00  00  00  00  00  00  00  00  00
  0x10 : 00  00  00  00  00  00  00  00  00  00  00  00  00  00  00  00
  ...
```

All registers start at zero on a fresh stub.

---

### 2. Read a single register

```bash
./i2c_stub_test /dev/i2c-5 0x50 0x10
```

```
I2C stub tester  |  adapter: /dev/i2c-5  |  slave: 0x50
READ  reg 0x10 -> 0x00 (0)
```

---

### 3. Write a value, then read it back

```bash
./i2c_stub_test /dev/i2c-5 0x50 0x10 0xAB
```

```
I2C stub tester  |  adapter: /dev/i2c-5  |  slave: 0x50
WRITE reg 0x10 <- 0xAB
READ  reg 0x10 -> 0xAB
OK  write/read-back matches.
```

---

### 4. Use i2c-tools alongside the test binary

The `i2c-tools` commands work on the same stub adapter — useful for
cross-checking:

```bash
# Write 0xBE to register 0x20 using i2cset
sudo i2cset -y 5 0x50 0x20 0xBE

# Read it back with i2cget
sudo i2cget -y 5 0x50 0x20
# 0xbe

# Now verify with the test binary
./i2c_stub_test /dev/i2c-5 0x50 0x20
# READ  reg 0x20 -> 0xBE (190)
```

---

### 5. Multiple slave addresses

```bash
# Load stub with two simulated devices
sudo modprobe i2c-stub chip_addr=0x50,0x68

# Test each independently
./i2c_stub_test /dev/i2c-5 0x50 0x00 0x11
./i2c_stub_test /dev/i2c-5 0x68 0x00 0x22

# Confirm they are independent
./i2c_stub_test /dev/i2c-5 0x50 0x00   # -> 0x11
./i2c_stub_test /dev/i2c-5 0x68 0x00   # -> 0x22
```

---

## Teardown

```bash
sudo rmmod i2c-stub
```

---

## Limitations

| Limitation | Notes |
|------------|-------|
| No interrupts | The stub does not generate IRQ signals |
| No clock stretching | Timing behaviour is not simulated |
| No SMBus alerts | Alert Response Address not supported |
| Register reset on rmmod | Memory is lost when the module is unloaded |
| Address must be declared at load time | Cannot add addresses dynamically |

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| `open i2c-dev: Permission denied` | User not in `i2c` group | `sudo usermod -aG i2c $USER` then re-login, or run with `sudo` |
| `ioctl I2C_SLAVE: Device or resource busy` | Another process has the address | Check with `fuser /dev/i2c-*` |
| `i2cdetect` does not show your address | Wrong adapter number, or address not in `chip_addr` | Reload module: `sudo rmmod i2c-stub && sudo modprobe i2c-stub chip_addr=0x50` |
| `Read failed` after no write | Register pointer not supported by stub before first write | Write any value first, then read |
