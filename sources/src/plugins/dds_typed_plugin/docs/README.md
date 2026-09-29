# DDS_TYPED plugin

The strongly-typed counterpart to the [DDS plugin](../../dds_plugin/docs/README.md).
Where `DDS.CMD` publishes/subscribes every topic as one generic
`{ string payload; }` sample, `DDS_TYPED.CMD` loads real, customer-specific
IDL types at runtime from a `.so` built with Eclipse Cyclone DDS's `idlc`,
and exchanges each type's actual struct on its own topic — with the
plugin and driver themselves never including a customer's generated
header or touching a single struct field.

```
DDS_TYPED.CMD > LOAD ./libcustomer1_types.so
DDS_TYPED.CMD > PUBLISH vehicle/state id=1,label=truck-07,speed=27.5
DDS_TYPED.CMD > SUBSCRIBE vehicle/state
DDS_TYPED.CMD <
```

## How the pieces fit together

- **`DdsTypePluginAbi.h`** (`include/driver/inc`) — the stable C ABI
  between the driver and a customer's `.so`: a Cyclone
  `dds_topic_descriptor_t`, alloc/free function pointers (exactly what
  `idlc` already generates per type), and one `decode`/`encode` pair that
  bridges `DDS_TYPED.CMD`'s plain-text PUBLISH/receive to that type's
  real struct. See the header's doc comment for the full design
  rationale — in particular, why it deliberately reuses Cyclone's own
  descriptor struct rather than wrapping it, and why `alloc`/`free` (not
  a raw `memcpy`) is required for any type with a `string`/`sequence`
  field.
- **`DdsTypedDriver`** (`src/lib/drivers/dds_typed`) — owns the Cyclone
  DDS participant and the `DDS_TYPED.CMD` command parsing. `dlopen()`s
  customer `.so`s (via `LOAD`, or `PRELOAD_PLUGINS` in the ini file) and
  routes each topic to whichever loaded type registered it. Never
  includes a customer's generated header.
- **`DdsTypedPlugin`** (this directory) — CONFIG storage and wiring, same
  shape as `DdsPlugin`.
- **`examples/customer1`** — a complete, buildable example: an `.idl`
  file, the one hand-written adapter file a customer needs
  (`src/customer1_adapter.c` — decode/encode plus the ABI table), and a
  `CMakeLists.txt` showing the full `idl -> .so` pipeline via
  `idlc_generate()`. Not linked or referenced by `DdsTypedDriver` or
  `DdsTypedPlugin` by name anywhere — that's the point: swapping in a
  different customer's `.so` needs zero changes to either.

## Swapping customers

Build a new `customer_N.so` following `examples/customer1`'s
`CMakeLists.txt`/adapter pattern (own `.idl`, own `dds_type_plugin_get()`
returning your own topics), then either:

```
DDS_TYPED.CONFIG pp=/path/to/customer_N_types.so
```

or `DDS_TYPED.CMD > LOAD /path/to/customer_N_types.so` at runtime. No
rebuild of `dds_typed_plugin`, `DdsTypedDriver`, or the host application
is needed either way. Several customers' `.so`s can also be loaded at
once (`pp=a.so;b.so`) — each topic name routes to whichever plugin most
recently registered it.

## Using an external Cyclone DDS configuration file

To talk to an application that is run with a native Cyclone DDS config
(the kind normally passed via `CYCLONEDDS_URI`, e.g. `cyclonedds-loopback.xml`,
`cyclonedds-eth0.xml`, `cyclonedds-trace.xml`), give the plugin that same file:

```
DDS_TYPED.CONFIG d=12 cf=./cyclonedds-loopback.xml pp=./libcustomer1_types.so
```

or in the ini file:

```
[DDS_TYPED]
DOMAIN             = 12
CYCLONE_CONFIG_FILE = ./cyclonedds-loopback.xml
PRELOAD_PLUGINS    = ./libcustomer1_types.so
```

Rules:

- The file **replaces** the XML the plugin otherwise generates. `IFACE`,
  `MCAST_IFACE`, `SPDP_MULTICAST_GROUP`, `USE_IPV6`, `TTL`, `SPDP_PERIOD_MS`,
  `LEASE_DURATION_SEC`, `FRAGMENT_THRESHOLD_BYTES` and `PARTICIPANT_ID` are then
  ignored — put what you need in the file. `DOMAIN` / `d=` still selects the
  domain (the files use `Domain Id="any"`). `RELIABLE`, `HISTORY_DEPTH`,
  `PARTICIPANT_NAME` are per-endpoint/participant QoS, not part of that XML, and
  keep working.
- A relative path is looked up in the working directory, then in `ARTEFACTS_PATH`.
  No commas in the path (Cyclone splits its config string on them); for `cf=`
  also no spaces.
- `cf=none` (or an empty `CYCLONE_CONFIG_FILE`) goes back to the generated config.
- If the file is missing, or Cyclone rejects it, the driver does **not** open
  (no silent fallback); Cyclone's own message says which element is wrong.
  If a file with tracing to a path (e.g. `/var/log/dds/...`) is used, that
  directory must exist and be writable.
- Only Cyclone *configuration* files are accepted. DDS-XML QoS profile files
  (`USER_QOS_PROFILES_*.xml`, root element `<dds>`/`<qos_library>`) are a
  different format and are rejected.
- The domain is deleted when the driver closes (if no other participant of this
  process still uses it), so `DDS_TYPED.CONFIG cf=<other file>` really takes effect
  the next time the participant opens.

## Using DDS-XML QoS profiles (`USER_QOS_PROFILES_*.xml`)

