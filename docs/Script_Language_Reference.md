# Script Interpreter — Language Reference

---

## Table of Contents

1. [Comments](#1-comments)
2. [Plugin Loading](#2-plugin-loading)
3. [Constant Macros](#3-constant-macros)
4. [Variable Macros](#4-variable-macros)
5. [Commands](#5-commands)
6. [Conditional Flow — IF / GOTO / LABEL](#6-conditional-flow)
7. [Loops — REPEAT / END_REPEAT](#7-loops)
8. [Loop Index Capture](#8-loop-index-capture)
9. [BREAK and CONTINUE](#9-break-and-continue)
10. [Macro Resolution Order](#10-macro-resolution-order)
11. [Validation Rules](#11-validation-rules)
12. [Complete Example Script](#12-complete-example-script)

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

## 4. Variable Macros — `?=`

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

## 5. Commands — `PLUGIN.COMMAND`

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

## 6. Conditional Flow

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

## 7. Loops — `REPEAT` / `END_REPEAT`

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

## 8. Loop Index Capture — `?= REPEAT`

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
  (validated statically).

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

# Nested loops — independent indices
bank  ?=  REPEAT  outer  3
    ch  ?=  REPEAT  inner  8
        SENSOR.SET_CHANNEL  $bank $ch
    END_REPEAT  inner
    # $ch is gone here; $bank is still live
    SENSOR.COMMIT_BANK  $bank
END_REPEAT  outer
# both $bank and $ch are gone here
```

---

## 9. BREAK and CONTINUE

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
slot  ?=  REPEAT  scan  8

    present  ?=  SENSOR.IS_PRESENT  $slot
    IF  $present  GOTO  present_ok
    CONTINUE  scan            # slot absent — skip to END_REPEAT, loop back
    LABEL  present_ok

    ok  ?=  SENSOR.RUN_SELFTEST  $slot
    IF  $ok  GOTO  test_ok
    CONTINUE  scan            # test failed — skip to END_REPEAT, loop back
    LABEL  test_ok

    SENSOR.ACTIVATE  $slot
    BREAK  scan               # first good slot found — exit immediately

END_REPEAT  scan
```

### Nested BREAK / CONTINUE

`BREAK outer` from inside the inner loop exits both loops. All inner
`LoopState` entries are unwound automatically as the skip cursor passes
their `END_REPEAT` nodes.

```
bank  ?=  REPEAT  outer  3
    ch  ?=  REPEAT  inner  8

        ok  ?=  SENSOR.TEST  $bank $ch
        IF  $ok  GOTO  ch_ok
        CONTINUE  inner           # bad channel — try next
        LABEL  ch_ok

        SENSOR.ACTIVATE  $bank $ch
        BREAK  outer              # found one — exit both loops

    END_REPEAT  inner
END_REPEAT  outer
```

---

## 10. Macro Resolution Order

When `$name` is encountered during execution, the interpreter resolves it
through three tiers in priority order:

| Priority | Source | Scope |
|----------|--------|-------|
| 1 (highest) | Loop index macros — `mapLoopMacros` in each `LoopState`, innermost first | Loop body only; destroyed on `END_REPEAT` |
| 2 | Script-level variable macros — `?=` plugin command results | Entire script; last written value wins |
| 3 (lowest) | Shell macros — set via `executeCmd()` or the shell plugin | Script-wide |

Constant macros (`$NAME` from `:=`) are expanded at **validation time**
and are never seen by the runtime resolver.

---

## 11. Validation Rules

| Rule | Severity |
|------|----------|
| Duplicate constant macro name | Error |
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
| Nested block comment | Error |

---

## 12. Complete Example Script

The script below uses every language feature in a realistic context: a
firmware update sequence with sensor initialisation, retry logic, and
per-slot scanning.

```
# =============================================================================
# complete_example.script
# Demonstrates every supported language feature.
# =============================================================================

---
Pre-conditions:
  - Device is powered and connected via USB serial.
  - Firmware binary is present at the path below.
!--


# ── 1. PLUGIN LOADING ────────────────────────────────────────────────────────

LOAD_PLUGIN  SERIAL   >= v1.0.0.0
LOAD_PLUGIN  GPIO
LOAD_PLUGIN  UPDATER  == v3.1.0.0
LOAD_PLUGIN  SENSOR
LOAD_PLUGIN  LOG


# ── 2. CONSTANT MACROS ───────────────────────────────────────────────────────

DEVICE      := /dev/ttyUSB0
BAUD        := 115200
FW_FILE     := /opt/fw/firmware_v3.bin
RESET_PIN   := 17
BOOT_PIN    := 18
BANK_COUNT  := 3
SLOT_COUNT  := 8


# ── 3. OPEN CHANNEL ──────────────────────────────────────────────────────────

SERIAL.OPEN  $DEVICE $BAUD
LOG.PRINT    Channel opened on $DEVICE at $BAUD


# ── 4. VARIABLE MACRO — capture plugin return value ──────────────────────────

fw_current  ?=  UPDATER.GET_VERSION
LOG.PRINT    Current firmware: $fw_current


# ── 5. CONDITIONAL FLOW — IF / GOTO / LABEL ──────────────────────────────────

IF  $fw_current  GOTO  fw_check_done   # TRUE means version string is non-empty
LOG.PRINT  Could not read firmware version
GOTO  abort
LABEL  fw_check_done


# ── 6. GOTO (unconditional) ───────────────────────────────────────────────────

GOTO  skip_version_print
LOG.PRINT  This line never executes
LABEL  skip_version_print


# ── 7. COUNTED LOOP — pulse reset line 3 times ───────────────────────────────

REPEAT  pulse  3
    GPIO.SET_HIGH  $RESET_PIN
    GPIO.DELAY_MS  50
    GPIO.SET_LOW   $RESET_PIN
    GPIO.DELAY_MS  50
END_REPEAT  pulse


# ── 8. COUNTED LOOP WITH INDEX CAPTURE ───────────────────────────────────────
#    $flash_step holds "0", "1", "2" on successive iterations.
#    It ceases to exist after END_REPEAT flash_seq.

flash_step  ?=  REPEAT  flash_seq  3
    LOG.PRINT  Flash preparation step $flash_step
    UPDATER.PREPARE_STEP  $flash_step
END_REPEAT  flash_seq

# $flash_step is gone here.


# ── 9. CONDITIONAL LOOP (do-while) WITH INDEX — firmware flash with retry ────
#    $attempt counts how many flashes have been tried (0-based).
#    $flash_ok resolves to TRUE once verification passes.

flash_ok  ?=  UPDATER.VERIFY   # initial check before entering the loop

attempt  ?=  REPEAT  flash_retry  UNTIL  $flash_ok
    LOG.PRINT  Flash attempt $attempt
    GPIO.SET_HIGH  $BOOT_PIN
    GPIO.SET_LOW   $RESET_PIN
    GPIO.DELAY_MS  100
    GPIO.SET_HIGH  $RESET_PIN

    UPDATER.FLASH  $FW_FILE

    GPIO.SET_LOW   $BOOT_PIN
    GPIO.SET_LOW   $RESET_PIN
    GPIO.DELAY_MS  50
    GPIO.SET_HIGH  $RESET_PIN

    flash_ok  ?=  UPDATER.VERIFY
END_REPEAT  flash_retry

LOG.PRINT  Firmware flashed after $attempt attempt(s)


# ── 10. NESTED LOOPS WITH BREAK AND CONTINUE ─────────────────────────────────
#    Outer loop iterates banks; inner loop iterates slots.
#    CONTINUE inner  — skip a bad slot.
#    CONTINUE outer  — skip a whole unpowered bank.
#    BREAK    outer  — stop scanning once the first good sensor is activated.

bank  ?=  REPEAT  outer_bank  $BANK_COUNT

    LOG.PRINT  Checking bank $bank

    bank_ok  ?=  SENSOR.BANK_POWER_CHECK  $bank
    IF  $bank_ok  GOTO  bank_powered
    LOG.PRINT  Bank $bank unpowered — skipping
    CONTINUE  outer_bank
    LABEL  bank_powered

    ch  ?=  REPEAT  inner_slot  $SLOT_COUNT

        LOG.PRINT  Testing bank $bank  slot $ch

        present  ?=  SENSOR.IS_PRESENT  $bank $ch
        IF  $present  GOTO  slot_present
        CONTINUE  inner_slot       # absent — try next slot
        LABEL  slot_present

        self_ok  ?=  SENSOR.RUN_SELFTEST  $bank $ch
        IF  $self_ok  GOTO  slot_passed
        LOG.PRINT  Slot $bank/$ch failed self-test
        CONTINUE  inner_slot       # failed — try next slot
        LABEL  slot_passed

        LOG.PRINT  Activating sensor at bank $bank  slot $ch
        SENSOR.ACTIVATE  $bank $ch
        BREAK  outer_bank          # found a good sensor — exit both loops

    END_REPEAT  inner_slot

    SENSOR.COMMIT_BANK  $bank      # runs only if inner loop completes naturally

END_REPEAT  outer_bank

# $bank and $ch are both out of scope here.


# ── 11. POST-LOOP VARIABLE MACRO ─────────────────────────────────────────────

fw_after  ?=  UPDATER.GET_VERSION
LOG.PRINT    Post-update firmware: $fw_after


# ── 12. FINAL CONDITIONAL — verify update succeeded ──────────────────────────

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

---

*End of Language Reference*
