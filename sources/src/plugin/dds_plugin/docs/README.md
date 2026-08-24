# DDS plugin — DDSI-RTPS publish/subscribe over Ethernet

The DDS plugin speaks the OMG **DDSI-RTPS** wire protocol — the
interoperable transport underneath every DDS implementation (OpenDDS, RTI
Connext, CycloneDDS, eProsima FastDDS, ...) — directly over UDP/IP on the
local Ethernet segment. It follows the same CONFIG/CMD/SCRIPT/CYCLIC shape
as the MQTT and gRPC plugins.

This is what NGVA (STANAG 4754, NATO Generic Vehicle Architecture) uses for
inter-subsystem data exchange in its Data Model — see *"NGVA Data Model and
OpenDDS"*, which documents building OpenDDS and generating C++ code from the
NGVA Video/LDM Common IDL modules to drive a Galleon Video Recorder over
DDS. This plugin does not require any of that code generation: instead of
linking a vendor's generated C++ types, it publishes/subscribes each topic
as one opaque payload string, matched purely by **topic name** (the same
model as an MQTT topic) — so it can talk to any `@topic`-annotated
NGVA-style struct's DDS traffic, or any other DDS topic, without prior
knowledge of its IDL type.

## Scope

- IPv4 or IPv6, single-stack per plugin instance (`v6=1`).
- Reliable QoS available (`r=1`): HEARTBEAT/ACKNACK tracked at **sample**
  granularity, with a bounded per-writer resend cache (`hd=`, default 32
  samples) — not an unbounded `KEEP_ALL` history.
- Fragmentation: samples over `fr=` bytes (default 1300, matching a safe
  single-Ethernet-frame UDP payload) are split into `DATA_FRAG`
  submessages and reassembled on the reader side. A lost fragment causes
  the *whole* sample to be re-requested via the normal ACKNACK path
  (`NACK_FRAG`, which would allow requesting just the missing fragments,
  is not implemented).
- Unkeyed topics: one CDR `string` sample in, one out — no DDS
  instance/key model.
- Discovery implements the real SPDP (participant) + SEDP
  (publication/subscription) built-in protocols, so a genuine OpenDDS (or
  any other RTPS-compliant) participant on the same domain/segment will see
  this plugin as a real DDS participant and match its topics normally.

## Quick start

```
DDS.CONFIG d=0 i=192.168.1.50 n=uScriptProbe
DDS.CMD > SUBSCRIBE C_Actual_Video_Sink
...
DDS.CMD <                       // blocks until a sample matching that topic arrives
```

Publish:

```
DDS.CONFIG d=0
DDS.CMD > PUBLISH C_Actual_Video_Stream_requestVideoStream 12
```

`PUBLISH` succeeds even before any subscriber has been discovered yet —
matching is asynchronous, same as any other best-effort pub/sub transport;
retry the publish (or use `DDS.CYCLIC`) once a matching `SUBSCRIBE` is
known to be running elsewhere on the domain.

List everything discovered so far (participants, and every endpoint seen
via SEDP):

```
DDS.CMD > LIST
DDS.CMD <
```

## Reliable QoS

```
DDS.CONFIG d=0 r=1 hb=500 hd=64
```

`r=1` makes every locally created writer/reader reliable: writers keep a
resend cache and send periodic HEARTBEATs; readers ACKNACK gaps against
each matched writer's HEARTBEAT. Works against a best-effort peer too —
it just never sends ACKNACK, so the extra HEARTBEATs are ignored.

## Fragmentation

```
DDS.CONFIG fr=1200   // fragment anything over 1200 bytes; fr=0 disables fragmentation
DDS.CMD > PUBLISH LargeTopic <a payload bigger than 1200 bytes>
```

Fragmentation and reliability compose: a fragmented reliable sample that's
only partially received is simply still "missing" as a whole and gets
fully re-sent (all fragments) on the next ACKNACK, rather than
re-requesting only the missing fragments.

## IPv6

```
DDS.CONFIG v6=1 i=fe80::1%eth0 mi=eth0 mg=ff03::1:7401
```

The DDSI-RTPS spec doesn't define a standard default IPv6 SPDP multicast
group the way it does for IPv4 (`239.255.0.1`) — `mg=` **must** be set to
whatever group your peer implementation is configured with, or automatic
discovery won't work. `mi=` is a local interface *name* (e.g. `eth0`) for
IPv6, unlike IPv4 where it's an interface IP address.

## Multiple local participants / co-located instances

`PARTICIPANT_ID` (CONFIG `pid=`) selects this instance's unicast
metatraffic/user-data ports per the RTPS well-known port formula
(`7400 + 250*domain + ...`); give each co-located `DDS` plugin instance
(e.g. `DDS:1`, `DDS:2` — see `PluginDataSet::strInstanceName`) a distinct
`pid` so their sockets don't collide.

## Interoperating with an OpenDDS peer

No special configuration is needed on the OpenDDS side beyond the standard
RTPS discovery transport (`OpenDDS`'s default) on the same `DOMAIN`
id — SPDP multicast discovery (`239.255.0.1`) finds this plugin
automatically, the same way two independent OpenDDS processes find each
other.
