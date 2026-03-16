# Script Interpreter — Learning Tutorial

> **From zero to full featured — step by step**
> All examples use only the `CORE` plugin — no hardware required.

---

## Table of Contents

| Step | Concept |
|------|---------|
| [Step 1](#step-1--your-first-script) | Your first script |
| [Step 2](#step-2--multiple-commands-and-delay) | Multiple commands and DELAY |
| [Step 3](#step-3--constant-macros--) | Constant macros `:=` |
| [Step 4](#step-4--variable-macros--) | Variable macros `?=` |
| [Step 5](#step-5--validation) | Validation |
| [Step 6](#step-6--conditional-flow) | Conditional flow — IF / GOTO / LABEL |
| [Step 7](#step-7--conditional-print) | Conditional PRINT |
| [Step 8](#step-8--counted-loops) | Counted loops — REPEAT / END_REPEAT |
| [Step 9](#step-9--loop-index-capture) | Loop index capture — `?= REPEAT` |
| [Step 10](#step-10--conditional-loop--until) | Conditional loop — REPEAT UNTIL |
| [Step 11](#step-11--break-and-continue) | BREAK and CONTINUE |
| [Step 12](#step-12--array-macros--) | Array macros `[=` |
| [Step 13](#step-13--math-and-format) | MATH and FORMAT |
| [Step 14](#step-14--boolean-expressions) | Boolean expressions |
| [Final](#comprehensive-example) | Comprehensive example |

---

## CORE Plugin — Quick Reference

All examples in this tutorial use only the `CORE` plugin. It needs no hardware and is
available on any machine where the interpreter is installed.

| Command | Description |
|---------|-------------|
| `CORE.MESSAGE <text>` | Print a message unconditionally |
| `CORE.PRINT <text> [| cond]` | Print a message, optionally only when condition is `TRUE` |
| `CORE.DELAY <ms>` | Pause execution for the given number of milliseconds |
| `CORE.RETURN <value>` | Return a string value to a variable macro |
| `CORE.VALIDATE v1 rule v2` | Compare two values; **abort script** if the check fails |
| `CORE.EVAL_VECT v1 rule v2` | Compare two values; return `"TRUE"` or `"FALSE"` as a string |
| `CORE.EVAL_BOEXPR <expression>` | Evaluate a boolean expression using `&&` `\|\|` `!` `()` |
| `CORE.EVAL_BOARRAY items... \| rule` | Reduce a list of booleans with `AND` or `OR` |
| `CORE.MATH v1 rule v2 [| HEX]` | Integer arithmetic on vectors; optional hex output |
| `CORE.FORMAT "items" \| "pattern"` | Build a string placing items at `%0` `%1` … positions |
| `CORE.FAIL [| cond]` | Force script failure, unconditionally or on a condition |
| `CORE.BREAKPOINT [message]` | Pause and wait for a key press; `ESC` aborts the script |
| `CORE.INFO` | Print the full built-in command reference |

**Comparison rules** used by `VALIDATE` and `EVAL_VECT`:

| Type | Rules |
|------|-------|
| Numeric / version | `<`  `<=`  `==`  `!=`  `>=`  `>` |
| String (case-sensitive) | `EQ`  `NE` |
| String (case-insensitive) | `eq`  `ne` |

---

## Step 1 — Your First Script

> **One plugin, one command — the absolute minimum.**

A script needs at least one `LOAD_PLUGIN` declaration and one command.
Nothing else is required.

```
# step_01_hello.script

LOAD_PLUGIN  CORE

CORE.MESSAGE  Hello, World!
```

**What happens:**
- `LOAD_PLUGIN CORE` loads the CORE shared library and registers its commands.
- `CORE.MESSAGE` prints the text to the log output.

> **Note:** Every plugin used by any command must be declared with `LOAD_PLUGIN`
> before that command appears. Undeclared plugins are caught at validation time,
> before execution starts.

**Try it — add a second message:**

```
LOAD_PLUGIN  CORE

CORE.MESSAGE  Hello, World!
CORE.MESSAGE  This is my second line.
```

Commands execute in the order they appear. The script stops immediately if any
command returns a failure.

---

## Step 2 — Multiple Commands and DELAY

> **Sequencing and timing.**

Scripts run as a simple sequence. `CORE.DELAY` introduces a pause between commands.

```
# step_02_delay.script

LOAD_PLUGIN  CORE

CORE.MESSAGE  Starting sequence...
CORE.DELAY    1000
CORE.MESSAGE  One second later.
CORE.DELAY    500
CORE.MESSAGE  Done.
```

- `CORE.DELAY` takes a single integer — the pause in **milliseconds**.
- `CORE.MESSAGE` always prints, regardless of any conditions.
- `CORE.DELAY 0` is valid and is a no-op — useful as a placeholder during development.

---

## Step 3 — Constant Macros `:=`

> **Name your values once, use them everywhere.**

A constant macro assigns a name to a literal value. It is expanded at **validation
time**, before execution, so there is zero runtime cost.

```
# step_03_cmacros.script

LOAD_PLUGIN  CORE

# Declare constant macros with :=
GREETING    := Hello from the tutorial
WAIT_MS     := 2000
VERSION     := 1.0.0.0

CORE.MESSAGE  $GREETING
CORE.MESSAGE  Waiting $WAIT_MS ms...
CORE.DELAY    $WAIT_MS
CORE.MESSAGE  Script version: $VERSION
```

**Rules:**
- Name must start with a letter or underscore, followed by letters, digits, or underscores.
- Value is everything after `:=`, trimmed of leading/trailing whitespace.
- The same name cannot be declared twice — the validator rejects duplicates.
- Use `$NAME` anywhere after the declaration to expand the value.

> **Convention:** Constant macro names are typically written in `UPPER_CASE`, but
> any valid identifier is accepted.

**A more realistic example:**

```
DEVICE_PATH  := /dev/ttyUSB0
BAUD_RATE    := 115200
BOARD_NAME   := DevKit-A

CORE.MESSAGE  Connecting to $BOARD_NAME
CORE.MESSAGE  Port: $DEVICE_PATH  Baud: $BAUD_RATE
```

---

## Step 4 — Variable Macros `?=`

> **Capture the return value of a command.**

A variable macro captures the string that a plugin command returns. Unlike constant
macros, the value is set at **runtime** and can change each time the command runs.

```
# step_04_vmacros.script

LOAD_PLUGIN  CORE

# CORE.RETURN simply hands back whatever string you give it.
# It is the simplest way to "simulate" a sensor reading.

sensor_value  ?=  CORE.RETURN  42
board_id      ?=  CORE.RETURN  "DevKit-A rev2"

CORE.MESSAGE  Sensor reading : $sensor_value
CORE.MESSAGE  Board identity : $board_id
```

**Key differences from constant macros:**

| | Constant `:=` | Variable `?=` |
|--|---------------|---------------|
| Right-hand side | Literal value | Plugin command |
| Expanded | Validation time | Execution time |
| Can be reassigned | No | Yes |

> **Note:** `CORE.RETURN` is the "echo" command — it returns exactly what you pass
> to it with no side effects. It is ideal for tutorials and testing.

**Variable macros can be reassigned:**

```
reading  ?=  CORE.RETURN  10
CORE.MESSAGE  First reading: $reading

reading  ?=  CORE.RETURN  20
CORE.MESSAGE  Second reading: $reading
```

Each time the command on the right is executed the macro is updated with the new
return value. The most recently written value is always the one used.

---

## Step 5 — Validation

> **Assert that values meet expectations.**

`CORE.VALIDATE` compares two values using a rule. If the comparison fails the
**entire script is aborted** — making it the right tool for mandatory checks.

```
# step_05_validate.script

LOAD_PLUGIN  CORE

expected  ?=  CORE.RETURN  42
actual    ?=  CORE.RETURN  42

# == checks numeric equality; script aborts here if the check fails
CORE.VALIDATE  $actual == $expected
CORE.MESSAGE   Value is correct: $actual

# String comparison — EQ is case-sensitive, eq is case-insensitive
label  ?=  CORE.RETURN  DevKit-A
CORE.VALIDATE  $label  EQ  "DevKit-A"
CORE.MESSAGE   Label matches.

# Version comparison
fw  ?=  CORE.RETURN  2.1.0.0
CORE.VALIDATE  $fw  >=  2.0.0.0
CORE.MESSAGE   Firmware version is acceptable: $fw
```

> ⚠️ **`CORE.VALIDATE` fails the entire script if the condition is not met.**
> Use `CORE.EVAL_VECT` (introduced in Step 6) if you need a `TRUE`/`FALSE` result
> without aborting.

---

## Step 6 — Conditional Flow

> **Skip sections based on a condition.**

The `IF` / `GOTO` / `LABEL` construct allows sections of a script to be skipped.
Jumps are always **forward-only**: `GOTO` must appear before its `LABEL` in the file.

```
# step_06_conditions.script

LOAD_PLUGIN  CORE

status  ?=  CORE.RETURN  TRUE

# If $status is TRUE, jump over the error block
IF  $status  GOTO  all_good
    CORE.MESSAGE  Something went wrong!
    CORE.FAIL
LABEL  all_good

CORE.MESSAGE  Status is OK, continuing.
```

**CORE.EVAL_VECT — get TRUE/FALSE without aborting:**

```
LOAD_PLUGIN  CORE

a  ?=  CORE.RETURN  10
b  ?=  CORE.RETURN  20

# Compare and capture the result as a string
result  ?=  CORE.EVAL_VECT  $a < $b
CORE.MESSAGE  Is a less than b? $result

IF  $result  GOTO  a_is_smaller
    CORE.MESSAGE  a is NOT smaller
    GOTO  comparison_done
LABEL  a_is_smaller

CORE.MESSAGE  a IS smaller
LABEL  comparison_done
```

**Rules:**
- `GOTO` without `IF` is an **unconditional** jump — always taken.
- Every `GOTO` must have a matching `LABEL`; every `LABEL` must have a preceding `GOTO`.
- Duplicate label names are rejected.
- A `GOTO` must not cross a loop boundary (validated statically).

> **Boolean expressions** support: `TRUE`  `FALSE`  `!`  `&&`  `||`  `()`
> Variable macros that hold `"TRUE"` or `"FALSE"` can be used directly.

---

## Step 7 — Conditional PRINT

> **Print a message only when a condition is met.**

`CORE.PRINT` is like `CORE.MESSAGE` but accepts an optional condition after a pipe `|`.

```
# step_07_conditional_print.script

LOAD_PLUGIN  CORE

verbose  ?=  CORE.RETURN  TRUE
count    ?=  CORE.RETURN  7

CORE.PRINT  Always visible
CORE.PRINT  Verbose mode is ON          | $verbose
CORE.PRINT  This will NOT print         | FALSE
CORE.PRINT  Count is $count             | TRUE

# Condition can also be an inverted macro
error  ?=  CORE.RETURN  FALSE
CORE.PRINT  No errors detected          | !$error
```

**Accepted condition values after the pipe `|`:**

| Form | Example |
|------|---------|
| Literal | `TRUE`  `FALSE`  `1`  `0` |
| Inverted literal | `!TRUE`  `!FALSE`  `!1`  `!0` |
| Macro | `$myMacro` (must hold `TRUE` or `FALSE`) |
| Inverted macro | `!$myMacro` |

> **Note:** `CORE.PRINT` with no condition behaves exactly like `CORE.MESSAGE`.

---

## Step 8 — Counted Loops

> **Repeat a block of commands N times.**

The `REPEAT` / `END_REPEAT` construct runs its body a fixed number of times. The
label is mandatory and must be unique; it matches the opening and closing lines.

```
# step_08_repeat.script

LOAD_PLUGIN  CORE

# Repeat the body exactly 3 times
REPEAT  blink  3
    CORE.MESSAGE  --- blink ---
    CORE.DELAY    500
END_REPEAT  blink

CORE.MESSAGE  Blinking done.
```

**Rules:**
- N must be a positive integer ≥ 1.
- The label after `REPEAT` and `END_REPEAT` must match exactly.
- Loop labels and `GOTO` labels are **separate namespaces** — no conflicts.
- Loops may be nested to any depth; each level needs its own unique label.

**Nested loops:**

```
REPEAT  outer  3
    CORE.MESSAGE  Outer iteration
    REPEAT  inner  2
        CORE.MESSAGE  -- Inner iteration
    END_REPEAT  inner
END_REPEAT  outer
```

---

## Step 9 — Loop Index Capture

> **Know which iteration you are on.**

Place `varname ?=` in front of `REPEAT` to receive the current **0-based iteration
index**. The macro is only visible inside the loop body — it is destroyed when the
loop exits.

```
# step_09_index.script

LOAD_PLUGIN  CORE

i  ?=  REPEAT  count  5
    CORE.MESSAGE  Iteration $i
END_REPEAT  count

# $i is out of scope here
CORE.MESSAGE  Loop finished.
```

**Output:**
```
Iteration 0
Iteration 1
Iteration 2
Iteration 3
Iteration 4
Loop finished.
```

**Using the index for arithmetic:**

```
i  ?=  REPEAT  math_demo  4
    result  ?=  CORE.MATH  $i + "10"
    CORE.MESSAGE  $i + 10 = $result
END_REPEAT  math_demo
```

> **Note:** The index macro name must not conflict with any existing script-level
> variable macro. The validator catches this statically.

---

## Step 10 — Conditional Loop — UNTIL

> **Loop until a condition becomes TRUE.**

The `UNTIL` form runs the body, then evaluates a condition at `END_REPEAT`.
If `FALSE` it loops again; if `TRUE` it exits. The body **always executes at least
once** (do-while semantics).

```
# step_10_until.script

LOAD_PLUGIN  CORE

# Simulate a retry loop: keep going until we get a passing result
attempt  ?=  REPEAT  retry_loop  UNTIL  $got_ok
    CORE.MESSAGE  Attempt $attempt ...

    # Simulate: succeed on attempt 3 (index 2)
    got_ok  ?=  CORE.EVAL_VECT  $attempt == 2

    CORE.PRINT  Still trying...  | !$got_ok
END_REPEAT  retry_loop

CORE.MESSAGE  Succeeded after $attempt attempt(s).
```

**Key behaviour:**
- The condition is evaluated **after** each iteration, at `END_REPEAT`.
- Loop exits when the condition is `TRUE`.
- `$attempt` is the combined index capture — it counts iterations from `0`.

> ⚠️ Always make sure the condition can eventually become `TRUE`, or the loop will
> run forever.

---

## Step 11 — BREAK and CONTINUE

> **Exit a loop early or skip the rest of an iteration.**

Both keywords name the **target loop** explicitly — following the same convention as
Rust's labelled loops. This eliminates ambiguity in nested loops.

### CONTINUE — skip the rest of this iteration

```
# step_11a_continue.script

LOAD_PLUGIN  CORE

i  ?=  REPEAT  skip_demo  5
    # Skip printing when i == 2
    is_two  ?=  CORE.EVAL_VECT  $i == 2
    IF  $is_two  GOTO  skip_msg
        CORE.MESSAGE  Processing item $i
        GOTO  after_skip
    LABEL  skip_msg
    CONTINUE  skip_demo
    LABEL  after_skip
END_REPEAT  skip_demo
```

### BREAK — exit the loop immediately

```
# step_11b_break.script

LOAD_PLUGIN  CORE

i  ?=  REPEAT  search  10
    CORE.MESSAGE  Checking item $i

    # Stop as soon as we reach item 4
    found  ?=  CORE.EVAL_VECT  $i == 4
    IF  $found  GOTO  found_it
        GOTO  not_yet
    LABEL  found_it

    CORE.MESSAGE  Found at index $i — stopping.
    BREAK  search
    LABEL  not_yet
END_REPEAT  search

CORE.MESSAGE  Search complete.
```

**Nested BREAK** — `BREAK outer` from inside the inner loop exits **both** loops:

```
bank  ?=  REPEAT  outer  3
    ch  ?=  REPEAT  inner  8
        ok  ?=  CORE.EVAL_VECT  $ch == 3
        IF  $ok  GOTO  found
            GOTO  keep_looking
        LABEL  found
        CORE.MESSAGE  Found at bank=$bank ch=$ch
        BREAK  outer          # exits both loops
        LABEL  keep_looking
    END_REPEAT  inner
END_REPEAT  outer
```

> **Note:** `BREAK` and `CONTINUE` must name an enclosing loop label. Using a
> non-enclosing or non-existent label is caught by the validator.

---

## Step 12 — Array Macros `[=`

> **Ordered lists of values.**

An array macro stores a list of string elements. Individual elements are accessed at
runtime with `$NAME.$index`, where `$index` is any macro that resolves to a number.

```
# step_12_arrays.script

LOAD_PLUGIN  CORE

# Declare an array
COLORS  [=  red, green, blue, yellow

# Access a specific element directly
CORE.MESSAGE  First color: $COLORS.$0

# Iterate with a loop index — the natural combination
i  ?=  REPEAT  color_loop  4
    CORE.MESSAGE  Color $i is $COLORS.$i
END_REPEAT  color_loop
```

**Multi-line declaration** using `\` continuation:

```
STEPS  [=  init, \
           configure, \
           run, \
           teardown

s  ?=  REPEAT  steps  4
    CORE.MESSAGE  Step $s: $STEPS.$s
END_REPEAT  steps
```

**Elements with internal spaces** — no quoting needed:

```
LABELS  [=  slot zero, slot one, slot two
```

**Elements containing commas** — must be quoted with `"..."`:

```
TAGS  [=  "alpha, beta",  "gamma, delta",  plain

CORE.MESSAGE  Tag 0: $TAGS.$0
CORE.MESSAGE  Tag 1: $TAGS.$1
CORE.MESSAGE  Tag 2: $TAGS.$2
```

> **Note:** Array macro names must not conflict with constant macro names or loop
> index macro names. The validator checks this at load time.

---

## Step 13 — MATH and FORMAT

> **Arithmetic and string construction.**

### CORE.MATH — integer arithmetic

```
# step_13a_math.script

LOAD_PLUGIN  CORE

a  ?=  CORE.RETURN  10
b  ?=  CORE.RETURN  3

sum     ?=  CORE.MATH  $a + $b
diff    ?=  CORE.MATH  $a - $b
prod    ?=  CORE.MATH  $a * $b
hexval  ?=  CORE.MATH  $a + $b  | HEX

CORE.MESSAGE  $a + $b = $sum
CORE.MESSAGE  $a - $b = $diff
CORE.MESSAGE  $a * $b = $prod
CORE.MESSAGE  $a + $b in hex = $hexval
```

Available operators: `+` `-` `*` `/` `%` `&` `|` `^` `<<` `>>` and their
compound-assignment forms (`+=` `-=` etc.). Append `| HEX` for hexadecimal output.

### CORE.FORMAT — string building

```
# step_13b_format.script

LOAD_PLUGIN  CORE

# Items are space-separated inside quotes; %0 %1 %2 reference them by index
label  ?=  CORE.FORMAT  "board_A 1.2.3 active"  |  "ID=%0  Ver=%1  State=%2"
CORE.MESSAGE  $label
# Output: ID=board_A  Ver=1.2.3  State=active

# Items can be reordered freely
report  ?=  CORE.FORMAT  "AA BB CC"  |  "C=%2  A=%0  B=%1"
CORE.MESSAGE  $report
# Output: C=CC  A=AA  B=BB
```

---

## Step 14 — Boolean Expressions

> **Compose multiple conditions into one result.**

### CORE.EVAL_BOEXPR — full expression syntax

```
# step_14a_boexpr.script

LOAD_PLUGIN  CORE

a  ?=  CORE.RETURN  TRUE
b  ?=  CORE.RETURN  FALSE
c  ?=  CORE.RETURN  TRUE

result  ?=  CORE.EVAL_BOEXPR  $a && ($b || $c)
CORE.MESSAGE  a AND (b OR c) = $result

result  ?=  CORE.EVAL_BOEXPR  !$b && $c
CORE.MESSAGE  NOT b AND c   = $result
```

### CORE.EVAL_BOARRAY — reduce a list of booleans

```
# step_14b_boarray.script

LOAD_PLUGIN  CORE

# Are ALL checks passing?
all_ok  ?=  CORE.EVAL_BOARRAY  TRUE TRUE TRUE FALSE | AND
CORE.MESSAGE  All checks pass: $all_ok

# Is AT LEAST ONE check passing?
any_ok  ?=  CORE.EVAL_BOARRAY  TRUE TRUE TRUE FALSE | OR
CORE.MESSAGE  Any check passes: $any_ok
```

`EVAL_BOARRAY` accepts: `TRUE`  `FALSE`  `1`  `0`  `!TRUE`  `!FALSE`  `!1`  `!0`
and `$macronames` that resolve to one of these values.

---

## Comprehensive Example

The script below combines every feature from Steps 1–14 in a realistic scenario:
simulating a multi-board test sequence with version checks, score-based retry logic,
per-board results, and a final pass/fail summary.

> All commands are CORE-only — no hardware needed. Run it as-is to see every
> feature interacting.

```
# =============================================================================
# comprehensive_example.script
# Simulates a multi-board validation sequence.
# Every language feature is exercised.
# =============================================================================

---
Tutorial: comprehensive example
Uses only the CORE plugin — no hardware required.
!--


# ── 1. Plugin loading ─────────────────────────────────────────────────────────

LOAD_PLUGIN  CORE


# ── 2. Constant macros ────────────────────────────────────────────────────────

REQUIRED_VER  := 2.0.0.0
MAX_RETRIES   := 3
PASS_SCORE    := 80


# ── 3. Array macros ───────────────────────────────────────────────────────────

BOARDS    [=  BoardA, BoardB, BoardC
FW_VERS   [=  2.0.0.0, 1.9.0.0, 2.0.0.0
SCORES    [=  95, 72, 88


# ── 4. Opening message ────────────────────────────────────────────────────────

CORE.MESSAGE  ================================================
CORE.MESSAGE  Multi-Board Validation Sequence
CORE.MESSAGE  Required firmware: $REQUIRED_VER
CORE.MESSAGE  ================================================
CORE.DELAY    500


# ── 5. Per-board loop (counted loop + index + array access) ───────────────────

b  ?=  REPEAT  board_loop  3

    board_name  ?=  CORE.RETURN  $BOARDS.$b
    fw_ver      ?=  CORE.RETURN  $FW_VERS.$b
    score       ?=  CORE.RETURN  $SCORES.$b

    CORE.MESSAGE  ── Board $b: $board_name ──


    # ── 6. Version check (EVAL_VECT + conditional PRINT + IF/GOTO) ──────────

    ver_ok  ?=  CORE.EVAL_VECT  $fw_ver >= $REQUIRED_VER
    CORE.PRINT   Firmware $fw_ver >= $REQUIRED_VER : $ver_ok  | TRUE

    IF  $ver_ok  GOTO  fw_ok
    CORE.MESSAGE  Board $board_name: firmware too old, skipping.
    CONTINUE  board_loop
    LABEL  fw_ok


    # ── 7. Score check (variable macro + EVAL_VECT) ──────────────────────────

    score_ok  ?=  CORE.EVAL_VECT  $score >= $PASS_SCORE


    # ── 8. Retry loop (REPEAT UNTIL + index capture + BREAK) ────────────────

    attempt  ?=  REPEAT  retry_loop  UNTIL  $score_ok

        CORE.MESSAGE  Attempt $attempt for $board_name (score $score)
        CORE.DELAY    200

        # Simulate improvement: score rises by 10 each retry
        score     ?=  CORE.MATH  $score + 10
        score_ok  ?=  CORE.EVAL_VECT  $score >= $PASS_SCORE

        # Give up after MAX_RETRIES attempts
        out_of_retries  ?=  CORE.EVAL_VECT  $attempt >= $MAX_RETRIES
        IF  $out_of_retries  GOTO  stop_retry
        GOTO  continue_retry
        LABEL  stop_retry
        BREAK  retry_loop
        LABEL  continue_retry

    END_REPEAT  retry_loop


    # ── 9. Per-board result (FORMAT + VALIDATE) ──────────────────────────────

    summary  ?=  CORE.FORMAT  "$board_name $score $score_ok"  |  \
                              "Board=%0  FinalScore=%1  Pass=%2"
    CORE.MESSAGE  RESULT: $summary

    # Fail the script if this board did not reach the required score
    CORE.FAIL  | !$score_ok


END_REPEAT  board_loop


# ── 10. Boolean composition (EVAL_BOEXPR) ────────────────────────────────────

CORE.MESSAGE  ================================================

final_check  ?=  CORE.EVAL_BOEXPR  TRUE && !FALSE
CORE.PRINT   Final sanity check passed: $final_check  | $final_check


# ── 11. Closing message ───────────────────────────────────────────────────────

CORE.MESSAGE  All boards processed successfully.
CORE.MESSAGE  ================================================
```

---

*End of Tutorial*
