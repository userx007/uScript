# Comm Script Interpreter — README

## Overview

The **Comm Script** system is a communication-oriented script interpreter built to drive any `ICommDriver`-derived transport (serial port, TCP socket, USB HID, etc.) through a terse, line-oriented scripting language. Each script line describes a **directed data exchange** — send, receive, or a timed delay — with rich data-type annotations for both sides of the exchange.

Unlike the Core Script system, Comm scripts have **no plugin machinery**, no `IF/GOTO` flow control, and no variable macros. They focus exclusively on protocol-level send/receive sequencing with precise data-type validation.

---

## Architecture

```
┌──────────────────────────────────────────────────────────────────────────┐
│                         CommScriptClient<TDriver>                        │
│  Template facade: wires the pipeline to a concrete driver instance,      │
│  exposes execute().                                                      │
└────────────────────────────────┬─────────────────────────────────────────┘
                                 │ owns
                                 ▼
┌──────────────────────────────────────────────────────────────────────────┐
│              CommScriptRunner<CommCommandsType, TDriver>                 │
│  Extends ScriptRunner; holds a typed reference to the comm interpreter   │
│  for potential post-run access (e.g. getLastReceived()).                 │
│  Inherits:  read → validate → interpret                                  │
└───────┬──────────────────────┬──────────────────────────────┬────────────┘
        │                      │                              │
 owns   │           owns       │              owns            │
        ▼                      ▼                              ▼
┌──────────────┐   ┌───────────────────────┐   ┌──────────────────────────┐
│ ScriptReader │   │  CommScriptValidator  │   │  CommScriptInterpreter   │
│  (shared)    │   │                       │   │     <TDriver>            │
│              │   │ Line-by-line:         │   │                          │
│ Same reader  │   │  • expand $MACROs     │   │ Iterates vCommands[],    │
│ as Core:     │   │  • if MACRO := style, │   │ dispatches each to:      │
│  comments,   │   │    store in mapMacros │   │                          │
│  blank lines,│   │  • else validate as   │   │  CommScriptCommand-      │
│  block       │   │    CommCommand via    │   │  Interpreter<TDriver>    │
│  comments    │   │    CommScriptCommand- │   │                          │
│              │   │    Validator          │   │ Applies inter-command    │
└──────────────┘   │  • push to vCommands  │   │ delay (m_szDelay)        │
                   └───────────┬───────────┘   └──────────────┬───────────┘
                               │                              │
                               ▼                              ▼
                   ┌───────────────────────┐   ┌──────────────────────────┐
                   │ CommScriptCommand-    │   │  CommScriptCommand-      │
                   │ Validator             │   │  Interpreter<TDriver>    │
                   │                       │   │                          │
                   │ ItemParser:           │   │ executeSend()            │
                   │ 1. parseDirection()   │   │  convertToData()         │
                   │ 2. splitFields()      │   │  driver->tout_write()    │
                   │ 3. getTokenType() ×2  │   │                          │
                   │ 4. evaluateAndValidate│   │ executeReceive()         │
                   │                       │   │  receiveAndMatchRegex()  │
                   │ Returns CommCommand   │   │  receiveUntilToken()     │
                   │  {direction,          │   │  receiveExactSize()      │
                   │   values{v1,v2},      │   │  receiveUntilDelimiter() │
                   │   tokens{t1,t2}}      │   │  receiveAndCompare()     │
                   └───────────────────────┘   │  receiveToFile()         │
                                               │  sendFile()              │
                                               │                          │
                                               │ driver->tout_read()      │
                                               │ driver->tout_write()     │
                                               └──────────────────────────┘
```

### Data Flow

