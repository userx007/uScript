#ifndef IDL_KV_H
#define IDL_KV_H
/**
 * @file idl_kv.h
 * @brief Generic, dependency-free "key=value" text-tree parser and
 * printer, shared by every generator-produced `<customer>_adapter.c`.
 *
 * This is the plain-text grammar the generator's decode()/encode() code
 * targets — you can freely hand-edit generated adapters to use a
 * different grammar (JSON, CDR-hex, ...) per DdsTypePluginAbi.h's own
 * note that decode/encode text format is entirely private to each
 * customer .so. This one was chosen because it is trivial to type by
 * hand from a script and supports nesting/sequences without needing a
 * real JSON library.
 *
 * Grammar:
 *   doc     := pairs
 *   pairs   := pair (';' pair)*
 *   pair    := IDENT '=' value
 *   value   := '{' pairs '}'            (nested struct)
 *            | '[' (value (',' value)*)? ']'   (sequence/array)
 *            | scalar                    (anything else, verbatim text)
 *   scalar  := chars up to next unescaped [;,{}\[\]] , trimmed
 *
 * Example: "id=1;label=truck-07;speed=27.5;pos={x=1,y=2};tags=[a,b,c]"
 *
 * No escaping is implemented for literal ';' ',' '{' '}' '[' ']' inside
 * scalars/strings — add it in kv_parse_scalar() below if a real payload
 * needs it (e.g. a label containing a comma).
 */
#include <stddef.h>
#include <stdbool.h>

typedef enum { KV_SCALAR, KV_OBJECT, KV_ARRAY } KvKind;

typedef struct KvNode {
    KvKind kind;
    char* key;              /* NULL for array elements */
    char* scalar;           /* KV_SCALAR only, NUL-terminated */
    struct KvNode** children; /* KV_OBJECT (keyed) / KV_ARRAY (unkeyed) */
    size_t n_children;
} KvNode;

/* Parse `text` into a tree of implicit top-level KV_OBJECT. Returns NULL
 * on malformed input. Caller must kv_free() the result. */
KvNode* kv_parse(const char* text);

/* Look up a direct child of an object node by key. NULL if absent or
 * `node` is not a KV_OBJECT. */
const KvNode* kv_get(const KvNode* node, const char* key);

/* Convenience scalar readers. Return false (leaving *out untouched) if
 * `node` is NULL or not a KV_SCALAR, or the text doesn't parse as that
 * type. */
bool kv_as_i64(const KvNode* node, long long* out);
bool kv_as_double(const KvNode* node, double* out);
bool kv_as_bool(const KvNode* node, bool* out);
const char* kv_as_str(const KvNode* node); /* NULL if not KV_SCALAR */

void kv_free(KvNode* node);

/* --- Printing (encode-side) ------------------------------------------- */

/* Minimal growable string buffer used by generated encode() functions so
 * they don't have to hand-roll snprintf offset bookkeeping. */
typedef struct {
    char* buf;
    size_t cap;
    size_t len; /* excludes NUL */
    bool overflow;
} KvWriter;

void kv_writer_init(KvWriter* w, char* buf, size_t cap);
/* Appends raw text (no escaping). Sets w->overflow on truncation. */
void kv_write(KvWriter* w, const char* fmt, ...);

#endif /* IDL_KV_H */
