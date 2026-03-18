# Script Interpreter — Learning Tutorial

> **From zero to full featured — step by step**

---

## Table of Contents

| Step | Concept |
|------|---------|
| [Step 1](#step-1--your-first-script) | Your first script — plugins and commands |
| [Step 2](#step-2--native-print-and-delay) | Native PRINT and DELAY |
| [Step 3](#step-3--constant-macros--) | Constant macros `:=` |
| [Step 4](#step-4--variable-macros--plugin-form) | Variable macros `?=` — plugin form |
| [Step 5](#step-5--direct-variable-initialisation) | Direct variable initialisation |
| [Step 6](#step-6--asserting-conditions) | Asserting conditions |
| [Step 7](#step-7--conditional-flow) | Conditional flow — IF / GOTO / LABEL |
| [Step 8](#step-8--counted-loops) | Counted loops — REPEAT / END_REPEAT |
| [Step 9](#step-9--loop-index-capture) | Loop index capture — `?= REPEAT` |
| [Step 10](#step-10--conditional-loop--until) | Conditional loop — REPEAT UNTIL |
| [Step 11](#step-11--break-and-continue) | BREAK and CONTINUE |
| [Step 12](#step-12--array-macros--) | Array macros `[=` |
| [Step 13](#step-13--native-math) | Native MATH |
| [Step 14](#step-14--native-format) | Native FORMAT |
| [Step 15](#step-15--eval-expressions) | EVAL expressions |
| [Step 16](#step-16--breakpoint) | BREAKPOINT |
| [Final](#comprehensive-example) | Comprehensive example |

---

## Step 1 — Your First Script

> **Plugins, commands, and the variable macro — the three building blocks.**

A real script loads one or more plugins and calls their commands. A command
may return a value; prefix the command with `varname ?=` to capture it.

```
# step_01_hello.script

LOAD_PLUGIN  UART

UART.OPEN    /dev/ttyUSB0  115200
UART.WRITE   Hello from the script engine
fw_ver  ?=   UART.READ_LINE
UART.CLOSE

PRINT  Received: $fw_ver
```

**What happens line by line:**
- `LOAD_PLUGIN UART` loads the UART shared library and registers its commands.
- `UART.OPEN` opens the serial port — parameters are passed as a free-form string.
- `UART.WRITE` sends a line.
- `fw_ver ?= UART.READ_LINE` reads the response and stores it in `$fw_ver`.
- `UART.CLOSE` closes the port.
- `PRINT` is a native statement — no plugin needed.

> Every plugin used by any command must be declared with `LOAD_PLUGIN` before
> that command appears. Undeclared plugins are caught at validation time, before
> execution starts. A failed command aborts the script immediately.

**Syntax rules for plugin commands:**
- `PLUGIN` and `COMMAND` must both be fully upper-case.
- Parameters after the command name are a free-form string; `$macros` are
  expanded immediately before the command runs.
- The same plugin can provide any number of commands.

---

## Step 2 — Native PRINT and DELAY

> **Output and timing — built into the interpreter, no plugin needed.**

### PRINT

```
PRINT [text]
```

Prints text to the log. `$macros` are expanded at execution time. A bare
`PRINT` with no text outputs a blank line.

```
# step_02a_print.script

version  ?=  2.1.0

PRINT  Starting sequence...
PRINT  Running version $version
PRINT
PRINT  Done.
```

`PRINT` needs no `LOAD_PLUGIN` and works in every script, including those that
load no plugins at all.

### DELAY

```
DELAY  <value>  <unit>
```

Pauses execution. The unit must be one of `us` (microseconds), `ms`
(milliseconds), or `sec` (seconds). The value must be a positive integer.

```
# step_02b_delay.script

PRINT  Starting...
DELAY  1    sec
PRINT  One second later.
DELAY  500  ms
PRINT  Half a second later.
DELAY  200  us
PRINT  200 microseconds later.
```

Both `PRINT` and `DELAY` work entirely independently of any plugin. No
`LOAD_PLUGIN` line is required for them.

---

## Step 3 — Constant Macros `:=`

> **Name your values once, use them everywhere.**

A constant macro assigns a name to a literal value. It is expanded at
**validation time** — before execution — so there is zero runtime cost.

```
# step_03_cmacros.script

LOAD_PLUGIN  UART

PORT       := /dev/ttyUSB0
BAUD       := 115200
WAIT_MS    := 500
TARGET_VER := 2.0.0

PRINT  Connecting to $PORT at $BAUD baud
UART.OPEN  $PORT  $BAUD

PRINT  Waiting $WAIT_MS ms...
DELAY  $WAIT_MS  ms

PRINT  Target version: $TARGET_VER
UART.CLOSE
```

**Rules:**
- Name must start with a letter or underscore, followed by letters, digits,
  or underscores.
- Value is everything after `:=`, trimmed.
- Cannot be declared twice — the validator rejects duplicates.
- Use `$NAME` anywhere after the declaration to expand the value.

> **Convention:** Constant macro names are typically `UPPER_CASE`, but any
> valid identifier is accepted.

---

## Step 4 — Variable Macros `?=` — Plugin Form

> **Capture the return value of a command.**

```
<n>  ?=  <PLUGIN>.<COMMAND>  [params]
```

The value is set at **runtime** and can change each time the command runs.

```
# step_04_vmacros.script

LOAD_PLUGIN  UART

PORT  := /dev/ttyUSB0
BAUD  := 115200

UART.OPEN  $PORT  $BAUD

fw_ver   ?=  UART.READ_LINE          # read firmware version string
board_id ?=  UART.READ_LINE          # read board identity

PRINT  Firmware : $fw_ver
PRINT  Board    : $board_id

# Re-read after a reset — the macro picks up the new value
UART.WRITE  RESET
DELAY  200  ms
fw_ver  ?=  UART.READ_LINE
PRINT  Post-reset firmware: $fw_ver

UART.CLOSE
```

| | Constant `:=` | Variable `?=` plugin form |
|--|---------------|--------------------------|
| Right-hand side | Literal value | Plugin command |
| Evaluated | Validation time | Execution time |
| Can be reassigned | No | Yes |

---

## Step 5 — Direct Variable Initialisation

> **Set a variable to a literal string at execution time — no plugin needed.**

When the right-hand side of `?=` is a plain string — not a `PLUGIN.COMMAND`
and not a native keyword — it is a **direct initialisation**:

```
<n>  ?=  <value>
```

`$macros` in the value are expanded when the line executes, so earlier results
and loop indices are always current.

```
# step_05_varinit.script

# Initialise flags and counters before use
done     ?=  FALSE
status   ?=  pending
retries  ?=  0

PRINT  done=$done  status=$status  retries=$retries

# Update at runtime — same syntax, new value
status  ?=  running
PRINT  New status: $status

# Copy another macro's value
last_status  ?=  $status
PRINT  Copied: $last_status

# Combine with a constant
PREFIX    := board
label     ?=  $PREFIX-001
PRINT  Label: $label
```

**When to use `:=` vs `?=` with a literal:**

| | Constant `:=` | Direct init `?=` |
|--|---------------|-----------------|
| Evaluated | Validation time | Execution time |
| Can change | No | Yes |
| Use for | Fixed configuration | Runtime flags, counters, loop state |

---

## Step 6 — Asserting Conditions

> **Abort the script if a value is not what you expect.**

There is no dedicated "assert" command — the pattern is to evaluate a condition
with `EVAL`, test the result with `IF`, and `GOTO` an abort label if it fails.
This is explicit and readable.

```
# step_06_assert.script

LOAD_PLUGIN  UART

PORT  := /dev/ttyUSB0
EXPECTED_VER  := 2.0.0

UART.OPEN  $PORT  115200
fw_ver  ?=  UART.READ_LINE
UART.CLOSE

PRINT  Reported firmware: $fw_ver

# Assert: firmware must be >= EXPECTED_VER
IF  EVAL  $fw_ver >= $EXPECTED_VER :VER  GOTO  ver_ok
PRINT  ERROR: firmware $fw_ver is below required $EXPECTED_VER
GOTO  abort
LABEL  ver_ok

PRINT  Version check passed.

# Assert: firmware string must not be empty
empty  ?=  EVAL  $fw_ver EQ  :STR
IF  EVAL  $empty == FALSE  GOTO  not_empty
PRINT  ERROR: empty firmware version string
GOTO  abort
LABEL  not_empty

PRINT  All assertions passed — continuing.
GOTO  done

LABEL  abort
PRINT  Aborting script due to assertion failure.
# Script ends here — no further commands execute after GOTO done skips them

LABEL  done
PRINT  Done.
```

**The pattern:**
1. Evaluate the condition with `EVAL`, store in a variable or use it inline.
2. `IF` the condition is met `GOTO` the success label — the failure path falls
   through naturally.
3. At the end of the failure path, `GOTO abort` (a label near the end of the
   script that handles clean shutdown).

> This is more explicit than a one-liner "assert" but gives full control over
> the error message and cleanup actions before the script stops.

---

## Step 7 — Conditional Flow

> **Skip sections based on a condition.**

The `IF` / `GOTO` / `LABEL` construct allows sections of a script to be
skipped. Jumps are always **forward-only**.

```
# step_07_conditions.script

LOAD_PLUGIN  UART

UART.OPEN  /dev/ttyUSB0  115200
status  ?=  UART.READ_LINE
UART.CLOSE

# IF $status holds "TRUE", jump over the error block
IF  $status  GOTO  all_good
    PRINT  Something went wrong — status: $status
    GOTO  done
LABEL  all_good

PRINT  Status OK, continuing.
LABEL  done
```

**Typed comparison with EVAL:**

```
LOAD_PLUGIN  UART

PORT  := /dev/ttyUSB0

UART.OPEN  $PORT  115200
a  ?=  UART.READ_LINE     # e.g. "10"
b  ?=  UART.READ_LINE     # e.g. "20"
UART.CLOSE

# Compute and compare
result  ?=  EVAL  $a < $b :NUM
PRINT  Is $a less than $b? $result

IF  $result  GOTO  a_is_smaller
    PRINT  $a is NOT smaller than $b
    GOTO  comparison_done
LABEL  a_is_smaller
PRINT  $a IS smaller than $b
LABEL  comparison_done
```

**Rules:**
- `GOTO` without `IF` is an unconditional jump — always taken.
- Every `GOTO` must have a matching `LABEL`; every `LABEL` must have a preceding `GOTO`.
- Duplicate label names are rejected.
- A `GOTO` must not cross a loop boundary.

> **Plain boolean expressions** support: `TRUE`  `FALSE`  `!`  `&&`  `||`  `()`  
> Variable macros that hold `"TRUE"` or `"FALSE"` can be used directly.  
> For typed comparisons (numbers, versions, strings) use `EVAL` — see Step 15.

---

## Step 8 — Counted Loops

> **Repeat a block of commands N times.**

```
REPEAT  <label>  <N>
    …
END_REPEAT  <label>
```

The label is mandatory and must be unique across all loops. N must be a
positive integer.

```
# step_08_repeat.script

LOAD_PLUGIN  UART

UART.OPEN  /dev/ttyUSB0  115200

# Send a reset pulse 3 times
REPEAT  pulse  3
    PRINT  Pulsing reset...
    UART.WRITE  RESET
    DELAY  100  ms
END_REPEAT  pulse

UART.CLOSE
PRINT  Reset pulses done.
```

**Nested loops:**

```
REPEAT  outer  3
    PRINT  Outer iteration
    REPEAT  inner  2
        PRINT  -- Inner iteration
    END_REPEAT  inner
END_REPEAT  outer
```

Loops may be nested to any depth. Loop labels and `GOTO` labels are
**separate namespaces** — no conflicts.

---

## Step 9 — Loop Index Capture

> **Know which iteration you are on.**

Place `varname ?=` in front of `REPEAT` to receive the current **0-based
iteration index**. The macro is only visible inside the loop body.

```
# step_09_index.script

i  ?=  REPEAT  count  5
    PRINT  Iteration $i
END_REPEAT  count

# $i is out of scope here
PRINT  Loop finished.
```

Output: `Iteration 0` … `Iteration 4`, then `Loop finished.`

**Using the index for arithmetic:**

```
i  ?=  REPEAT  math_demo  4
    result  ?=  MATH  $i + 10
    PRINT  $i + 10 = $result
END_REPEAT  math_demo
```

> The index macro name must not conflict with any existing script-level variable
> macro. The validator catches this statically.

---

## Step 10 — Conditional Loop — UNTIL

> **Loop until a condition becomes TRUE.**

```
REPEAT  <label>  UNTIL  <condition>
    …
END_REPEAT  <label>
```

The body **always executes at least once** (do-while semantics). The condition
is evaluated at `END_REPEAT` after each iteration.

```
# step_10_until.script

LOAD_PLUGIN  UART

UART.OPEN  /dev/ttyUSB0  115200

# Poll until the device reports ready
attempt  ?=  REPEAT  poll_loop  UNTIL  $device_ready
    UART.WRITE   STATUS?
    device_ready  ?=  UART.READ_LINE    # returns "TRUE" or "FALSE"
    PRINT  Attempt $attempt: ready=$device_ready
    DELAY  100  ms
END_REPEAT  poll_loop

PRINT  Device ready after $attempt attempt(s).
UART.CLOSE
```

- The condition is evaluated **after** each iteration.
- The loop exits when the condition is `TRUE`.
- For typed numeric or version comparisons, use `EVAL` in the condition — see Step 15.

> ⚠️ Always ensure the condition can eventually become `TRUE`, or the loop will
> run forever.

---

## Step 11 — BREAK and CONTINUE

> **Exit a loop early or skip the rest of an iteration.**

Both keywords name the **target loop** explicitly — no ambiguity in nested loops.

### CONTINUE — skip the rest of this iteration

```
# step_11a_continue.script

LOAD_PLUGIN  UART
UART.OPEN  /dev/ttyUSB0  115200

i  ?=  REPEAT  scan  5
    UART.WRITE   PROBE $i
    result  ?=  UART.READ_LINE    # "OK" or "FAIL"

    is_fail  ?=  EVAL  $result EQ FAIL :STR
    IF  $is_fail  GOTO  skip_item
    PRINT  Slot $i: $result
    GOTO  after_skip
    LABEL  skip_item
    PRINT  Slot $i: skipped (FAIL)
    CONTINUE  scan
    LABEL  after_skip
END_REPEAT  scan

UART.CLOSE
```

### BREAK — exit the loop immediately

```
# step_11b_break.script

LOAD_PLUGIN  UART
UART.OPEN  /dev/ttyUSB0  115200

i  ?=  REPEAT  search  10
    UART.WRITE   PROBE $i
    found  ?=  UART.READ_LINE    # "TRUE" when target located

    IF  $found  GOTO  found_it
    GOTO  not_yet
    LABEL  found_it
    PRINT  Target found at slot $i
    BREAK  search
    LABEL  not_yet
END_REPEAT  search

UART.CLOSE
PRINT  Search complete.
```

**Nested BREAK** — `BREAK outer` from inside the inner loop exits **both** loops:

```
LOAD_PLUGIN  UART
UART.OPEN  /dev/ttyUSB0  115200

bank  ?=  REPEAT  outer  3
    ch  ?=  REPEAT  inner  8
        UART.WRITE   PROBE $bank $ch
        ok  ?=  UART.READ_LINE

        IF  $ok  GOTO  found
        GOTO  keep_looking
        LABEL  found
        PRINT  Found at bank=$bank ch=$ch
        BREAK  outer          # exits both loops
        LABEL  keep_looking
    END_REPEAT  inner
END_REPEAT  outer

UART.CLOSE
```

> `BREAK` and `CONTINUE` must name an enclosing loop label. A non-enclosing or
> non-existent label is caught by the validator.

---

## Step 12 — Array Macros `[=`

> **Ordered lists of values.**

```
<n>  [=  elem0, elem1, elem2, ...
```

Individual elements are accessed with `$NAME.$index`.

```
# step_12_arrays.script

LOAD_PLUGIN  UART

PORTS  [=  /dev/ttyUSB0, /dev/ttyUSB1, /dev/ttyUSB2

# Access a specific element directly
PRINT  First port: $PORTS.$0

# Combine with loop index — the natural pairing
i  ?=  REPEAT  port_scan  3
    PRINT  Scanning $PORTS.$i
    UART.OPEN   $PORTS.$i  115200
    result  ?=  UART.READ_LINE
    PRINT  $PORTS.$i replied: $result
    UART.CLOSE
END_REPEAT  port_scan
```

**Multi-line** with `\` continuation:

```
FIRMWARE_IMAGES  [=  /opt/fw/board_A.bin, \
                     /opt/fw/board_B.bin, \
                     /opt/fw/board_C.bin

i  ?=  REPEAT  flash  3
    PRINT  Flashing image $i: $FIRMWARE_IMAGES.$i
END_REPEAT  flash
```

**Elements with internal spaces** — no quoting needed:

```
SLOT_NAMES  [=  slot zero, slot one, slot two
```

**Elements containing commas** — must be quoted with `"..."`:

```
TAGS  [=  "rev3, production",  "rev2, prototype",  plain

PRINT  Tag 0: $TAGS.$0
PRINT  Tag 1: $TAGS.$1
PRINT  Tag 2: $TAGS.$2
```

> Array macro names must not conflict with constant macro names or loop index
> macro names. The validator checks this at load time.

---

## Step 13 — Native MATH

> **Floating-point arithmetic — built into the interpreter, no plugin needed.**

```
<n>  ?=  MATH  <expression>
```

`$macros` in the expression are expanded at execution time. Integer-valued
results are stored without a decimal point.

```
# step_13_math.script

a  ?=  5
b  ?=  3

sum    ?=  MATH  $a + $b          # 8
diff   ?=  MATH  $a - $b          # 2
prod   ?=  MATH  $a * $b          # 15
quot   ?=  MATH  $a / $b          # 1.66666666666667
floor  ?=  MATH  $a // $b         # 1  (floor division)
mod    ?=  MATH  $a % $b          # 2  (modulo)
power  ?=  MATH  $a ** $b         # 125  (5³)

PRINT  $a + $b = $sum
PRINT  $a - $b = $diff
PRINT  $a * $b = $prod
PRINT  $a / $b = $quot
PRINT  $a // $b = $floor
PRINT  $a % $b = $mod
PRINT  $a ** $b = $power
```

**Built-in functions and constants:**

```
root   ?=  MATH  sqrt(144)           # 12
absv   ?=  MATH  abs(-7.5)           # 7.5
big    ?=  MATH  max(10, 3)          # 10
pi_v   ?=  MATH  pi                  # 3.14159265358979...
tau_v  ?=  MATH  tau                 # 6.28318530717959...
```

**Comparison and ternary inside MATH:**

```
# Returns 1.0 (true) or 0.0 (false)
flag    ?=  MATH  $a > $b

# Ternary: choose a value based on a condition
result  ?=  MATH  $score >= 80 ? 100 : 0
```

**Persisting Calculator variables — survive across MATH calls in the same script run:**

```
counter  ?=  MATH  x = 0           # x = 0 in Calculator's variable map
counter  ?=  MATH  x = x + 1       # x is now 1; result stored in $counter
PRINT  counter=$counter
```

---

## Step 14 — Native FORMAT

> **Build strings by placing whitespace-separated values into a pattern.**

```
<n>  ?=  FORMAT  <input>  |  <pattern>
```

`<input>` is tokenised by whitespace into `%0`, `%1`, `%2`, …  
`<pattern>` substitutes those tokens anywhere, in any order.

```
# step_14_format.script

# Reorder three values
out  ?=  FORMAT  Hello world Paris  |  Greetings from %2 to the %1 via %0
PRINT  $out
# Output: Greetings from Paris to the world via Hello

# Build a version string from separate variables
major  ?=  2
minor  ?=  1
patch  ?=  0
ver    ?=  FORMAT  $major $minor $patch  |  v%0.%1.%2
PRINT  Version: $ver
# Output: Version: v2.1.0

# Repeat a token
note  ?=  FORMAT  WARNING  |  %0 %0 %0: check required
PRINT  $note
# Output: WARNING WARNING WARNING: check required

# Build a structured log line from runtime values
LOAD_PLUGIN  UART
UART.OPEN  /dev/ttyUSB0  115200
board_id  ?=  UART.READ_LINE
fw_ver    ?=  UART.READ_LINE
UART.CLOSE

log_line  ?=  FORMAT  $board_id $fw_ver active  |  [%2] board=%0 fw=%1
PRINT  $log_line
```

**Rules:**
- The `|` separator is mandatory.
- The pattern must contain at least one `%N` placeholder (single decimal digit 0–9).
- Items may be skipped, repeated, or placed in any order.
- An index `%N` where N ≥ number of input tokens is a runtime error.

---

## Step 15 — EVAL Expressions

> **Typed comparisons — string, number, version, boolean.**

`EVAL` is a native expression evaluator that works without any plugin. It can
be used in three places:

```
name  ?=  EVAL  <expression>              # stores "TRUE" or "FALSE"
IF  EVAL  <expression>  GOTO  label       # conditional jump
REPEAT  loop  UNTIL  EVAL  <expression>   # loop termination
```

### Basic usage

```
# step_15a_eval_basic.script

# String comparison (case-sensitive)
ok  ?=  EVAL  hello EQ hello :STR
PRINT  exact match: $ok              # TRUE

ok  ?=  EVAL  hello EQ Hello :STR
PRINT  case mismatch: $ok            # FALSE

# Numeric comparison
ok  ?=  EVAL  42 > 10 :NUM
PRINT  42 > 10: $ok                  # TRUE

# Version comparison
ok  ?=  EVAL  1.2.3 < 2.0.0 :VER
PRINT  version check: $ok            # TRUE

# Boolean comparison
ok  ?=  EVAL  TRUE == TRUE :BOOL
PRINT  bool check: $ok               # TRUE
```

### Type inference — no hint needed for unambiguous values

```
ok  ?=  EVAL  hello EQ hello          # STR inferred (plain text)
ok  ?=  EVAL  42 == 42                # NUM inferred (all digits)
ok  ?=  EVAL  1.2.3 == 1.2.3         # VER inferred (N.N.N pattern)
ok  ?=  EVAL  TRUE == TRUE            # BOOL inferred
```

> **Important:** Always use `:NUM` when comparing MATH floating-point results.
> A value like `3.14` is inferred as `VER` (matches the `N.N` pattern), which
> gives wrong results for arithmetic comparisons.

```
pi_approx  ?=  MATH  355 / 113
ok  ?=  EVAL  $pi_approx > 3.14 :NUM    # CORRECT — explicit :NUM
PRINT  pi > 3.14: $ok
```

### All three type-hint syntactic forms are equivalent

```
ok  ?=  EVAL  $x ==:NUM $y       # inline on operator
ok  ?=  EVAL  $x == $y :NUM      # postfix, one token
ok  ?=  EVAL  $x == $y : NUM     # postfix, two tokens
```

### Compound expressions

`&&` binds tighter than `||`, matching standard C precedence:

```
a  ?=  10
b  ?=  20
c  ?=  30

ok  ?=  EVAL  $a == 10 && $b == 20
PRINT  both: $ok                     # TRUE

ok  ?=  EVAL  $a == 99 || $b == 20
PRINT  either: $ok                   # TRUE

# && binds tighter: parsed as $a==99 || ($b==20 && $c==30)
ok  ?=  EVAL  $a == 99 || $b == 20 && $c == 30
PRINT  mixed: $ok                    # TRUE
```

### EVAL in IF GOTO

```
# step_15d_eval_if.script

LOAD_PLUGIN  UART

THRESHOLD  := 2.0.0

UART.OPEN  /dev/ttyUSB0  115200
fw  ?=  UART.READ_LINE
UART.CLOSE

IF  EVAL  $fw >= $THRESHOLD :VER  GOTO  fw_ok
PRINT  Firmware $fw is below required $THRESHOLD
GOTO  end
LABEL  fw_ok
PRINT  Firmware $fw is acceptable.
LABEL  end
```

### EVAL in REPEAT UNTIL

```
# step_15e_eval_until.script

counter  ?=  0

REPEAT  count_loop  UNTIL  EVAL  $counter == 5 :NUM
    counter  ?=  MATH  $counter + 1
    PRINT  count=$counter
END_REPEAT  count_loop

PRINT  Finished at counter=$counter
```

### MATH result into EVAL

```
# step_15f_math_eval.script

base    ?=  5
result  ?=  MATH  $base ** 2           # 25

ok  ?=  EVAL  $result == 25 :NUM
PRINT  5 squared == 25: $ok            # TRUE

# MATH comparison operators return 1.0/0.0 — compare as :NUM
flag  ?=  MATH  $result > 20           # 1.0
ok   ?=  EVAL  $flag == 1 :NUM
PRINT  result > 20: $ok                # TRUE
```

---

## Step 16 — BREAKPOINT

> **Pause execution and wait for user input.**

```
BREAKPOINT [label]
```

Suspends the script and displays a prompt in the log. The optional label
identifies which breakpoint fired — `$macros` are expanded at execution time.

```
# step_16_breakpoint.script

LOAD_PLUGIN  UART

counter  ?=  0

i  ?=  REPEAT  loop  5
    counter  ?=  MATH  $counter + 1

    BREAKPOINT  iteration $i — counter=$counter

    PRINT  Continued after breakpoint $i
END_REPEAT  loop

PRINT  Final counter: $counter
```

**Key responses:**

| Key | Effect |
|-----|--------|
| `a` or `A` | Asks for confirmation — `y` aborts the script, `n` continues |
| Any other key | Continues to the next command |

BREAKPOINT is silently skipped during the dry-run validation pass.

**Useful patterns:**

```
# Plain — no label
BREAKPOINT

# Static label
BREAKPOINT  before firmware flash

# Label with runtime values
BREAKPOINT  bank=$bank slot=$slot

# Conditional breakpoint — only pause in debug mode
debug_mode  ?=  TRUE
IF  EVAL  $debug_mode == TRUE  GOTO  do_break
GOTO  skip_break
LABEL  do_break
BREAKPOINT  debug stop at $stage
LABEL  skip_break
```

---

## Comprehensive Example

Combines every feature from Steps 1–16 in a realistic scenario: a multi-board
validation sequence that opens a UART channel per board, reads firmware versions
and test scores, retries boards that miss the pass threshold, formats a per-board
result, and produces a final pass/fail summary.

Uses one plugin (`UART`) to illustrate `PLUGIN.COMMAND` and `?=` plugin form.
Everything else — PRINT, DELAY, MATH, FORMAT, EVAL, BREAKPOINT, all loops and
conditions — uses only native statements.

```
# =============================================================================
# comprehensive_example.script
# Multi-board validation — UART plugin + all native statements
# =============================================================================

---
Hardware: three boards connected to /dev/ttyUSB0, /dev/ttyUSB1, /dev/ttyUSB2.
Each board responds to simple text commands over serial.
!--


# ── 1. Plugin ─────────────────────────────────────────────────────────────────

LOAD_PLUGIN  UART


# ── 2. Constant macros ────────────────────────────────────────────────────────

BAUD          := 115200
REQUIRED_VER  := 2.0.0.0
PASS_SCORE    := 80
MAX_RETRIES   := 3


# ── 3. Array macros ───────────────────────────────────────────────────────────

PORTS       [=  /dev/ttyUSB0, /dev/ttyUSB1, /dev/ttyUSB2
BOARD_NAMES [=  BoardA, BoardB, BoardC


# ── 4. Native PRINT and DELAY ─────────────────────────────────────────────────

PRINT  ================================================
PRINT  Multi-Board Validation Sequence
PRINT  Required firmware : $REQUIRED_VER
PRINT  Pass score        : $PASS_SCORE
PRINT  ================================================
DELAY  500  ms


# ── 5. Direct variable initialisation ────────────────────────────────────────

pass_count  ?=  0
fail_count  ?=  0


# ── 6. Per-board loop — counted + index + array access ───────────────────────

b  ?=  REPEAT  board_loop  3

    board_name  ?=  $BOARD_NAMES.$b
    PRINT  ── Board $b: $board_name ──

    # Open the UART channel for this board
    UART.OPEN  $PORTS.$b  $BAUD


    # ── 7. Read firmware version via plugin command ───────────────────────

    UART.WRITE  VERSION?
    fw_ver  ?=  UART.READ_LINE
    PRINT  Firmware: $fw_ver


    # ── 8. EVAL version check ────────────────────────────────────────────

    IF  EVAL  $fw_ver >= $REQUIRED_VER :VER  GOTO  fw_ok
    PRINT  $board_name: firmware $fw_ver too old — skipping.
    UART.CLOSE
    CONTINUE  board_loop
    LABEL  fw_ok


    # ── 9. Read initial test score ───────────────────────────────────────

    UART.WRITE  SCORE?
    score  ?=  UART.READ_LINE
    PRINT  Initial score: $score

    score_ok  ?=  EVAL  $score >= $PASS_SCORE :NUM


    # ── 10. Retry loop — REPEAT UNTIL with EVAL ──────────────────────────

    retries  ?=  0

    attempt  ?=  REPEAT  retry_loop  UNTIL  EVAL  $score_ok == TRUE || $retries >= $MAX_RETRIES :NUM

        retries   ?=  MATH  $retries + 1
        PRINT  Retry $retries for $board_name (current score $score)
        DELAY  200  ms

        UART.WRITE  RETEST
        score     ?=  UART.READ_LINE
        score_ok  ?=  EVAL  $score >= $PASS_SCORE :NUM

    END_REPEAT  retry_loop

    UART.CLOSE


    # ── 11. MATH — compute weighted score ────────────────────────────────

    weighted  ?=  MATH  $score * 0.9 + $retries * (-2)
    PRINT  Weighted score: $weighted


    # ── 12. FORMAT per-board result ───────────────────────────────────────

    result_str  ?=  FORMAT  $board_name $score $weighted $score_ok $retries  |  Board=%0  Raw=%1  Weighted=%2  Pass=%3  Retries=%4
    PRINT  RESULT: $result_str


    # ── 13. Update global counters ────────────────────────────────────────

    IF  EVAL  $score_ok == TRUE  GOTO  board_passed
    fail_count  ?=  MATH  $fail_count + 1
    GOTO  board_counted
    LABEL  board_passed
    pass_count  ?=  MATH  $pass_count + 1
    LABEL  board_counted

END_REPEAT  board_loop


# ── 14. Breakpoint before final report ───────────────────────────────────────

BREAKPOINT  all boards done — passed=$pass_count  failed=$fail_count


# ── 15. Final summary ────────────────────────────────────────────────────────

total    ?=  MATH  $pass_count + $fail_count
pct      ?=  MATH  $pass_count * 100 // $total
summary  ?=  FORMAT  $pass_count $total $pct  |  Passed %0 of %1 boards (%2%)
PRINT  $summary


# ── 16. Final EVAL check — abort if any board failed ─────────────────────────

IF  EVAL  $fail_count == 0 :NUM  GOTO  all_passed
PRINT  FAIL — $fail_count board(s) did not meet requirements.
GOTO  abort

LABEL  all_passed
PRINT  PASS — All boards validated successfully.
GOTO  done

LABEL  abort
PRINT  Script ended with failures — review log above.

LABEL  done
PRINT  ================================================
```

---

*End of Tutorial*