```
 Comm script file (.txt)
        │
        ▼
  ScriptReader::readScript()           ← shared with Core Script
  ─────────────────────────────────────
  • Strips #-comments, blank lines, --- ... !-- blocks
  • Returns vector<string> of clean lines
        │
        ▼
  CommScriptValidator::validateScript()
  ─────────────────────────────────────
  For each line:
    1. replaceMacros($NAME)  ← expand already-defined constant macros
    2. if matches  NAME := value  → store in mapMacros{}, continue
    3. else CommScriptCommandValidator::validateCommand()
         → ItemParser::parse()
              a. parseDirection()    → SEND_RECV / RECV_SEND / DELAY
              b. splitFields()       → field1, field2  (pipe-aware quoting)
              c. getTokenType(f1)    → CommCommandTokenType (+ validation)
              d. getTokenType(f2)    → CommCommandTokenType (+ validation)
              e. evaluateAndValidate() → semantic direction/type rules
         → CommCommand{ direction, values{f1,f2}, tokens{t1,t2} }
         → push to vCommands[]
        │
        │  CommCommandsType { vCommands[], mapMacros{} }
        ▼
  CommScriptInterpreter::interpretScript()
  ─────────────────────────────────────────
  For each CommCommand in vCommands[]:
    CommScriptCommandInterpreter::interpretCommand(command)
      ┌─ SEND_RECV:  executeSend(values.first, tokens.first)
      │              executeReceive(values.second, tokens.second)
      ├─ RECV_SEND:  executeReceive(values.first, tokens.first)
      │              executeReceive(values.second, tokens.second)
      └─ DELAY:      delay_us / delay_ms / delay_seconds
    delay_ms(m_szDelay)   ← configurable inter-command pause
```

---

## Internal Intermediate Representation (IR)

```
CommCommandsType
├── mapMacros : unordered_map<string,string>
│     "MACRO_NAME" → "literal value"     (constant macros, $-substituted)
│
└── vCommands : vector<CommCommand>
      CommCommand {
        direction : CommCommandDirection  { SEND_RECV | RECV_SEND | DELAY | INVALID }
        values    : pair<string,string>   { field1_value, field2_value }
        tokens    : pair<CommCommandTokenType, CommCommandTokenType>
      }
```

---

## Line Syntax

Every non-blank, non-comment line is one of three forms:

### Form 1 — Constant Macro

```
MACRO_NAME  :=  literal value
```

Identical in syntax to Core Script constant macros. The value is substituted
into subsequent lines via `$MACRO_NAME` at validation time.

---

### Form 2 — Command (Send / Receive)

```
<direction>  <expr1>  [|  <expr2>]
```

| Symbol | Direction | Meaning |
|--------|-----------|---------|
| `>`    | SEND_RECV | Send `expr1`, then optionally receive/validate `expr2` |
| `<`    | RECV_SEND | Receive/validate `expr1`, then optionally send `expr2` |

- The pipe `|` separator is **mandatory** only when both sides are present.
- A pipe inside a quoted expression (`"..."`) is **preserved** and not treated as a separator.

---

### Form 3 — Delay

```
!  <value>  <unit>
```

| Unit | Meaning |
|------|---------|
| `us` | Microseconds |
| `ms` | Milliseconds |
| `s`  | Seconds |

---

## Data Type Decorators

Each expression `expr1` or `expr2` is annotated with an optional **prefix decorator** that determines how the value is encoded/decoded. The decorator is a single uppercase letter immediately followed by `"..."`.

| Decorator | Token Type | Description | Valid for |
|-----------|------------|-------------|-----------|
| *(none — quotes)* | `STRING_DELIMITED` | Quoted literal string: `"hello world"` | Send & Receive |
| `""` | `STRING_DELIMITED_EMPTY` | Empty quoted string | Send only |
| *(none — no quotes)* | `STRING_RAW` | Unquoted plain token: `hello` | Send & Receive |
| `H"…"` | `HEXSTREAM` | Hex byte stream: `H"4A6F686E00"` | Send & Receive |
| `R"…"` | `REGEX` | Regex pattern for matching received data: `R"OK.*\r\n"` | Receive only |
| `F"…"` | `FILENAME` | File path (must exist and be non-empty): `F"firmware.bin"` | Send & Receive |
| `T"…"` | `TOKEN_STRING` | Receive until string token found: `T"OK"` | Receive only |
| `X"…"` | `TOKEN_HEXSTREAM` | Receive until hex-byte sequence found: `X"CAFE00FF"` | Receive only |
| `L"…"` | `LINE` | Read/compare a newline-terminated line: `L"OK"` | Send & Receive |
| `S"…"` | `SIZEOF` | Receive exactly N bytes: `S"256"` | Receive only |

> **Note on `F"…"` file format:**
> - Send file: `F"path/file.bin"` or `F"path/file.bin,chunksize"` (default chunk = 1024 bytes)
> - Receive to file: `F"out.bin"` or `F"out.bin,expected_size"` or `F"out.bin,expected_size,chunksize"`

---

## Semantic Validation Rules

The `evaluateAndValidate()` step enforces direction-aware rules:

