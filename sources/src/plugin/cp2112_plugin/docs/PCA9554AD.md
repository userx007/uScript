

# PCA9554AD via CP2112 Plugin — Command Sequences

## Command Syntax

```
CP2112.<MODULE> <subcommand> [args]
```

The two modules are **I2C** and **GPIO**. Both open their own HID handle independently.

---

## I2C Module — PCA9554AD Access

### 1. Open the I2C Bus Targeting the PCA9554AD

```
CP2112.I2C open addr=0x20 clock=400000
```

> `addr` = 7-bit address (A2=A1=A0=0 → 0x20). Use `clock=100000` if the bus or pull-ups are slow.

---

### 2. Scan the Bus (discover all devices before committing to an address)

```
CP2112.I2C scan
```

> Probes 0x08–0x77 with a zero-byte write. No prior `open` needed. Uses current `clock` and `device` index.

---

### 3. Configure All Pins as Outputs, Drive All LOW

```
# Step 1 — set Configuration register (reg 0x03) = 0x00  →  all pins = output
CP2112.I2C write 03 00

# Step 2 — set Output Port register (reg 0x01) = 0x00  →  all pins LOW
CP2112.I2C write 01 00
```

Each `write` produces: `S | 0x40 | A | <reg> | A | <data> | A | P`

---

### 4. Drive Individual Pins HIGH (e.g., P0 and P2)

```
# Output reg = 0x05  (bits 0 and 2 set)
CP2112.I2C write 01 05
```

---

### 5. Read the Input Port (all 8 pins)

```
# Write pointer to register 0x00, then read 1 byte
CP2112.I2C wrrd 00:1
```

This produces the full write-then-read sequence:
```
S | 0x40 | A | 0x00 | A | Sr | 0x41 | A | [pins] | N | P
```

---

### 6. Read Any Register (generic pattern)

```
# Read Configuration register (0x03)
CP2112.I2C wrrd 03:1

# Read Output latch register (0x01)
CP2112.I2C wrrd 01:1

# Read Polarity Inversion register (0x02)
CP2112.I2C wrrd 02:1
```

Format: `wrrd <hex_register_pointer>:<read_byte_count>`

---

### 7. Read-Modify-Write an Output Pin (toggle P2)

```
# 1. Read the output latch
CP2112.I2C wrrd 01:1         # → returns e.g. 0x05

# 2. Compute new value externally: 0x05 ^ 0x04 = 0x01

# 3. Write back
CP2112.I2C write 01 01
```

> Always read register **0x01** (output latch), not 0x00 (input port), before modifying outputs.

---

### 8. Set Polarity Inversion on P0 and P1

```
CP2112.I2C write 02 03
```

---

### 9. Mixed Config: P0–P3 outputs, P4–P7 inputs, outputs to 0xA (1010 on P3:P0)

```
# Configuration register: 0xF0 (upper nibble = inputs, lower = outputs)
CP2112.I2C write 03 F0

# Output register: 0x0A
CP2112.I2C write 01 0A
```

---

### 10. Execute a Script (automated sequence)

```
CP2112.I2C open addr=0x20 clock=100000
CP2112.I2C script pca9554_init.txt
```

The script file sits in `ARTEFACTS_PATH` and contains one command per line (same `wrrd`/`write`/`read` syntax). Timing between commands is controlled by `SCRIPT_DELAY` (INI).

---

### 11. Reconfigure Speed on the Fly

```
CP2112.I2C cfg clock=100000      # update pending config
CP2112.I2C close
CP2112.I2C open addr=0x20        # reopens at new speed
```

Or with a named preset:
```
CP2112.I2C speed 100kHz
CP2112.I2C speed 400kHz
CP2112.I2C speed 10kHz
```

---

### 12. Close

```
CP2112.I2C close
```

---

## GPIO Module — CP2112 Pins (not PCA9554)

These control the **CP2112's own 8 GPIO pins**, separate from the I2C bus.

### Open with all outputs, push-pull

```
CP2112.GPIO open dir=0xFF pp=0xFF
```

### Drive pin GPIO.0 HIGH, GPIO.1 LOW (others unchanged)

```
CP2112.GPIO set   0x01     # GPIO.0 → HIGH
CP2112.GPIO clear 0x02     # GPIO.1 → LOW
```

### Selective write (VALUE + MASK)

```
# Set GPIO.3 HIGH, GPIO.2 LOW, touch only those two
CP2112.GPIO write 0x08 0x0C
#                 ^^^^  ^^^^
#                value  mask (bits 2 and 3)
```

### Read all 8 GPIO pins

```
CP2112.GPIO read
# Returns: GPIO: 0x3F  [00111111]
```

### Reconfigure live (takes effect immediately if open)

```
CP2112.GPIO cfg dir=0x0F pp=0x0F    # lower 4 = outputs, upper 4 = inputs
```

### Special functions (e.g. enable clock output on GPIO.6)

```
CP2112.GPIO open dir=0x40 pp=0x40 special=0x40 clkdiv=4
```

### Close

```
CP2112.GPIO close
```

---

## Putting It All Together — PCA9554AD Init + Toggle Loop

```
# --- I2C: Init PCA9554AD ---
CP2112.I2C open  addr=0x20 clock=400000
CP2112.I2C write 03 00          # all pins = outputs
CP2112.I2C write 01 00          # all LOW

# --- Blink P0 (script loop handles timing) ---
CP2112.I2C write 01 01          # P0 HIGH
CP2112.I2C write 01 00          # P0 LOW

# --- Read back inputs ---
CP2112.I2C wrrd  00:1           # read Input Port

CP2112.I2C close

# --- GPIO: signal-ready LED on CP2112 GPIO.7 ---
CP2112.GPIO open  dir=0x80 pp=0x80
CP2112.GPIO set   0x80
CP2112.GPIO close
```

---

## Key Constraints from the Plugin

| Constraint | Value |
|---|---|
| Max single I2C read (`read` / `wrrd`) | **512 bytes** |
| Max `write` payload | **512 bytes** (auto-chunked at 61 B HID reports) |
| Max bulk `wrrdf` payload | **4096 bytes** |
| Default chunk size (`wrrdf`) | **512 bytes** |
| No register auto-increment | Each `write` targets one register |
| `wrrd` format | `HEXDATA:READLEN` or `:READLEN` (read-only) or `HEXDATA` (write-only) |

