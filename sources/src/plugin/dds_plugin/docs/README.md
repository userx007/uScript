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

- Best-effort QoS only — no `HEARTBEAT`/`ACKNACK` reliability protocol, no
  fragmentation.
- IPv4 (`LOCATOR_KIND_UDPv4`) only.
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
