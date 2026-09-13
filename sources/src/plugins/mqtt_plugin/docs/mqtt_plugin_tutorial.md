# MQTT Plugin — Usage Tutorial

This walks through the MQTT plugin's command surface with a realistic
scenario for each, then shows how to run several independent MQTT sessions
side by side using the script engine's plugin-instance mechanism (`MQTT:1`,
`MQTT:2`, ...).

It assumes a Mosquitto broker reachable at `broker.local`, and that scripts
are run through the core script engine described in
`src/script/core/README.md` (the `LOAD_PLUGIN` / `PLUGIN.COMMAND` / `?=` /
`&` syntax used throughout).

---

## Table of Contents

1. [How MQTT.CMD works](#1-how-mqttcmd-works)
2. [Command reference, with a scenario for each](#2-command-reference-with-a-scenario-for-each)
   - [INFO](#info)
   - [CONFIG](#config)
   - [CMD > SUBSCRIBE](#cmd--subscribe)
   - [CMD > UNSUBSCRIBE](#cmd--unsubscribe)
   - [CMD > PING](#cmd--ping)
   - [CMD > PUBLISH](#cmd--publish)
   - [CMD < (receive)](#cmd--receive)
   - [SCRIPT](#script)
3. [End-to-end scenarios (single connection)](#3-end-to-end-scenarios-single-connection)
4. [Running several MQTT plugin instances in parallel](#4-running-several-mqtt-plugin-instances-in-parallel)
5. [CONFIG key reference](#5-config-key-reference)
6. [Gotchas](#6-gotchas)

---

## 1. How MQTT.CMD works

Every MQTT operation — subscribe, unsubscribe, ping, publish, receive —
goes through a single command, `MQTT.CMD`, on one persistent MQTT session
per loaded plugin instance. That session (the CONNECT/CONNACK handshake) is
opened lazily by whichever `MQTT.CMD` call needs it first, and stays open
for as long as the plugin is loaded — there's no separate "connect" step
and no per-call reconnect. This is what makes `MQTT.CMD <` meaningful: it
waits on whatever `MQTT.CMD > SUBSCRIBE ...` calls happened earlier on that
same session, including from a background thread.

The direction character tells the plugin what kind of call this is:

- **`MQTT.CMD > <COMMAND> [args...] [| expected]`** — send a command:
  `SUBSCRIBE`, `UNSUBSCRIBE`, `PING`, or `PUBLISH`. Everything after `>` is
  one plain string; the plugin itself splits out the command keyword and
  its arguments (see each command's section below) — there's no `~`
  topic-addressing syntax here, unlike TCPIP.CMD/UART.CMD.
- **`MQTT.CMD <`** (optionally `MQTT.CMD < &` for a background thread —
  the `&` is handled by the script engine itself, exactly like every other
  threaded command) — wait for one incoming message on an active
  subscription.

The optional `| expected` on a `>` line asserts the operation's
acknowledgement: `SUBACK`, `UNSUBACK`, `PONG`, or `PUBACK`/`PUBCOMP`
(depending on PUBLISH's QoS). It's how the plugin confirms an operation
actually completed rather than just being sent — see each command's section
for what to expect there, and [Gotchas](#6-gotchas) for one edge case worth
knowing about before you skip it.

---

## 2. Command reference, with a scenario for each

### INFO

**Purpose:** print the plugin's version and a usage summary to the logger —
a quick sanity check that the plugin loaded, with no broker required.

```
MQTT.INFO
```

**Scenario:** the very first line of any new MQTT test script, right after
`LOAD_PLUGIN MQTT`, to confirm the build in use has the syntax you're about
to rely on.

---

### CONFIG

**Purpose:** set or change connection parameters — host, port, TLS, auth,
Last Will, QoS/retain defaults, timeouts — before the session is opened.
Any subset of keys can be given; omitted keys keep their current value.
Every key can equally be set once via the `.ini` file's `[MQTT]` (or
`[MQTT:N]`) section instead of a `CONFIG` line — see
[section 5](#5-config-key-reference) for the full key table.

```
MQTT.CONFIG [h=host] [p=port] [q=qos] [t=tls] [r=retain] [ca=capath] [crt=certpath]
            [key=keypath] [rt=read_tout] [rb=read_bufsize] [id=clientid]
            [u=username] [pw=password] [wt=will_topic] [wp=will_payload]
            [wq=will_qos] [wr=will_retain] [cs=cleansession] [it=include_topic]
```

**Scenario:** point the plugin at a local Mosquitto instance for a test run.
Since the session is opened lazily by the first `MQTT.CMD`, `CONFIG` needs
to run before that — reconfiguring host/port/TLS/auth *after* the session
is already open has no effect on that live session.

```
LOAD_PLUGIN MQTT

MQTT.CONFIG h=broker.local p=1883 q=1
MQTT.CMD > PUBLISH 21.5 sensors/temp | PUBACK
```

---

### CMD > SUBSCRIBE

**Purpose:** send `SUBSCRIBE` for one topic filter and wait for `SUBACK`.
May be called more than once, including with different filters, to build
up several subscriptions on the same session.

```
MQTT.CMD > SUBSCRIBE <topic> [qos] [| SUBACK]
```

- `qos` (0-2) defaults to `CONFIG`'s `q=` if omitted.
- The subscription is confirmed (or refused) internally regardless of
  whether `| SUBACK` is present — see [Gotchas](#6-gotchas) for why it's
  still worth including.

**Scenario:** a monitoring script that wants both a specific topic and a
wildcard branch of the tree, at different QoS levels:

```
MQTT.CONFIG h=broker.local
MQTT.CMD > SUBSCRIBE sensors/temp 1 | SUBACK
MQTT.CMD > SUBSCRIBE alerts/# 2 | SUBACK
```

---

### CMD > UNSUBSCRIBE

**Purpose:** send `UNSUBSCRIBE` for one topic filter and wait for
`UNSUBACK`.

```
MQTT.CMD > UNSUBSCRIBE <topic> [| UNSUBACK]
```

**Scenario:** a test that verifies messages stop arriving once
unsubscribed — subscribe, drain a couple of messages, unsubscribe, then
confirm the next receive times out instead of returning data:

```
MQTT.CMD > SUBSCRIBE sensors/temp | SUBACK
temp1 ?= MQTT.CMD <
temp2 ?= MQTT.CMD <

MQTT.CMD > UNSUBSCRIBE sensors/temp | UNSUBACK
# A publisher continues sending on sensors/temp elsewhere — this receive
# should now time out (CONFIG rt: read timeout) rather than return a value.
temp3 ?= MQTT.CMD <
```

---

### CMD > PING

**Purpose:** explicit `PINGREQ`/`PINGRESP` round-trip. A long-idle
`MQTT.CMD <` already sends a keepalive `PINGREQ` on its own when due, so
this is mainly for explicitly probing (or forcing) liveness independently
of waiting for a message.

```
MQTT.CMD > PING [| PONG]
```

**Scenario:** a connectivity health-check step in a longer script, run
right before a batch of important publishes, to fail fast with a clear
"broker unreachable" message instead of timing out deep inside `SCRIPT`.
Capture the result into a macro and branch on it:

```
MQTT.CMD > SUBSCRIBE heartbeat/topic | SUBACK
pong ?= MQTT.CMD > PING | PONG
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

### CMD > PUBLISH

**Purpose:** publish one message. This is the direct replacement for what
used to be MQTT.CMD's whole job — it's now just one more command in the
same dispatch as `SUBSCRIBE`/`UNSUBSCRIBE`/`PING`.

```
MQTT.CMD > PUBLISH <payload> <topic> [| PUBACK | PUBCOMP]
```

- QoS/retain come from `CONFIG` (`q=`/`r=`), not from the `CMD` line.
- **The topic is always the last token; everything before it is the
  payload.** This is what lets a multi-word payload work without quoting —
  the underlying grammar this text passes through doesn't support embedded
  `"quoted strings"` here, so `PUBLISH door is now open actuators/door/status`
  is parsed as payload `"door is now open"`, topic `actuators/door/status`.
- QoS 0 publishes have no acknowledgement — `| PUBACK`/`| PUBCOMP` only
  makes sense at QoS 1/2.

**Scenario:** a one-off sensor reading push, fire-and-forget at QoS 0, then
a safety-critical command that must be confirmed delivered at QoS 1:

```
MQTT.CONFIG h=broker.local q=0
MQTT.CMD > PUBLISH 21.5 sensors/temp/reading            # QoS 0 — no ack to wait for

MQTT.CONFIG q=1
MQTT.CMD > PUBLISH OPEN actuators/valve3/cmd | PUBACK    # QoS 1 — fails the line if PUBACK never arrives
```

---

### CMD < (receive)

**Purpose:** wait for one incoming `PUBLISH` on an active subscription and
store it — `"payload"`, or `"topic payload"` (space separated) if `CONFIG it=true` — into a
variable macro. Requires an active `SUBSCRIBE` on the session first.
Automatically sends a keepalive `PINGREQ` first if the session has been
idle for a while, so a long-running receive loop isn't silently dropped by
the broker.

```
temp ?= MQTT.CMD <          # one message, blocks up to CONFIG's rt: read timeout
temp ?= MQTT.CMD < &        # background thread; $temp always holds the latest message
```

**Scenario 1 — one-shot, blocking:** wait for exactly the next reading and
act on it once:

```
MQTT.CMD > SUBSCRIBE sensors/temp | SUBACK
reading ?= MQTT.CMD <
LOG.PRINT latest reading: $reading
```

**Scenario 2 — background, continuously updated:** keep a live variable
fed while the rest of the script does other work, checking it whenever
convenient:

```
MQTT.CONFIG h=broker.local it=true
MQTT.CMD > SUBSCRIBE alerts/# | SUBACK
latest_alert ?= MQTT.CMD < &

REPEAT poll UNTIL $latest_alert != ""
  DELAY 200 ms
END_REPEAT poll

LOG.PRINT first alert seen: $latest_alert
# $latest_alert keeps refreshing in the background for the rest of the script
```

---

### SCRIPT

**Purpose:** run several `MQTT.CMD`-style lines from a file over the same
session — the batch equivalent of repeated `CMD` calls.

```
MQTT.SCRIPT <scriptfile> [delay_ms]
```

Each non-empty, non-`#`-comment line in the file is exactly one `MQTT.CMD`
argument string (everything that would follow `MQTT.CMD` on a script line —
`>`/`<` and all).

**Scenario:** replay a recorded sequence of sensor readings for a soak
test, with a small delay between each to mimic real device timing.

`readings.txt` (resolved under the plugin's `ARTEFACTS_PATH`):
```
> PUBLISH 20.1 sensors/temp/reading
> PUBLISH 20.3 sensors/temp/reading
> PUBLISH 20.6 sensors/temp/reading | PUBACK
```

```
MQTT.CONFIG h=broker.local q=1
MQTT.SCRIPT readings.txt 200
```

---

## 3. End-to-end scenarios (single connection)

**TLS against a Mosquitto listener with a self-signed test cert**
(no verification — fine for a local dev broker, not for anything else):

```
MQTT.CONFIG h=broker.local p=8883 t=true
MQTT.CMD > PUBLISH hello test/topic
```

**TLS with server verification** (Mosquitto's own CA):

```
MQTT.CONFIG h=broker.local p=8883 t=true ca=/etc/mosquitto/ca_certificates/ca.crt
MQTT.CMD > PUBLISH hello test/topic
```

**Mutual TLS** (Mosquitto's `require_certificate true`):

```
MQTT.CONFIG h=broker.local p=8883 t=true ca=/path/ca.crt crt=/path/client.crt key=/path/client.key
MQTT.CMD > PUBLISH hello test/topic
```

**Username/password auth** (Mosquitto's `password_file`):

```
MQTT.CONFIG h=broker.local u=alice pw=secret
MQTT.CMD > PUBLISH hello test/topic | PUBACK
```

**Last Will and Testament** — the standard way to test a broker-side
liveness/offline indicator. Configure the Will, connect, subscribe as an
observer on another logical client, then simulate a crash by *not* calling
a clean disconnect (dropping the TCP connection outright is what makes the
broker publish the Will — a graceful script/plugin shutdown sends a clean
DISCONNECT and would not):

```
MQTT.CONFIG h=broker.local id=device_42 wt=devices/device_42/status wp=offline wq=1 wr=true
MQTT.CMD > SUBSCRIBE devices/device_42/status | SUBACK
# ... device_42's session is running elsewhere and drops unexpectedly ...
status ?= MQTT.CMD <
LOG.PRINT device_42 went: $status   # expect "offline"
```

**Persistent broker-side session across reconnects** — QoS 1/2 messages
published while the client is offline should be delivered on the next
connect, provided the same `clientId` is reused with `cs=false`:

```
MQTT.CONFIG h=broker.local id=fixed_client_1 cs=false q=1
MQTT.CMD > SUBSCRIBE sensors/temp | SUBACK
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
independent host/port/credentials/Will/persistent session. Nothing about
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
MQTT:1.CMD > SUBSCRIBE sensors/# | SUBACK
reading ?= MQTT:1.CMD < &

MQTT:2.CONFIG h=broker-cloud.example.com p=8883 t=true u=gw01 pw=secret

# Wait for the first reading to arrive before starting to mirror it
REPEAT wait_first UNTIL $reading != ""
  DELAY 100 ms
END_REPEAT wait_first

REPEAT forward 100          # simplified: mirror the next 100 readings seen
  MQTT:2.CMD > PUBLISH $reading mirrored/sensors
  DELAY 100 ms
END_REPEAT forward
```

`MQTT:1` keeps its own persistent subscriber session to the internal
broker feeding `$reading` in the background; `MQTT:2` independently
maintains its own session to the cloud broker (with its own TLS/auth
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
MQTT:2.CMD > SUBSCRIBE devices/device_1/cmd | SUBACK
cmd ?= MQTT:2.CMD < &

# ...controller_1 sends one
MQTT:1.CMD > PUBLISH REBOOT devices/device_1/cmd | PUBACK

REPEAT wait_cmd UNTIL $cmd != ""
  DELAY 50 ms
END_REPEAT wait_cmd

LOG.PRINT device_1 received: $cmd

# device_1 replies on its own status topic
MQTT:2.CMD > PUBLISH REBOOTING devices/device_1/status

# controller_1 confirms it saw the reply
MQTT:1.CMD > SUBSCRIBE devices/device_1/status | SUBACK
reply ?= MQTT:1.CMD <
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
separate sessions, three separate client IDs) rather than one after the
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
| `cs=` | `CLEAN_SESSION` | Clean Session (`false` = persistent broker-side session, requires a stable `id=`) |
| `rt=` | `READ_TIMEOUT` | Read timeout (ms) used to wait for acks and for `MQTT.CMD <` |
| `rb=` | `READ_BUFFER_SIZE` | Max size (bytes) of one `MQTT.CMD <` result / ack confirmation |
| `it=` | `RECEIVE_TOPIC` | `true`: `MQTT.CMD <` stores `"topic payload"` (space separated) instead of just `"payload"` |

---

## 6. Gotchas

- **Every MQTT.CMD shares one persistent session per plugin instance,**
  opened lazily by whichever call needs it first and kept open for as long
  as the plugin is loaded. `CONFIG` changes to host/port/TLS/auth/Will only
  take effect on the *next* session — they don't reconfigure a session
  that's already open.
- **`MQTT.CMD <`/`UNSUBSCRIBE`/`PING` all require a prior `SUBSCRIBE`-or-
  `PUBLISH`-established session** (any `MQTT.CMD >` call opens the session
  if it isn't already open; `MQTT.CMD <` on its own does not).
- **Always pair a `SUBSCRIBE`/`UNSUBSCRIBE`/`PING`/QoS>0 `PUBLISH` with its
  `| expected` when you're about to follow it with `MQTT.CMD <`.** The
  acknowledgement wait happens on that same pipe; if you omit it, the ack
  is left unread on the wire, and the *next* `MQTT.CMD <` will read (and
  return) that leftover acknowledgement text once instead of waiting for a
  live message — a one-time mismatch rather than persistent corruption, but
  easy to avoid by always including the pipe.
- **A PUBLISH payload can contain spaces, but the topic is always the last
  token** — `PUBLISH a b c topic/name` is payload `"a b c"`, topic
  `"topic/name"`. There is no quoting available inside a `MQTT.CMD`
  argument string, so a payload or topic containing a literal `"` cannot be
  expressed this way.
- **A Will is only delivered on an unclean disconnect.** A script that lets
  the plugin clean up normally at the end of a run sends a proper
  DISCONNECT first — the broker will *not* publish the Will in that case.
  To actually test a Will, the disconnect has to be abrupt (process killed,
  network dropped) rather than a graceful script end.
- **`cs=false` only helps if `id=` is stable** across runs — an
  auto-generated client id (the default when `id=` is left unset) is
  different every time, so the broker has no prior session to resume.
- **Each `MQTT:N` instance is a fully separate session.** Config set on
  `MQTT:1` (host, auth, Will, ...) has no effect on `MQTT:2` — there is no
  shared state between instances beyond both being loaded from the same
  `.so` file.
