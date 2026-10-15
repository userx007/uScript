#! /bin/bash

ls -l /dev/t*
#../../testers/bin/uart_loopback /dev/tnt0 115200
konsole -e "bash -c '../../testers/bin/uart_loopback /dev/tnt1 115200'" &