# DDS_TYPED Plugin — Communication Flow Tutorial

This tutorial explains **how the `DDS_TYPED` plugin actually moves a
message from one instance to another** — from the moment a script calls
`PUBLISH` on one instance to the moment `SUBSCRIBE` + `<` returns the
value on a different instance. It's based on the real sources:

- `src/plugins/dds_typed_plugin/src/dds_typed_plugin.cpp` / `inc/dds_typed_plugin.hpp` — the script-facing plugin (`DDS_TYPED.CONFIG` / `.CMD` / `.SCRIPT` / `.CYCLIC`)
- `src/libs/drivers/dds_typed/src/dds_typed_driver.cpp` / `inc/dds_typed_driver.hpp` — `DdsTypedDriver`, the generic Cyclone DDS participant that never sees a customer struct
- `include/driver/inc/DdsTypePluginAbi.h` — the plain-C ABI between the driver and a customer's `.so`
- `src/plugins/dds_typed_plugin/examples/customer1` — a concrete `VehicleState` type plugin used as the running example

For the full command reference (every `CONFIG` key, every `CMD` verb),
see the existing [`dds_typed_plugin_tutorial.md`](../src/plugins/dds_typed_plugin/docs/dds_typed_plugin_tutorial.md).
This document is narrower and deeper: it's about **what happens on the
wire and in memory** between two instances of the plugin.

---

## Table of contents

