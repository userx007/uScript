# Script examples

## Buspirate 

### Scan I2C at a specific address

```
LOAD_PLUGIN BUSPIRATE

BUSPIRATE.MODE bitbang
BUSPIRATE.MODE i2c
BUSPIRATE.I2C scan 0x3C
BUSPIRATE.I2C exit
BUSPIRATE.MODE reset
```