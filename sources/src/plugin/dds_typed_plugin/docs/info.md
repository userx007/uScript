Now fix the call site in `send()` to drop the `const_cast`, and fix `open()`'s call (still fine since it's non-const calling a const method):`LOG_SIZET` exists exactly as used. Now let's write the driver's CMakeLists.txt:All just the known LOG_HDR macro-noise, exactly matching the existing codebase convention. Clean compile confirmed. Now let's write the plugin layer, mirroring `dds_plugin` closely.Now the `.cpp` — plugin entry points, `m_OpenDriver` (with the semicolon-split for `PRELOAD_PLUGINS`), `INFO`, `CONFIG`, `CMD`, `SCRIPT`, `CYCLIC`:Now the plugin's `CMakeLists.txt` (mirroring `dds_plugin`'s exactly, swapping the driver dependency):Now the example's CMake, guarded by an opt-in option:Good, real tree intact. Let's wire the new driver in:Now let's do the full real-build + functional validation, mirroring the rigor from the driver rework.Configuration succeeded (just a harmless dev warning from the original repo's own root CMakeLists.txt, unrelated to my changes). Now let's build everything:Both entry points resolve correctly. Now the full functional test through the real driver, with the real customer plugin:Fully validated end-to-end: `LOAD` genuinely `dlopen()`ed the separately-built `libcustomer1_types.so`, `PUBLISH`→`SUBSCRIBE`→`receive` round-tripped through the customer's own text grammar (`id=42,label=truck-07,speed=27.500000`), publishing to an unregistered topic failed cleanly, and `LIST` reported the loaded type correctly. Let's clean up and package everything.Let me add one short README so the new plugin is self-documenting like `dds_plugin` is:Now let's package everything from this turn and present it:Extract at your repo root — it includes two files with small additive edits (`src/lib/drivers/CMakeLists.txt`, `src/plugin/CMakeLists.txt` — just one new `add_subdirectory` line each) plus everything new:

```
include/driver/inc/DdsTypePluginAbi.h                       — the stable ABI (Cyclone descriptor + alloc/free + decode/encode)
src/lib/drivers/dds_typed/                                  — DdsTypedDriver: dlopen loader + generic Cyclone core
src/plugin/dds_typed_plugin/                                — DdsTypedPlugin: CONFIG/CMD/SCRIPT/CYCLIC, mirrors dds_plugin
src/plugin/dds_typed_plugin/examples/customer1/              — complete, buildable reference customer (idl → .so)
src/plugin/dds_typed_plugin/docs/README.md
```

**What's validated, not just written:** built the whole chain through the real, checked-in CMake tree (`uDdsTypedDriver` → `dds_typed_plugin.so`, plus the example `customer1.idl` → `libcustomer1_types.so` via `idlc_generate`), confirmed both `.so`s `dlopen()` with correct entry points, then ran the actual command sequence end-to-end against the real built artifacts:

```
LOAD ./libcustomer1_types.so     → status=0
SUBSCRIBE vehicle/state          → status=0
PUBLISH vehicle/state id=42,label=truck-07,speed=27.5  → status=0
receive()                        → 'id=42,label=truck-07,speed=27.500000'   (exact round-trip)
PUBLISH unknown/topic ...        → fails cleanly (no loaded type registers it)
LIST                             → loaded_types=1 vehicle/state[customer1::VehicleState] ...
```

The driver never included `customer1.h` — the whole round trip went through `DdsTypeEntry`'s function pointers, proving the swap-without-rebuild property for real, not just in the earlier standalone spike.

A few implementation notes worth flagging:
- **No `UNLOAD` command** — a loaded `.so` backing live DDS entities can't be safely `dlclose()`d without tearing those down first, and the driver doesn't track that fine-grained. Reconfigure (`DDS_TYPED.CONFIG`) to swap customers, which resets the driver.
- **Topic-name collisions** across multiple loaded customers: last-loaded wins, logged as a warning — deliberate, documented choice rather than a hard error, since loading several customers side by side is a supported case.
- `DDS_TYPED` is a fully independent plugin from `DDS` — both can be loaded simultaneously, and I didn't touch anything in `dds_plugin`/`DdsDriver` from the earlier rework.

Good question — let me check both paths concretely rather than just describe them.Configured clean. Now build and install:Built clean — `libddsc.so` and the `idlc` compiler both came out of a straight-from-GitHub source build. Let's install and inspect the artifacts:Now let's prove my earlier driver/plugin code builds against *this* self-built CycloneDDS (not the apt one) — confirming it's a drop-in replacement either way:Confirmed — it correctly picked up the from-source-built `libddsc.so`, not the apt one. Now I have everything needed for a concrete answer.## Short answer: it's already `.so` — the question is just *which* `.so`

Everything in what I built (`dds_plugin.so`, `dds_typed_plugin.so`, `libcustomer1_types.so`) is a shared library, dynamically linked against Cyclone DDS's own `libddsc.so` — none of it statically embeds Cyclone. So the real question is where `libddsc.so` itself comes from. Both paths work; here's what each actually gets you, checked concretely rather than assumed.

## Path 1 — distro package (`apt install cyclonedds-dev`)

This is what I used for all the earlier validation. It already installs as a shared library:

```
/usr/lib/x86_64-linux-gnu/libddsc.so.0.10.4
```

but it comes with baggage: Ubuntu's build has TLS security (`libssl`/`libcrypto`) and Iceoryx shared-memory transport compiled in, so `libddsc.so` pulls in three extra `.so`s at runtime whether you use those features or not.

## Path 2 — build from GitHub source (what I'd actually recommend for shipping)I just did this end-to-end to confirm the exact process:

```bash
git clone --depth 1 --branch 0.10.5 https://github.com/eclipse-cyclonedds/cyclonedds.git
cd cyclonedds && mkdir build && cd build

cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_INSTALL_PREFIX=/opt/cyclonedds \
         -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF \
         -DENABLE_SSL=OFF -DENABLE_SHM=OFF   # trim what you don't need

make -j$(nproc)
make install
```

That produces:

```
/opt/cyclonedds/lib/libddsc.so.0.10.5   (+ .so, .so.0 symlinks)
/opt/cyclonedds/bin/idlc
/opt/cyclonedds/lib/cmake/CycloneDDS/*  ← the find_package(CycloneDDS) config files
```

With `ENABLE_SSL=OFF -DENABLE_SHM=OFF`, `libddsc.so` came out depending on **only `libc`** — nothing else:~1.5 MB, `libc` only — a self-contained `.so` you can drop next to your plugins with no other runtime dependencies to manage.

## Wiring it into *your* build without changing any code

`find_package(CycloneDDS)` — which every CMakeLists.txt I gave you uses — just needs `CMAKE_PREFIX_PATH` pointed at wherever you installed it. No code changes at all. I confirmed this by rebuilding `dds_plugin` against this from-source install instead of the apt one:

```bash
cmake .. -DCMAKE_PREFIX_PATH=/opt/cyclonedds \
         -DCMAKE_INSTALL_RPATH=/opt/cyclonedds/lib
```

and `ldd` on the resulting `dds_plugin.so` confirmed it correctly picked up the from-source `libddsc.so`, not the system one:

```
libddsc.so.0 => /opt/cyclonedds/lib/libddsc.so.0
```

## What I'd actually do for a shippable product

Build from source once (pinned to a specific tag/commit, not "whatever apt has today" — this is exactly the version-skew risk `DdsTypePluginAbi.h`'s doc comment warns about between core and customer `.so`s), then lay it out as:

```
product/
├── bin/host_app
└── lib/
    ├── libddsc.so.0.10.5, libddsc.so.0, libddsc.so   ← from your source build
    ├── dds_plugin.so
    ├── dds_typed_plugin.so
    └── libcustomer1_types.so
```

with `RPATH $ORIGIN` on your plugins (rather than `LD_LIBRARY_PATH` at launch, or a system install) so they find `libddsc.so` next to themselves regardless of where the product is deployed — the same `CMAKE_INSTALL_RPATH` mechanism I used above to prove the swap works, just pointed at `$ORIGIN` instead of an absolute dev path for a real install.

**Building `idlc` is part of the same step** — it comes out of that same source build (`BUILD_IDLC=ON` by default), so `find_package(CycloneDDS)` + `idlc_generate()` in your customer type-plugin CMakeLists work identically whether Cyclone came from apt or from this source build — nothing in the plugin/driver code needs to know or care which.