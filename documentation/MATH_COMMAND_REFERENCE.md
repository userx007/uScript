# MATH Statement — Complete Reference

The `MATH` statement evaluates a floating-point expression and stores the result
as a string in a variable macro.

```
<name>  ?=  MATH  <expression>
```

`$macros` in the expression are expanded at execution time. Integer-valued
results are stored without a decimal point (`5`, not `5.000000`). Floating-point
results use up to 15 significant digits.

---

## Table of Contents

1. [Arithmetic operators](#1-arithmetic-operators)
2. [Unary operators](#2-unary-operators)
3. [Power operator](#3-power-operator)
4. [Relational operators](#4-relational-operators)
5. [Equality operators](#5-equality-operators)
6. [Logical operators](#6-logical-operators)
7. [Bitwise operators](#7-bitwise-operators)
8. [Ternary operator](#8-ternary-operator)
9. [Assignment operator](#9-assignment-operator)
10. [Implicit multiplication](#10-implicit-multiplication)
11. [Scientific notation](#11-scientific-notation)
12. [Built-in constants](#12-built-in-constants)
13. [Trigonometric functions](#13-trigonometric-functions)
14. [Inverse trigonometric functions](#14-inverse-trigonometric-functions)
15. [Hyperbolic functions](#15-hyperbolic-functions)
16. [Exponential and power functions](#16-exponential-and-power-functions)
17. [Logarithmic functions](#17-logarithmic-functions)
18. [Rounding functions](#18-rounding-functions)
19. [Absolute value and sign](#19-absolute-value-and-sign)
20. [Two-argument functions](#20-two-argument-functions)
21. [Operator precedence summary](#21-operator-precedence-summary)

---

## 1. Arithmetic Operators

Standard four operations plus floor division and modulo. All operate on
floating-point values.

```
r ?= MATH  10 + 3          # 13
r ?= MATH  10 - 3          # 7
r ?= MATH  10 * 3          # 30
r ?= MATH  10 / 3          # 3.33333333333333
r ?= MATH  10 // 3         # 3      (floor division — rounds toward -∞)
r ?= MATH  10 % 3          # 1      (modulo — same sign as divisor)
```

Floor division and modulo work on floating-point too:

```
r ?= MATH  7.5 // 2.0      # 3      (floor(7.5 / 2.0) = floor(3.75) = 3)
r ?= MATH  7.5 % 2.0       # 1.5
```

---

## 2. Unary Operators

Applied to a single operand before evaluation. All four can be chained.

```
r ?= MATH  -5              # -5     (unary minus)
r ?= MATH  +5              # 5      (unary plus — no-op, for clarity)
r ?= MATH  --5             # 5      (double negation)
r ?= MATH  !0              # 1      (logical NOT of 0 → true)
r ?= MATH  !1              # 0      (logical NOT of non-zero → false)
r ?= MATH  !42             # 0
r ?= MATH  ~0              # -1     (bitwise NOT — operates on int64)
r ?= MATH  ~7              # -8
```

---

## 3. Power Operator

`**` is right-associative: `a ** b ** c` is evaluated as `a ** (b ** c)`.

```
r ?= MATH  2 ** 10         # 1024
r ?= MATH  3 ** 3          # 27
r ?= MATH  9 ** 0.5        # 3      (square root via power)
r ?= MATH  2 ** -1         # 0.5
r ?= MATH  2 ** 2 ** 3     # 256    (right-assoc: 2 ** (2**3) = 2**8)
```

> **Note:** `^` means bitwise XOR in this evaluator — not power. Use `**` or
> `pow(b, e)` for exponentiation.

---

## 4. Relational Operators

Return `1` (true) or `0` (false). All six standard comparisons.

```
r ?= MATH  5 < 10          # 1
r ?= MATH  5 < 5           # 0
r ?= MATH  5 <= 5          # 1
r ?= MATH  5 <= 4          # 0
r ?= MATH  10 > 5          # 1
r ?= MATH  5 > 5           # 0
r ?= MATH  5 >= 5          # 1
r ?= MATH  4 >= 5          # 0
```

---

## 5. Equality Operators

Return `1` (true) or `0` (false). Comparison is floating-point.

```
r ?= MATH  5 == 5          # 1
r ?= MATH  5 == 6          # 0
r ?= MATH  5 != 6          # 1
r ?= MATH  5 != 5          # 0
r ?= MATH  0.1 + 0.2 == 0.3   # 0  (floating-point precision — see note)
```

> **Floating-point equality:** Due to floating-point representation, avoid
> using `==` to compare computed results. Prefer a tolerance check:
> `abs($a - $b) < 1e-9`.

---

## 6. Logical Operators

`&&` and `||` use short-circuit evaluation. Any non-zero value is truthy;
zero is falsy. `!` is the unary logical NOT (see Section 2).

```
r ?= MATH  1 && 1          # 1
r ?= MATH  1 && 0          # 0
r ?= MATH  0 && 1          # 0
r ?= MATH  0 || 1          # 1
r ?= MATH  0 || 0          # 0
r ?= MATH  1 || 0          # 1
r ?= MATH  !0              # 1
r ?= MATH  !5              # 0
```

`&&` binds tighter than `||`:

```
r ?= MATH  1 || 0 && 0     # 1   (parsed as 1 || (0 && 0))
r ?= MATH  (1 || 0) && 0   # 0   (parentheses override)
```

---

## 7. Bitwise Operators

All operate on values truncated to 64-bit signed integers. Applying them to
non-finite values or out-of-range values throws a runtime error.

### AND, OR, XOR

```
r ?= MATH  0b1100 & 0b1010     # 8     (bitwise AND → 0b1000)
r ?= MATH  12 & 10             # 8
r ?= MATH  12 | 10             # 14    (bitwise OR  → 0b1110)
r ?= MATH  12 ^ 10             # 6     (bitwise XOR → 0b0110)
```

### Bit shifts

```
r ?= MATH  1 << 4          # 16    (shift left  — multiply by 2⁴)
r ?= MATH  256 >> 3        # 32    (shift right — divide by 2³)
r ?= MATH  3 << 8          # 768
```

### Bitwise NOT (unary `~`)

```
r ?= MATH  ~0              # -1
r ?= MATH  ~1              # -2
r ?= MATH  ~255            # -256
```

---

## 8. Ternary Operator

`condition ? value_if_true : value_if_false`

Evaluates only the selected branch (short-circuit).

```
r ?= MATH  1 ? 42 : 99         # 42   (condition true)
r ?= MATH  0 ? 42 : 99         # 99   (condition false)
r ?= MATH  5 > 3 ? 100 : 0     # 100
r ?= MATH  5 < 3 ? 100 : 0     # 0
r ?= MATH  $score >= 80 ? 1 : 0   # 1 if score passes, 0 otherwise
```

Ternaries can be nested (right-associative):

```
r ?= MATH  $x > 0 ? 1 : $x < 0 ? -1 : 0   # sign function
```

---

## 9. Assignment Operator

`varname = expression` stores the result in the Calculator's internal variable
map **and** returns it. These internal variables persist across all `MATH` calls
in the same script run, making them useful for accumulators and counters that
must survive loop iterations without routing through `$macros`.

```
r ?= MATH  x = 10          # x=10, r="10"
r ?= MATH  x = x + 1       # x=11, r="11"
r ?= MATH  y = x * 2       # y=22, r="22"
r ?= MATH  z = x + y       # z=33, r="33"
```

> **Caution — `=` vs `==`:** The parser uses first-`=`-not-followed-by-`=` as
> the assignment trigger. `==` is always the equality comparison.

Named accumulator pattern inside a loop:

```
r ?= MATH  acc = 0               # initialise

i ?= REPEAT  sum_loop  5
    r ?= MATH  acc = acc + $i    # acc accumulates 0+1+2+3+4 = 10
END_REPEAT  sum_loop

PRINT  sum=$r                    # sum=10
```

---

## 10. Implicit Multiplication

A number or closing `)` immediately followed by an identifier or `(` —
with no operator between them — is treated as multiplication.

```
r ?= MATH  2pi             # 6.28318530717959  (2 × π)
r ?= MATH  3e               # 8.15484548537858  (3 × e)
r ?= MATH  2(3 + 4)         # 14   (2 × 7)
r ?= MATH  (2 + 1)(3 + 1)   # 12   (3 × 4)
r ?= MATH  2sqrt(9)         # 6    (2 × 3)
```

> **Ambiguity note:** `2e3` is parsed as scientific notation (`2000`), not as
> `2 × e × 3`. Use explicit multiplication `2 * e * 3` if the latter is needed.

---

## 11. Scientific Notation

Numeric literals accept `e` or `E` followed by an optional sign and digits.

```
r ?= MATH  1e3             # 1000
r ?= MATH  1.5e3           # 1500
r ?= MATH  2.5E-2          # 0.025
r ?= MATH  1.23e10         # 12300000000
r ?= MATH  6.674e-11       # 6.674e-11   (gravitational constant)
```

---

## 12. Built-in Constants

| Name | Value | Description |
|------|-------|-------------|
| `pi` | 3.14159265358979… | π |
| `e` | 2.71828182845904… | Euler's number |
| `tau` | 6.28318530717958… | 2π |
| `phi` | 1.61803398874989… | Golden ratio (φ) |
| `inf` | ∞ | Positive infinity |
| `nan` | NaN | Not-a-Number |

```
r ?= MATH  pi              # 3.14159265358979
r ?= MATH  e               # 2.71828182845904
r ?= MATH  tau             # 6.28318530717959
r ?= MATH  phi             # 1.6180339887499
r ?= MATH  inf             # inf
r ?= MATH  1 / inf         # 0
r ?= MATH  inf + 1         # inf
r ?= MATH  inf - inf       # nan
```

Constants can be overwritten with `=` inside an expression, but this is
generally not recommended.

---

## 13. Trigonometric Functions

All angles are in **radians**. To convert degrees to radians: `degrees * pi / 180`.

```
r ?= MATH  sin(0)              # 0
r ?= MATH  sin(pi / 2)         # 1
r ?= MATH  sin(pi)             # 0   (≈ 1.2e-16 due to float precision)
r ?= MATH  cos(0)              # 1
r ?= MATH  cos(pi)             # -1
r ?= MATH  cos(pi / 3)         # 0.5
r ?= MATH  tan(0)              # 0
r ?= MATH  tan(pi / 4)         # 1
r ?= MATH  tan(pi / 2)         # 1.6e+16   (numerically large, not ∞)
```

Degree conversion:

```
r ?= MATH  sin(90 * pi / 180)  # 1      (sin of 90°)
r ?= MATH  cos(60 * pi / 180)  # 0.5    (cos of 60°)
```

---

## 14. Inverse Trigonometric Functions

Return angles in **radians**. `asin` and `acos` require the argument to be
in `[-1, 1]` — values outside this range throw a domain error.

```
r ?= MATH  asin(0)             # 0
r ?= MATH  asin(1)             # 1.5707963267949   (π/2 = 90°)
r ?= MATH  asin(0.5)           # 0.523598775598299  (30°)
r ?= MATH  acos(1)             # 0
r ?= MATH  acos(0)             # 1.5707963267949   (90°)
r ?= MATH  acos(0.5)           # 1.0471975511966   (60°)
r ?= MATH  atan(0)             # 0
r ?= MATH  atan(1)             # 0.785398163397448  (45°, π/4)
r ?= MATH  atan(inf)           # 1.5707963267949   (90°)
```

---

## 15. Hyperbolic Functions

```
r ?= MATH  sinh(0)             # 0
r ?= MATH  sinh(1)             # 1.1752011936438
r ?= MATH  sinh(-1)            # -1.1752011936438
r ?= MATH  cosh(0)             # 1
r ?= MATH  cosh(1)             # 1.54308063481524
r ?= MATH  tanh(0)             # 0
r ?= MATH  tanh(1)             # 0.761594155955765
r ?= MATH  tanh(inf)           # 1
```

---

## 16. Exponential and Power Functions

```
r ?= MATH  exp(0)              # 1      (e⁰)
r ?= MATH  exp(1)              # 2.71828182845905  (e¹ = e)
r ?= MATH  exp(2)              # 7.38905609893065  (e²)
r ?= MATH  exp(-1)             # 0.367879441171442
r ?= MATH  exp2(0)             # 1      (2⁰)
r ?= MATH  exp2(8)             # 256    (2⁸)
r ?= MATH  exp2(10)            # 1024
r ?= MATH  sqrt(0)             # 0
r ?= MATH  sqrt(1)             # 1
r ?= MATH  sqrt(4)             # 2
r ?= MATH  sqrt(2)             # 1.4142135623731
r ?= MATH  sqrt(144)           # 12
r ?= MATH  cbrt(0)             # 0      (cube root)
r ?= MATH  cbrt(8)             # 2
r ?= MATH  cbrt(27)            # 3
r ?= MATH  cbrt(-8)            # -2     (negative argument is valid)
```

---

## 17. Logarithmic Functions

`log`, `log2`, and `log10` throw a domain error if the argument is ≤ 0.

```
r ?= MATH  log(1)              # 0      (natural log)
r ?= MATH  log(e)              # 1
r ?= MATH  log(e ** 3)         # 3
r ?= MATH  log(0.5)            # -0.693147180559945
r ?= MATH  log2(1)             # 0      (log base 2)
r ?= MATH  log2(2)             # 1
r ?= MATH  log2(8)             # 3
r ?= MATH  log2(1024)          # 10
r ?= MATH  log10(1)            # 0      (log base 10)
r ?= MATH  log10(10)           # 1
r ?= MATH  log10(1000)         # 3
r ?= MATH  log10(0.01)         # -2
```

### `log_b(value, base)` — logarithm to an arbitrary base

`log_b` throws if `value ≤ 0` or `base ≤ 0` or `base == 1`.

```
r ?= MATH  log_b(8, 2)         # 3      (log₂ 8)
r ?= MATH  log_b(1000, 10)     # 3      (log₁₀ 1000)
r ?= MATH  log_b(81, 3)        # 4      (log₃ 81)
r ?= MATH  log_b(2, 2)         # 1
```

---

## 18. Rounding Functions

```
r ?= MATH  ceil(2.1)           # 3      (round toward +∞)
r ?= MATH  ceil(2.9)           # 3
r ?= MATH  ceil(-2.1)          # -2
r ?= MATH  floor(2.9)          # 2      (round toward -∞)
r ?= MATH  floor(2.1)          # 2
r ?= MATH  floor(-2.1)         # -3
r ?= MATH  round(2.4)          # 2      (round to nearest, ties away from 0)
r ?= MATH  round(2.5)          # 3
r ?= MATH  round(2.6)          # 3
r ?= MATH  round(-2.5)         # -3
r ?= MATH  trunc(2.9)          # 2      (round toward 0 — drop fractional part)
r ?= MATH  trunc(-2.9)         # -2
r ?= MATH  trunc(3.0)          # 3
```

---

## 19. Absolute Value and Sign

```
r ?= MATH  abs(5)              # 5
r ?= MATH  abs(-5)             # 5
r ?= MATH  abs(-3.7)           # 3.7
r ?= MATH  abs(0)              # 0
r ?= MATH  sign(10)            # 1      (positive)
r ?= MATH  sign(-10)           # -1     (negative)
r ?= MATH  sign(0)             # 0      (zero)
r ?= MATH  sign(-0.001)        # -1
```

---

## 20. Two-Argument Functions

### `pow(base, exponent)` — power as a function

```
r ?= MATH  pow(2, 10)          # 1024
r ?= MATH  pow(3, 3)           # 27
r ?= MATH  pow(4, 0.5)         # 2      (square root)
r ?= MATH  pow(2, -1)          # 0.5
```

### `atan2(y, x)` — four-quadrant inverse tangent

Returns the angle in radians of the point `(x, y)` from the positive x-axis.
Range: `(-π, π]`.

```
r ?= MATH  atan2(0, 1)         # 0         (angle of (1, 0) = 0°)
r ?= MATH  atan2(1, 0)         # 1.5707963267949   (90°, positive y-axis)
r ?= MATH  atan2(0, -1)        # 3.14159265358979  (180°)
r ?= MATH  atan2(-1, 0)        # -1.5707963267949  (-90°)
r ?= MATH  atan2(1, 1)         # 0.785398163397448 (45°)
```

### `min(a, b)` and `max(a, b)`

```
r ?= MATH  min(3, 7)           # 3
r ?= MATH  min(-5, -2)         # -5
r ?= MATH  min(0, 0)           # 0
r ?= MATH  max(3, 7)           # 7
r ?= MATH  max(-5, -2)         # -2
r ?= MATH  max(4.5, 4.5)       # 4.5
```

### `hypot(a, b)` — Euclidean distance

`hypot(a, b)` = √(a² + b²). Numerically stable — avoids overflow/underflow.

```
r ?= MATH  hypot(3, 4)         # 5
r ?= MATH  hypot(5, 12)        # 13
r ?= MATH  hypot(1, 1)         # 1.4142135623731
```

### `fmod(a, b)` — floating-point remainder

Result has the same sign as `a`. Throws if `b == 0`.

```
r ?= MATH  fmod(7.5, 2.0)      # 1.5
r ?= MATH  fmod(10.0, 3.0)     # 1
r ?= MATH  fmod(-7.5, 2.0)     # -1.5   (sign follows dividend)
r ?= MATH  fmod(5.0, 2.5)      # 0
```

---

## 21. Operator Precedence Summary

From highest to lowest. Operators on the same row have equal precedence.

| Level | Operator(s) | Associativity | Description |
|------:|------------|---------------|-------------|
| 13 | `(…)` | — | Parentheses |
| 12 | `+` `-` `!` `~` *(unary)* | Right | Unary plus, minus, logical NOT, bitwise NOT |
| 11 | `**` | Right | Power |
| 10 | `*` `/` `//` `%` | Left | Multiply, divide, floor-divide, modulo |
| 9 | `+` `-` | Left | Add, subtract |
| 8 | `<<` `>>` | Left | Bitwise shift |
| 7 | `<` `<=` `>` `>=` | Left | Relational comparison |
| 6 | `==` `!=` | Left | Equality comparison |
| 5 | `&` | Left | Bitwise AND |
| 4 | `^` | Left | Bitwise XOR |
| 3 | `\|` | Left | Bitwise OR |
| 2 | `&&` | Left | Logical AND |
| 1 | `\|\|` | Left | Logical OR |
| 0 | `? :` | Right | Ternary |
| −1 | `=` | Right | Assignment (to Calculator internal variable) |

### Precedence in practice

```
r ?= MATH  2 + 3 * 4           # 14  (* before +)
r ?= MATH  (2 + 3) * 4         # 20  (parentheses first)
r ?= MATH  2 ** 3 ** 2         # 512 (right-assoc: 2 ** (3**2) = 2**9)
r ?= MATH  8 >> 1 + 1          # 2   (+ before >>: 8 >> 2)
r ?= MATH  3 > 2 && 5 < 10     # 1   (relational before &&)
r ?= MATH  1 || 0 && 0         # 1   (&& before ||: 1 || (0 && 0))
r ?= MATH  5 > 3 ? 10 : 20     # 10  (relational before ternary)
```

---

## Error Conditions

| Condition | Example | Behaviour |
|-----------|---------|-----------|
| Division by zero | `5 / 0` | Throws `runtime_error` |
| Floor-division by zero | `5 // 0` | Throws `runtime_error` |
| Modulo by zero | `5 % 0` | Throws `runtime_error` |
| `fmod` with zero divisor | `fmod(5, 0)` | Throws `runtime_error` |
| `sqrt` negative argument | `sqrt(-1)` | Throws domain error |
| `log`/`log2`/`log10` argument ≤ 0 | `log(0)` | Throws domain error |
| `asin`/`acos` argument outside `[-1, 1]` | `asin(2)` | Throws domain error |
| `log_b` invalid base | `log_b(8, 1)` | Throws `runtime_error` |
| Bitwise op on non-finite | `~inf` | Throws `runtime_error` |
| Bitwise op on out-of-range | `~1e20` | Throws `runtime_error` |
| Undefined variable | `$unknown` (unexpanded) | Throws `runtime_error` |
| Unmatched parenthesis | `(2 + 3` | Throws `runtime_error` |
| Trailing unconsumed input | `3 + + +` | Throws `runtime_error` |

All errors are caught by the interpreter, logged, and cause the MATH statement
to fail — which aborts the script at that line.
