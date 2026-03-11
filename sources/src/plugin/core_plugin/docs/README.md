# Core Plugin

A C++ shared-library plugin that provides general-purpose helper commands for script orchestration: flow control, user interaction, logging, data evaluation, boolean logic, arithmetic, and string formatting. The plugin carries no hardware dependencies and is intended to be used alongside other hardware-facing plugins in a test or automation sequence.

**Version:** 1.0.0.0

---

## Table of Contents

1. [Overview](#overview)
2. [Project Structure](#project-structure)
3. [Architecture](#architecture)
   - [Plugin Lifecycle](#plugin-lifecycle)
   - [Command Dispatch Model](#command-dispatch-model)
   - [Evaluation and Validation Engine](#evaluation-and-validation-engine)
4. [Building](#building)
5. [Command Reference](#command-reference)
   - [INFO](#info)
   - [BREAKPOINT](#breakpoint)
   - [DELAY](#delay)
   - [EVAL\_VECT](#eval_vect)
   - [EVAL\_BOARRAY](#eval_boarray)
   - [EVAL\_BOEXPR](#eval_boexpr)
   - [FAIL](#fail)
   - [FORMAT](#format)
   - [MATH](#math)
   - [MESSAGE](#message)
   - [PRINT](#print)
   - [RETURN](#return)
   - [VALIDATE](#validate)
6. [Common Concepts](#common-concepts)
   - [Vectors](#vectors)
   - [Boolean Literals and Conditions](#boolean-literals-and-conditions)
   - [Comparison Rules](#comparison-rules)
   - [Macro References](#macro-references)
7. [Fault-Tolerant and Privileged Modes](#fault-tolerant-and-privileged-modes)
8. [Error Handling and Return Values](#error-handling-and-return-values)

---

## Overview

The plugin loads as a dynamic shared library (`.so` / `.dll`). The host application calls the exported C entry points `pluginEntry()` / `pluginExit()` to create and destroy the plugin object. Once loaded, the host passes optional configuration settings via `setParams()`, calls `doInit()`, and then calls `doDispatch()` for every command it wants to execute.

All commands follow the pattern:

```
<PLUGIN>.<COMMAND> [arguments]
```

For example:

```
CORE.DELAY 2000
CORE.MESSAGE Please switch Power ON
CORE.BREAKPOINT Switch Power OFF then press a key

RESULT ?= CORE.EVAL_VECT "1 2 3 4" == "1 2 3 4"
RESULT ?= CORE.EVAL_BOEXPR ($M1 || $M2) && !$M3
CORE.VALIDATE $VERSION >= "2.0.0"
CORE.FAIL | $ERROR_FLAG
```

This plugin has no INI configuration keys. It requires no hardware connection and `doInit()` succeeds unconditionally.

---

## Project Structure

```
core_plugin/
├── CMakeLists.txt          # Build definition (shared library, C++17)
├── inc/
│   └── core_plugin.hpp     # Class definition, command table, private helper declarations
└── src/
    └── core_plugin.cpp     # Entry points, all command handlers, evaluation engine wiring
```

All thirteen commands are implemented in a single source file. The evaluation and validation logic is delegated to external utility libraries (`uVectorValidator`, `uVectorMath`, `uEvaluator`, `uBoolExprEvaluator`).

---

## Architecture

### Plugin Lifecycle

```
pluginEntry()           → creates CorePlugin instance
  setParams()           → loads common framework settings (no plugin-specific INI keys)
  doInit()              → marks plugin as initialized (always succeeds)
  doEnable()            → enables real execution (without this, commands validate args only)
  doDispatch(cmd, args) → routes a command string to the correct handler
  doCleanup()           → marks plugin as uninitialized and disabled
pluginExit(ptr)         → deletes the CorePlugin instance
```

`doEnable()` controls a "dry-run / validation" mode: when not enabled, every command validates its arguments and returns `true` without performing any side effects. This allows test frameworks to verify all command syntax in a sequence before executing it against live hardware.

### Command Dispatch Model

Commands are registered in a single-level `std::map` (`m_mapCmds`) populated in the constructor via X-macro expansion:

```cpp
#define CORE_PLUGIN_COMMANDS_CONFIG_TABLE      \
CORE_PLUGIN_CMD_RECORD( INFO                ) \
CORE_PLUGIN_CMD_RECORD( BREAKPOINT          ) \
CORE_PLUGIN_CMD_RECORD( DELAY               ) \
...

// In the constructor:
#define CORE_PLUGIN_CMD_RECORD(a) \
    m_mapCmds.insert(std::make_pair(#a, &CorePlugin::m_Utils_##a));
CORE_PLUGIN_COMMANDS_CONFIG_TABLE
#undef CORE_PLUGIN_CMD_RECORD
```

### Evaluation and Validation Engine

Several commands share a common internal evaluation pipeline:

- **`m_EvaluateExpression()`** — parses a `"op1 rule op2"` expression, detects the data type (version, number, boolean, string) from the operand content, and calls `m_GenericEvaluationHandling()`.
- **`m_GenericEvaluationHandling()`** — strips quotes from operands, selects the comparison type, and delegates to `m_Validate()`.
- **`m_Validate()`** — tokenises both sides and calls `VectorValidator::validate()`, which compares element-by-element.

Type detection follows this priority: version strings → numeric vectors → boolean vectors → plain strings.

`EVAL_BOARRAY` and `EVAL_BOEXPR` use separate paths: `eval::validateVectorBooleans()` for array reduction and `BoolExprEvaluator::evaluate()` for infix expression parsing with `&&`, `||`, `!`, and parentheses.

---

## Building

The plugin is built as a CMake shared library. It links against `uSharedConfig`, `uIPlugin`, `uPluginOps`, and `uUtils`. On non-MSVC platforms it also links against `Threads`.

```bash
mkdir build && cd build
cmake ..
make core_plugin
```

The output is `libcore_plugin.so` (Linux) or `core_plugin.dll` (Windows).

---

## Command Reference

### INFO

Prints version information and a complete usage summary of all supported commands directly to the logger. This command takes **no arguments**.

```
CORE.INFO
```

---

### BREAKPOINT

Prints an optional message to the console and pauses script execution, waiting for the user to press a key. Any key continues execution; **Esc** aborts the sequence by returning `false`.

```
CORE.BREAKPOINT [message]
```

```
# Pause with no message
CORE.BREAKPOINT

# Pause with an instruction for the operator
CORE.BREAKPOINT Switch Power OFF then press a key

# Pause before a sensitive operation
CORE.BREAKPOINT Disconnect the cable and press Enter to continue
```

> **Note:** `MESSAGE` prints unconditionally without waiting; `BREAKPOINT` always pauses for input.

---

### DELAY

Introduces a pause in script execution. The delay value must be a single unsigned integer in milliseconds. A value of `0` is accepted and silently skipped.

```
CORE.DELAY <delay_ms>
```

```
# Wait 2 seconds
CORE.DELAY 2000

# Wait 500 ms
CORE.DELAY 500

# No-op (zero delay, accepted without error)
CORE.DELAY 0
```

---

### EVAL\_VECT

Evaluates two values or vectors against a comparison rule and writes `"TRUE"` or `"FALSE"` to the result macro. The type (number, version, string, boolean) is auto-detected from the operand content.

```
RESULT ?= CORE.EVAL_VECT <op1> <rule> <op2>
```

- `op1`, `op2` — quoted vectors of space-separated values, or `$MACRONAME`.
- `rule` — see [Comparison Rules](#comparison-rules).

**Return:** `"TRUE"` or `"FALSE"` written to the caller macro.

```
# Compare two numeric vectors (element-by-element)
RESULT ?= CORE.EVAL_VECT "1 2 3 4" == "1 2 3 4"

# Compare two macro values
RESULT ?= CORE.EVAL_VECT $MACRO1 != $MACRO2

# Version comparison
RESULT ?= CORE.EVAL_VECT $FW_VERSION >= "2.1.0"

# Case-sensitive string equality
RESULT ?= CORE.EVAL_VECT $STR1 EQ "hello"

# Case-insensitive string inequality
RESULT ?= CORE.EVAL_VECT $STR1 ne "HELLO"
```

> **Note:** Use `EVAL_VECT` when you need the `TRUE`/`FALSE` result as a macro value. Use `VALIDATE` when you want to fail the script directly on a false condition.

---

### EVAL\_BOARRAY

Reduces an array of boolean values to a single `"TRUE"` or `"FALSE"` result using AND or OR reduction. Values can be literals, negated literals, or macro references.

```
RESULT ?= CORE.EVAL_BOARRAY <op1> [op2 ... opN] | <AND|OR>
```

- `opN` — boolean operands separated by spaces: `TRUE`, `FALSE`, `!TRUE`, `!FALSE`, `1`, `0`, `!1`, `!0`, or `$MACRONAME`.
- `rule` — `AND` (all must be true) or `OR` (at least one must be true), separated from the array by `|`.

**Return:** `"TRUE"` or `"FALSE"` written to the caller macro.

```
# AND reduction — all must be true
RESULT ?= CORE.EVAL_BOARRAY TRUE TRUE 1 !FALSE | AND

# OR reduction — at least one must be true
RESULT ?= CORE.EVAL_BOARRAY FALSE 0 TRUE | OR

# Using macros
RESULT ?= CORE.EVAL_BOARRAY $M1 $M2 $M3 | AND

# Mixed literals and macros
RESULT ?= CORE.EVAL_BOARRAY TRUE !FALSE $M1 $M2 | OR
```

---

### EVAL\_BOEXPR

Evaluates a boolean infix expression supporting `&&`, `||`, `!`, and parentheses. Values can be literals or macro references.

```
RESULT ?= CORE.EVAL_BOEXPR <expression>
```

**Return:** `"TRUE"` or `"FALSE"` written to the caller macro.

```
# Simple expression
RESULT ?= CORE.EVAL_BOEXPR TRUE || FALSE

# With negation
RESULT ?= CORE.EVAL_BOEXPR !FALSE && TRUE

# Parenthesised sub-expressions
RESULT ?= CORE.EVAL_BOEXPR (TRUE || FALSE) && !FALSE

# Using macros
RESULT ?= CORE.EVAL_BOEXPR $M1 && ($M2 || $M3)

# Complex expression
RESULT ?= CORE.EVAL_BOEXPR ($M1 || !$M2) && ($M3 && !$M4)
```

---

### FAIL

Forces the script to return `false`, either unconditionally or based on a boolean condition. When a condition is supplied and evaluates to `false`, the command returns `true` (i.e., the failure is not triggered).

```
CORE.FAIL [| <condition>]
```

- `condition` — optional boolean value or macro: `TRUE`, `FALSE`, `!TRUE`, `!FALSE`, `1`, `0`, `!1`, `!0`, `$MACRONAME`.

```
# Unconditional failure
CORE.FAIL

# Fail only if condition is true
CORE.FAIL | TRUE
CORE.FAIL | $ERROR_DETECTED

# Fail only if macro is set (truthy)
CORE.FAIL | $HAS_ERROR

# Do not fail (condition is false)
CORE.FAIL | FALSE
CORE.FAIL | !TRUE
```

---

### FORMAT

Builds a string by substituting items from a space-separated input vector into a format pattern. Each `%N` in the pattern is replaced by the item at 0-based index N from the input.

```
RESULT ?= CORE.FORMAT "<input_vector>" | "<format_pattern>"
```

- `input_vector` — quoted, space-separated list of tokens.
- `format_pattern` — template string where `%0`, `%1`, … are replaced by the corresponding input items.

**Return:** The formatted string written to the caller macro.

```
# Basic token substitution
RESULT ?= CORE.FORMAT "AA BB CC DD" | "%0:%1:%2:%3"
# → AA:BB:CC:DD

# Hex prefix formatting
RESULT ?= CORE.FORMAT "11 22 33 44" | "0x%0 0x%1 0x%2 0x%3"
# → 0x11 0x22 0x33 0x44

# Build a comparison expression string
RESULT ?= CORE.FORMAT "11 22 33 44" | "0x%0 0x%1 == 0x%2 0x%3"
# → 0x11 0x22 == 0x33 0x44

# Reorder tokens
RESULT ?= CORE.FORMAT "alpha beta gamma" | "%2 %0 %1"
# → gamma alpha beta

# Using a macro as input
RESULT ?= CORE.FORMAT $DATA_VECTOR | "%0/%1/%2"
```

---

### MATH

Performs element-wise arithmetic or bitwise operations between two vectors of integers. Operands must be quoted, space-separated integer vectors or macro references. An optional `| HEX` suffix formats the result in hexadecimal.

```
RESULT ?= CORE.MATH "<op1>" <rule> "<op2>" [| HEX]
```

- `op1`, `op2` — quoted vectors of space-separated integers, or `$MACRONAME`.
- `rule` — arithmetic or bitwise operator (see table below).
- `| HEX` — optional; outputs each result element prefixed with `0x`.

**Return:** Space-separated result vector written to the caller macro.

#### Supported operators

| Category | Operators |
|---|---|
| Arithmetic | `+` `-` `*` `/` `%` |
| Bitwise | `&` `\|` `^` `<<` `>>` |
| Assignment forms | `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` |

```
# Add two vectors
RESULT ?= CORE.MATH "1 2 3" + "5 6 7"
# → 6 8 10

# Subtract
RESULT ?= CORE.MATH "10 20 30" - "1 2 3"
# → 9 18 27

# Multiply with hex output
RESULT ?= CORE.MATH "2 4 8" * "3 3 3" | HEX
# → 0x6 0xC 0x18

# Bitwise AND
RESULT ?= CORE.MATH "0xFF 0x0F" & "0x55 0x55"
# → 85 5

# Left shift with hex output
RESULT ?= CORE.MATH "1 2 3" << "4 4 4" | HEX
# → 0x10 0x20 0x30

# Using macros
RESULT ?= CORE.MATH $VEC1 + $VEC2
```

---

### MESSAGE

Prints a message unconditionally to the logger. The message is required; use `PRINT` if conditional output is needed.

```
CORE.MESSAGE <message>
```

```
CORE.MESSAGE Please switch Power ON
CORE.MESSAGE Waiting for device to boot...
CORE.MESSAGE Test sequence complete
```

---

### PRINT

Prints a message or macro value to the logger, optionally gated by a boolean condition. If no arguments are given, an `<empty>` placeholder is printed.

```
CORE.PRINT <message> [| <condition>]
```

- `message` — plain string or `$MACRONAME`.
- `condition` — optional boolean value: `TRUE`, `FALSE`, `!TRUE`, `!FALSE`, `1`, `0`, `!1`, `!0`, or `$MACRONAME`. If omitted, the message is always printed.

```
# Print a plain string
CORE.PRINT This is the message

# Print the value of a macro
CORE.PRINT $RETVAL

# Print only if condition is true
CORE.PRINT Test passed | TRUE
CORE.PRINT $RESULT | $IS_VERBOSE

# Print only if macro is false (negated)
CORE.PRINT Skipping step | !$FLAG

# Print empty line
CORE.PRINT
```

---

### RETURN

Writes a value to the result data buffer so the host framework can assign it to a caller macro. The value is stored as-is; quote it if it contains spaces.

```
RETVAL ?= CORE.RETURN <value>
```

```
# Return a single token
RETVAL ?= CORE.RETURN HELLO

# Return a space-separated vector (must be quoted)
RETVAL ?= CORE.RETURN "11 22 33"

# Return a hex string
RETVAL ?= CORE.RETURN "AA BB CC DD"

# Return a status string
RETVAL ?= CORE.RETURN "PASS"
```

---

### VALIDATE

Compares two values or vectors against a rule and **fails the script** (returns `false`) if the condition is not met. Type is auto-detected identically to `EVAL_VECT`.

```
CORE.VALIDATE <op1> <rule> <op2>
```

- `op1`, `op2` — quoted vectors of space-separated values, or `$MACRONAME`.
- `rule` — see [Comparison Rules](#comparison-rules).

```
# Fail if two numeric vectors are not equal
CORE.VALIDATE "1 2 3 4" == "1 2 3 4"

# Fail if firmware version is too old
CORE.VALIDATE $FW_VERSION >= "2.1.0"

# Fail if two macro values are equal (expect them to differ)
CORE.VALIDATE $VAL1 != $VAL2

# Case-sensitive string comparison
CORE.VALIDATE $STR1 EQ $STR2

# Case-insensitive string comparison
CORE.VALIDATE $MODEL eq "unit-a"
```

---

## Common Concepts

### Vectors

Many commands operate on **vectors** — ordered lists of space-separated values treated as a unit. A vector is passed as a quoted string:

```
"1 2 3 4"
"AA BB CC DD"
"1.2.0 2.0.1"
```

Operations between two vectors are element-by-element. Both vectors must have the same number of elements.

### Boolean Literals and Conditions

Boolean values and conditions accept the following tokens (case-sensitive):

| Token | Meaning |
|---|---|
| `TRUE` or `1` | true |
| `FALSE` or `0` | false |
| `!TRUE` or `!1` | negated true → false |
| `!FALSE` or `!0` | negated false → true |
| `$MACRONAME` | value from macro (resolved at runtime) |

### Comparison Rules

#### Numeric and version rules

| Rule | Meaning |
|---|---|
| `==` | equal |
| `!=` | not equal |
| `<` | less than |
| `<=` | less than or equal |
| `>` | greater than |
| `>=` | greater than or equal |

These rules apply to integer vectors and to dotted version strings (e.g. `"1.2.3"`). Version comparison follows lexicographic segment ordering.

#### String rules

| Rule | Meaning |
|---|---|
| `EQ` | equal (case-sensitive) |
| `NE` | not equal (case-sensitive) |
| `eq` | equal (case-insensitive) |
| `ne` | not equal (case-insensitive) |

### Macro References

A macro reference takes the form `$MACRONAME`. When the plugin is in dry-run (validation) mode, macro references are accepted syntactically without being resolved. During enabled execution the host framework substitutes the macro's current value before the command handler receives it.

---

## Fault-Tolerant and Privileged Modes

- **Fault-tolerant mode** (`setFaultTolerant()` / `isFaultTolerant()`): when set, the host framework continues executing subsequent commands even after this plugin returns `false`. Useful when a `VALIDATE` or `FAIL` failure should be recorded but should not abort the entire sequence.
- **Privileged mode** (`isPrivileged()`): always returns `false`. Reserved for future use in the plugin framework.

---

## Error Handling and Return Values

Every command handler returns `bool`:
- `true` — command executed successfully, or argument validation passed in disabled (dry-run) mode.
- `false` — a required argument was missing or malformed, an unknown rule was supplied, a `VALIDATE` comparison failed, `FAIL` was triggered (conditionally or unconditionally), a `BREAKPOINT` was aborted by the user pressing Esc, or an internal evaluation error occurred.

Errors are emitted via `LOG_PRINT` at `LOG_ERROR` severity. Informational output (delay start/end, print messages, breakpoint prompts) is emitted at `LOG_INFO`. Evaluation internals are logged at `LOG_VERBOSE` and `LOG_DEBUG`. The host application controls log verbosity through the shared `uLogger` configuration.
