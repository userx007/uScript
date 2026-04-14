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

