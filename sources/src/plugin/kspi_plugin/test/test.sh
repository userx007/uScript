#! /bin/bash

# ----------- NOTE -----------------------------------------------------
#
# Uses spidev with a physical MOSI→MISO jumper wire for true data echo.
#
# ----------------------------------------------------------------------

./spi_loopback_test                         # /dev/spidev0.0, 500 kHz, mode 0
./spi_loopback_test /dev/spidev1.0 1000000  # custom device & speed
./spi_loopback_test /dev/spidev0.0 500000 3 # SPI mode 3