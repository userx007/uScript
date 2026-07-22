#!/bin/bash

konsole \
  --new-tab  --profile "test" -e "bash -c 'kvcan_run.sh; exec bash'" \
  --new-tab  --profile "test" -e "bash -c 'raweth_run.sh; exec bash'" \
  --new-tab  --profile "test" -e "bash -c 'tcpip_run.sh; exec bash'" \
  --new-tab  --profile "test" -e "bash -c 'udp_run.sh; exec bash'" \
  --new-tab  --profile "test" -e "bash -c 'uart_run.sh; exec bash'"
