#!/bin/bash
cd "$(dirname "$0")"

./grpc_loopback_server 50051 127.0.0.1
