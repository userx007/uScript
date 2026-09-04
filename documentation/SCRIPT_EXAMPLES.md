# Script examples

## Buspirate 

### Scan I2C at a specific address

**Binary Mode**

```
LOAD_PLUGIN BUSPIRATE

BUSPIRATE.MODE bitbang
BUSPIRATE.MODE i2c
BUSPIRATE.I2C speed 100kHz
BUSPIRATE.I2C per WP
BUSPIRATE.I2C scan all 
BUSPIRATE.I2C exit
BUSPIRATE.MODE reset
```
---

**Text Mode**

```
LOAD_PLUGIN UART

UART.SCRIPT bp_i2c_scan.txt
```

> bp_i2c_scan.txt
```
> m\r |		# mode
! 100ms

> 4\r |		# i2c
! 100ms

> 3\r |		# speed 100KHz
! 500ms

> W\r 		# power supply ON
! 200ms

> P\r |     # pull-up resistors ON
! 1000ms

> (1)\r |   # macro (1) - scan
! 5000ms

<           # read UART buffer to get the scan result
```

---

## Core Script — `EVAL`

### Comparing strings that contain spaces

Unquoted operands end at the first space; wrap the value in double quotes to
compare strings containing spaces. The quotes are stripped before the
comparison, and matching them requires an exact, case-sensitive match:

```
LOAD_PLUGIN  UART

UART.OPEN  /dev/ttyUSB0  115200
label  ?=  UART.READ_LINE          # e.g. "Hello World"
UART.CLOSE

ok  ?=  EVAL  $label == "Hello World" |STR
IF  EVAL  $label != "Hello World"  GOTO  mismatch
    PRINT  Label matches: $label
    GOTO  done
LABEL  mismatch
    PRINT  Unexpected label: $label
LABEL  done
```

### Trailing tokens fail the expression

Anything left after the last operand/type hint that isn't `&&`/`||` makes
the whole `EVAL` fail — it is a syntax error, not something that's ignored:

```
# Both of these FAIL — they do not evaluate to FALSE, they abort the EVAL:
ok  ?=  EVAL  TRUE == FALSE hjghghj
ok  ?=  EVAL  "Hello World" == "Hello World" jhhhh
```

### Emulating `IF / ELSIF / ELSE` with `IF … GOTO`

There is no native `ELSIF`/`ELSE` keyword. The same behaviour is built from
`IF … GOTO`: each branch is guarded by the **negated** condition (skip the
branch when the condition is false), and every branch except the last ends
with an unconditional jump to a shared `end_if` label so exactly one branch
runs:

```
status  ?=  SENSOR.READ_STATUS

IF  EVAL  $status != "OK"    GOTO  not_ok
    PRINT  branch: OK
    GOTO  end_if
LABEL  not_ok

IF  EVAL  $status != "WARN"  GOTO  not_warn
    PRINT  branch: WARN
    GOTO  end_if
LABEL  not_warn

# else — reached only if neither guard above matched
PRINT  branch: ERROR

LABEL  end_if
```

Equivalent to:

```
if   status == "OK":    branch: OK
elif status == "WARN":  branch: WARN
else:                    branch: ERROR
```

Add more `ELSIF` rungs by repeating `IF EVAL <negated condition> GOTO not_X`
/ branch body / `GOTO end_if` / `LABEL not_X` before the final `ELSE` body
and the closing `LABEL end_if`.

---

