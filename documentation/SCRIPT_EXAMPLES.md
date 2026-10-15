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

## Core Script — `GENERATOR`

A native background signal generator — no plugin required. It writes a new value into a
macro every tick until it is stopped with `KILL` (or the script ends):

```
# Each line starts a generator thread; $name always holds the latest sample.
ctr    ?=  GENERATOR  100 ms  0:255:1      |  SAWTOOTH  |  HEX_8      # 1, 2 .. 255, 0 ... as 1-byte hex
cnt    ?=  GENERATOR  100 ms  255:0:1      |  SAWTOOTH               # reverse range: counts down
angle  ?=  GENERATOR  20  ms  0:360:1      |  SINE
lvl    ?=  GENERATOR  50  ms  0:100:5      |  TRIANGLE  |  HEX_16_LE
gpio   ?=  GENERATOR  500 ms  0:1:1        |  SQUARE                 # step = ticks per level
dice   ?=  GENERATOR  200 ms  1:6:1        |  RANDOM

DELAY  1  sec
PRINT  ctr=$ctr  angle=$angle  dice=$dice
```

Array sources (`SAWTOOTH`, `TRIANGLE` and `RANDOM` only):

```
x      ?=  7
seq    ?=  GENERATOR  200 ms  1,7,$x,8,9  |  SAWTOOTH     # inline list
LIST   [=  10, 20, 30
pick   ?=  GENERATOR  200 ms  $LIST        |  RANDOM       # shuffled playlist of a declared array
```

Re-running a `GENERATOR` for the same name restarts it — handy for sweeping a parameter:

```
s  ?=  REPEAT  sweep  5, 20, 5
    level  ?=  GENERATOR  50 ms  0:100:$s  |  TRIANGLE
    DELAY  1  sec
END_REPEAT  sweep
```

---

## Core Script — `KILL`

### Stopping a background command or generator

`KILL` takes the **name of the volatile macro** that a background activity
keeps refreshing — never the command itself. Capture a threaded (`&`) command
into a macro when you launch it, then kill that macro. The optional `result ?=`
prefix receives how many threads were actually stopped (`1`, or `0` if nothing
was left to stop — which is not an error):

```
LOAD_PLUGIN  UART

UART.CONFIG  p=/dev/ttyUSB0  b=115200

# threaded capture — sends on a schedule in the background until killed
retval  ?=  UART.CYCLIC  100:> H'AABBCCDD', 250:> 'ping\r\n'  &
DELAY  2  sec

stopped  ?=  KILL  retval
PRINT  stopped $stopped thread(s)           # 1
```

The same works for `GENERATOR`. `$ctr` keeps its last value after the `KILL`:

```
ctr  ?=  GENERATOR  100 ms  0:255:1  |  SAWTOOTH
DELAY  1  sec
KILL  ctr
PRINT  frozen at $ctr
```

Anything still running at the end of the script is stopped automatically.
There is no `GENERATOR STOP` / `STOP ALL` — `KILL name` is the only way to stop
one early.

### Things `KILL` rejects at validation time

```
KILL                          # missing target
ok  ?=  KILL  UART.CYCLIC     # a command, not a volatile macro
KILL  UART:2.CMD              # ditto, instanced
KILL  never_started           # never launched with '&' or GENERATOR on an earlier line

# launched WITHOUT '&' — already finished, nothing to kill
val  ?=  UART.CMD  <
KILL  val
```

---

## Core Script — `CYCLIC` and `CMD` results

`CYCLIC` (available on every comm-driver plugin) takes comma-separated
`time_ms:command` entries; each command is a full `CMD` argument. Without `&` it
sends one full pattern and returns; with `&` it keeps going until killed:

```
UART.CYCLIC  100:> H'AABBCCDD', 250:> 'ping\r\n'          # one super-period (500 ms), then returns
tick  ?=  UART.CYCLIC  1000:> 'hello'  &                   # forever, in the background
```

A `name ?= PLUGIN.CMD …` captures what the command *received*, hexlified by default
(`raw=1` returns the raw bytes):

```
reply  ?=  UART.CMD  >  'AT\r\n'  |                        # send, then read whatever arrives
```

---

## Core Script — `MATH` with hex output

```
a  ?=  MATH  255   |  HEX              # FF
b  ?=  MATH  255   |  HEX_16_BE        # 00FF
c  ?=  MATH  255   |  HEX_16_LE        # FF00
d  ?=  MATH  -1.0  |  HEX_FLOAT_BE     # BF800000
```

---

## Core Script — `INCLUDE`

```
# common.script
LOAD_PLUGIN  UART
BAUD     :=  115200
BOARDS   [=  /dev/ttyUSB0, /dev/ttyUSB1
```

```
# main.script — included lines are placed ahead of this file's own lines
INCLUDE  "common.script"

i  ?=  REPEAT  boards  $BOARDS.SIZE
    UART.CONFIG  p=$BOARDS.$i  b=$BAUD
END_REPEAT  boards
```

---

## Comm Script (`.txt` files used by `PLUGIN.SCRIPT`)

Comm scripts use **single-quote** decorators. `>` sends (then optionally receives after
`|`), `<` receives (then optionally sends), `!` delays, `@` prints:

```
USER := admin                                    # constant macro

@ Starting login sequence                        # log at INFO, no I/O
< T'login:'        | 'admin\r\n'                 # wait for token, then send
< T'Password:'     | '$USER\r\n'
> 'AT\r\n'         | T'OK'                       # send string, wait for token
> H'DEADBEEF'      | H'CAFE0001'                 # send hex, expect exact hex
> 'GET / HTTP/1.0' | R'HTTP/1\.[01].*'           # regex match
> 'DUMP\r\n'       | S'128'                      # exactly 128 bytes
> F'firmware.bin,512'  | T'DONE'                 # send a file in 512-byte chunks
< F'capture.bin,4096'                            # receive 4096 bytes into a file
! 200 ms                                         # delay: us | ms | sec
> 'ID?\r\n'        | L'v2.1'                     # line comparison
```

The optional `~ param` suffix passes a channel/address override to the driver
(for example a CAN id); `~ a | b` gives the send side `a` and the receive side `b`:

```
> H'AABBCCDD'  | H'06'   ~ 0x123
> H'AABBCCDD'  | H'06'   ~ 0x123 | 0x456
```

---
