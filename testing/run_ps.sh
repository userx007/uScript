#!/bin/bash

# Run the two executables with the PTYs as arguments
./uart_responder /dev/pts/2 &
./uart_sender /dev/pts/3


