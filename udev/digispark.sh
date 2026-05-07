#!/bin/bash


# DigiSpark
sudo tee /etc/udev/rules.d/49-digispark.rules <<'EOF'
SUBSYSTEMS=="usb", ATTRS{idVendor}=="16d0", ATTRS{idProduct}=="0753", MODE:="0666"
SUBSYSTEMS=="usb", ATTRS{idVendor}=="16c0", ATTRS{idProduct}=="05df", MODE:="0666"
EOF

sudo udevadm control --reload-rules && sudo udevadm trigger