1. [The three layers, in one picture](#1-the-three-layers-in-one-picture)
2. [Why there are two "DDS" plugins](#2-why-there-are-two-dds-plugins)
3. [The ABI: what a customer `.so` actually hands the driver](#3-the-abi-what-a-customer-so-actually-hands-the-driver)
4. [Publisher-side flow: from `PUBLISH` text to a DDS sample on the wire](#4-publisher-side-flow-from-publish-text-to-a-dds-sample-on-the-wire)
5. [Subscriber-side flow: from a DDS sample to text in the receive queue](#5-subscriber-side-flow-from-a-dds-sample-to-text-in-the-receive-queue)
6. [End-to-end: two instances, one topic](#6-end-to-end-two-instances-one-topic)
7. [Discovery: how the two participants find each other](#7-discovery-how-the-two-participants-find-each-other)
8. [Failure paths](#8-failure-paths)
9. [Summary cheat sheet](#9-summary-cheat-sheet)

---

## 1. The three layers, in one picture

Every `DDS_TYPED` instance is built out of three separable pieces. Only
the bottom one ever touches the *shape* of your data.

```
┌───────────────────────────────────────────────────────────────────────┐
│  SCRIPT LAYER            DDS_TYPED.CONFIG / .CMD / .SCRIPT / .CYCLIC  │
│  (dds_typed_plugin.cpp)  parses text commands, one DdsTypedDriver     │
│                           per "DDS_TYPED:N" instance                  │
└───────────────────────────────┬───────────────────────────────────────┘
                                │ send()/receive() — plain text in/out
┌───────────────────────────────▼───────────────────────────────────────┐
│  DRIVER LAYER             DdsTypedDriver                              │
│  (dds_typed_driver.cpp)   • owns ONE Cyclone DDS participant          │
│                            • owns writers/readers per topic           │
│                            • NEVER includes a customer's generated    │
│                              header, NEVER touches a struct field     │
│                            • only calls function pointers it was      │
│                              handed by a loaded customer .so          │
└───────────────────────────────┬───────────────────────────────────────┘
                                │ dlopen()/dlsym() "dds_type_plugin_get"
┌───────────────────────────────▼───────────────────────────────────────┐
│  TYPE LAYER                customer_*.so  (e.g. libcustomer1_types.so)│
│  (generated from .idl +    • real idlc-generated struct + descriptor  │
│   one hand-written adapter)• alloc_sample / free_sample               │
│                             • decode(text)  -> struct                 │
│                             • encode(struct)-> text                   │
│                             THIS is the only place field layout exists│
└───────────────────────────────────────────────────────────────────────┘
```

The split matters for the communication flow because it explains a
subtlety you'll see below: **the text that crosses `DDS_TYPED.CMD`'s
`PUBLISH`/`<` boundary is never the wire format.** The wire format is a
real, binary, `idlc`-marshalled IDL struct. The text is just this one
process's convenient front door to it, defined entirely by whichever
customer `.so` owns that topic.

---

## 2. Why there are two "DDS" plugins

`DDS_TYPED` has a plain sibling, `DDS` (`DdsDriver`/`dds_plugin`), which
publishes/subscribes *every* topic as one generic
`{ string payload; }` IDL type — no `.idl`, no build step, just a
string in, a string out. `DDS_TYPED` exists for the opposite case: you
need to put a **real** struct (e.g. NGVA's `VehicleState`) on the wire
because some external, non-uScript DDS participant expects exactly that
type. `DDS_TYPED` is otherwise deliberately the same shape — same
`CONFIG`/`CMD`/`SCRIPT`/`CYCLIC` surface, same one-participant-per-instance
model, same `DDS_TYPED:N` multi-instance convention — with one addition
(`LOAD`) needed to teach it what a real struct looks like before it can
use one.

---

## 3. The ABI: what a customer `.so` actually hands the driver

`DdsTypePluginAbi.h` defines a small, plain-C, versioned contract
(`DDS_TYPE_PLUGIN_ABI_VERSION`). One exported symbol,
`dds_type_plugin_get()`, returns a `DdsTypePlugin*` — a list of
`DdsTypeEntry`, one per topic:

```
DdsTypeEntry
┌─────────────────────────────────────────────────────────────────┐
│ topic_name     "vehicle/state"                                  │
│ descriptor     idlc-generated dds_topic_descriptor_t*           │
│                 (handed straight to dds_create_topic())         │
│alloc_sample() correctly-sized/initialized malloc for this struct│
│free_sample()  its matching, layout-aware free                   │
│decode(text, out_sample)   PUBLISH's text  ->  filled struct     │
│encode(sample, out_buf)    a received struct ->  text            │
└─────────────────────────────────────────────────────────────────┘
```

Two design details explain *why* the driver can stay ignorant of the
struct:

- **`alloc_sample`/`free_sample` instead of `malloc`+`memcpy`.** A
  struct with a `string` or `sequence` field owns heap memory beyond its
  top-level size (`descriptor->m_size`); only code that knows the real
  layout — i.e. `idlc`-generated code inside the `.so` — can allocate or
  free it correctly.
- **`decode`/`encode` instead of a shared serialization format.** The
  text grammar (`customer1` uses `id=1,label=truck-07,speed=27.5`; a
  different customer could use JSON) is entirely private to that one
  `.so`. `DdsTypedDriver` forwards the text unmodified in both
  directions and never parses it.

---

## 4. Publisher-side flow: from `PUBLISH` text to a DDS sample on the wire

```
Script                                                    Cyclone DDS
  │
  │  DDS_TYPED.CMD > PUBLISH vehicle/state id=42,label=truck-07,speed=27.5
  ▼
dds_typed_plugin.cpp
  parses "PUBLISH <topic> <text...>", calls driver->send(...)
  │
  ▼
DdsTypedDriver::m_Publish(topic, text)
  │
  ├─ 1. m_EnsureLocalWriter(topic)
  │        │
  │        ├─ topic already has a writer?  → reuse it
  │        └─ else: look up topic in m_typesByTopic
  │                 (populated only by a prior LOAD / pp=)
  │                 not found → ERROR "No loaded type plugin
  │                              publishes topic '...'" · STOP, no DDS traffic
  │                 found → dds_create_topic(descriptor)
  │                          dds_create_writer(qos: reliability, history)
  │
  ├─ 2. sample = entry->alloc_sample()        [customer .so code runs]
  │
  ├─ 3. ok = entry->decode(text, sample)       [customer .so code runs]
  │        text doesn't parse → ERROR, free_sample(), STOP — still no
  │        DDS traffic
  │
  ├─ 4. dds_write(writer, sample)  ─────────────────────────►  RTPS packet
  │        real, binary, idlc-marshalled VehicleState            on the
  │        goes out — NOT the "id=..." text                      network
  │
  └─ 5. entry->free_sample(sample)
```

Key point: steps 2, 3 and 5 are the **only** moments any code outside
`DdsTypedDriver` runs, and it's always the loaded customer `.so`'s code
— the driver itself never dereferences a single struct field.

---

## 5. Subscriber-side flow: from a DDS sample to text in the receive queue

Subscribing sets up a Cyclone reader with a data-available **listener**
callback — it is *not* a polling loop. Data arrives asynchronously,
independent of any script line waiting on it:

```
                                                     Cyclone DDS
                                                          │
                                          RTPS packet in  │
                                                          ▼
DdsTypedDriver::m_EnsureLocalReader(topic)     [set up once, on first
  dds_create_topic(descriptor)                  SUBSCRIBE for this topic]
  dds_create_reader(qos, listener = m_OnReaderDataAvailable)
                                                          │
                                                          ▼
DdsTypedDriver::m_OnReaderDataAvailable(reader, arg)   ← fired by Cyclone's
  │                                                       own thread, NOT
  ├─ alloc_sample() for a batch of slots  [customer .so]  the script thread
  │
  ├─ dds_take(reader, samples, infos, ...)  — drains what Cyclone
  │       decoded from the wire into real VehicleState structs
  │
  ├─ for each valid sample:
  │     entry->encode(sample, buf, cap)      [customer .so]
  │     buf, e.g. "id=42,label=truck-07,speed=27.500000"
  │     │
  │     ▼
  │   localReader->queue.push_back(buf)   ── guarded by queueMutex
  │   localReader->queueCv.notify_all()
  │
  └─ free_sample() for the batch            [customer .so]
```

Meanwhile, on the **script thread**:

```
DDS_TYPED.CMD > SUBSCRIBE vehicle/state
  → m_EnsureLocalReader(topic) creates the reader above,
    and marks this topic as the "active topic" for this thread

DDS_TYPED.CMD <
  → DdsTypedDriver::receive()
      waits on localReader->queueCv until the queue is non-empty
      (or u32ReadTimeout / stop_tok fires first)
      pops the front string, returns it as the CMD's result
```

The **"active topic"** hand-off (`m_strActiveTopic`) is what lets a bare
`DDS_TYPED.CMD <` — with no topic argument — know which reader's queue
to drain: it's always the most recently `SUBSCRIBE`d topic on that
thread.

---

## 6. End-to-end: two instances, one topic

Putting sections 4 and 5 together, with `DDS_TYPED:1` as publisher and
`DDS_TYPED:2` as subscriber (matching the tutorial's own two-instance
scenario, `d=92`, distinct `pid=`):

```
 DDS_TYPED:1  (publisher)                        DDS_TYPED:2  (subscriber)
 participant_id=1                                 participant_id=2
┌───────────────────────────┐                    ┌───────────────────────────┐
│ dds_typed_plugin.cpp      │                    │ dds_typed_plugin.cpp      │
│  CONFIG d=92 pid=1        │                    │  CONFIG d=92 pid=2        │
│    pp=libcustomer1_types  │                    │    pp=libcustomer1_types  │
└─────────────┬─────────────┘                    └─────────────┬─────────────┘
              │ open()                                         │ open()
              ▼                                                ▼
┌───────────────────────────┐                    ┌───────────────────────────┐
│ DdsTypedDriver            │                    │ DdsTypedDriver            │
│  dds_create_participant() │                    │  dds_create_participant() │
│  m_LoadPlugin(pp=...)     │                    │  m_LoadPlugin(pp=...)     │
│   -> registers            │                    │   -> registers            │
│      "vehicle/state"      │                    │      "vehicle/state"      │
│      -> customer1::       │                    │      -> customer1::       │
│         VehicleState      │                    │         VehicleState      │
└─────────────┬─────────────┘                    └─────────────┬─────────────┘
              │                                                │
              │        ◄─────── SPDP (participant discovery) ────────►
              │        ◄─────── SEDP (endpoint/topic matching) ──────►
              │                          (needs a brief DELAY —          │
              │                           see §7)                        │
              │                                                          │
   CMD > PUBLISH vehicle/state                        CMD > SUBSCRIBE vehicle/state
   id=7,label=forklift-03,speed=4.2                     (reader + listener created,
              │                                          "vehicle/state" becomes the
              ▼                                          active topic)
   alloc_sample() / decode() / dds_write()                         │
              │                                                    │
              └──────────────── RTPS DATA submessage ─────────────►│
                  (real, binary VehicleState struct,               │
                   matched writer→reader over UDP/mcast)           ▼
                                                        m_OnReaderDataAvailable()
                                                          dds_take() / encode()
                                                          -> "id=7,label=forklift-03,
                                                              speed=4.200000"
                                                          queue.push_back(...)
                                                                   │
                                                           CMD <   ▼
                                                        pops queue, returns text
                                                        to the waiting script line
```

Two things worth calling out that trip people up:

- **Both instances need their own `LOAD`/`pp=`.** Each `DDS_TYPED:N` is
  a fully separate `DdsTypedDriver` with its own independently loaded
  set of customer `.so`s and its own `m_typesByTopic` map — nothing is
  shared between instances, not even within the same process.
- **The text never travels over DDS.** `"id=7,label=forklift-03,speed=4.2"`
  is consumed by `decode()` on the publisher side and produced by
  `encode()` on the subscriber side; what actually crosses the network
  is the binary `VehicleState` struct, matched purely by topic name +
  type (Cyclone's normal DDS matching, nothing `DDS_TYPED`-specific).

---

## 7. Discovery: how the two participants find each other

This part is identical to the plain `DDS` plugin and is standard DDS —
`DDS_TYPED` adds nothing here:

1. **SPDP** (Simple Participant Discovery Protocol) — each participant
   periodically (`sp=`/`SPDP_PERIOD_MS`) multicasts/announces itself so
   peers on the domain learn it exists, tracked with a lease
   (`l=`/`LEASE_DURATION_SEC`).
2. **SEDP** (Simple Endpoint Discovery Protocol) — once participants
   know about each other, they exchange their writers'/readers'
   topic + type + QoS, and Cyclone matches a writer to a reader only
   when topic name, type, and compatible QoS (e.g. `RELIABLE` vs.
   `BEST_EFFORT`) all agree.
3. Only **after** that match completes will a `dds_write()` on the
   publisher actually reach the subscriber's `dds_take()`. This is why
   the two-instance scenario needs a short `DELAY` (~2.5s is used in the
   tutorial) between bringing both instances up and the first `PUBLISH`
   — publishing before discovery finishes is not an error, the sample
   is just never delivered (DDS's normal best-effort-by-default,
   no-retroactive-delivery semantics).

`DDS_TYPED.CMD > LIST` surfaces this discovery state directly: loaded
types/topics, discovered remote participants (via the
`DCPSPARTICIPANT` builtin topic reader), and discovered
publications/subscriptions (via `DCPSPUBLICATION`/`DCPSSUBSCRIPTION`) —
useful for confirming the two instances actually see each other before
assuming a `PUBLISH` should have arrived.

---

## 8. Failure paths

All of these fail **before any DDS traffic happens** — they're pure
local checks against `m_typesByTopic` / the customer `.so`'s functions:

```
PUBLISH/SUBSCRIBE on a topic no loaded .so registered
   └─► "No loaded type plugin publishes/subscribes topic '...'"
        — LOAD its customer .so first. No topic, no writer/reader,
          nothing sent.

PUBLISH text that decode() rejects (e.g. customer1's grammar needs
at least one recognized key=value field)
   └─► "decode() rejected PUBLISH payload for '...'"
        — sample freed, dds_write() never called.

LOAD on a .so with a different DDS_TYPE_PLUGIN_ABI_VERSION
   └─► "ABI version mismatch loading '...'"
        — dlclose()d immediately, nothing registered. Fails loudly
          rather than risking a struct-layout misinterpretation.

Two loaded .so's registering the same topic name
   └─► logged as a WARNING, not an error — last LOAD wins in
        m_typesByTopic. Deliberate (loading several customers side
        by side is supported); just watch load order.
```

---

## 9. Summary cheat sheet

| Step | Who does it | Layer |
|---|---|---|
| Parse `DDS_TYPED.CMD > PUBLISH ...` text | `dds_typed_plugin.cpp` | Script |
| Look up topic → `DdsTypeEntry` | `DdsTypedDriver::m_EnsureLocalWriter` | Driver |
| `alloc_sample()` / `decode()` | customer `.so` | Type |
| `dds_write()` → RTPS on the wire | Cyclone DDS | (external) |
| SPDP/SEDP discovery + writer↔reader match | Cyclone DDS | (external) |
| `dds_take()` in the reader listener | `DdsTypedDriver::m_OnReaderDataAvailable` | Driver |
| `encode()` → text, pushed to queue | customer `.so` | Type |
| `DDS_TYPED.CMD <` pops the queue | `DdsTypedDriver::receive` | Driver |
| Script line resolves `?=` | `dds_typed_plugin.cpp` | Script |

The one sentence version: **the plugin/driver never see your struct —
they see a topic name and two function pointers (`decode`/`encode`)
supplied by your customer `.so`, and everything in between is ordinary
Cyclone DDS discovery and RTPS delivery of the real, binary IDL type.**


---

Cyclone DDS is configured to use **UDP over IPv4 (`udp`) or IPv6 (`udp6`)**. Concretely, that breaks into two kinds of traffic:

`DdsTypedDriver::m_BuildDomainConfigXml()`

```cpp
xml << "<Transport>" << (m_config.useIpv6 ? "udp6" : "udp") << "</Transport>";
```
```
┌────────────────────────────────────────────────────────────────────┐
│ Discovery (SPDP)   → UDP MULTICAST                                 │
│   periodic participant announcements, interval = SPDPInterval      │
│   (CONFIG sp=), group = SPDPMulticastAddress (CONFIG mg=,          │
│   Cyclone's own default if empty), hop limit = MulticastTimeToLive │
│   (CONFIG t=)                                                      │
├────────────────────────────────────────────────────────────────────┤
│ Discovery (SEDP) + Data (RTPS DATA submessages) → UDP UNICAST      │
│   (or also multicast, depending on Cyclone's defaults/QoS) once    │
│   participants know each other's unicast locators from SPDP        │
└────────────────────────────────────────────────────────────────────┘
```

A few things that follow from this:

- **Which physical link it actually rides on is whatever carries IP for that interface** — Ethernet, Wi-Fi, a VLAN, a virtual/bridge interface, or `lo` (loopback). `CONFIG i=`/`mi=` (`ifaceAddress`/`multicastInterface`, → `<Interfaces><NetworkInterface address="..." or name="..."/>`) pick *which* interface Cyclone binds to; nothing in `DdsTypedDriver` or the plugin cares whether that interface happens to be Ethernet.
- **Two `DDS_TYPED:N` instances in the same uScript process** (the two-instance scenario from the previous tutorial) don't get any shortcut — they're two independent `DdsTypedDriver`s, each with its own `dds_create_participant()`. They still talk over the network stack (typically over `lo` if bound to the same host), going through the full SPDP/SEDP/RTPS path like any two separate DDS participants would. There's no in-process shared-memory or function-call shortcut between them.
- **`v6=`/`useIpv6`** just switches `udp` → `udp6`; same UDP-based mechanism, IPv6 sockets instead of IPv4.
- **`RELIABLE` (`r=`)** doesn't change the transport — it's still UDP (no TCP fallback here) — it adds Cyclone's own RTPS-level `HEARTBEAT`/`ACKNACK` retransmission on top of UDP to get reliable delivery despite UDP itself being unreliable/unordered.

So: same-host instances typically go out over loopback, cross-host instances go out over whatever Ethernet/Wi-Fi interface is configured — but in both cases it's the identical UDP/RTPS path; the plugin has no separate "local" transport mode.