| Rule | Detail |
|------|--------|
| **Cannot send** `TOKEN_STRING`, `TOKEN_HEXSTREAM`, `SIZEOF`, `REGEX`, `EMPTY` | These are receive-only concepts |
| **Cannot receive** `EMPTY` or `STRING_DELIMITED_EMPTY` | Receiving nothing is not meaningful |
| Both expressions cannot be `EMPTY` | At least one side must carry data |
| `DELAY` value must be a numeric string convertible to `size_t` | Non-numeric values are rejected at validation time |
| File type (`FILENAME`) validated at parse time | File must exist on disk and be non-empty |
| Hex streams (`HEXSTREAM`, `TOKEN_HEXSTREAM`) validated for hex format | Non-hex characters are rejected |
| Size value (`SIZEOF`) validated as a positive numeric | Must parse as a valid `size_t` |
| Regex patterns (`REGEX`) must be non-empty | Empty pattern is rejected |

---

## Supported Syntax Reference

### Comments

```
# Line comment — entire line ignored

---
   Block comment
   All lines until closing marker are skipped
!--

> "hello"   # inline comment after command
```

### Constant Macros

```
PROMPT      := login:
BAUD        := 115200
CMD_TIMEOUT := 5000
FW_PATH     := /opt/fw/image.bin
```

Macros are expanded with `$` prefix:

```
> "$PROMPT"
```

### Send — `>` (Send only)

```
> "hello\r\n"                  # send delimited string
> H"0D0A"                      # send raw bytes (CRLF)
> raw_command                  # send unquoted raw string
> L"AT"                        # send line (string + newline)
> F"firmware.bin"              # send file in 1024-byte chunks
> F"firmware.bin,512"          # send file in 512-byte chunks
```

### Send then Receive — `> expr | expr`

```
> "AT\r\n"         | T"OK"             # send string, wait for token
> H"DEADBEEF"      | H"CAFE0001"       # send hex, expect exact hex response
> "GET / HTTP/1.0" | R"HTTP/1\.[01].*" # send string, match regex
> "READ_TEMP\r\n"  | L"25.3"           # send, receive matching line
> "DUMP\r\n"       | S"128"            # send, receive exactly 128 bytes
> F"image.bin"     | T"DONE"           # send file, wait for completion token
```

### Receive — `<` (Receive only)

```
< T"login:"                    # wait for string token
< X"FF00CAFE"                  # wait for hex-byte sequence
< R"[0-9]{1,3}\.[0-9]{1,3}"   # receive and match regex
< "expected response"          # receive and compare exact string
< H"4F4B0D0A"                  # receive and compare exact bytes
< S"64"                        # receive exactly 64 bytes
< L"READY"                     # receive until newline, compare to "READY"
< F"received.bin,4096"         # receive 4096 bytes, write to file
```

### Receive then Send — `< expr | expr`

```
< T"login:"    | "admin\r\n"   # wait for prompt, send username
< T"Password:" | "secret\r\n"  # wait for password prompt, send it
< "OK"         | "NEXT\r\n"    # receive exact confirmation, send next command
< L"READY"     | H"01020304"   # wait for line, send binary header
```

### Delay — `!`

```
! 100 ms        # wait 100 milliseconds
! 500 us        # wait 500 microseconds
! 2 s           # wait 2 seconds
```

---

## Receive Modes — Implementation Details

| Token Type | Driver Read Mode | Behavior |
|------------|-----------------|----------|
| `STRING_DELIMITED`, `STRING_RAW`, `HEXSTREAM` | `Exact` | Read up to `maxRecvSize` bytes, compare with expected |
| `REGEX` | `Exact` | Read up to `maxRecvSize` bytes, apply `std::regex_match` |
| `TOKEN_STRING` | `UntilToken` | Read until the exact string sequence is found in the stream |
| `TOKEN_HEXSTREAM` | `UntilToken` | Read until the exact byte sequence is found in the stream |
| `LINE` | `UntilDelimiter('\n')` | Read until newline; optionally compare content |
| `SIZEOF` | `Exact` | Read exactly N bytes; verify count matches |
| `FILENAME` | `Exact` (chunked) | Write received chunks to a file; stop at expected size |

---

## Complete Example Script

