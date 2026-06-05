#! /bin/bash

./i2c_stub_test /dev/i2c-5 0x50            # dump all 256 registers
./i2c_stub_test /dev/i2c-5 0x50 0x10       # read register 0x10
./i2c_stub_test /dev/i2c-5 0x50 0x10 0xAB  # write 0xAB, read back & verify