#!/bin/bash
# Quick smoke test for modbus_server. Starts the server (detached, output
# redirected to a log file so this script doesn't block on its stdout),
# exercises a read of its seeded data and a write+read-back round trip
# using nothing but python3's stdlib socket module (deliberately no
# pymodbus dependency -- this tool exists so modbus_plugin can be tested
# without pymodbus), then stops the server.
set -e

PORT=5021
LOG=/tmp/modbus_server_test.log

cleanup() {
    pkill -9 -f "modbus_server --port $PORT" >/dev/null 2>&1 || true
}
trap cleanup EXIT

setsid ./modbus_server --port "$PORT" > "$LOG" 2>&1 < /dev/null &
sleep 1

python3 - "$PORT" << 'EOF'
import socket, struct, sys

port = int(sys.argv[1])

def txn(sock, unit, pdu, txn_id=1):
    header = struct.pack(">HHH", txn_id, 0, 1 + len(pdu))
    sock.sendall(header + bytes([unit]) + pdu)
    prefix = sock.recv(6)
    length = struct.unpack(">H", prefix[4:6])[0]
    rest = b""
    while len(rest) < length:
        rest += sock.recv(length - len(rest))
    return rest[0], rest[1:]  # unit id, pdu

s = socket.create_connection(("127.0.0.1", port), timeout=3)

# READ_HOLDING_REGISTERS unit=1 addr=100 qty=4 -> expect 12,34,56,78 (seeded)
unit, pdu = txn(s, 1, struct.pack(">BHH", 0x03, 100, 4))
assert pdu[0] == 0x03, f"unexpected function code 0x{pdu[0]:02X}"
regs = struct.unpack(">4H", pdu[2:10])
assert regs == (12, 34, 56, 78), f"seeded registers mismatch: {regs}"
print("PASS: read seeded holding registers", regs)

# WRITE_SINGLE_REGISTER unit=1 addr=10 value=1234, then read it back
unit, pdu = txn(s, 1, struct.pack(">BHH", 0x06, 10, 1234), txn_id=2)
assert pdu == struct.pack(">BHH", 0x06, 10, 1234), "write echo mismatch"
unit, pdu = txn(s, 1, struct.pack(">BHH", 0x03, 10, 1), txn_id=3)
value = struct.unpack(">H", pdu[2:4])[0]
assert value == 1234, f"readback mismatch: {value}"
print("PASS: write + read back holding register ->", value)

# Exception path: illegal address (65530 + qty 10 overruns the 65536-entry table)
unit, pdu = txn(s, 1, struct.pack(">BHH", 0x03, 65530, 10), txn_id=4)
assert pdu[0] == (0x03 | 0x80), "expected exception function code"
assert pdu[1] == 0x02, f"expected illegal-address exception code, got {pdu[1]}"
print("PASS: out-of-range read correctly returned EXCEPTION code 2")

s.close()
print("ALL TESTS PASSED")
EOF
