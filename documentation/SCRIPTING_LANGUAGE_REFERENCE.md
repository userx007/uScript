# Script Interpreter — Language Reference

---

## Table of Contents

1. [Comments](#1-comments)
2. [Plugin Loading](#2-plugin-loading)
3. [Constant Macros](#3-constant-macros)
4. [Array Macros](#4-array-macros)
5. [Variable Macros — Plugin Form](#5-variable-macros--plugin-form)
6. [Variable Macros — Direct Initialisation](#6-variable-macros--direct-initialisation)
7. [Commands](#7-commands)
8. [Native Statements](#8-native-statements)
    - [8.1 PRINT](#81-print)
    - [8.2 DELAY](#82-delay)
    - [8.3 FORMAT](#83-format)
    - [8.4 MATH](#84-math)
    - [8.5 BREAKPOINT](#85-breakpoint)
9. [Conditional Flow — IF / GOTO / LABEL](#9-conditional-flow)
10. [EVAL Expression Evaluator](#10-eval-expression-evaluator)
11. [Loops — REPEAT / END_REPEAT](#11-loops)
12. [Loop Index Capture](#12-loop-index-capture)
13. [BREAK and CONTINUE](#13-break-and-continue)
14. [Macro Resolution Order](#14-macro-resolution-order)
15. [Validation Rules](#15-validation-rules)
16. [Complete Example Script](#16-complete-example-script)

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
LOAD_PLUGIN  <n>
LOAD_PLUGIN  <n>  <op>  v<major>.<minor>.<patch>.<build>
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
<n>  :=  <value>
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
<n>  [=  <elem0>, <elem1>, <elem2>, ...
```

### Multi-line declaration

A line ending with `\` is joined to the following line. The `\` and any
surrounding whitespace are stripped; the two fragments are concatenated
directly.

```
<n>  [=  <elem0>, \
                <elem1>, \
                <elem2>
```

The line number recorded for error reporting is that of the **first** physical line.

### Elements with spaces

Whitespace within an element is preserved as-is; no quoting is required
unless the element itself contains a comma.

```
LABELS  [=  slot zero, slot one, slot two
```

### Elements containing commas

Any element that contains a comma must be enclosed in double-quotes. The
quotes are stripped from the stored value.

```
TAGS  [=  "alpha, beta", "gamma, delta", plain
```

### Element access

`$NAME.$indexmacro` is resolved as a single token: the expander first resolves
`$indexmacro` to an integer, then retrieves the element at that position.

```
PORTS  [=  /dev/ttyUSB0, /dev/ttyUSB1, /dev/ttyUSB2

i  ?=  REPEAT  open_ports  3
    SERIAL.OPEN  $PORTS.$i
END_REPEAT  open_ports
```

### Rules

- Array names must not conflict with constant macro names or with other
  array macro names (validated statically).
- Array macro names must not conflict with loop index macro names
  (validated statically).
- An out-of-bounds index leaves the `$NAME.$i` token unexpanded and logs
  an error; it does **not** abort execution.
- Arrays are a **declaration** — they occupy no position in the command stream
  and have no execution cost.

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
    UPDATER.FLASH  $FIRMWARE_PATHS.$board
END_REPEAT  flash_all
```

---

## 5. Variable Macros — Plugin Form

```
<n>  ?=  <PLUGIN>.<COMMAND>  [params]
```

- Captures the **return value** of a plugin command at **execution time**.
- Referenced as `$name` in subsequent commands.
- The most recently written value is used when the same name appears more than once.

```
fw_ver   ?=  UPDATER.GET_VERSION
dev_id   ?=  SENSOR.READ_ID

LOG.PRINT  version=$fw_ver  device=$dev_id
```

---

## 6. Variable Macros — Direct Initialisation

```
<n>  ?=  <value>
```

Sets a variable macro to a literal string value at **execution time**, with no
plugin involved. `$macros` in the value are expanded when the line executes, so
earlier results and loop indices are always reflected.

This form is recognised when the right-hand side is a plain string — not a
`PLUGIN.COMMAND` pattern and not one of the native keywords `MATH`, `FORMAT`,
`EVAL`, or `REPEAT`.

```
done     ?=  FALSE
status   ?=  pending
count    ?=  0
label    ?=  board_$index         # $index expanded at execution time
copy     ?=  $other_macro
element  ?=  $ARRAY.$0
```

**Constant macro vs. direct initialisation:**

| | Constant `:=` | Direct init `?=` |
|--|---------------|-----------------|
| Right-hand side | Literal string | Literal string (with `$macro` expansion) |
| Evaluated | Validation time | Execution time |
| Can change | No | Yes — assign again anywhere |
| Runtime cost | Zero | Minimal |

Use `:=` when the value is a fixed configuration constant. Use `?=` with a
literal when the assignment must happen at a specific point during execution —
inside a loop, after a condition, or to initialise a flag before a retry loop.

---

## 7. Commands — `PLUGIN.COMMAND`

```
<PLUGIN>.<COMMAND>  [params]
```

- `PLUGIN` and `COMMAND` must be fully upper-case.
- `params` is an optional free-form string; `$macros` inside it are expanded
  immediately before dispatch.
- Validated twice: dry-run (argument check), then real execution.
- A failed dispatch aborts the entire script immediately, regardless of nesting depth.

```
SERIAL.OPEN    /dev/ttyUSB0 115200
GPIO.SET_HIGH  PIN_RESET
GPIO.SET_LOW   PIN_RESET
SERIAL.CLOSE
```

---

## 8. Native Statements

Native statements are built into the interpreter. They require no plugin
declaration and produce no variable macro result unless otherwise noted.
They are silently skipped during the dry-run validation pass and inside
any active GOTO / BREAK / CONTINUE skip region.

### 8.1 PRINT

```
PRINT [text]
```

Prints `text` to the log at INFO level. `$macros` in the text are expanded at
execution time. A bare `PRINT` with no text outputs a blank line.

```
PRINT  Starting sequence...
PRINT  Firmware version: $fw_ver
PRINT
PRINT  Done.
```

### 8.2 DELAY

```
DELAY  <value>  <unit>
```

Pauses execution for the specified duration. `<value>` must be a positive integer
(≥ 1). `<unit>` is case-sensitive.

| Unit | Meaning |
|------|---------|
| `us` | microseconds |
| `ms` | milliseconds |
| `sec` | seconds |

```
DELAY  50   us
DELAY  300  ms
DELAY  2    sec
```

### 8.3 FORMAT

```
<n>  ?=  FORMAT  <input>  |  <pattern>
```

Tokenises `<input>` by whitespace into items `[0]`, `[1]`, `[2]`, … and
substitutes `%0`, `%1`, `%2`, … in `<pattern>` with the corresponding item.
Items may be reordered, repeated, or omitted. The result string is stored in
`<n>`.

Both `<input>` and `<pattern>` may contain `$macros` — expanded at execution
time. The `|` separator is mandatory.

```
out  ?=  FORMAT  Hello world Paris  |  Greetings from %2 to the %1 via %0
# out = "Greetings from Paris to the world via Hello"

# Build a version tag from separate variables
ver  ?=  FORMAT  $major $minor $patch  |  v%0.%1.%2

# Reorder columns in a report
row  ?=  FORMAT  $name $score $status  |  [%2] %0 scored %1
```

**Validation rules:**
- Both sides of `|` must be non-empty.
- The pattern must contain at least one `%N` placeholder (single decimal digit 0–9).
- Using `%N` where N ≥ number of input tokens is a runtime error.

### 8.4 MATH

```
<n>  ?=  MATH  <expression>
```

Evaluates `<expression>` as a floating-point arithmetic expression and stores the
result as a string in `<n>`. `$macros` are expanded at execution time.
Integer-valued results are stored without a decimal point (`5`, not `5.000000`).

**Operator precedence** (highest to lowest):

| Operators | Description |
|-----------|-------------|
| `**` | Power — right-associative |
| `+` `-` `!` `~` *(unary)* | Unary plus/minus, logical NOT, bitwise NOT |
| `*` `/` `//` `%` | Multiply, divide, floor-divide, modulo |
| `+` `-` | Add, subtract |
| `<<` `>>` | Bitwise shift |
| `<` `<=` `>` `>=` | Relational (return `1.0` or `0.0`) |
| `==` `!=` | Equality (return `1.0` or `0.0`) |
| `&` | Bitwise AND |
| `^` | Bitwise XOR |
| `\|` | Bitwise OR |
| `&&` | Logical AND |
| `\|\|` | Logical OR |
| `? :` | Ternary |
| `=` | Variable assignment (persists across MATH calls in the same script run) |

**Built-in constants:** `pi`  `e`  `tau`  `phi`  `inf`  `nan`

**Single-argument functions:**
`sin` `cos` `tan` `asin` `acos` `atan` `sinh` `cosh` `tanh`
`sqrt` `cbrt` `exp` `exp2` `log` `log2` `log10`
`abs` `ceil` `floor` `round` `trunc` `sign`

**Two-argument functions:**
`pow(b, e)` `atan2(y, x)` `min(a, b)` `max(a, b)` `hypot(a, b)` `fmod(a, b)` `log_b(v, base)`

```
r1  ?=  MATH  2 + 3                    # 5
r2  ?=  MATH  $x * $y + 1
r3  ?=  MATH  sqrt($val) + pi
r4  ?=  MATH  17 // 5                  # 3  (floor division)
r5  ?=  MATH  17 % 5                   # 2  (modulo)
r6  ?=  MATH  2 ** 10                  # 1024
r7  ?=  MATH  $score >= 80 ? 1 : 0    # ternary
r8  ?=  MATH  min($a, $b) + max($c, $d)
```

**Persisting Calculator variables** — `=` inside MATH assigns a named variable
that survives across subsequent MATH calls in the same script run:

```
counter  ?=  MATH  x = 0          # sets x=0 in the Calculator's variable map
counter  ?=  MATH  x = x + 1      # x is now 1; result also stored in $counter
```

### 8.5 BREAKPOINT

```
BREAKPOINT [label]
```

Suspends script execution and waits for a single keypress. The optional `label`
is printed in the log to identify which breakpoint fired. `$macros` in the label
are expanded at execution time.

| Key | Effect |
|-----|--------|
| `a` or `A` | Asks for abort confirmation — `y`/`Y` aborts the script, `n`/`N` continues |
| Any other key | Continues to the next command |

Aborting causes the script to fail at that point, exactly as if a plugin command
had returned an error.

```
BREAKPOINT
BREAKPOINT  before firmware flash
BREAKPOINT  loop iteration $i — value=$current_val
```

---

## 9. Conditional Flow

### `GOTO` — unconditional jump

```
GOTO  <label>
```

### `IF … GOTO` — conditional jump

```
IF  <condition>  GOTO  <label>
```

`<condition>` is either a **plain boolean expression** or an **EVAL expression**
(see [Section 10](#10-eval-expression-evaluator)).

### `LABEL` — jump target

```
LABEL  <label>
```

**Rules:**
- Jumps are **forward-only**: `GOTO` must precede its `LABEL` in the file.
- Every `GOTO` must have a matching `LABEL`; every `LABEL` must have a preceding
  `GOTO`. Both are validated statically.
- Duplicate label names are rejected.
- A `GOTO` must not cross a loop boundary (validated statically).

**Plain boolean expressions** support `TRUE`, `FALSE`, `!`, `&&`, `||`, `()`.
Variable macros used as conditions must resolve to `"TRUE"` or `"FALSE"` after
expansion.

```
# Unconditional skip
GOTO  skip_block
    PLUGIN.CMD_A            # never executes
LABEL  skip_block

# Conditional — plain boolean
status  ?=  SENSOR.READ_STATUS
IF  $status  GOTO  already_ok
SENSOR.RESET
LABEL  already_ok

# Conditional — EVAL typed comparison
fw  ?=  UPDATER.GET_VERSION
IF  EVAL  $fw >= 2.0.0 :VER  GOTO  fw_ok
UPDATER.FLASH  firmware.bin
GOTO  done
LABEL  fw_ok
PRINT  Already up to date: $fw
LABEL  done
```

---

## 10. EVAL Expression Evaluator

`EVAL` provides typed scalar comparisons with optional compound logic. It works
in three integration points: variable macro assignment, `IF … GOTO`, and
`REPEAT … UNTIL`.

### Syntax

```
EVAL  <atom>  [ && <atom>  || <atom> ... ]
```

An **atom** is one of:

```
<lhs> <op>[:<TYPE>]  <rhs>          # type hint inline on operator (no spaces)
<lhs> <op>           <rhs> :<TYPE>  # type hint as postfix token
<lhs> <op>           <rhs> : <TYPE> # type hint split across two tokens
TRUE | FALSE | !TRUE | !FALSE        # lone boolean literal
```

### Type hints

| Suffix | Type | Permitted operators |
|--------|------|---------------------|
| `:STR` | String (case-sensitive) | `EQ` `NE` `eq` `ne` `==` `!=` |
| `:NUM` | Floating-point number | `==` `!=` `<` `<=` `>` `>=` |
| `:VER` | Version string — `N.N[.N[.N]]` | `==` `!=` `<` `<=` `>` `>=` |
| `:BOOL` | Boolean | `==` `!=` |

When no type hint is present the type is **inferred** from the operand values:

| Priority | Condition | Inferred type |
|----------|-----------|---------------|
| 1 | Either operand is a boolean keyword (`TRUE`/`FALSE`/`yes`/`no`/`on`/`off`/`1`/`0`/`!TRUE`/`!FALSE`) | `BOOL` |
| 2 | Both operands are all-digit strings | `NUM` |
| 3 | Either operand matches canonical version form — `N.N[.N[.N]]` with ≤ 9 digits per component | `VER` |
| 4 | Everything else | `STR` |

> **Important:** Always use `:NUM` when comparing MATH floating-point results
> such as `3.14159`. A value like `3.14` would otherwise be inferred as `VER`
> (it matches the `N.N` version pattern) and produce incorrect comparisons.

### String operators

All string comparisons are **case-sensitive**. `EQ`/`eq`/`==` are
exact-equality synonyms; `NE`/`ne`/`!=` are exact-inequality synonyms. The
word-form operators `EQ`/`NE` are only valid with `:STR` type (explicit or
inferred) — they are **not** in the numeric rule table.

### Compound expressions

`&&` binds tighter than `||`, matching standard C precedence. Short-circuit
evaluation is applied.

```
EVAL  $a == $b  &&  $c != $d  ||  $e >= $f
# parsed as: ($a == $b && $c != $d) || ($e >= $f)
```

### Integration points

**Variable macro assignment:**
```
ok      ?=  EVAL  hello EQ hello :STR
ok      ?=  EVAL  $fw >= 2.0.0 :VER
is_big  ?=  EVAL  $x > 100 && $y > 100 :NUM
```
The stored result is always the string `"TRUE"` or `"FALSE"`.

**Conditional jump:**
```
IF  EVAL  $mode EQ active :STR           GOTO  run_active
IF  EVAL  $count >= 10 && $done EQ TRUE  GOTO  exit_loop
```

**Loop termination:**
```
REPEAT  poll    UNTIL  EVAL  $status EQ done :STR
REPEAT  count   UNTIL  EVAL  $ctr == 5 :NUM
REPEAT  search  UNTIL  EVAL  $found == TRUE && $retries < 3
```

### All three type-hint syntactic forms are equivalent

```
ok  ?=  EVAL  $x ==:NUM $y       # inline on operator
ok  ?=  EVAL  $x == $y :NUM      # postfix, one token
ok  ?=  EVAL  $x == $y : NUM     # postfix, two tokens
```

### Examples

```
# Type inference — unambiguous values need no hint
ok  ?=  EVAL  hello EQ hello           # STR inferred
ok  ?=  EVAL  42 == 42                 # NUM inferred (all digits)
ok  ?=  EVAL  1.2.3 < 2.0.0           # VER inferred
ok  ?=  EVAL  TRUE == TRUE             # BOOL inferred

# Explicit hints — required for floats and ambiguous short versions
ok  ?=  EVAL  $pi_approx > 3.14 :NUM  # "3.14" would be VER without hint
ok  ?=  EVAL  $fw < 2.0.0 :VER

# Compound
ok  ?=  EVAL  $a == $b && $c != $d
ok  ?=  EVAL  hello EQ hello :STR && 42 == 42 && 1.2.3 == 1.2.3 :VER

# MATH result piped into EVAL — always use :NUM for float results
approx  ?=  MATH  355 / 113
ok      ?=  EVAL  $approx > 3.14 :NUM
```

---

## 11. Loops — `REPEAT` / `END_REPEAT`

Two forms share the same keyword pair. The **label** is mandatory in both the
opening and closing line; it identifies which `END_REPEAT` closes which `REPEAT`
and has no relationship to `GOTO` labels.

### Counted loop

```
REPEAT  <label>  <N>
    …
END_REPEAT  <label>
```

- Body executes exactly `N` times (`N` ≥ 1).
- `N` may be a literal integer or a `$macro` that resolves to one.

```
REPEAT  pulse  3
    GPIO.SET_HIGH  17
    DELAY  50  ms
    GPIO.SET_LOW   17
    DELAY  50  ms
END_REPEAT  pulse
```

### Conditional loop (do-while)

```
REPEAT  <label>  UNTIL  <condition>
    …
END_REPEAT  <label>
```

- Body **always executes at least once**.
- `<condition>` is evaluated at `END_REPEAT` after each iteration.
- `<condition>` follows the same rules as `IF … GOTO` — plain boolean expression
  or an `EVAL` expression.

```
# Plain boolean condition
ready  ?=  FALSE
REPEAT  wait  UNTIL  $ready
    DELAY  100  ms
    ready  ?=  SENSOR.IS_READY
END_REPEAT  wait

# EVAL typed condition
ctr  ?=  0
REPEAT  count_up  UNTIL  EVAL  $ctr == 5 :NUM
    ctr  ?=  MATH  $ctr + 1
END_REPEAT  count_up
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

## 12. Loop Index Capture

An optional `varname ?=` prefix captures the **0-based iteration index** into a
variable macro scoped to the loop body.

```
<n>  ?=  REPEAT  <label>  <N>
<n>  ?=  REPEAT  <label>  UNTIL  <condition>
```

- `$name` holds `"0"` on the first iteration, `"1"` on the second, and so on.
- `$name` is **only visible inside the loop body** — it does not exist before
  the `REPEAT` line or after `END_REPEAT`.
- An inner loop's index macro shadows an outer one with the same name for the
  duration of the inner loop; the outer value is restored on exit.
- The macro name must not conflict with any existing script-level `?=` macro
  or any array macro name (validated statically).

```
# Counted loop with index
slot  ?=  REPEAT  flash  4
    PRINT  Flashing slot $slot
    SENSOR.CALIBRATE  $slot
END_REPEAT  flash
# $slot no longer exists here

# Conditional loop with counter
attempt  ?=  REPEAT  retry  UNTIL  $ok
    UPDATER.FLASH   firmware.bin
    ok  ?=  UPDATER.VERIFY
END_REPEAT  retry

# Nested loops — independent indices, both usable as array subscripts
BANKS     [=  bank_A, bank_B, bank_C
CHANNELS  [=  ch_0,   ch_1,   ch_2,   ch_3

bank  ?=  REPEAT  outer  3
    ch  ?=  REPEAT  inner  4
        SENSOR.CONFIGURE  $BANKS.$bank  $CHANNELS.$ch
    END_REPEAT  inner
    SENSOR.COMMIT_BANK  $BANKS.$bank
END_REPEAT  outer
```

---

## 13. BREAK and CONTINUE

Both keywords name the **target loop** explicitly, following the same convention
as Rust's labelled loops. This eliminates ambiguity in nested loops.

### `BREAK <label>`

Exits the named enclosing loop immediately. All loops between the current
innermost and the named target are also unwound. Execution resumes after
`END_REPEAT <label>`.

### `CONTINUE <label>`

Skips the remainder of the current loop body and resumes at `END_REPEAT` of the
named enclosing loop. The loop's normal exit-or-loop-back logic then runs as
usual (decrement counter or evaluate condition).

**Rules:**
- Both keywords must appear inside the named loop (validated statically).
- Neither can be used outside any loop.
- `GOTO` labels and loop labels are separate namespaces.

```
PORTS  [=  /dev/ttyUSB0, /dev/ttyUSB1, /dev/ttyUSB2, /dev/ttyUSB3

slot  ?=  REPEAT  scan  4

    present  ?=  SENSOR.IS_PRESENT  $PORTS.$slot
    IF  $present  GOTO  present_ok
    CONTINUE  scan
    LABEL  present_ok

    ok  ?=  SENSOR.RUN_SELFTEST  $PORTS.$slot
    IF  $ok  GOTO  test_ok
    CONTINUE  scan
    LABEL  test_ok

    SENSOR.ACTIVATE  $PORTS.$slot
    BREAK  scan

END_REPEAT  scan
```

### Nested BREAK / CONTINUE

`BREAK outer` from inside the inner loop exits both loops:

```
BANKS  [=  bank_A, bank_B, bank_C

bank  ?=  REPEAT  outer  3
    ch  ?=  REPEAT  inner  8

        ok  ?=  SENSOR.TEST  $BANKS.$bank $ch
        IF  $ok  GOTO  ch_ok
        CONTINUE  inner
        LABEL  ch_ok

        SENSOR.ACTIVATE  $BANKS.$bank $ch
        BREAK  outer              # exits both loops

    END_REPEAT  inner
END_REPEAT  outer
```

---

## 14. Macro Resolution Order

### Plain `$name`

| Priority | Source | Scope |
|----------|--------|-------|
| 1 (highest) | Loop index macros — innermost active loop first | Loop body only; destroyed on `END_REPEAT` |
| 2 | Script-level variable macros — `?=` plugin results, direct initialisations, MATH results, FORMAT results | Entire script; last written value wins |
| 3 (lowest) | Shell macros — set via `executeCmd()` | Script-wide |

### Array element `$NAME.$index`

1. `NAME` is looked up in `mapArrayMacros`.
2. `index` is resolved through the plain `$name` chain above.
3. The resolved index string is converted to an unsigned integer and used to
   subscript the element vector.
4. If `NAME` is not an array, it is resolved as a plain macro and `.$index` is
   re-emitted literally.

Constant macros (`:=`) are expanded at **validation time** and are never seen
by the runtime resolver.

---

## 15. Validation Rules

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
| Backward `GOTO` (label before GOTO in file) | Error |
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
| `DELAY` value is zero or non-numeric | Error |
| `DELAY` unit is not `us`, `ms`, or `sec` | Error |
| `FORMAT` missing `\|` separator | Error |
| `FORMAT` pattern contains no `%N` placeholder | Error |
| `FORMAT` `%N` index is not a single decimal digit (0–9) | Error |
| `FORMAT` or `MATH` destination name conflicts with a constant macro | Error |
| `MATH` expression template is empty | Error |
| Nested block comment | Error |

---

## 16. Complete Example Script

The script below uses every language feature: plugin loading, constant and array
macros, direct variable initialisation, native PRINT / DELAY / MATH / FORMAT /
BREAKPOINT, EVAL typed comparisons in IF and REPEAT UNTIL, counted and
conditional loops with index capture, array access, and nested BREAK / CONTINUE.

```
# =============================================================================
# complete_example.script — every language feature in one realistic script
# =============================================================================

---
Pre-conditions:
  Device is powered and connected via USB serial.
  Firmware binaries are present at the paths listed below.
!--


# ── 1. PLUGIN LOADING ────────────────────────────────────────────────────────

LOAD_PLUGIN  SERIAL   >= v1.0.0.0
LOAD_PLUGIN  GPIO
LOAD_PLUGIN  UPDATER  == v3.1.0.0
LOAD_PLUGIN  SENSOR


# ── 2. CONSTANT MACROS ───────────────────────────────────────────────────────

RESET_PIN   := 17
BOOT_PIN    := 18
BANK_COUNT  := 3
SLOT_COUNT  := 8
TARGET_VER  := 3.1.0


# ── 3. ARRAY MACROS ──────────────────────────────────────────────────────────

PORTS       [=  /dev/ttyUSB0, /dev/ttyUSB1, /dev/ttyUSB2

FW_IMAGES   [=  /opt/fw/board_A.bin, \
                /opt/fw/board_B.bin, \
                /opt/fw/board_C.bin

BANK_NAMES  [=  bank alpha, bank beta, bank gamma
BOARD_TAGS  [=  "rev3, production", "rev2, prototype", "rev3, production"


# ── 4. NATIVE PRINT ──────────────────────────────────────────────────────────

PRINT  Starting firmware update sequence
PRINT  Target version: $TARGET_VER
PRINT


# ── 5. NATIVE DELAY ──────────────────────────────────────────────────────────

DELAY  500  ms


# ── 6. VARIABLE MACRO — plugin result ────────────────────────────────────────

fw_current  ?=  UPDATER.GET_VERSION
PRINT  Current firmware: $fw_current


# ── 7. DIRECT VARIABLE INITIALISATION ────────────────────────────────────────

attempt_count  ?=  0
last_result    ?=  none


# ── 8. EVAL IN IF — typed string comparison ───────────────────────────────────

IF  EVAL  $fw_current EQ $TARGET_VER :STR  GOTO  already_current
PRINT  Update required — proceeding.
GOTO  do_update
LABEL  already_current
PRINT  Firmware already at target — skipping flash.
GOTO  skip_flash
LABEL  do_update


# ── 9. COUNTED LOOP WITH INDEX + ARRAY ACCESS ────────────────────────────────

board_idx  ?=  REPEAT  flash_boards  3
    PRINT  Flashing board $board_idx  tag: $BOARD_TAGS.$board_idx
    SERIAL.OPEN    $PORTS.$board_idx 115200
    UPDATER.FLASH  $FW_IMAGES.$board_idx
    SERIAL.CLOSE
END_REPEAT  flash_boards

# $board_idx is gone here


# ── 10. NATIVE MATH ──────────────────────────────────────────────────────────

total_boards  ?=  MATH  3
flashed_ok    ?=  MATH  $total_boards - 0
pct_done      ?=  MATH  $flashed_ok * 100 // $total_boards
PRINT  Progress: $pct_done%


# ── 11. NATIVE FORMAT — build a result string ─────────────────────────────────

summary  ?=  FORMAT  $flashed_ok $total_boards $pct_done  |  Flashed %0 of %1 boards (%2%)
PRINT  $summary


# ── 12. CONDITIONAL LOOP WITH EVAL UNTIL ─────────────────────────────────────

flash_ok  ?=  FALSE
retries   ?=  0

REPEAT  verify_loop  UNTIL  EVAL  $flash_ok == TRUE || $retries >= 3 :NUM
    flash_ok  ?=  UPDATER.VERIFY
    retries   ?=  MATH  $retries + 1
    PRINT  Verify attempt $retries: $flash_ok
END_REPEAT  verify_loop

IF  EVAL  $flash_ok == TRUE  GOTO  verify_passed
PRINT  Verification failed after $retries attempts
GOTO  abort
LABEL  verify_passed


# ── 13. BREAKPOINT — interactive pause ───────────────────────────────────────

BREAKPOINT  post-flash checkpoint — press any key to continue


# ── 14. NESTED LOOPS WITH BREAK AND CONTINUE ─────────────────────────────────

b  ?=  REPEAT  outer_bank  $BANK_COUNT

    bank_ok  ?=  SENSOR.BANK_POWER_CHECK  $b
    IF  EVAL  $bank_ok == TRUE  GOTO  bank_powered
    PRINT  $BANK_NAMES.$b unpowered — skipping
    CONTINUE  outer_bank
    LABEL  bank_powered

    s  ?=  REPEAT  inner_slot  $SLOT_COUNT

        present  ?=  SENSOR.IS_PRESENT  $b $s
        IF  EVAL  $present == TRUE  GOTO  slot_present
        CONTINUE  inner_slot
        LABEL  slot_present

        self_ok  ?=  SENSOR.RUN_SELFTEST  $b $s
        IF  EVAL  $self_ok == TRUE  GOTO  slot_passed
        PRINT  Slot $b/$s failed self-test
        CONTINUE  inner_slot
        LABEL  slot_passed

        PRINT  Activating — $BANK_NAMES.$b  slot $s
        SENSOR.ACTIVATE  $b $s
        BREAK  outer_bank

    END_REPEAT  inner_slot
    SENSOR.COMMIT_BANK  $b

END_REPEAT  outer_bank

# $b and $s are both out of scope here


# ── 15. LABEL skip_flash (target for early-exit path above) ──────────────────

LABEL  skip_flash


# ── 16. FINAL EVAL CHECK ─────────────────────────────────────────────────────

fw_after    ?=  UPDATER.GET_VERSION
up_to_date  ?=  EVAL  $fw_after EQ $TARGET_VER :STR
PRINT  Final firmware: $fw_after   up-to-date: $up_to_date

IF  EVAL  $up_to_date == TRUE  GOTO  update_ok
PRINT  Update verification failed
GOTO  abort

LABEL  update_ok
PRINT  Update successful
GOTO  cleanup

LABEL  abort
PRINT  Script aborted — check logs

LABEL  cleanup
SERIAL.CLOSE
PRINT  Done.
```
