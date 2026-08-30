#ifndef DDS_TYPE_PLUGIN_ABI_H
#define DDS_TYPE_PLUGIN_ABI_H
/**
 * @file DdsTypePluginAbi.h
 * @brief Stable, plain-C ABI between `DdsTypedDriver` (src/lib/drivers/dds_typed —
 * a generic, no-IDL-knowledge Cyclone DDS core) and a customer-specific,
 * IDL-generated "type plugin" `.so`, loaded at runtime via `dlopen()` —
 * see `dds_typed_driver.hpp`'s class doc comment for the full picture
 * and `src/plugin/dds_typed_plugin/examples/customer1` for a complete,
 * working example built from an `.idl` file using this header.
 *
 * This is deliberately a second, separate mechanism from `DdsDriver`/
 * `dds_plugin` (which stays exactly as it was: one generic
 * `{ string payload; }` IDL type for every DDS.CMD topic, no per-customer
 * builds). Reach for `DdsTypedDriver`/`dds_typed_plugin` only when you
 * need this process to exchange *real*, specific IDL structs with an
 * external DDS participant that expects them on the wire — e.g. an NGVA
 * subsystem publishing `VehicleState` — and want to swap which
 * customer's data model is loaded (or support several at once) without
 * rebuilding the core.
 *
 * # Design rationale
 *
 * Reuses Cyclone's own `dds_topic_descriptor_t` (`<dds/ddsc/dds_public_impl.h>`)
 * as-is for the type descriptor, rather than inventing a parallel
 * wrapper struct: it is already a plain-C, purpose-built boundary type —
 * exactly what `idlc` generates and exactly what `dds_create_topic()`
 * consumes — so wrapping it would add a translation step with no extra
 * safety. The actual cross-`.so` risk is Cyclone *version* skew between
 * `dds_typed_driver` and a given `customer_*.so` (`dds_topic_descriptor_t::m_ops`,
 * the marshalling metadata `idlc` bakes in, is interpreted by whatever
 * `libddsc.so` is loaded at runtime) — no ABI wrapper changes that; it
 * has to be a packaging discipline instead: one `libddsc.so` per
 * deployment, every `customer_*.so` rebuilt against it whenever Cyclone
 * is upgraded, never mixed-and-matched.
 *
 * `alloc_sample`/`free_sample` are exactly what `idlc` already generates
 * per type (`<Type>__alloc`/`<Type>_free`, normally macros — a customer
 * adapter just needs one-line wrapper functions to make them addressable
 * — see the example). This is what lets `DdsTypedDriver` create/destroy a
 * correctly-sized, correctly-initialized sample for types with `string`/
 * `sequence` fields without ever seeing the struct definition: a raw
 * `malloc(descriptor->m_size)` + `memcpy` is *not* safe for such a type —
 * `m_size` is only the size of the top-level struct, not any
 * heap-allocated field content, so a byte-for-byte copy would produce a
 * dangling/garbage pointer. `DdsTypedDriver` never does this; it always
 * goes through these two function pointers.
 *
 * `decode`/`encode` are the one place a customer's real field layout is
 * used at all, and they exist entirely inside the customer `.so` — this
 * is what makes DDS_TYPED.CMD's plain-text PUBLISH/receive interface
 * (see dds_typed_plugin.hpp's class doc comment) possible without
 * `DdsTypedDriver` itself understanding any customer struct: PUBLISH's
 * text argument goes straight into `decode()`, and whatever comes back
 * from `dds_take()` goes straight into `encode()` before `DdsTypedDriver`
 * ever touches it. A customer author is free to make this JSON, a
 * simple "field=value,field=value" grammar, or anything else — it is
 * entirely private to that one customer `.so`, `DdsTypedDriver` only
 * ever forwards the text through unmodified.
 */
#include <dds/dds.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DDS_TYPE_PLUGIN_ABI_VERSION 1u

/** One IDL type, addressed by the one DDS topic name it's published/subscribed on. */
typedef struct DdsTypeEntry {
    const char* topic_name;
    const dds_topic_descriptor_t* descriptor; /* from idlc — passed straight to dds_create_topic() */

    void* (*alloc_sample)(void);
    void  (*free_sample)(void* sample, dds_free_op_t op);

    /**
     * DDS_TYPED.CMD > PUBLISH <topic> <text...>'s <text...> (everything
     * after <topic>, space-joined — same convention as DdsDriver's
     * PUBLISH) goes here. Fill *out_sample (already allocated via
     * alloc_sample() by the caller) from it. Return false to fail the
     * PUBLISH (e.g. malformed text) — DdsTypedDriver logs it and does
     * not call dds_write().
     */
    bool (*decode)(const char* text, void* out_sample);

    /**
     * The inverse, used for DDS_TYPED.CMD < (receive) and DDS_TYPED.CMD
     * > LIST's discovered-sample dump: render *sample as text into
     * out_buf (capacity out_cap, NUL-terminate). Return false if it
     * doesn't fit or otherwise can't be rendered.
     */
    bool (*encode)(const void* sample, char* out_buf, size_t out_cap);
} DdsTypeEntry;

/**
 * The plugin itself — one per customer `.so`, one exported instance
 * reachable via dds_type_plugin_get() (the one symbol DdsTypedDriver
 * dlsym()s for). abi_version MUST be checked by the core (against
 * DDS_TYPE_PLUGIN_ABI_VERSION) before anything else in this struct is
 * touched — see dds_typed_driver.cpp's m_LoadPlugin().
 */
typedef struct DdsTypePlugin {
    uint32_t abi_version;
    const char* customer_name;
    size_t (*get_type_count)(void);
    const DdsTypeEntry* (*get_type)(size_t index);
} DdsTypePlugin;

/** The one exported symbol every customer type plugin `.so` must provide. */
const DdsTypePlugin* dds_type_plugin_get(void);

#ifdef __cplusplus
}
#endif
#endif /* DDS_TYPE_PLUGIN_ABI_H */
