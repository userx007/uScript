# DDS Plugin — Usage Tutorial

This walks through the DDS plugin's command surface with a realistic
scenario for each, then shows how to run several independent RTPS
participants side by side using the script engine's plugin-instance
mechanism (`DDS:1`, `DDS:2`, ...) — including having one instance act as
the publisher ("service") and another as the subscriber ("client") within
a single script.

It assumes scripts are run through the core script engine described in
`src/script/core/README.md` (the `LOAD_PLUGIN` / `PLUGIN.COMMAND` / `?=` /
`&` syntax used throughout), and that a DDS domain is reachable on the
local network segment — either a real OpenDDS/RTI/CycloneDDS/FastDDS
participant, or, for the self-contained scenarios in section 4, just two
instances of this plugin talking to each other.

---

## Table of Contents

1. [How DDS.CMD works](#1-how-ddscmd-works)
2. [Command reference, with a scenario for each](#2-command-reference-with-a-scenario-for-each)
   - [INFO](#info)
   - [CONFIG](#config)
   - [CMD > PUBLISH](#cmd--publish)
   - [CMD > SUBSCRIBE](#cmd--subscribe)
   - [CMD > UNSUBSCRIBE](#cmd--unsubscribe)
   - [CMD > LIST](#cmd--list)
   - [CMD < (receive)](#cmd--receive)
   - [SCRIPT](#script)
   - [CYCLIC](#cyclic)
3. [End-to-end scenarios (single instance)](#3-end-to-end-scenarios-single-instance)
4. [Publisher and subscriber together: running several DDS plugin instances in one script](#4-publisher-and-subscriber-together-running-several-dds-plugin-instances-in-one-script)
5. [CONFIG key reference](#5-config-key-reference)
6. [Gotchas](#6-gotchas)

---

## 1. How DDS.CMD works

Every DDS operation — publish, subscribe, unsubscribe, list — goes through
a single command, `DDS.CMD`, on one persistent RTPS participant per loaded
plugin instance (its SPDP/SEDP discovery threads and its three RTPS UDP
sockets — see `dds_driver.hpp`'s class doc comment). That participant is
opened lazily by whichever `DDS.CMD` call needs it first, and stays open
for as long as the plugin is loaded — there's no separate "connect" step.
This is what makes `DDS.CMD <` meaningful: it waits on whatever
`DDS.CMD > SUBSCRIBE <topic>` call happened earlier on that same
participant, including from a background thread.

Unlike TCPIP/UART's `~`-addressed sessions or MQTT's broker connection,
**DDS has no central server** — a DDS.CMD PUBLISH goes straight, unicast,
to every remote reader this participant has discovered (via SEDP) as
matching that topic name. There's also **no ack pipe** the way MQTT has
`| SUBACK`/`| PUBACK`: DDS reliability (HEARTBEAT/ACKNACK, `r=1`) happens
automatically inside the driver's discovery thread, not as something
`DDS.CMD` reads back and compares — see [Gotchas](#6-gotchas).

The direction character tells the plugin what kind of call this is:

- **`DDS.CMD > <COMMAND> [args...]`** — send a command: `PUBLISH`,
  `SUBSCRIBE`, `UNSUBSCRIBE`, or `LIST`. Everything after `>` is one plain
  string; the plugin itself splits out the command keyword and its
  arguments (see each command's section below).
- **`DDS.CMD <`** (optionally `DDS.CMD < &` for a background thread — the
  `&` is handled by the script engine itself, exactly like every other
  threaded command) — wait for one incoming sample on an active
  subscription, or (right after `> LIST`) fetch the discovery snapshot.

---

## 2. Command reference, with a scenario for each

### INFO

**Purpose:** print the plugin's version and a usage summary to the
logger — a quick sanity check that the plugin loaded, with no domain
traffic required.

```
DDS.INFO
```

**Scenario:** the very first line of any new DDS test script, right after
`LOAD_PLUGIN DDS`, to confirm the build in use has the syntax and CONFIG
keys you're about to rely on.

---

### CONFIG

**Purpose:** set or change domain/transport parameters — domain id,
participant id, interface, reliability, fragmentation — before the
participant is opened. Any subset of keys can be given; omitted keys keep
their current value. Every key can equally be set once via the `.ini`
file's `[DDS]` (or `[DDS:N]`) section instead of a `CONFIG` line — see
[section 5](#5-config-key-reference) for the full key table.

```
DDS.CONFIG [d=domain] [pid=participant_id] [v6=0|1] [i=iface] [mi=mcast_iface]
           [mg=spdp_mcast_group] [n=name] [t=ttl] [sp=spdp_period_ms]
           [l=lease_sec] [r=0|1] [hb=heartbeat_ms] [hd=history_depth]
           [fr=fragment_threshold_bytes] [rt=read_tout] [rb=read_bufsize]
```

**Scenario:** join DDS domain 12 on a specific NIC, as a reliable
participant, before doing anything else. Since the participant is opened
lazily by the first `DDS.CMD`, `CONFIG` needs to run before that —
reconfiguring `d=`/`pid=`/`i=` *after* the participant is already open has
no effect on that live participant (the sockets stay bound to the old
ports).

```
LOAD_PLUGIN DDS

DDS.CONFIG d=12 i=192.168.1.50 n=uScriptProbe r=1
DDS.CMD > PUBLISH 21.5 sensors/temp
```

---

### CMD > PUBLISH

**Purpose:** publish one sample on a topic. Creates the local writer (and
announces it via SEDP) the first time a given topic name is published.

```
DDS.CMD > PUBLISH <topic> [payload words...]
```

- **The topic is always the first token; everything after it is the
  payload**, joined back together with single spaces — the mirror image
  of MQTT's PUBLISH, where the topic is last. `PUBLISH sensors/temp 21.5 C`
  is topic `sensors/temp`, payload `"21.5 C"`.
- Succeeds even if no subscriber has been discovered yet for this topic
  (best-effort/eventual matching — a warning is logged, not an error); see
  [Gotchas](#6-gotchas) for the discovery-latency implication.
- With `CONFIG fr=` set below the payload size, the sample is
  transparently fragmented (`DATA_FRAG`) — nothing different to write at
  the `CMD` level, it just works for larger payloads.

**Scenario:** a one-off sensor reading push, then (after switching to
reliable QoS) a command that should actually be retried until acknowledged
by a matched reader:

```
DDS.CONFIG d=0
DDS.CMD > PUBLISH sensors/temp/reading 21.5     # best-effort — no retry if lost

DDS.CONFIG r=1
DDS.CMD > PUBLISH actuators/valve3/cmd OPEN     # reliable — HEARTBEAT/ACKNACK retries automatically
```

---

### CMD > SUBSCRIBE

**Purpose:** create a local reader for one topic and start delivering
matching samples into that topic's receive queue, ready for `DDS.CMD <`.
May be called more than once, for different topics, on the same
participant.

```
DDS.CMD > SUBSCRIBE <topic>
```

**Scenario:** a monitoring script that wants both a specific reading topic
and a separate alarm topic:

```
DDS.CONFIG d=0
DDS.CMD > SUBSCRIBE sensors/temp
DDS.CMD > SUBSCRIBE alerts/critical
```

---

### CMD > UNSUBSCRIBE

**Purpose:** drop a local reader — no more samples for that topic are
queued after this.

```
DDS.CMD > UNSUBSCRIBE <topic>
```

**Scenario:** a test that verifies samples stop arriving once
unsubscribed — subscribe, drain a couple of samples, unsubscribe, then
confirm the next receive times out instead of returning data:

```
DDS.CMD > SUBSCRIBE sensors/temp
temp1 ?= DDS.CMD <
temp2 ?= DDS.CMD <

DDS.CMD > UNSUBSCRIBE sensors/temp
# A publisher continues sending on sensors/temp elsewhere — this receive
# should now time out (CONFIG rt: read timeout) rather than return a value.
temp3 ?= DDS.CMD <
```

---

### CMD > LIST

**Purpose:** dump every participant discovered via SPDP, and every
publication/subscription discovered via SEDP, plus this participant's own
local writers/readers and whether each is reliable. A "send now, read
result next" pair, unlike every other `CMD >` — the text is produced by
the following `DDS.CMD <`, not by `LIST` itself.

```
DDS.CMD > LIST
DDS.CMD <
```

**Scenario:** a diagnostic step confirming an expected remote participant
was actually discovered before relying on it in the rest of the script —
useful right after `CONFIG` and a short settle delay:

```
DDS.CONFIG d=0 n=uScriptProbe
DELAY 3000 ms            # give SPDP a few announce periods to find peers
DDS.CMD > LIST
snapshot ?= DDS.CMD <
LOG.PRINT $snapshot
```

---

### CMD < (receive)

**Purpose:** wait for one incoming sample on an active subscription and
store its payload into a variable macro. Requires an active `SUBSCRIBE` on
this participant first (on the same thread/`>`/`<` chain — see
[Gotchas](#6-gotchas)).

```
temp ?= DDS.CMD <          # one sample, blocks up to CONFIG's rt: read timeout
temp ?= DDS.CMD < &        # background thread; $temp always holds the latest sample
```

**Scenario 1 — one-shot, blocking:** wait for exactly the next reading and
act on it once:

```
DDS.CMD > SUBSCRIBE sensors/temp
reading ?= DDS.CMD <
LOG.PRINT latest reading: $reading
```

**Scenario 2 — background, continuously updated:** keep a live variable
fed while the rest of the script does other work:

```
DDS.CONFIG d=0
DDS.CMD > SUBSCRIBE alerts/critical
latest_alert ?= DDS.CMD < &

REPEAT poll UNTIL $latest_alert != ""
  DELAY 200 ms
END_REPEAT poll

LOG.PRINT first alert seen: $latest_alert
# $latest_alert keeps refreshing in the background for the rest of the script
```

---

### SCRIPT

**Purpose:** run several `DDS.CMD`-style lines from a file over the same
participant — the batch equivalent of repeated `CMD` calls.

```
DDS.SCRIPT <scriptfile> [delay_ms]
```

Each non-empty, non-`#`-comment line in the file is exactly one `DDS.CMD`
argument string (everything that would follow `DDS.CMD` on a script line —
`>`/`<` and all).

**Scenario:** replay a recorded sequence of sensor readings for a soak
test, with a small delay between each to mimic real device timing.

`readings.txt` (resolved under the plugin's `ARTEFACTS_PATH`):
```
> PUBLISH sensors/temp/reading 20.1
> PUBLISH sensors/temp/reading 20.3
> PUBLISH sensors/temp/reading 20.6
```

```
DDS.CONFIG d=0
DDS.SCRIPT readings.txt 200
```

---

### CYCLIC

**Purpose:** periodic publish on the same participant — the DDS analogue
of a hardware bus's cyclic-send table, useful for simulating a steady
telemetry stream or a device's periodic status heartbeat.

```
DDS.CYCLIC "time1:val1, time2:val2, ..."
```

Each `valN` is a full `DDS.CMD`-style `> ...` argument, so it's normally
`> PUBLISH <topic> <payload>`.

**Scenario:** simulate a vehicle subsystem's periodic status publish while
the rest of the script does something else:

```
DDS.CONFIG d=0
DDS.CYCLIC "1000:> PUBLISH C_Actual_Video_Sink 3, 5000:> PUBLISH device/heartbeat ALIVE"
```

---

## 3. End-to-end scenarios (single instance)

**Fragmented large payload** — a payload bigger than the safe single-frame
threshold is split into `DATA_FRAG` submessages and reassembled on the
subscriber side automatically; nothing to change at the `CMD` level, only
`CONFIG`:

```
DDS.CONFIG d=0 fr=1200
DDS.CMD > PUBLISH imaging/frame_dump <a payload well over 1200 bytes...>
```

**Reliable delivery over a lossy link** — `r=1` makes this participant's
writers keep a resend cache and answer ACKNACK; its readers ACKNACK gaps
against a matched writer's HEARTBEAT. Works even if the remote peer is
best-effort (it just never sends ACKNACK back, so nothing is lost on
*this* side that the remote side actually needed):

```
DDS.CONFIG d=0 r=1 hb=250 hd=64
DDS.CMD > SUBSCRIBE telemetry/critical
sample ?= DDS.CMD <
```

**IPv6 on a specific link-local interface:**

```
DDS.CONFIG v6=1 i=fe80::1%eth0 mi=eth0 mg=ff03::1:7401 d=0
DDS.CMD > SUBSCRIBE sensors/temp
```

(`mg=` is required for IPv6 — see [section 5](#5-config-key-reference)'s
note and the driver's own warning if it's left unset.)

**Interoperating with a real OpenDDS participant** — no special
configuration needed on the OpenDDS side beyond its default RTPS discovery
transport on the same `DOMAIN` id; SPDP multicast finds this plugin
automatically:

```
DDS.CONFIG d=7
DDS.CMD > SUBSCRIBE C_Actual_Video_Sink
video_status ?= DDS.CMD <
```

---

## 4. Publisher and subscriber together: running several DDS plugin instances in one script

Because the host loads plugins as shared libraries and supports the
`PLUGIN:N` instance suffix, you can load the DDS plugin several times in
one script — each instance gets **its own** `.so` handle, its own C++
plugin object, its own `[DDS:N]` `.ini` section, and therefore its own
independent `DdsDriver` (its own GUID, its own three RTPS sockets, its own
discovery thread). This is exactly what lets one script act as **both** a
publisher/"service" and a subscriber/"client" at once — two RTPS
participants on the same domain, each with a role, talking to each other
(or to a third, external, real DDS participant) entirely from within one
script.

### Declaring instances

Either declare the base plugin once and let the engine auto-instantiate
each `DDS:N` the first time it's referenced in a command:

```
LOAD_PLUGIN DDS
DDS:1.CONFIG d=0 n=publisher_1
DDS:2.CONFIG d=0 n=subscriber_1
```

or declare each instance explicitly up front:

```
LOAD_PLUGIN DDS:1
LOAD_PLUGIN DDS:2
```

Each instance's settings can also come entirely from the `.ini` file, one
section per instance — **remember to give each instance a distinct
`PARTICIPANT_ID`** when they run on the same host/domain, since the RTPS
port formula (`7400 + 250*domain + ... + 2*participant_id`) would
otherwise make two instances try to bind the same three ports:

```ini
[DDS:1]
DOMAIN = 0
PARTICIPANT_ID = 1
PARTICIPANT_NAME = publisher_1

[DDS:2]
DOMAIN = 0
PARTICIPANT_ID = 2
PARTICIPANT_NAME = subscriber_1
```

### Scenario A — self-contained publish/subscribe loopback test

The simplest possible use of two instances: verify the plugin's own
publish and subscribe paths actually reach each other, with no external
DDS participant needed at all. Useful as a smoke test after any change to
`CONFIG`/network settings, or in CI where a real OpenDDS peer isn't
available.

```
LOAD_PLUGIN DDS

DDS:1.CONFIG d=99 pid=1 n=loopback_pub
DDS:2.CONFIG d=99 pid=2 n=loopback_sub

DDS:2.CMD > SUBSCRIBE selftest/ping
received ?= DDS:2.CMD < &

# Give SPDP+SEDP a moment to discover each other and match the topic
# before publishing — see Gotchas for why this delay matters.
DELAY 2500 ms

DDS:1.CMD > PUBLISH selftest/ping hello

REPEAT wait_ping UNTIL $received != ""
  DELAY 100 ms
END_REPEAT wait_ping

IF $received == "hello" GOTO pass
LOG.PRINT SELF-TEST FAILED: expected 'hello', got '$received'
GOTO done

LABEL pass
LOG.PRINT DDS loopback self-test passed

LABEL done
```

### Scenario B — simulating two vehicle subsystems talking to each other

Interop-testing a "controller" and a "video recorder" subsystem against
the same domain, as two distinct participants with distinct names, in the
NGVA style referenced in the plugin's `INFO` text (request a video stream,
wait for the sink to confirm it started) — entirely within one script:

```
LOAD_PLUGIN DDS

DDS:1.CONFIG d=7 pid=1 n=controller
DDS:2.CONFIG d=7 pid=2 n=video_recorder

# video_recorder waits for a stream request...
DDS:2.CMD > SUBSCRIBE C_Actual_Video_Stream_requestVideoStream
request ?= DDS:2.CMD < &

DELAY 2500 ms   # let SPDP/SEDP settle before controller publishes

# ...controller sends one
DDS:1.CMD > PUBLISH C_Actual_Video_Stream_requestVideoStream 3

REPEAT wait_req UNTIL $request != ""
  DELAY 100 ms
END_REPEAT wait_req

LOG.PRINT video_recorder received request: $request

# video_recorder replies on its own status topic
DDS:2.CMD > PUBLISH C_Actual_Video_Sink 3

# controller confirms it saw the sink come up
DDS:1.CMD > SUBSCRIBE C_Actual_Video_Sink
sink_status ?= DDS:1.CMD <
LOG.PRINT controller saw sink status: $sink_status
```

### Scenario C — bridging two DDS domains

DDS domains are fully isolated from each other by design (different SPDP
multicast traffic, no cross-domain discovery) — a common real integration
task is bridging a topic from one domain to another. Two instances, one
per domain, do this directly:

```
LOAD_PLUGIN DDS

DDS:1.CONFIG d=0 pid=1 n=bridge_domain0_side
DDS:2.CONFIG d=5 pid=2 n=bridge_domain5_side

DDS:1.CMD > SUBSCRIBE sensors/legacy_temp
reading ?= DDS:1.CMD < &

REPEAT wait_first UNTIL $reading != ""
  DELAY 100 ms
END_REPEAT wait_first

REPEAT forward 100          # simplified: mirror the next 100 readings seen
  DDS:2.CMD > PUBLISH sensors/bridged_temp $reading
  DELAY 100 ms
END_REPEAT forward
```

`DDS:1` keeps its own persistent subscriber participant on domain 0
feeding `$reading` in the background; `DDS:2` independently maintains its
own participant on domain 5 for every forwarded publish — two fully
separate RTPS participants, each with its own GUID and sockets.

### Scenario D — parallel load/soak publishing

Several instances hammering different topics on the same domain
concurrently, each on its own background thread:

```
LOAD_PLUGIN DDS

DDS:1.CONFIG d=0 pid=1 n=loadgen_1
DDS:2.CONFIG d=0 pid=2 n=loadgen_2
DDS:3.CONFIG d=0 pid=3 n=loadgen_3

DDS:1.SCRIPT batch_a.txt 10 &
DDS:2.SCRIPT batch_b.txt 10 &
DDS:3.SCRIPT batch_c.txt 10 &
```

Each `SCRIPT ... &` line dispatches its whole batch on its own background
thread and returns immediately, so all three run truly in parallel (three
separate participants, three separate GUIDs) rather than one after the
other.

---

## 5. CONFIG key reference

| Short key | INI key | Meaning |
|---|---|---|
| `d=` | `DOMAIN` | DDS domain id |
| `pid=` | `PARTICIPANT_ID` | Selects this participant's unicast metatraffic/user ports — give co-located instances distinct values |
| `v6=` | `USE_IPV6` | `true`/`false` — IPv6 sockets/locators instead of IPv4 |
| `i=` | `IFACE` | Local bind address (`"::"` default for IPv6) |
| `mi=` | `MCAST_IFACE` | IPv4: local interface IP. IPv6: local interface *name* (e.g. `eth0`) |
| `mg=` | `SPDP_MULTICAST_GROUP` | SPDP multicast group; empty = `239.255.0.1` for IPv4. **Required** for IPv6 — no spec default exists |
| `n=` | `PARTICIPANT_NAME` | Advertised in SPDP, shown by `DDS.CMD > LIST` on peers |
| `t=` | `TTL` | SPDP multicast TTL/hop limit |
| `sp=` | `SPDP_PERIOD_MS` | Participant announcement interval |
| `l=` | `LEASE_DURATION_SEC` | How long a peer is kept without hearing a fresh SPDP |
| `r=` | `RELIABLE` | `true`/`false` — HEARTBEAT/ACKNACK reliability for this participant's local writers/readers |
| `hb=` | `HEARTBEAT_PERIOD_MS` | Reliable writers only |
| `hd=` | `HISTORY_DEPTH` | Reliable writer resend-cache depth, in samples |
| `fr=` | `FRAGMENT_THRESHOLD_BYTES` | Samples larger than this are `DATA_FRAG`'d; `0` disables |
| `rt=` | `READ_TIMEOUT` | Read timeout (ms) used by `DDS.CMD <` |
| `rb=` | `READ_BUFFER_SIZE` | Max size (bytes) of one `DDS.CMD <` result |

---

## 6. Gotchas

- **Discovery is asynchronous — a `PUBLISH` immediately after `CONFIG`
  may find no subscribers yet.** SPDP announces on `sp=` (default 2000 ms)
  and SEDP follows once a participant is discovered; give it at least one
  or two announce periods (a couple of seconds) before relying on a
  cross-instance/cross-participant publish actually being delivered — see
  every scenario above's `DELAY` after `CONFIG`/`SUBSCRIBE`.
- **There is no ack pipe like MQTT's `| SUBACK`/`| PUBACK`.** DDS
  reliability happens automatically inside the driver (HEARTBEAT/ACKNACK,
  `r=1`) — `DDS.CMD >` always returns as soon as the operation is issued,
  not once a remote peer has confirmed it.
- **`DDS.CMD <` always needs a `SUBSCRIBE` earlier in the same `>`/`<`
  chain (or thread).** Unlike MQTT, there's no implicit "the session" to
  fall back on — a standalone `DDS.CMD <` with nothing subscribed on that
  thread is an error, not a timeout.
- **Topics are matched by name only, not by IDL type.** Two writers/readers
  using the same topic name always match here, even if a real DDS
  application on the other end expects a specific IDL struct — see
  `dds_protocol.hpp`'s class doc comment for the "unkeyed, opaque payload"
  scope this plugin operates in.
- **Reliable delivery is sample-granularity, not fragment-granularity.** A
  fragmented reliable sample that's only partially received is simply
  still "missing" as a whole, and gets fully re-sent (every fragment)
  rather than just the missing pieces — see `dds_protocol.hpp`'s note on
  why `NACK_FRAG` isn't implemented.
- **Co-located instances need distinct `PARTICIPANT_ID`s.** The RTPS port
  formula only varies by `domain` and `participant_id`; two instances with
  the same `(d=, pid=)` on the same host will fail to bind (the second
  `open()` logs an error and the participant never comes up).
- **`CONFIG` changes to `d=`/`pid=`/`i=`/`v6=` only take effect on the
  *next* participant.** Like MQTT's host/port, reconfiguring these after
  `DDS.CMD` has already opened the participant has no effect on the live
  sockets — the plugin resets its cached driver on any `CONFIG` call
  specifically so the *next* `DDS.CMD` re-opens with the new settings, but
  a participant already mid-script won't retroactively rebind.
- **Each `DDS:N` instance is a fully separate RTPS participant.** Config
  set on `DDS:1` (domain, reliability, name, ...) has no effect on `DDS:2`
  — there is no shared state between instances beyond both being loaded
  from the same `.so` file, exactly like MQTT's `MQTT:N` instances.
