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
