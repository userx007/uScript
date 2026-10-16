#!/bin/bash

konsole -e "bash -c 'sudo tcpdump -ni lo -s 0 -vv udp'" &