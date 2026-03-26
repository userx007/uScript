#!/bin/bash

VENDOR="10c4"
PRODUCT="ea90"
RULES_FILE="/etc/udev/rules.d/99-hidraw.rules"

cat > "$RULES_FILE" << EOF
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="$VENDOR", ATTRS{idProduct}=="$PRODUCT", MODE="0666"
EOF

# Reload udev rules
udevadm control --reload-rules
udevadm trigger

echo "udev rule installed: $RULES_FILE"