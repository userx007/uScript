# Using `gen_typed_adapter.py` with `dds_typed_plugin` — a worked tutorial

This walks through the whole pipeline for one topic family — from a set of
`.idl` files to a loaded, PUBLISH/SUBSCRIBE-able customer `.so` inside
`dds_typed_plugin` — using the real `Alarms` family throughout. Every
command below was actually run (Cyclone DDS `idlc` 0.10.4, gcc, and the
plugin's own `DDS_TYPED.CMD` shape) to produce the output shown, so you can
follow along verbatim and substitute your own family name at the end.

## 1. Where this fits

`dds_typed_plugin` never knows about any customer's actual struct layout —
it only calls through the fixed `DdsTypePluginAbi.h` C ABI (`DdsTypeEntry`
/ `DdsTypePlugin`, see that header's doc comment) into whichever
customer `.so` you `LOAD`. **`gen_typed_adapter.py` writes the one file a
customer needs to hand-write to satisfy that ABI** — the
`<family>_adapter.c` with each topic's `decode()`/`encode()` pair, plus
the `alloc`/`free`/`descriptor` wiring — so you never write that file by
hand (see `examples/customer1/src/customer1_adapter.c` in the plugin repo
for what it looks like written by hand, for comparison).

```
 .idl files  →  idlc            →  <Module>.h / <Module>.c   (structs + dds_topic_descriptor_t)
             →  gen_typed_adapter.py →  <family>_adapter.c    (decode/encode + DdsTypeEntry table)
                                                │
                                    gcc -shared → lib<family>_adapter.so
                                                │
                              DDS_TYPED.CMD > LOAD lib<family>_adapter.so
```

## 2. Prerequisites

```bash
sudo apt-get install cyclonedds-dev cyclonedds-tools   # idlc + libddsc
```

You'll also need, in one directory:

- `gen_typed_adapter.py` (the generator)
- `idl_kv.h` / `idl_kv.c` — the shared KV text-grammar library every
  generated adapter's `decode()`/`encode()` targets (copy these from
  `dds_typed_plugin/examples/customer2/{inc,src}/idl_kv.{h,c}`)
- `DdsTypePluginAbi.h` — the plugin's stable C ABI header (from
  `dds_typed_plugin/include/driver/inc/`, wherever your checkout of the
  plugin repo puts it — see that repo's CMakeLists.txt for the exact path
  it's included from)
- Your topic family's IDL files, named by convention:
  `<Family>_PSM.idl`, `<Family>_topicNames.idl`,
  `<Family>_topicTypeNames.idl`, and the shared `LDM_Common.idl`

## 3. Generate the adapter

```bash
python3 gen_typed_adapter.py --family Alarms
```

```
wrote Alarms_adapter.c: module=Alarms_PSM, 35 struct(s), 16 topic type(s), 1 TODO marker(s) to review
```

`--idl-dir` defaults to `.`, and the output filename defaults to
`<family>_adapter.c` — pass `-o` / `--idl-dir` explicitly if your files
live elsewhere. This step reads `Alarms_topicNames.idl` to fill in each
topic's real wire name (e.g. `Alarms__Crew_Role_In_Mission_State`) and
cross-checks `Alarms_topicTypeNames.idl`, so the only `TODO` left in a
clean run is the instructional line in the file's own header comment —
run `grep -c 'TODO: placeholder' Alarms_adapter.c` and expect `0`.

## 4. Generate the real Cyclone types

```bash
idlc -l c LDM_Common.idl
idlc -l c -I . Alarms_PSM.idl
```

This produces `LDM_Common.h/.c` and `Alarms_PSM.h/.c` — the actual
`P_Alarms_PSM_C_*` structs and their `dds_topic_descriptor_t`s that
`Alarms_adapter.c` includes and calls into. `LDM_Common.idl` has to be
compiled first since `Alarms_PSM.idl` references its types.

## 5. Build the customer `.so`

A minimal, hand-written `CMakeLists.txt` (mirroring
`examples/customer1/CMakeLists.txt` in the plugin repo, just with two
`idlc_generate()` inputs instead of one):

```cmake
cmake_minimum_required(VERSION 3.25)
project(alarms_types C)

find_package(CycloneDDS REQUIRED)

idlc_generate(TARGET alarms_types_idl FILES LDM_Common.idl Alarms_PSM.idl)

add_library(alarms_adapter SHARED
    Alarms_adapter.c
)

target_include_directories(alarms_adapter PRIVATE
    ${CMAKE_SOURCE_DIR}                 # idl_kv.h
    /path/to/dds_typed_plugin/include/driver/inc   # DdsTypePluginAbi.h
)

target_sources(alarms_adapter PRIVATE idl_kv.c)

target_link_libraries(alarms_adapter PRIVATE
    alarms_types_idl
    CycloneDDS::ddsc
)
```

```bash
mkdir build && cd build
cmake .. && make
```

That produces `libalarms_adapter.so`. (Doing it by hand with `gcc`
instead, exactly as this tutorial's author verified end to end:

```bash
gcc -c -fPIC -I. -std=c11 LDM_Common.c Alarms_PSM.c idl_kv.c Alarms_adapter.c
gcc -shared -o libalarms_adapter.so \
    LDM_Common.o Alarms_PSM.o idl_kv.o Alarms_adapter.o -lddsc
```
)

## 6. Load it into `dds_typed_plugin` and use it

Same `DDS_TYPED.CMD` surface as any other customer `.so` — see the
plugin's own tutorial (`docs/dds_typed_plugin_tutorial.md`) for the full
command reference; here's the `Alarms` family walked through it:

```
LOAD_PLUGIN DDS_TYPED

DDS_TYPED.CONFIG d=90 pp=./libalarms_adapter.so
DDS_TYPED.CMD > LIST
summary ?= DDS_TYPED.CMD <
LOG.PRINT $summary
# loaded_types=16  Alarms__Crew_Role_In_Mission_State[...] Alarms__Alarm_Category[...] ...
```

The payload/result text is the shared `idl_kv` grammar
(`key=value;key=value`, `{...}` for nested structs, `[...]` for
sequences/arrays — see `idl_kv.h`'s doc comment). Publishing the
simplest `Alarms` topic, `C_Alarm_Category`:

```
DDS_TYPED.CMD > PUBLISH Alarms__Alarm_Category \
    A_sourceID={A_resourceId=1;A_instanceId=1};\
    A_timeOfDataGeneration={A_second=1700000000;A_nanoseconds=0};\
    A_activeAlarmCount=3;\
    A_unacknowledgedAlarmCount=1;\
    A_categorisedActualAlarm_sourceID=[{A_resourceId=10;A_instanceId=1},{A_resourceId=11;A_instanceId=1}];\
    A_alarmCategorySpecification_sourceID={A_resourceId=5;A_instanceId=1}
```

Subscribe and read it back the same way as any other topic:

```
DDS_TYPED.CMD > SUBSCRIBE Alarms__Alarm_Category
state ?= DDS_TYPED.CMD <
LOG.PRINT $state
```

## 7. Round-trip sanity check without any DDS traffic

Before wiring this into a script, it's worth confirming `decode()`/
`encode()` actually round-trip correctly by calling the `.so` directly —
no domain, no participant:

```c
#include "DdsTypePluginAbi.h"
#include <dlfcn.h>
#include <stdio.h>

int main(void) {
    void* h = dlopen("./libalarms_adapter.so", RTLD_NOW);
    typedef const DdsTypePlugin* (*get_fn)(void);
    const DdsTypePlugin* p = ((get_fn)dlsym(h, "dds_type_plugin_get"))();

    const DdsTypeEntry* e = &p->get_type_count() ? p->get_type(0) : NULL; /* or search by topic_name */
    void* sample = e->alloc_sample();
    e->decode("A_sourceID={A_resourceId=1;A_instanceId=1};...", sample);

    char out[1024];
    e->encode(sample, out, sizeof(out));
    printf("%s\n", out);

    e->free_sample(sample, DDS_FREE_ALL);
    dlclose(h);
}
```

```bash
gcc -std=c11 -I. smoke_test.c -o smoke_test -ldl
LD_LIBRARY_PATH=. ./smoke_test
```

## 8. Regenerating after an IDL change

Re-run steps 3–5 whenever `<Family>_PSM.idl`, `<Family>_topicNames.idl`,
or `LDM_Common.idl` change — the generator and `idlc` are both
deterministic, so a clean regenerate-and-rebuild is always safe; there's
no generated state to hand-merge. `<family>_adapter.c` isn't meant to be
hand-edited except deliberately (e.g. swapping the KV grammar for JSON —
see `idl_kv.h`'s doc comment) — re-running the generator overwrites it.

## 9. Troubleshooting

- **`LOAD` fails with an ABI-version mismatch.** The customer `.so` was
  built against a different `DdsTypePluginAbi.h` than the running
  `dds_typed_plugin` — rebuild against the current header (see that
  header's `DDS_TYPE_PLUGIN_ABI_VERSION`).
- **`PUBLISH`/`SUBSCRIBE` fails immediately with "no loaded type
  publishes topic ..."** — the topic name has to match exactly what's in
  `<Family>_topicNames.idl` (e.g. `Alarms__Alarm_Category`, double
  underscore where the IDL module and struct name join); `DDS_TYPED.CMD >
  LIST` shows exactly what got registered.
- **A field silently doesn't show up after `decode()`.** Check the key
  name matches the IDL field name exactly (case-sensitive) — unknown
  keys are silently ignored by design (see `idl_kv.h`), which is
  convenient for partial payloads but easy to typo against.
- **Use a script generated from a recent `gen_typed_adapter.py`.** Earlier
  revisions had bugs specific to how Cyclone's `idlc` represents
  `sequence<char, N>` string-typedef fields (e.g. `T_ShortString`) and
  unbounded `sequence<T>` fields — both are common in PSM-style IDL like
  `Alarms_PSM.idl`/`LDM_Common.idl`. If `decode()` never seems to
  populate a sequence field, or a string field doesn't compile/crashes,
  regenerate with the current script.