```
# =============================================================================
# Example: Embedded device boot & firmware update via serial
# =============================================================================

/*
  Scenario:
    1. Device boots and prints a login prompt.
    2. We authenticate, then check running firmware version.
    3. If version is old, we send the new firmware binary.
    4. Confirm flash success and reboot.
*/

# Constant macros
USER     := admin
PASS     := secret123
FW_BIN   := /opt/firmware/v3.1.bin

# ---------------------------------------------------------------------------
# Phase 1: Login sequence
# ---------------------------------------------------------------------------

# Wait for device boot banner (regex: any version string)
< R".*Boot v[0-9]+\.[0-9]+.*"

# Wait for login prompt, send credentials
< T"login:"     | "$USER\r\n"
< T"Password:"  | "$PASS\r\n"

# Expect the shell prompt (exact string)
< "$ "

# ---------------------------------------------------------------------------
# Phase 2: Check current firmware version
# ---------------------------------------------------------------------------

# Send version query command
> "fw_version\r\n"   | L"v2.9.0"

# Small pause before next command
! 200 ms

# ---------------------------------------------------------------------------
# Phase 3: Initiate firmware update
# ---------------------------------------------------------------------------

# Send the update start command
> "fw_update start\r\n"   | T"READY"

# Delay for device to prepare its flash buffer
! 500 ms

# Transfer the firmware binary (default 1024-byte chunks)
> F"$FW_BIN"   | T"DONE"

# ---------------------------------------------------------------------------
# Phase 4: Verify and reboot
# ---------------------------------------------------------------------------

! 1 s

# Query version after flashing — expect the new version
> "fw_version\r\n"   | L"v3.1.0"

# Send reboot command, device will stop responding
> "reboot\r\n"

# Wait for device to come back up (boot banner with new version)
! 3 s
< R".*Boot v3\.1\.0.*"

# ---------------------------------------------------------------------------
# Phase 5: Binary data exchange example
# ---------------------------------------------------------------------------

# Send a binary probe packet and receive exact 8-byte ACK
> H"AA5501000000FF"   | H"AA5500000000FF"

# Receive a variable-length certificate (exact byte count)
> "get_cert\r\n"   | S"512"

# Save a full memory dump (receive 65536 bytes) to file
> "mem_dump\r\n"   | F"/tmp/memdump.bin,65536"

# Send a pre-built binary image file in 256-byte chunks, wait for token
> F"/tmp/payload.bin,256"   | X"DEADBEEF"
```

---

## Configuration Parameters

`CommScriptInterpreter` (and by extension `CommScriptClient`) accepts the following parameters at construction:

| Parameter | Default | Description |
|-----------|---------|-------------|
| `shpDriver` | — | Shared pointer to the concrete `ICommDriver` implementation |
| `szMaxRecvSize` | 4096 | Maximum receive buffer size in bytes |
| `u32DefaultTimeout` | 5000 | Default I/O timeout in milliseconds |
| `szDelay` | 0 | Inter-command delay in milliseconds |

---

## Driver Interface Contract (`ICommDriver`)

The Comm Script system is fully driver-agnostic. The concrete driver must implement:

```cpp
// Non-blocking write with timeout
WriteResult tout_write(uint32_t timeout_ms, std::span<const uint8_t> data);

// Flexible read with timeout and read options
ReadResult  tout_read(uint32_t timeout_ms,
                      std::span<uint8_t> buffer,
                      ReadOptions options);

bool is_open() const;
```

`ReadOptions` selects the read strategy:
- `ReadMode::Exact` — fill buffer exactly
- `ReadMode::UntilToken` — search for a byte pattern; sets `found_terminator`
- `ReadMode::UntilDelimiter` — read until a single-byte delimiter (e.g. `\n`)

---

## Component Summary

| Component | File | Role |
|-----------|------|------|
| `CommScriptClient<TDriver>` | `uCommScriptClient.hpp` | Facade: wires pipeline to a driver, exposes `execute()` |
| `CommScriptRunner<TScriptEntries, TDriver>` | `uCommScriptRunner.hpp` | Extends `ScriptRunner`; holds typed interpreter reference |
| `ScriptReader` | `uScriptReader.hpp` | **Shared** file reader (same as Core Script) |
| `CommScriptValidator` | `uCommScriptValidator.hpp` | Validates & builds `CommCommandsType` IR |
| `CommScriptCommandValidator` | `uCommScriptCommandValidator.hpp` | Parses direction, splits fields, classifies token types, semantic rules |
| `CommScriptInterpreter<TDriver>` | `uCommScriptInterpreter.hpp` | Iterates commands, applies inter-command delay |
| `CommScriptCommandInterpreter<TDriver>` | `uCommScriptCommandInterpreter.hpp` | Executes individual send/receive/delay commands against driver |
| `CommScriptDataTypes` | `uCommScriptDataTypes.hpp` | `CommCommand`, `CommCommandDirection`, `CommCommandTokenType` enums & helpers |
