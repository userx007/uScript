# MQTT Plugin — Usage Tutorial

This walks through every `MQTT.*` command with a realistic scenario for each,
then shows how to run several independent MQTT sessions side by side using
the script engine's plugin-instance mechanism (`MQTT:1`, `MQTT:2`, ...).

It assumes a Mosquitto broker reachable at `broker.local`, and that scripts
are run through the core script engine described in
`src/script/core/README.md` (the `LOAD_PLUGIN` / `PLUGIN.COMMAND` / `?=` /
`&` syntax used throughout).

---

## Table of Contents

1. [Two different meanings of "instance"](#1-two-different-meanings-of-instance)
2. [Command reference, with a scenario for each](#2-command-reference-with-a-scenario-for-each)
   - [INFO](#info)
   - [CONFIG](#config)
   - [CMD](#cmd)
   - [SCRIPT](#script)
   - [SUBSCRIBE](#subscribe)
   - [UNSUBSCRIBE](#unsubscribe)
   - [RECEIVE](#receive)
   - [PING](#ping)
3. [End-to-end scenarios (single connection)](#3-end-to-end-scenarios-single-connection)
4. [Running several MQTT plugin instances in parallel](#4-running-several-mqtt-plugin-instances-in-parallel)
5. [CONFIG key reference](#5-config-key-reference)
6. [Gotchas](#6-gotchas)

---

## 1. Two different meanings of "instance"

Before mixing commands and parallel instances, it's worth separating two
things that both get called "instance" but operate at different levels:

**a) Connections *inside* one loaded MQTT plugin.**
Every loaded MQTT plugin (whether it's the only one, or one of several —
see (b)) internally keeps at most two live MQTT sessions:

- A **fresh** connection, opened and closed around every single `CMD` or
  `SCRIPT` call (unless `ss=true`, see below).
- One **persistent** connection, opened by the first `SUBSCRIBE` and kept
  alive for every later `SUBSCRIBE` / `UNSUBSCRIBE` / `RECEIVE` / `PING` call
  — including calls made from a background thread via `?= MQTT.RECEIVE &`.

`CONFIG ss=true` makes `CMD`/`SCRIPT` reuse that same persistent connection
instead of opening their own — useful when you want a publish to definitely
land on the exact same broker session your subscription is on (e.g. so a
`retain`ed message you just published is guaranteed visible before you
`RECEIVE` it back).

**b) Several independently loaded copies of the MQTT plugin itself,** each
with its own config (host, credentials, client id, Will, ...) and its own
pair of connections from (a). This is what the script engine's
`PLUGIN:N` syntax gives you — `MQTT:1`, `MQTT:2`, ... — see
[section 4](#4-running-several-mqtt-plugin-instances-in-parallel).

Sections 2–3 below cover a single loaded instance (or an unqualified
`MQTT.` — the very first plugin instance the engine sees is addressable
either as `MQTT` or the same way any instance is). Section 4 covers running
several `MQTT:N` instances together.

---

## 2. Command reference, with a scenario for each

### INFO

**Purpose:** print the plugin's version and a usage summary of every command
to the logger — a quick sanity check that the plugin loaded, with no broker
required.

```
MQTT.INFO
```

**Scenario:** the very first line of any new MQTT test script, right after
`LOAD_PLUGIN MQTT`, to confirm the build in use actually has the commands
you're about to rely on (e.g. `UNSUBSCRIBE`/`PING` if you're on an older
build of the plugin).

---

### CONFIG

**Purpose:** set or change connection parameters — host, port, TLS, auth,
Last Will, QoS/retain defaults, timeouts — without reloading the plugin.
Any subset of keys can be given; omitted keys keep their current value.
Every key can equally be set once via the `.ini` file's `[MQTT]` (or
`[MQTT:N]`) section instead of a `CONFIG` line — see
[section 5](#5-config-key-reference) for the full key table.

```
MQTT.CONFIG [h=host] [p=port] [q=qos] [t=tls] [r=retain] [ca=capath] [crt=certpath]
            [key=keypath] [rt=read_tout] [rb=read_bufsize] [id=clientid]
            [u=username] [pw=password] [wt=will_topic] [wp=will_payload]
            [wq=will_qos] [wr=will_retain] [cs=cleansession] [ss=share_session]
            [it=include_topic]
```

**Scenario:** point the plugin at a local Mosquitto instance for a test run,
then reconfigure mid-script to move to a different topic-QoS combination:

```
LOAD_PLUGIN MQTT

MQTT.CONFIG h=broker.local p=1883 q=1
MQTT.CMD > "21.5" ~ sensors/temp | PUBACK

# Now switch to QoS 2 and retained messages for a "last known state" topic
MQTT.CONFIG q=2 r=true
MQTT.CMD > "online" ~ devices/gateway/status | PUBCOMP
```

---

### CMD

**Purpose:** publish exactly one message on a live MQTT session, optionally
asserting the acknowledgement the broker sends back for QoS 1/2. This is the
*only* way to publish — there's no separate `PUBLISH` command. A fresh
CONNECT/CONNACK session is opened for the call and closed once it completes
(unless `ss=true` — see [section 1](#1-two-different-meanings-of-instance)).

```
MQTT.CMD > <payload> ~ <topic> [| expected_ack]
```

- Payload can be a quoted string (`"21.5"`) or hex bytes (`H"48656C6C6F"`).
- Topic is required and always comes after `~` — there's no default topic.
- QoS/retain come from `CONFIG` (`q=`/`r=`), not from the `CMD` line.
- `| PUBACK` (QoS 1) / `| PUBCOMP` (QoS 2) asserts the acknowledgement text;
  QoS 0 publishes have nothing to assert.

**Scenario:** a one-off sensor reading push, fire-and-forget at QoS 0, then
a safety-critical command that must be confirmed delivered at QoS 1:

```
MQTT.CONFIG h=broker.local q=0
MQTT.CMD > "21.5" ~ sensors/temp/reading           # QoS 0 — no ack to wait for

MQTT.CONFIG q=1
MQTT.CMD > "OPEN" ~ actuators/valve3/cmd | PUBACK   # QoS 1 — fails the line if PUBACK never arrives
```

---

### SCRIPT

**Purpose:** run several publishes (each with its own optional ack
assertion) from a text file over a *single* MQTT session — the batch
equivalent of repeated `CMD` calls, without paying for a new
CONNECT/CONNACK per line.

```
MQTT.SCRIPT <scriptfile> [|delay_ms]
```

**Scenario:** replay a recorded sequence of sensor readings for a soak
test, with a small delay between each to mimic real device timing.

`readings.txt` (resolved under the plugin's `ARTEFACTS_PATH`):
```
> "20.1" ~ sensors/temp/reading
> "20.3" ~ sensors/temp/reading
> "20.6" ~ sensors/temp/reading | PUBACK
```

```
MQTT.CONFIG h=broker.local q=1
MQTT.SCRIPT readings.txt 200
```

---

### SUBSCRIBE

**Purpose:** send `SUBSCRIBE` for one topic filter and wait for `SUBACK`.
Opens (or reuses) the plugin's one persistent connection — the same
connection `RECEIVE`/`UNSUBSCRIBE`/`PING` operate on. Can be called
repeatedly, including with different filters, to build up several
subscriptions on that one connection.

```
MQTT.SUBSCRIBE <topic> [qos]
```

**Scenario:** a monitoring script that wants both a specific topic and a
wildcard branch of the tree, at different QoS levels:

```
MQTT.CONFIG h=broker.local
MQTT.SUBSCRIBE sensors/temp 1
MQTT.SUBSCRIBE alerts/#     2
```

---

### UNSUBSCRIBE

**Purpose:** send `UNSUBSCRIBE` for one topic filter and wait for
`UNSUBACK`, on the same persistent connection `SUBSCRIBE` opened.

```
MQTT.UNSUBSCRIBE <topic>
```

**Scenario:** a test that verifies messages stop arriving once
unsubscribed — subscribe, drain a couple of messages, unsubscribe, then
confirm the next `RECEIVE` times out instead of returning data:

```
MQTT.SUBSCRIBE sensors/temp
temp1 ?= MQTT.RECEIVE
temp2 ?= MQTT.RECEIVE

MQTT.UNSUBSCRIBE sensors/temp
# A publisher continues sending on sensors/temp elsewhere — this RECEIVE
# should now time out (CONFIG rt: read timeout) rather than return a value.
temp3 ?= MQTT.RECEIVE
```

---

### RECEIVE

**Purpose:** wait for one incoming `PUBLISH` on an active subscription and
store it — `"payload"`, or `"topic:payload"` if `CONFIG it=true` — into a
variable macro. Requires an active `SUBSCRIBE` on the connection first.
Automatically sends a keepalive `PINGREQ` first if the session has been
idle for a while, so a long-running `RECEIVE` loop isn't silently dropped
by the broker.

```
temp ?= MQTT.RECEIVE          # one message, blocks up to CONFIG's rt: read timeout
temp ?= MQTT.RECEIVE &        # background thread; $temp always holds the latest message
```

**Scenario 1 — one-shot, blocking:** wait for exactly the next reading and
act on it once:

```
MQTT.SUBSCRIBE sensors/temp
reading ?= MQTT.RECEIVE
LOG.PRINT latest reading: $reading
```

**Scenario 2 — background, continuously updated:** keep a live variable
fed while the rest of the script does other work, checking it whenever
convenient:

```
MQTT.CONFIG h=broker.local it=true
MQTT.SUBSCRIBE alerts/#
latest_alert ?= MQTT.RECEIVE &

REPEAT poll UNTIL $latest_alert != ""
  DELAY 200 ms
END_REPEAT poll

LOG.PRINT first alert seen: $latest_alert
# $latest_alert keeps refreshing in the background for the rest of the script
```

---

### PING

**Purpose:** explicit `PINGREQ`/`PINGRESP` round-trip on the persistent
connection. `RECEIVE` already does this automatically when due — this
command is for explicitly probing (or forcing) liveness independently of
waiting for a message.

```
MQTT.PING
```

**Scenario:** a connectivity health-check step in a longer script, run
right before a batch of important publishes, to fail fast with a clear
"broker unreachable" message instead of timing out deep inside `SCRIPT`.
Capture the result into a macro (`PING` only sets it to `"PONG"` on
success — a failed `PING` leaves it empty) and branch on that, the same
pattern used for `RECEIVE` above:

```
MQTT.SUBSCRIBE heartbeat/topic
pong ?= MQTT.PING
IF $pong == "PONG" GOTO broker_alive

LOG.PRINT Broker did not respond to PING — aborting
GOTO done

LABEL broker_alive
MQTT.SCRIPT critical_publishes.txt
LABEL done
```

> By default a command returning `false` aborts the whole script (unless
> the interpreter has been told to continue, e.g. a `FAULT_TOLERANT=true`
> entry in the instance's `.ini` section). For the branch above to actually
> be reached on a failed `PING` rather than the script just stopping, mark
> this `MQTT` instance fault-tolerant.



---

## 3. End-to-end scenarios (single connection)

**TLS against a Mosquitto listener with a self-signed test cert**
(no verification — fine for a local dev broker, not for anything else):

```
MQTT.CONFIG h=broker.local p=8883 t=true
MQTT.CMD > "hello" ~ test/topic
```

**TLS with server verification** (Mosquitto's own CA):

```
MQTT.CONFIG h=broker.local p=8883 t=true ca=/etc/mosquitto/ca_certificates/ca.crt
MQTT.CMD > "hello" ~ test/topic
```

**Mutual TLS** (Mosquitto's `require_certificate true`):

```
MQTT.CONFIG h=broker.local p=8883 t=true ca=/path/ca.crt crt=/path/client.crt key=/path/client.key
MQTT.CMD > "hello" ~ test/topic
```

**Username/password auth** (Mosquitto's `password_file`):

```
MQTT.CONFIG h=broker.local u=alice pw=secret
MQTT.CMD > "hello" ~ test/topic | PUBACK
```

**Last Will and Testament** — the standard way to test a broker-side
liveness/offline indicator. Configure the Will, connect, subscribe as an
observer on another logical client, then simulate a crash by *not* calling
a clean `DISCONNECT` (dropping the TCP connection outright is what makes
the broker publish the Will — a graceful shutdown would not):

```
MQTT.CONFIG h=broker.local id=device_42 wt=devices/device_42/status wp=offline wq=1 wr=true
MQTT.SUBSCRIBE devices/device_42/status
# ... device_42's session is running elsewhere and drops unexpectedly ...
status ?= MQTT.RECEIVE
LOG.PRINT device_42 went: $status   # expect "offline"
```

**Persistent session across reconnects** — QoS 1/2 messages published while
the client is offline should be delivered on the next connect, provided the
same `clientId` is reused with `cs=false`:

```
MQTT.CONFIG h=broker.local id=fixed_client_1 cs=false q=1
MQTT.SUBSCRIBE sensors/temp
# ... connection drops, then a later run of the same script reconnects with
#     the same id= and cs=false — queued QoS 1 messages are redelivered
#     rather than lost, and the broker does not require re-subscribing.
```

---

## 4. Running several MQTT plugin instances in parallel

Because the host loads plugins as shared libraries and supports the
`PLUGIN:N` instance suffix, you can load the MQTT plugin several times in
one script — each instance gets **its own** `.so` handle, its own C++
plugin object, its own `[MQTT:N]` `.ini` section, and therefore its own
independent host/port/credentials/Will/persistent connection. Nothing about
instance `MQTT:1` affects `MQTT:2` — they are two fully separate MQTT
sessions that can point at different brokers entirely.

### Declaring instances

Either declare the base plugin once and let the engine auto-instantiate
each `MQTT:N` the first time it's referenced in a command:

```
LOAD_PLUGIN MQTT
MQTT:1.CONFIG h=broker-a.local
MQTT:2.CONFIG h=broker-b.local
```

or declare each instance explicitly up front (equivalent, just more
visible at the top of the script):

```
LOAD_PLUGIN MQTT:1
LOAD_PLUGIN MQTT:2
```

Each instance's settings can also come entirely from the `.ini` file, one
section per instance:

```ini
[MQTT:1]
HOST = broker-a.local
CLIENT_ID = collector_1

[MQTT:2]
HOST = broker-b.local
USERNAME = alice
PASSWORD = secret
```

### Scenario A — two independent brokers at once

A gateway test that mirrors readings from an internal broker to a cloud
broker, live:

```
LOAD_PLUGIN MQTT

MQTT:1.CONFIG h=broker-internal.local
MQTT:1.SUBSCRIBE sensors/#
reading ?= MQTT:1.RECEIVE &

MQTT:2.CONFIG h=broker-cloud.example.com p=8883 t=true u=gw01 pw=secret

# Wait for the first reading to arrive before starting to mirror it
REPEAT wait_first UNTIL $reading != ""
  DELAY 100 ms
END_REPEAT wait_first

REPEAT forward 100          # simplified: mirror the next 100 readings seen
  MQTT:2.CMD > $reading ~ mirrored/sensors
  DELAY 100 ms
END_REPEAT forward
```

`MQTT:1` keeps its own persistent subscriber connection to the internal
broker feeding `$reading` in the background; `MQTT:2` independently opens
its own fresh connection to the cloud broker (with its own TLS/auth
config) for every forwarded publish.

### Scenario B — simulating two devices talking to each other

Interop-testing a "controller" and a "device" against the same broker, as
two distinct MQTT clients with distinct client IDs, entirely within one
script:

```
LOAD_PLUGIN MQTT

MQTT:1.CONFIG h=broker.local id=controller_1
MQTT:2.CONFIG h=broker.local id=device_1

# device_1 waits for commands...
MQTT:2.SUBSCRIBE devices/device_1/cmd
cmd ?= MQTT:2.RECEIVE &

# ...controller_1 sends one
MQTT:1.CMD > "REBOOT" ~ devices/device_1/cmd | PUBACK

REPEAT wait_cmd UNTIL $cmd != ""
  DELAY 50 ms
END_REPEAT wait_cmd

LOG.PRINT device_1 received: $cmd

# device_1 replies on its own status topic
MQTT:2.CMD > "REBOOTING" ~ devices/device_1/status

# controller_1 confirms it saw the reply
MQTT:1.SUBSCRIBE devices/device_1/status
reply ?= MQTT:1.RECEIVE
LOG.PRINT controller saw: $reply
```

### Scenario C — parallel load/soak publishing

Several instances hammering different topic trees on the same broker
concurrently, each on its own background thread, to shake out
broker-side contention independent of any one client's pacing:

```
LOAD_PLUGIN MQTT

MQTT:1.CONFIG h=broker.local id=loadgen_1
MQTT:2.CONFIG h=broker.local id=loadgen_2
MQTT:3.CONFIG h=broker.local id=loadgen_3

MQTT:1.SCRIPT batch_a.txt 10 &
MQTT:2.SCRIPT batch_b.txt 10 &
MQTT:3.SCRIPT batch_c.txt 10 &
```

Each `SCRIPT ... &` line dispatches its whole batch on its own background
thread and returns immediately, so all three run truly in parallel (three
separate connections, three separate client IDs) rather than one after the
other.

---

## 5. CONFIG key reference

| Short key | INI key | Meaning |
|---|---|---|
| `h=` | `HOST` | Broker hostname/IP |
| `p=` | `PORT` | Broker port (1883 plain, 8883 TLS by convention) |
| `t=` | `TLS_ENABLED` | Enable TLS (`true`/`false`) |
| `ca=` | `TLS_CA_CERT` | CA cert path; verifies the server cert when set, otherwise TLS accepts any server cert |
| `crt=` | `TLS_CLIENT_CERT` | Client cert path (mutual TLS — set together with `key=`) |
| `key=` | `TLS_CLIENT_KEY` | Client private key path |
| `q=` | `QOS` | Default publish/subscribe QoS (0-2) |
| `r=` | `RETAIN` | Default retain flag for publishes |
| `id=` | `CLIENT_ID` | MQTT client id |
| `u=` | `USERNAME` | Broker username |
| `pw=` | `PASSWORD` | Broker password |
| `wt=` | `WILL_TOPIC` | Last Will topic (empty = no Will) |
| `wp=` | `WILL_PAYLOAD` | Last Will payload |
| `wq=` | `WILL_QOS` | Last Will QoS |
| `wr=` | `WILL_RETAIN` | Last Will retain flag |
| `cs=` | `CLEAN_SESSION` | Clean Session (`false` = persistent session, requires a stable `id=`) |
| `rt=` | `READ_TIMEOUT` | Read timeout (ms) used by `CMD`/`SCRIPT`/`RECEIVE` |
| `rb=` | `READ_BUFFER_SIZE` | Receive buffer size (bytes) used by `CMD`/`SCRIPT` |
| `ss=` | `SHARE_SESSION` | `true`: `CMD`/`SCRIPT` publish on the same persistent connection `SUBSCRIBE`/`RECEIVE` use, instead of a fresh one each time |
| `it=` | `RECEIVE_TOPIC` | `true`: `RECEIVE` stores `"topic:payload"` instead of just `"payload"` |

---

## 6. Gotchas

- **`RECEIVE`/`UNSUBSCRIBE`/`PING` all require a prior `SUBSCRIBE`** — they
  operate on the persistent connection, which only exists once `SUBSCRIBE`
  has opened it.
- **`CMD`/`SCRIPT` use a fresh connection by default**, separate from
  `SUBSCRIBE`'s persistent one, unless `ss=true`. A retained message you
  just published with `ss=false` (the default) is visible to `RECEIVE`
  through the broker like any other client's publish — there's no special
  same-process shortcut.
- **A Will is only delivered on an unclean disconnect.** A script that
  calls `MQTT.CMD`/`SCRIPT` normally, or that lets the plugin clean up at
  the end of a run, sends a proper `DISCONNECT` first — the broker will
  *not* publish the Will in that case. To actually test a Will, the
  disconnect has to be abrupt (process killed, network dropped) rather
  than a graceful script end.
- **`cs=false` only helps if `id=` is stable** across runs — an
  auto-generated client id (the default when `id=` is left unset) is
  different every time, so the broker has no prior session to resume.
- **Each `MQTT:N` instance is a fully separate session.** Config set on
  `MQTT:1` (host, auth, Will, ...) has no effect on `MQTT:2` — there is no
  shared state between instances beyond both being loaded from the same
  `.so` file.