A peer application (e.g. an NGVA one) usually expects specific reader/writer QoS —
reliability, durability, history, destination order, ... — that it defines in a
DDS-XML QoS profile file. The plugin can read such a file with Cyclone's QoS Provider
and apply a profile per topic. This is **separate from** `CYCLONE_CONFIG_FILE`
(Cyclone's transport/tracing config, root `<CycloneDDS>`): a typical setup uses both.

```
DDS_TYPED.CONFIG d=12 cf=./cyclonedds-loopback.xml pp=./libcustomer1_types.so \
    qf=./USER_QOS_PROFILES_Cyclone.xml qd=StatePattern \
    qt=Alarms__Actual_Alarm=AlarmPattern;*__acknowledgeAlarm=CommandPattern
```
```ini
[DDS_TYPED]
DOMAIN              = 12
CYCLONE_CONFIG_FILE = ./cyclonedds-loopback.xml
QOS_PROFILE_FILE    = ./USER_QOS_PROFILES_Cyclone.xml
QOS_LIBRARY         = DDSDefaultQoSLibrary
QOS_DEFAULT_PROFILE = StatePattern
QOS_TOPIC_PROFILES  = Alarms__Actual_Alarm=AlarmPattern;*__acknowledgeAlarm=CommandPattern
```

| INI key | CONFIG key | Meaning |
|---|---|---|
| `QOS_PROFILE_FILE` | `qf=` | DDS-XML QoS file. Empty / `none` = feature off (everything built from `RELIABLE`/`HISTORY_DEPTH` as before) |
| `QOS_LIBRARY` | `ql=` | Library used for a profile name written without `::` (default `DDSDefaultQoSLibrary`) |
| `QOS_DEFAULT_PROFILE` | `qd=` | Profile for every topic no rule below matches |
| `QOS_TOPIC_PROFILES` | `qt=` | `topic=profile;topic=profile;...` (`;` or `,`) — `topic` is an exact name or a glob (`*`, `?`, `[..]`); **first match wins**, so put specific names before broad patterns |

Rules:

- A selected profile's `<datawriter_qos>` / `<datareader_qos>` / `<topic_qos>` are used
  **as-is** for that topic. Policies the profile leaves unset get the DDS defaults —
  what an application using the QoS Provider would get. `RELIABLE` / `HISTORY_DEPTH`
  are **not** layered on top.
- A topic with no matching rule and no default profile keeps the built-in
  `RELIABLE`/`HISTORY_DEPTH` QoS.
- A profile that has no entry for one of the three kinds (the empty `Default` profile in
  `USER_QOS_PROFILES_Cyclone.xml` has none) means DDS defaults for that kind. Cyclone may
  print `Failed to get qos with name: ...` for the missing kind; that is expected.
- Profile names are case-sensitive; `Lib::Profile` or a bare `Profile` (then looked up
  in `QOS_LIBRARY`). A missing profile, a missing file, a malformed `QOS_TOPIC_PROFILES`
  or a file Cyclone can't parse makes the driver **refuse to open** — never a silent fallback.
- **Which reader parses the file** depends on how the CycloneDDS you build against was
  configured. With Cyclone's own QoS Provider (Cyclone >= 11.0, `DDS_HAS_QOS_PROVIDER`
  defined) that is used; otherwise (older Cyclone, or built with
  `ENABLE_QOS_PROVIDER=OFF`) the plugin's **built-in DDS-XML reader** takes over
  automatically — no rebuild of Cyclone needed. Both give identical QoS for
  `USER_QOS_PROFILES_Cyclone.xml` (verified profile by profile). Define
  `DDS_TYPED_BUILTIN_QOS_PARSER` to force the built-in one. The debug log says which is
  in use (`QoS profiles: using the built-in DDS-XML reader`).
- Differences between the two: Cyclone's provider parses and validates the **whole**
  file and needs a `<dds>` root, so it rejects `USER_QOS_PROFILES_Consolidated.xml`
  (no `<dds>` root, empty `<deadline><period/>` templates). The built-in reader accepts
  a `<dds>` or `<qos_library>` root and validates **only the profiles you select**, so
  that file works for its complete profiles (State, Command, Event, Alarm,
  Specification) and fails with a clear message for a template one (Periodic, Watchdog)
  — `USER_QOS_PROFILES_Cyclone.xml` works with either.
- Built-in reader scope: durability, reliability (+`max_blocking_time`), history,
  destination_order, deadline, latency_budget, lifespan, liveliness, ownership,
  ownership_strength, transport_priority, resource_limits, writer_data_lifecycle,
  reader_data_lifecycle, time_based_filter. Policies not applicable to that entity, or
  unsupported (user_data, topic_data, ...), and `base_name` inheritance are reported as
  warnings and skipped; an invalid value is an error.
- Publisher / subscriber / participant QoS in the file (partition, ...) is not applied,
  only writer, reader and topic QoS.
- In an INI value do not put whitespace before a `;` (it would start an inline comment);
  `CONFIG` values contain no spaces at all.
- `LIST` shows the applied profile: `local_writers=1 vehicle/state{qos=DDSDefaultQoSLibrary::StatePattern}`.
- Discovery shows what was applied: with `StatePattern` a writer is advertised as
  RELIABLE / TRANSIENT_LOCAL / BY_SOURCE_TIMESTAMP, so a subscriber that starts *later*
  still receives the last sample.

## When to use this instead of (or alongside) `DDS`

Only when this process needs to exchange real, specific IDL structs with
an external DDS participant that expects them on the wire (e.g. an NGVA
subsystem publishing a real `VehicleState`). The plain `DDS` plugin's
generic string-topic model remains the right tool for ad-hoc publish/
subscribe/bridging where no fixed struct is required — the two plugins
are independent and can run side by side.

## Requirements

Same as the `DDS` plugin: `cyclonedds-dev`/`cyclonedds-tools` (for
`find_package(CycloneDDS)` and `idlc`) available at build time.
