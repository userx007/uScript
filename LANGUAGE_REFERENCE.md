# Script Interpreter — Language Reference

---

## Table of Contents

1. [Comments](#1-comments)
2. [Plugin Loading](#2-plugin-loading)
3. [Constant Macros](#3-constant-macros)
4. [Array Macros](#4-array-macros)
5. [Variable Macros](#5-variable-macros)
6. [Commands](#6-commands)
7. [Conditional Flow — IF / GOTO / LABEL](#7-conditional-flow)
8. [Loops — REPEAT / END_REPEAT](#8-loops)
9. [Loop Index Capture](#9-loop-index-capture)
10. [BREAK and CONTINUE](#10-break-and-continue)
11. [Macro Resolution Order](#11-macro-resolution-order)
12. [Validation Rules](#12-validation-rules)
13. [Complete Example Script](#13-complete-example-script)

---

## 1. Comments

Three comment forms are supported.

### Line comment

The `#` character discards everything from that point to the end of the line.

```
# This entire line is ignored
SERIAL.OPEN  /dev/ttyUSB0 115200    # inline comment after a command
```

### Block comment

Everything between `---` and `!--` on their own lines is discarded.
Nesting is not supported.

```
---
This block is completely ignored.
Multiple lines. No nesting allowed.
!--
```

---

## 2. Plugin Loading — `LOAD_PLUGIN`

```
LOAD_PLUGIN  <NAME>
LOAD_PLUGIN  <NAME>  <op>  v<major>.<minor>.<patch>.<build>
```

- `NAME` is upper-case; maps to a shared library on disk.
- Optional version constraint: `<`, `<=`, `>`, `>=`, `==`.
- Duplicate declarations and references to undeclared plugins are rejected at
  validation time.
- Settings are read from the matching `[NAME]` section of the `.ini` file.

```
LOAD_PLUGIN  SERIAL
LOAD_PLUGIN  GPIO
LOAD_PLUGIN  UPDATER  >= v2.0.0.0
LOAD_PLUGIN  SENSOR   == v1.4.2.0
```

---

## 3. Constant Macros — `:=`

```
<NAME>  :=  <value>
```

- Defined once; re-declaration is an error.
- Expanded at **validation time** wherever `$NAME` appears in subsequent lines.
- Value is the literal string after `:=`, trimmed.

```
DEVICE   := /dev/ttyUSB0
BAUD     := 115200
TIMEOUT  := 3000

SERIAL.OPEN  $DEVICE $BAUD
```

---

## 4. Array Macros — `[=`

An array macro declares an ordered list of string elements. Elements are
accessed at runtime by index using the `$NAME.$index` syntax.

### Declaration

```
<NAME>  [=  <elem0>, <elem1>, <elem2>, ...
```

### Multi-line declaration

A line ending with `\` is joined to the following line. The `\` and any
surrounding whitespace are stripped; the two fragments are concatenated
directly. This allows long element lists to be split across multiple lines
for readability.

```
<NAME>  [=  <elem0>, \
            <elem1>, \
            <elem2>
```

This is exactly equivalent to the single-line form. The line number recorded
for error reporting is that of the **first** physical line.

### Elements with spaces

Whitespace within an element is preserved as-is; no quoting is required
unless the element itself contains a comma.

```
LABELS  [=  slot zero, slot one, slot two
```

### Elements containing commas

Any element that contains a comma must be enclosed in double-quotes. The
quotes are stripped from the stored value; only the content between them
is kept.

```
TAGS  [=  "alpha, beta", "gamma, delta", plain
```

### Element access

Inside a loop, elements are retrieved with `$NAME.$indexmacro`, where
`$indexmacro` is any macro that resolves to a non-negative integer string
at runtime. The 0-based loop index capture is the natural pairing.

```
PORTS  [=  /dev/ttyUSB0, /dev/ttyUSB1, /dev/ttyUSB2

i  ?=  REPEAT  open_ports  3
    SERIAL.OPEN  $PORTS.$i
    LOG.PRINT    opened $PORTS.$i
END_REPEAT  open_ports
```

The pattern `$NAME.$i` is consumed as a **single token** by the macro
expander: it first resolves `$i` to an integer, then retrieves
`PORTS[i]`. If `$i` is not yet resolved (e.g. on a pass before the loop
index is set), the whole token is left unexpanded and retried on the next
expansion pass.

### Rules

- Array names must not conflict with constant macro names or with other
  array macro names (validated statically).
- Array macro names must not conflict with loop index macro names
  (validated statically).
- An out-of-bounds index leaves the `$NAME.$i` token unexpanded and logs
  an error; it does **not** abort execution, allowing the script to handle
  the condition gracefully.
- Arrays are a **declaration**, like constant macros. They occupy no
  position in the command stream and have no execution cost.

### Examples

```
# Simple list
DEVICES  [=  sensor_A, sensor_B, sensor_C

# Elements with internal spaces — no quoting needed
LABELS  [=  slot zero, slot one, slot two, slot three

# Elements containing commas — must be quoted
TAGS  [=  "alpha, beta", "gamma, delta", plain_value

# Multi-line for readability
FIRMWARE_PATHS  [=  /opt/fw/board_A.bin, \
                    /opt/fw/board_B.bin, \
                    /opt/fw/board_C.bin, \
                    /opt/fw/board_D.bin

# Access with loop index
board  ?=  REPEAT  flash_all  4
    LOG.PRINT    Flashing board $board with $FIRMWARE_PATHS.$board
    UPDATER.FLASH  $FIRMWARE_PATHS.$board
END_REPEAT  flash_all
```

---

## 5. Variable Macros — `?=`

```
<name>  ?=  <PLUGIN>.<COMMAND>  [params]
```

- Captures the **return value** of a plugin command at **execution time**.
- Referenced as `$name` in subsequent commands.
- The most recently written value is used when the same name appears more
  than once.

```
fw_ver   ?=  UPDATER.GET_VERSION
dev_id   ?=  SENSOR.READ_ID

LOG.PRINT  version=$fw_ver  device=$dev_id
```

---

## 6. Commands — `PLUGIN.COMMAND`

```
<PLUGIN>.<COMMAND>  [params]
```

- `PLUGIN` and `COMMAND` must be fully upper-case.
- `params` is an optional free-form string; `$macros` inside it are expanded
  immediately before dispatch.
- Validated twice: dry-run (argument check), then real execution.
- A failed dispatch aborts the entire script immediately, regardless of
  nesting depth.

```
SERIAL.OPEN    /dev/ttyUSB0 115200
GPIO.SET_HIGH  PIN_RESET
GPIO.DELAY_MS  50
GPIO.SET_LOW   PIN_RESET
SERIAL.CLOSE
```

---

## 7. Conditional Flow

### `GOTO` — unconditional jump

```
GOTO  <label>
```

### `IF … GOTO` — conditional jump

```
IF  <boolean-expression>  GOTO  <label>
```

### `LABEL` — jump target

```
LABEL  <label>
```

**Rules:**
- Jumps are **forward-only**: `GOTO` must precede its `LABEL` in the file.
- Every `GOTO` must have a matching `LABEL`; every `LABEL` must have a
  preceding `GOTO`. Both are validated statically.
- Duplicate label names are rejected.
- A `GOTO` must not cross a loop boundary (validated statically).
- Boolean expressions support: `TRUE`, `FALSE`, `!`, `&&`, `||`, `()`.
  Variable macros used as conditions must resolve to the string `TRUE` or
  `FALSE` after expansion.

```
# Unconditional skip
GOTO  skip_block
    PLUGIN.CMD_A        # never runs
LABEL  skip_block

# Conditional skip
status  ?=  SENSOR.READ_STATUS
IF  $status  GOTO  already_ok
SENSOR.RESET
LABEL  already_ok

# Multi-step conditional
fw  ?=  UPDATER.GET_VERSION
IF  $fw  GOTO  version_ok
UPDATER.FLASH  firmware.bin
GOTO  done
LABEL  version_ok
LOG.PRINT  already up to date
LABEL  done
```

---

## 8. Loops — `REPEAT` / `END_REPEAT`

Two forms share the same keyword pair. The **label** is mandatory in both
the opening and closing line; it is purely structural (identifies which
`END_REPEAT` closes which `REPEAT`) and has no relationship to `GOTO` labels.

### Counted loop

```
REPEAT  <label>  <N>
    …
END_REPEAT  <label>
```

- Body executes exactly `N` times (`N` ≥ 1).

```
REPEAT  pulse  3
    GPIO.SET_HIGH  17
    GPIO.DELAY_MS  50
    GPIO.SET_LOW   17
    GPIO.DELAY_MS  50
END_REPEAT  pulse
```

### Conditional loop (do-while)

```
REPEAT  <label>  UNTIL  <condition>
    …
END_REPEAT  <label>
```

- Body always executes at least once.
- `condition` is evaluated at `END_REPEAT` after each iteration.
- Same boolean expression rules as `IF … GOTO`.

```
ready  ?=  SENSOR.IS_READY

REPEAT  wait  UNTIL  $ready
    GPIO.DELAY_MS  100
    ready  ?=  SENSOR.IS_READY
END_REPEAT  wait
```

### Nesting

Loops may be nested to any depth. Labels must be unique across all loops.

```
REPEAT  outer  3
    REPEAT  inner  4
        PLUGIN.CMD
    END_REPEAT  inner
END_REPEAT  outer
```

---

## 9. Loop Index Capture — `?= REPEAT`

An optional `varname ?=` prefix captures the current **0-based iteration
index** into a variable macro that is scoped to the loop body.

```
<name>  ?=  REPEAT  <label>  <N>
<name>  ?=  REPEAT  <label>  UNTIL  <condition>
```

- `$name` holds `"0"` on the first iteration, `"1"` on the second, and so on.
- `$name` is **only visible inside the loop body**. It does not exist before
  the `REPEAT` line or after `END_REPEAT`. This is identical to a local
  variable in a C block.
- An inner loop's index macro shadows an outer one with the same name for
  the duration of the inner loop, then the outer value is restored.
- The macro name must not conflict with any existing script-level `?=` macro
  or any array macro name (validated statically).
- Combined with array macros, the loop index is the natural subscript
  for iterating over array elements with `$ARRAY.$index`.

```
# Counted loop with index
slot  ?=  REPEAT  flash  4
    LOG.PRINT  flashing slot $slot
    SENSOR.SELECT   $slot
    SENSOR.CALIBRATE
END_REPEAT  flash
# $slot no longer exists here

# Conditional loop with counter
attempt  ?=  REPEAT  retry  UNTIL  $ok
    UPDATER.FLASH   firmware.bin
    ok  ?=  UPDATER.VERIFY
END_REPEAT  retry

# Nested loops — independent indices, both usable as array subscripts
BANKS     [=  bank_A,  bank_B,  bank_C
CHANNELS  [=  ch_0,    ch_1,    ch_2,    ch_3

bank  ?=  REPEAT  outer  3
    ch  ?=  REPEAT  inner  4
        SENSOR.CONFIGURE  $BANKS.$bank  $CHANNELS.$ch
    END_REPEAT  inner
    SENSOR.COMMIT_BANK  $BANKS.$bank
END_REPEAT  outer
```

---

## 10. BREAK and CONTINUE

Both keywords name the **target loop** explicitly, following the same
convention as Rust's labelled loops. This eliminates ambiguity in nested
loops and is consistent with the rest of the language requiring labels on
all loop constructs.

### `BREAK <label>`

Exits the named enclosing loop immediately. All loops between the current
innermost and the named target are also unwound. Execution resumes after
`END_REPEAT <label>`.

### `CONTINUE <label>`

Skips the remainder of the current loop body and resumes at `END_REPEAT` of
the named enclosing loop. The loop's normal exit-or-loop-back logic then
runs as usual (decrement counter or evaluate condition).

**Rules:**
- Both keywords must appear inside the named loop (validated statically).
- Neither can be used outside any loop.
- `GOTO` labels and loop labels are separate namespaces; a `BREAK`/`CONTINUE`
  label cannot name a `GOTO` target.

```
PORTS  [=  /dev/ttyUSB0, /dev/ttyUSB1, /dev/ttyUSB2, /dev/ttyUSB3

slot  ?=  REPEAT  scan  4

    present  ?=  SENSOR.IS_PRESENT  $PORTS.$slot
    IF  $present  GOTO  present_ok
    CONTINUE  scan            # port absent — skip to END_REPEAT, loop back
    LABEL  present_ok

    ok  ?=  SENSOR.RUN_SELFTEST  $PORTS.$slot
    IF  $ok  GOTO  test_ok
    CONTINUE  scan            # test failed — skip to END_REPEAT, loop back
    LABEL  test_ok

    SENSOR.ACTIVATE  $PORTS.$slot
    BREAK  scan               # first good port found — exit immediately

END_REPEAT  scan
```

### Nested BREAK / CONTINUE

`BREAK outer` from inside the inner loop exits both loops. All inner
`LoopState` entries are unwound automatically as the skip cursor passes
their `END_REPEAT` nodes.

```
BANKS  [=  bank_A, bank_B, bank_C

bank  ?=  REPEAT  outer  3
    ch  ?=  REPEAT  inner  8

        ok  ?=  SENSOR.TEST  $BANKS.$bank $ch
        IF  $ok  GOTO  ch_ok
        CONTINUE  inner           # bad channel — try next
        LABEL  ch_ok

        SENSOR.ACTIVATE  $BANKS.$bank $ch
        BREAK  outer              # found one — exit both loops

    END_REPEAT  inner
END_REPEAT  outer
```

---

## 11. Macro Resolution Order

When `$name` or `$ARRAY.$index` is encountered during execution, the
interpreter resolves it through the following priority order.

### Plain `$name`

| Priority | Source | Scope |
|----------|--------|-------|
| 1 (highest) | Loop index macros — innermost active loop first | Loop body only; destroyed on `END_REPEAT` |
| 2 | Script-level variable macros (`?=` results) | Entire script; last written value wins |
| 3 (lowest) | Shell macros — set via `executeCmd()` | Script-wide |

### Array element `$NAME.$index`

1. `NAME` is looked up in `mapArrayMacros`.
2. `index` is resolved through the plain `$name` chain above.
3. The resolved index string is converted to an unsigned integer and used
   to subscript the element vector.
4. If `NAME` is not an array, it is resolved as a plain macro and
   `.$index` is re-emitted literally.

Constant macros (`$NAME` from `:=`) are expanded at **validation time**
and are never seen by the runtime resolver. Array macro names (`[=`) are
similarly resolved at validation time and stored directly in
`mapArrayMacros`; only the per-element access `$NAME.$i` is a runtime
operation.

---

## 12. Validation Rules

| Rule | Severity |
|------|----------|
| Duplicate constant macro name | Error |
| Duplicate array macro name | Error |
| Array macro name conflicts with constant macro name | Error |
| Array element list is empty | Error |
| Unterminated quote in array element list | Error |
| Plugin used but not declared with `LOAD_PLUGIN` | Error |
| `LOAD_PLUGIN` declared but no command uses it | Warning |
| Command not in plugin's supported command list | Error |
| `GOTO` without a matching `LABEL` | Error |
| `LABEL` without a preceding `GOTO` | Error |
| Duplicate `LABEL` name | Error |
| Backward `GOTO` (label before goto in file) | Error |
| `GOTO` crossing a loop boundary | Error |
| Duplicate loop label | Error |
| Loop label conflicts with a `GOTO`/`LABEL` name | Error |
| `END_REPEAT` without matching `REPEAT` | Error |
| `END_REPEAT` label mismatch | Error |
| Unclosed loop (missing `END_REPEAT`) | Error |
| `BREAK`/`CONTINUE` used outside any loop | Error |
| `BREAK`/`CONTINUE` label not an enclosing loop | Error |
| Loop index macro name shadows a script-level macro | Error |
| Loop index macro name conflicts with an array macro name | Error |
| Nested block comment | Error |

---

## 13. Complete Example Script

The script below uses every language feature in a realistic context: a
firmware update sequence with sensor initialisation, retry logic, and
per-slot scanning driven by array-backed device lists.

```
# =============================================================================
# complete_example.script
# Demonstrates every supported language feature.
# =============================================================================

---
Pre-conditions:
  - Device is powered and connected via USB serial.
  - Firmware binaries are present at the paths listed below.
!--


# ── 1. PLUGIN LOADING ────────────────────────────────────────────────────────

LOAD_PLUGIN  SERIAL   >= v1.0.0.0
LOAD_PLUGIN  GPIO
LOAD_PLUGIN  UPDATER  == v3.1.0.0
LOAD_PLUGIN  SENSOR
LOAD_PLUGIN  LOG


# ── 2. CONSTANT MACROS ───────────────────────────────────────────────────────

RESET_PIN   := 17
BOOT_PIN    := 18
BANK_COUNT  := 3
SLOT_COUNT  := 8


# ── 3. ARRAY MACROS ──────────────────────────────────────────────────────────

# Serial ports for each board (single-line)
PORTS  [=  /dev/ttyUSB0, /dev/ttyUSB1, /dev/ttyUSB2

# Firmware image paths per board (multi-line with \ continuation)
FW_IMAGES  [=  /opt/fw/board_A.bin, \
               /opt/fw/board_B.bin, \
               /opt/fw/board_C.bin

# Bank names with spaces inside elements — no quoting needed
BANK_NAMES  [=  bank alpha, bank beta, bank gamma

# Tags that contain commas — must be quoted
BOARD_TAGS  [=  "rev3, production", "rev2, prototype", "rev3, production"


# ── 4. OPEN CHANNEL ──────────────────────────────────────────────────────────

SERIAL.OPEN  /dev/ttyUSB0 115200
LOG.PRINT    Channel opened


# ── 5. VARIABLE MACRO — capture plugin return value ──────────────────────────

fw_current  ?=  UPDATER.GET_VERSION
LOG.PRINT    Current firmware: $fw_current


# ── 6. CONDITIONAL FLOW — IF / GOTO / LABEL ──────────────────────────────────

IF  $fw_current  GOTO  fw_check_done
LOG.PRINT  Could not read firmware version
GOTO  abort
LABEL  fw_check_done


# ── 7. GOTO (unconditional) ───────────────────────────────────────────────────

GOTO  skip_print
LOG.PRINT  This line never executes
LABEL  skip_print


# ── 8. COUNTED LOOP — pulse reset line 3 times ───────────────────────────────

REPEAT  pulse  3
    GPIO.SET_HIGH  $RESET_PIN
    GPIO.DELAY_MS  50
    GPIO.SET_LOW   $RESET_PIN
    GPIO.DELAY_MS  50
END_REPEAT  pulse


# ── 9. COUNTED LOOP WITH INDEX + ARRAY ACCESS ────────────────────────────────
#    $board_idx is the loop index; $FW_IMAGES.$board_idx selects the path.
#    $PORTS.$board_idx selects the corresponding serial port.

board_idx  ?=  REPEAT  flash_boards  3
    LOG.PRINT  Flashing board $board_idx via $PORTS.$board_idx
    LOG.PRINT  Using image $FW_IMAGES.$board_idx
    LOG.PRINT  Tag: $BOARD_TAGS.$board_idx
    SERIAL.OPEN    $PORTS.$board_idx 115200
    UPDATER.FLASH  $FW_IMAGES.$board_idx
    SERIAL.CLOSE
END_REPEAT  flash_boards

# $board_idx is gone here.


# ── 10. CONDITIONAL LOOP (do-while) WITH INDEX ───────────────────────────────
#    $attempt counts retry iterations (0-based).
#    $flash_ok resolves to TRUE once verification passes.

flash_ok  ?=  UPDATER.VERIFY

attempt  ?=  REPEAT  flash_retry  UNTIL  $flash_ok
    LOG.PRINT  Flash attempt $attempt
    GPIO.SET_HIGH  $BOOT_PIN
    GPIO.SET_LOW   $RESET_PIN
    GPIO.DELAY_MS  100
    GPIO.SET_HIGH  $RESET_PIN

    UPDATER.FLASH  /opt/fw/board_A.bin

    GPIO.SET_LOW   $BOOT_PIN
    GPIO.SET_LOW   $RESET_PIN
    GPIO.DELAY_MS  50
    GPIO.SET_HIGH  $RESET_PIN

    flash_ok  ?=  UPDATER.VERIFY
END_REPEAT  flash_retry

LOG.PRINT  Firmware flashed after $attempt attempt(s)


# ── 11. NESTED LOOPS WITH ARRAY ACCESS, BREAK AND CONTINUE ───────────────────
#    Outer loop iterates banks by index ($b → $BANK_NAMES.$b).
#    Inner loop iterates slots.
#    CONTINUE inner_slot  — skip a bad slot.
#    CONTINUE outer_bank  — skip a whole unpowered bank.
#    BREAK    outer_bank  — stop once the first good sensor is activated.

b  ?=  REPEAT  outer_bank  $BANK_COUNT

    LOG.PRINT  Checking $BANK_NAMES.$b

    bank_ok  ?=  SENSOR.BANK_POWER_CHECK  $b
    IF  $bank_ok  GOTO  bank_powered
    LOG.PRINT  $BANK_NAMES.$b unpowered — skipping
    CONTINUE  outer_bank
    LABEL  bank_powered

    s  ?=  REPEAT  inner_slot  $SLOT_COUNT

        LOG.PRINT  Testing $BANK_NAMES.$b  slot $s

        present  ?=  SENSOR.IS_PRESENT  $b $s
        IF  $present  GOTO  slot_present
        CONTINUE  inner_slot
        LABEL  slot_present

        self_ok  ?=  SENSOR.RUN_SELFTEST  $b $s
        IF  $self_ok  GOTO  slot_passed
        LOG.PRINT  Slot $b/$s failed self-test
        CONTINUE  inner_slot
        LABEL  slot_passed

        LOG.PRINT  Activating sensor — $BANK_NAMES.$b  slot $s
        SENSOR.ACTIVATE  $b $s
        BREAK  outer_bank

    END_REPEAT  inner_slot

    SENSOR.COMMIT_BANK  $b

END_REPEAT  outer_bank

# $b and $s are both out of scope here.


# ── 12. POST-LOOP VARIABLE MACRO ─────────────────────────────────────────────

fw_after  ?=  UPDATER.GET_VERSION
LOG.PRINT    Post-update firmware: $fw_after


# ── 13. FINAL CONDITIONAL — verify update succeeded ──────────────────────────

IF  $fw_after  GOTO  update_ok
LOG.PRINT  Update verification failed
GOTO  abort

LABEL  update_ok
LOG.PRINT  Update successful
GOTO  cleanup

LABEL  abort
LOG.PRINT  Script aborted

LABEL  cleanup
SERIAL.CLOSE
LOG.PRINT  Done
```

