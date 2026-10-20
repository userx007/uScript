#! /bin/bash

konsole -e "bash -c '../../testers/bin/uart_loopback /dev/tnt1 115200'" &
konsole -e "bash -c '../../testers/bin/uart_loopback /dev/tnt3 115200'" &
konsole -e "bash -c '../../testers/bin/uart_loopback /dev/tnt5 115200'" &