# DDS_TYPED Plugin — Usage Tutorial

This walks through the `DDS_TYPED` plugin's command surface with a
realistic scenario for each, how to build and load a customer-specific
type plugin `.so`, and how to run a publisher and a subscriber — each
using real IDL structs, not generic strings — side by side in one script.

It assumes scripts are run through the core script engine described in
`src/script/core/README.md` (the `LOAD_PLUGIN` / `PLUGIN.COMMAND` / `?=` /
`&` syntax used throughout), and builds throughout on the `customer1`
example plugin at `../examples/customer1` — see that directory's
`CMakeLists.txt`/`src/customer1_adapter.c` for the actual source this
tutorial's `libcustomer1_types.so` comes from.

If you haven't yet, read the [DDS plugin tutorial](../../dds_plugin/docs/dds_plugin_tutorial.md)
first — `DDS_TYPED` is deliberately the same shape (`CONFIG`/`CMD`/
`SCRIPT`/`CYCLIC`, one persistent Cyclone DDS participant per instance,
`DDS_TYPED:N` for multiple instances) with one addition (`LOAD`) and one
difference (real IDL structs instead of one generic string type) — this
tutorial only covers what's actually different, and cross-references that
one for everything else.

---

## Table of Contents

1. [How DDS_TYPED.CMD works](#1-how-dds_typedcmd-works)
2. [Building your first customer type plugin](#2-building-your-first-customer-type-plugin)
3. [Command reference, with a scenario for each](#3-command-reference-with-a-scenario-for-each)
   - [INFO](#info)
   - [CONFIG](#config)
   - [CMD > LOAD](#cmd--load)
   - [CMD > PUBLISH](#cmd--publish)
   - [CMD > SUBSCRIBE](#cmd--subscribe)
   - [CMD > UNSUBSCRIBE](#cmd--unsubscribe)
   - [CMD > LIST](#cmd--list)
   - [CMD < (receive)](#cmd--receive)
   - [SCRIPT](#script)
   - [CYCLIC](#cyclic)
4. [End-to-end scenarios (single instance)](#4-end-to-end-scenarios-single-instance)
5. [Publisher and subscriber together: two instances, one real struct](#5-publisher-and-subscriber-together-two-instances-one-real-struct)
6. [CONFIG key reference](#6-config-key-reference)
7. [Gotchas](#7-gotchas)

---

## 1. How DDS_TYPED.CMD works

Exactly the same participant model as `DDS.CMD` — see the DDS plugin
tutorial's section 1 — with one addition: before any topic can be
published or subscribed, a **customer type plugin** (a `.so` built from
real `.idl`, see section 2) has to be `LOAD`ed, either at runtime
(`DDS_TYPED.CMD > LOAD`) or automatically via `PRELOAD_PLUGINS` in
`CONFIG`/the `.ini` file. That `.so` is the only thing in the whole system
that knows a topic's actual struct layout — `DdsTypedDriver` itself never
includes a customer's generated header (see `dds_typed_driver.hpp`'s class
doc comment) — so `PUBLISH`/`SUBSCRIBE` on a topic no loaded plugin
registered simply fails with a clear error telling you to `LOAD` it first.

```
DDS_TYPED.CMD > LOAD <path.so>
DDS_TYPED.CMD > PUBLISH <topic> <payload...>
DDS_TYPED.CMD > SUBSCRIBE <topic>   |   > UNSUBSCRIBE <topic>   |   > LIST   |   <
```

`PUBLISH`'s payload and `<`'s result are always **plain text** — never a
raw struct — because the text grammar itself is entirely up to whichever
customer `.so` registered that topic (its `decode()`/`encode()` pair, see
`DdsTypePluginAbi.h`'s doc comment). This tutorial's `customer1` example
uses a simple `key=value,key=value` grammar; a different customer plugin
could just as easily use JSON — `DDS_TYPED.CMD` itself never parses or
generates the text, only forwards it.

---

## 2. Building your first customer type plugin

Every scenario below uses the `customer1` example already in this repo —
build it once, the same way any real customer's `.so` would be built:

```bash
cd src/plugin/dds_typed_plugin/examples/customer1
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
```

This produces `libcustomer1_types.so`, from three inputs:

- **`idl/customer1.idl`** — the real IDL, compiled by Cyclone's own
  `idlc` (via `idlc_generate()` in `CMakeLists.txt`) into the actual
  `customer1_VehicleState` struct and its `dds_topic_descriptor_t`:
  ```idl
  module customer1 {
      @final
      struct VehicleState {
          long id;
          string label;
          float speed;
      };
  };
  ```
- **`src/customer1_adapter.c`** — the one hand-written file: registers
  the `vehicle/state` topic against `VehicleState`, and defines the
  `id=1,label=truck-07,speed=27.5` text grammar used in every `PUBLISH`
  below (see that file for the full `decode()`/`encode()` implementation).
- **`CMakeLists.txt`** — `idlc_generate()` + `find_package(CycloneDDS)`,
  nothing else; see the DDS_TYPED plugin `README.md`'s "Swapping
  customers" section for the equivalent starting point when writing a new
  customer plugin from scratch instead of building this example.

Note the path you just built it to — every `LOAD`/`pp=` example below
uses `./libcustomer1_types.so`, adjust to wherever your build actually put it.

---

## 3. Command reference, with a scenario for each

### INFO

**Purpose:** print the plugin's version and a usage summary — same role
as `DDS.INFO`, no domain traffic required.

```
DDS_TYPED.INFO
```

**Scenario:** the first line of any new `DDS_TYPED` test script, right
after `LOAD_PLUGIN DDS_TYPED`, to confirm the build in use has the syntax
and `CONFIG` keys you're about to rely on.

---

### CONFIG

**Purpose:** set domain/transport parameters, exactly like `DDS.CONFIG`
(see that tutorial's CONFIG section — same keys, same "before the
participant opens" timing rule), plus one new key: `pp=`, a
semicolon-separated list of customer type-plugin `.so` paths to `LOAD`
automatically the first time the driver opens.

```
DDS_TYPED.CONFIG [d=domain] [pid=participant_id] [v6=0|1] [i=iface] [mi=mcast_iface]
                 [mg=spdp_mcast_group] [n=name] [t=ttl] [sp=spdp_period_ms]
                 [l=lease_sec] [r=0|1] [hd=history_depth]
                 [fr=fragment_threshold_bytes] [pp=path1.so;path2.so]
                 [rt=read_tout] [rb=read_bufsize]
```

**Scenario:** join domain 90 and have `customer1`'s types available
immediately, with no explicit `LOAD` line needed later in the script:

```
LOAD_PLUGIN DDS_TYPED

DDS_TYPED.CONFIG d=90 n=uScriptProbe pp=./libcustomer1_types.so
DDS_TYPED.CMD > PUBLISH vehicle/state id=1,label=truck-07,speed=27.5
```

---

### CMD > LOAD

**Purpose:** `dlopen()` one customer type plugin `.so` and register every
topic it exports (see `DdsTypedDriver::m_LoadPlugin()`'s doc comment).
Can be called more than once, for different `.so`s — several customers'
types can be loaded into the same participant at once. There is no
`UNLOAD` — see [Gotchas](#7-gotchas).

```
DDS_TYPED.CMD > LOAD <path-to-customer.so>
```

**Scenario:** load `customer1`'s types at runtime instead of via
`PRELOAD_PLUGINS`, and confirm what got registered:

```
LOAD_PLUGIN DDS_TYPED

DDS_TYPED.CONFIG d=90
DDS_TYPED.CMD > LOAD ./libcustomer1_types.so

DDS_TYPED.CMD > LIST
types ?= DDS_TYPED.CMD <
LOG.PRINT loaded types: $types
```

---

### CMD > PUBLISH

**Purpose:** publish one sample on a topic — same shape as `DDS.CMD >
PUBLISH`, but the payload text goes through that topic's loaded type's
`decode()` before anything is written to the DDS domain.

```
DDS_TYPED.CMD > PUBLISH <topic> [payload words...]
```

- Fails immediately (no DDS traffic at all) if no loaded plugin
  registered `<topic>` — `LOAD` it first.
- Fails if `decode()` rejects the text (`customer1`'s `decode()` fails if
  it doesn't recognize at least one `key=value` field — see
  `customer1_adapter.c`).
- Otherwise identical eventual-matching/best-effort-by-default behavior
  to `DDS.CMD > PUBLISH` — see that tutorial's Gotchas for the
  discovery-latency implication.

**Scenario:** publish a `VehicleState` update, then deliberately publish
to a topic nothing has registered, to see the failure path — a `PUBLISH`
to an unregistered topic returns a failed `WriteResult` and logs an error
(`"No loaded type plugin publishes topic '...'"`) with no DDS traffic at
all, same fire-and-forget shape as any other `DDS_TYPED.CMD >` line:

```
DDS_TYPED.CONFIG d=90 pp=./libcustomer1_types.so

DDS_TYPED.CMD > PUBLISH vehicle/state id=42,label=truck-07,speed=27.5

DDS_TYPED.CMD > PUBLISH unknown/topic whatever
# logs an error and fails — no loaded type owns unknown/topic
```

---

### CMD > SUBSCRIBE

**Purpose:** create a local reader for one topic — same shape as `DDS.CMD
> SUBSCRIBE`, feeding a queue of `encode()`d text, one entry per received
sample, ready for `DDS_TYPED.CMD <`.

```
DDS_TYPED.CMD > SUBSCRIBE <topic>
```

**Scenario:** subscribe before the publisher side runs (see section 5 for
the full two-instance version of this):

```
DDS_TYPED.CONFIG d=90 pp=./libcustomer1_types.so
DDS_TYPED.CMD > SUBSCRIBE vehicle/state
```

---

### CMD > UNSUBSCRIBE

**Purpose:** drop a local reader — identical to `DDS.CMD > UNSUBSCRIBE`.

```
DDS_TYPED.CMD > UNSUBSCRIBE <topic>
```

**Scenario:** same drain-then-confirm-timeout pattern as the plain DDS
tutorial's UNSUBSCRIBE scenario:

```
DDS_TYPED.CMD > SUBSCRIBE vehicle/state
state1 ?= DDS_TYPED.CMD <

DDS_TYPED.CMD > UNSUBSCRIBE vehicle/state
# A publisher continues sending on vehicle/state elsewhere — this receive
# should now time out (CONFIG rt=) rather than return a value.
state2 ?= DDS_TYPED.CMD <
```

---

### CMD > LIST

**Purpose:** dump every loaded customer type/topic, every participant
discovered via SPDP, every publication/subscription discovered via SEDP,
and this participant's own local writers/readers — the `DDS_TYPED`
equivalent of `DDS.CMD > LIST`, with the loaded-types summary in front.
Same "send now, read result next" pair.

```
DDS_TYPED.CMD > LIST
DDS_TYPED.CMD <
```

**Scenario:** confirm `customer1` actually registered `vehicle/state`
before relying on it later in the script:

```
DDS_TYPED.CONFIG d=90 pp=./libcustomer1_types.so
DDS_TYPED.CMD > LIST
summary ?= DDS_TYPED.CMD <
LOG.PRINT $summary
# e.g.: loaded_types=1 vehicle/state[customer1::VehicleState] ; participants=0 ; ...
```

---

### CMD < (receive)

**Purpose:** block for one sample on the most recently `SUBSCRIBE`d topic
on this thread, same "active topic" hand-off rule as `DDS.CMD <` — see
that tutorial's section for the full explanation, identical here. What
comes back is whatever that topic's loaded type's `encode()` produced —
for `customer1`, the same `key=value,...` grammar `PUBLISH` accepts.

```
DDS_TYPED.CMD <
```

**Scenario:**

```
DDS_TYPED.CMD > SUBSCRIBE vehicle/state
state ?= DDS_TYPED.CMD <
LOG.PRINT got: $state   # e.g. "id=42,label=truck-07,speed=27.500000"
```

---

### SCRIPT

**Purpose:** run several `DDS_TYPED.CMD`-style lines from a file over the
same participant — identical mechanism to `DDS.SCRIPT`.

```
DDS_TYPED.SCRIPT <scriptfile> [delay_ms]
```

Each non-empty, non-`#`-comment line in the file is exactly one
`DDS_TYPED.CMD` argument string, resolved under the plugin's
`ARTEFACTS_PATH` — same convention as `DDS.SCRIPT`.

**Scenario:** a `publish_batch.txt` containing several `> PUBLISH` lines:

```
# publish_batch.txt
> PUBLISH vehicle/state id=1,label=truck-01,speed=10.0
> PUBLISH vehicle/state id=2,label=truck-02,speed=15.5
> PUBLISH vehicle/state id=3,label=truck-03,speed=22.0
```

```
DDS_TYPED.CONFIG d=90 pp=./libcustomer1_types.so
DDS_TYPED.SCRIPT publish_batch.txt 200
```

---

### CYCLIC

**Purpose:** periodic publish, same participant as `CMD` — identical
mechanism to `DDS.CYCLIC`.

```
DDS_TYPED.CYCLIC "time1:val1, time2:val2, ..."
```

Each `valN` is a full `DDS_TYPED.CMD`-style `> ...` argument, so it's
normally `> PUBLISH <topic> <payload>`.

**Scenario:** publish a simulated heartbeat reading every second:

```
DDS_TYPED.CONFIG d=90 pp=./libcustomer1_types.so
DDS_TYPED.CYCLIC "1000:> PUBLISH vehicle/state id=1,label=truck-07,speed=27.5"
```

---

## 4. End-to-end scenarios (single instance)

### Scenario A — load, publish, and read back within one participant

The simplest possible smoke test: publish and subscribe on the same
participant, confirming `customer1`'s round trip works before relying on
it across two instances.

```
LOAD_PLUGIN DDS_TYPED

DDS_TYPED.CONFIG d=91 pp=./libcustomer1_types.so
DDS_TYPED.CMD > SUBSCRIBE vehicle/state

DDS_TYPED.CMD > PUBLISH vehicle/state id=42,label=truck-07,speed=27.5
state ?= DDS_TYPED.CMD <

IF $state == "id=42,label=truck-07,speed=27.500000" GOTO pass
LOG.PRINT SELF-TEST FAILED: got '$state'
GOTO done

LABEL pass
LOG.PRINT DDS_TYPED round-trip self-test passed

LABEL done
```

### Scenario B — several customers' types loaded at once

`LOAD` (or `pp=`) can register more than one `.so`'s topics on the same
participant — useful when one process genuinely needs to speak more than
one customer's data model simultaneously:

```
LOAD_PLUGIN DDS_TYPED

DDS_TYPED.CONFIG d=91 pp=./libcustomer1_types.so;./libcustomer2_types.so

DDS_TYPED.CMD > LIST
summary ?= DDS_TYPED.CMD <
LOG.PRINT $summary   # loaded_types=2, one topic per customer .so
```

(`libcustomer2_types.so` isn't included in this repo — build one
following `customer1`'s pattern, on a topic name that doesn't collide
with `vehicle/state`; see [Gotchas](#7-gotchas) for what happens if it does.)

---

## 5. Publisher and subscriber together: two instances, one real struct

Exactly the multi-instance mechanism from the DDS plugin tutorial's
section 4 (`DDS_TYPED:1`, `DDS_TYPED:2`, distinct `PARTICIPANT_ID`s
required — see that tutorial for the full explanation of why), replaying
its Scenario A loopback test but with `customer1`'s real `VehicleState`
struct on the wire instead of a generic string:

```
LOAD_PLUGIN DDS_TYPED

DDS_TYPED:1.CONFIG d=92 pid=1 n=publisher pp=./libcustomer1_types.so
DDS_TYPED:2.CONFIG d=92 pid=2 n=subscriber pp=./libcustomer1_types.so

DDS_TYPED:2.CMD > SUBSCRIBE vehicle/state
received ?= DDS_TYPED:2.CMD < &

# Give SPDP+SEDP a moment to discover each other and match the topic —
# same reasoning as the DDS plugin tutorial's identical DELAY.
DELAY 2500 ms

DDS_TYPED:1.CMD > PUBLISH vehicle/state id=7,label=forklift-03,speed=4.2

REPEAT wait_state UNTIL $received != ""
  DELAY 100 ms
END_REPEAT wait_state

IF $received == "id=7,label=forklift-03,speed=4.200000" GOTO pass
LOG.PRINT SELF-TEST FAILED: expected forklift-03, got '$received'
GOTO done

LABEL pass
LOG.PRINT DDS_TYPED two-instance self-test passed

LABEL done
```

Both instances need their own `pp=./libcustomer1_types.so` — each
`DDS_TYPED:N` is a fully separate `DdsTypedDriver`/participant with its
own independently-loaded set of customer `.so`s (see the DDS plugin
tutorial's Gotchas: "no shared state between instances" applies here
identically).

---

## 6. CONFIG key reference

Same table as the DDS plugin tutorial's, minus `hb=` (no `DDS_TYPED`
equivalent — there's no legacy ini key to stay compatible with here, so
it was simply never added), plus `pp=`:

| Short key | INI key | Meaning |
|---|---|---|
| `d=` | `DOMAIN` | DDS domain id |
| `pid=` | `PARTICIPANT_ID` | Selects this participant's RTPS discovery port index — give co-located instances distinct values |
| `v6=` | `USE_IPV6` | `true`/`false` — IPv6 transport instead of IPv4 |
| `i=` | `IFACE` | Local bind address (`"::"` default for IPv6) |
| `mi=` | `MCAST_IFACE` | IPv4: local interface IP. IPv6: local interface *name* (e.g. `eth0`) |
| `mg=` | `SPDP_MULTICAST_GROUP` | SPDP multicast group; empty = Cyclone's own family default |
| `n=` | `PARTICIPANT_NAME` | Carried in standard USER_DATA QoS, shown by `DDS_TYPED.CMD > LIST` on peers |
| `t=` | `TTL` | SPDP multicast TTL/hop limit |
| `sp=` | `SPDP_PERIOD_MS` | Participant announcement interval |
| `l=` | `LEASE_DURATION_SEC` | How long a peer is kept without hearing a fresh SPDP |
| `r=` | `RELIABLE` | `true`/`false` — HEARTBEAT/ACKNACK reliability for this participant's local writers/readers |
| `hd=` | `HISTORY_DEPTH` | `KEEP_LAST` depth QoS, in samples |
| `fr=` | `FRAGMENT_THRESHOLD_BYTES` | Cyclone `General/FragmentSize`; `0` leaves Cyclone's own default |
| `pp=` | `PRELOAD_PLUGINS` | Semicolon-separated customer type-plugin `.so` paths, loaded automatically the first time the driver opens |
| `rt=` | `READ_TIMEOUT` | Read timeout (ms) used by `DDS_TYPED.CMD <` |
| `rb=` | `READ_BUFFER_SIZE` | Max size (bytes) of one `DDS_TYPED.CMD <` result |

---

## 7. Gotchas

- **A topic must be `LOAD`ed before it can be `PUBLISH`ed or
  `SUBSCRIBE`d.** Unlike `DDS.CMD`, where any topic name works
  immediately, `DDS_TYPED.CMD` needs a loaded customer `.so` to have
  registered that exact topic name first — `PUBLISH`/`SUBSCRIBE` on an
  unregistered topic fails immediately with no DDS traffic at all. Put
  `pp=` in `CONFIG` rather than relying on remembering a `LOAD` line.
- **There is no `UNLOAD`.** A loaded `.so` may still be backing live
  topics/writers/readers, and `DdsTypedDriver` doesn't track that finely
  enough to safely `dlclose()` it on demand — see
  `dds_typed_driver.hpp`'s class doc comment. To swap which customer
  `.so` is active, reconfigure with `DDS_TYPED.CONFIG` (any `CONFIG` call
  resets the cached driver, same as `DDS.CONFIG` — the *next*
  `DDS_TYPED.CMD` opens a fresh participant and re-runs `pp=` from
  scratch).
- **Two loaded plugins registering the same topic name: last-loaded
  wins**, logged as a warning — not an error. Deliberate, since loading
  several customers side by side (section 4, Scenario B) is a supported
  case; just be aware of load order if two `.so`s happen to use the same
  topic name.
- **The payload/result text grammar is entirely up to the loaded
  customer `.so`, not this plugin.** `customer1`'s `id=1,label=x,speed=1.0`
  grammar doesn't trim whitespace around the commas — `PUBLISH vehicle/state
  id=1, label=x` (space after the comma) decodes `label` as `" label"`
  (leading space) and silently fails to match, since `decode()` only
  recognizes the exact keys `id`/`label`/`speed`. Check whatever grammar
  your actual customer `.so` implements (`customer1_adapter.c`'s
  `VehicleState_decode()` for this tutorial's) rather than assuming any
  particular convention holds generally.
- **ABI version mismatches fail `LOAD` cleanly, not silently.** If a
  customer `.so` was built against a different `DdsTypePluginAbi.h`
  version than `DdsTypedDriver` was, `LOAD` logs the mismatch and fails
  rather than loading something that could misinterpret memory layouts —
  rebuild the customer `.so` against the current header if this happens.
- Every other DDS-level gotcha (discovery latency needing a `DELAY`, no
  ack pipe, `DDS_TYPED.CMD <` needing a prior `SUBSCRIBE` on the same
  thread, co-located instances needing distinct `PARTICIPANT_ID`s, no
  shared state between `DDS_TYPED:N` instances) is identical to the plain
  `DDS` plugin — see [its tutorial's Gotchas](../../dds_plugin/docs/dds_plugin_tutorial.md#6-gotchas)
  rather than duplicating them here.
