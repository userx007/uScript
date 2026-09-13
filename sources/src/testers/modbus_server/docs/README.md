# modbus_server

A small, self-contained Modbus TCP server — a test target for
`modbus_plugin` (or any Modbus TCP master) that needs no Python, no
pymodbus, and no external Modbus tooling at all. Just POSIX sockets and
the C++ standard library.

Architecture-wise it mirrors pymodbus: a `ModbusDataStore`
(`modbus_datastore.hpp`) holding the four independently-addressed data
tables (coils, discrete inputs, holding registers, input registers), and
`modbus_server.cpp` playing the role of pymodbus's server/context —
decoding each request PDU, calling into the datastore, and encoding the
response (or a proper Modbus exception) back.

---

## Supported functions

| Code | Function |
|------|----------|
| 0x01 | Read Coils |
| 0x02 | Read Discrete Inputs |
| 0x03 | Read Holding Registers |
| 0x04 | Read Input Registers |
| 0x05 | Write Single Coil |
| 0x06 | Write Single Register |
| 0x0F | Write Multiple Coils |
| 0x10 | Write Multiple Registers |

Anything else, or a malformed/out-of-range request, gets a proper Modbus
exception response (illegal function / illegal data address / illegal
data value) rather than being silently ignored.

---

## Build

```bash
./build.sh
```

Just `g++ -std=c++20 -O2 -pthread` under the hood — see `build.sh`.

---

## Run

```bash
./modbus_server                    # listens on 0.0.0.0:5020
./modbus_server --port 502         # standard Modbus port (needs root)
./modbus_server --port 5020 -v     # verbose: hex-dumps every request/response ADU
```

On startup it seeds a little test data so there's something to read right
away:
- Coils 0-7: `1,0,1,1,0,0,0,1`
- Holding registers 100-103: `12,34,56,78`

All four tables are shared across every connected client (guarded by a
mutex), so a write from one client is immediately visible to a read from
another — the same behaviour as a real Modbus TCP gateway multiplexing
several masters onto one slave.

Stop with Ctrl-C (SIGINT/SIGTERM are handled for a clean shutdown).

---

## Smoke test

```bash
./build.sh
./test.sh
```

Starts the server on a scratch port, exercises a read of the seeded data,
a write-then-read-back round trip, and an out-of-range read (checking the
exception response), then stops the server. Uses only python3's stdlib
`socket` module — no pymodbus needed here either.

---

## Testing modbus_plugin against it

```
MODBUS.CONFIG h=127.0.0.1 p=5020
MODBUS.CMD > READ_HOLDING_REGISTERS 1 100 4 | 12,34,56,78
MODBUS.CMD > WRITE_SINGLE_REGISTER 1 10 1234 | OK
MODBUS.CMD > READ_HOLDING_REGISTERS 1 10 1 | 1234
```
