#include "idl_kv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

typedef struct {
    const char* p;
} Cursor;

static void skip_ws(Cursor* c) { while (*c->p == ' ' || *c->p == '\t') c->p++; }

static char* dup_trimmed(const char* start, const char* end) {
    while (start < end && (*start == ' ' || *start == '\t')) start++;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
    size_t n = (size_t)(end - start);
    char* s = (char*)malloc(n + 1);
    if (!s) return NULL;
    memcpy(s, start, n);
    s[n] = '\0';
    return s;
}

static KvNode* new_node(KvKind kind) {
    KvNode* n = (KvNode*)calloc(1, sizeof(KvNode));
    if (n) n->kind = kind;
    return n;
}

static bool push_child(KvNode* parent, KvNode* child) {
    KvNode** grown = (KvNode**)realloc(parent->children, (parent->n_children + 1) * sizeof(KvNode*));
    if (!grown) return false;
    parent->children = grown;
    parent->children[parent->n_children++] = child;
    return true;
}

static KvNode* parse_value(Cursor* c);

static KvNode* parse_pairs(Cursor* c, char terminator) {
    KvNode* obj = new_node(KV_OBJECT);
    if (!obj) return NULL;
    skip_ws(c);
    if (*c->p == terminator || *c->p == '\0') return obj; /* empty object */

    for (;;) {
        skip_ws(c);
        const char* key_start = c->p;
        while (*c->p && *c->p != '=' && *c->p != ';' && *c->p != terminator) c->p++;
        if (*c->p != '=') { kv_free(obj); return NULL; } /* malformed: no '=' */
        char* key = dup_trimmed(key_start, c->p);
        c->p++; /* consume '=' */

        KvNode* val = parse_value(c);
        if (!key || !val) { free(key); kv_free(val); kv_free(obj); return NULL; }
        val->key = key;
        if (!push_child(obj, val)) { kv_free(val); kv_free(obj); return NULL; }

        skip_ws(c);
        if (*c->p == ';') { c->p++; continue; }
        break;
    }
    return obj;
}

static KvNode* parse_array(Cursor* c) {
    KvNode* arr = new_node(KV_ARRAY);
    if (!arr) return NULL;
    skip_ws(c);
    if (*c->p == ']') { c->p++; return arr; }
    for (;;) {
        KvNode* val = parse_value(c);
        if (!val) { kv_free(arr); return NULL; }
        if (!push_child(arr, val)) { kv_free(val); kv_free(arr); return NULL; }
        skip_ws(c);
        if (*c->p == ',') { c->p++; continue; }
        if (*c->p == ']') { c->p++; break; }
        kv_free(arr);
        return NULL; /* malformed */
    }
    return arr;
}

static KvNode* parse_value(Cursor* c) {
    skip_ws(c);
    if (*c->p == '{') {
        c->p++;
        KvNode* obj = parse_pairs(c, '}');
        if (!obj) return NULL;
        skip_ws(c);
        if (*c->p != '}') { kv_free(obj); return NULL; }
        c->p++;
        return obj;
    }
    if (*c->p == '[') {
        c->p++;
        return parse_array(c);
    }
    const char* start = c->p;
    while (*c->p && *c->p != ';' && *c->p != ',' && *c->p != '}' && *c->p != ']') c->p++;
    char* s = dup_trimmed(start, c->p);
    if (!s) return NULL;
    KvNode* n = new_node(KV_SCALAR);
    if (!n) { free(s); return NULL; }
    n->scalar = s;
    return n;
}

KvNode* kv_parse(const char* text) {
    if (!text) return NULL;
    Cursor c = { .p = text };
    KvNode* obj = parse_pairs(&c, '\0');
    if (!obj) return NULL;
    skip_ws(&c);
    if (*c.p != '\0') { kv_free(obj); return NULL; } /* trailing garbage */
    return obj;
}

const KvNode* kv_get(const KvNode* node, const char* key) {
    if (!node || node->kind != KV_OBJECT) return NULL;
    for (size_t i = 0; i < node->n_children; i++) {
        if (node->children[i]->key && strcmp(node->children[i]->key, key) == 0) {
            return node->children[i];
        }
    }
    return NULL;
}

const char* kv_as_str(const KvNode* node) {
    return (node && node->kind == KV_SCALAR) ? node->scalar : NULL;
}

bool kv_as_i64(const KvNode* node, long long* out) {
    const char* s = kv_as_str(node);
    if (!s || *s == '\0') return false;
    char* end = NULL;
    long long v = strtoll(s, &end, 10);
    if (end == s || *end != '\0') return false;
    *out = v;
    return true;
}

bool kv_as_double(const KvNode* node, double* out) {
    const char* s = kv_as_str(node);
    if (!s || *s == '\0') return false;
    char* end = NULL;
    double v = strtod(s, &end);
    if (end == s || *end != '\0') return false;
    *out = v;
    return true;
}

bool kv_as_bool(const KvNode* node, bool* out) {
    const char* s = kv_as_str(node);
    if (!s) return false;
    if (strcmp(s, "true") == 0 || strcmp(s, "1") == 0) { *out = true; return true; }
    if (strcmp(s, "false") == 0 || strcmp(s, "0") == 0) { *out = false; return true; }
    return false;
}

void kv_free(KvNode* node) {
    if (!node) return;
    for (size_t i = 0; i < node->n_children; i++) kv_free(node->children[i]);
    free(node->children);
    free(node->key);
    free(node->scalar);
    free(node);
}

/* --- Writer ------------------------------------------------------------ */

void kv_writer_init(KvWriter* w, char* buf, size_t cap) {
    w->buf = buf;
    w->cap = cap;
    w->len = 0;
    w->overflow = false;
    if (cap > 0) buf[0] = '\0';
}

void kv_write(KvWriter* w, const char* fmt, ...) {
    if (w->overflow) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(w->buf + w->len, w->cap - w->len, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= w->cap - w->len) {
        w->overflow = true;
        return;
    }
    w->len += (size_t)n;
}
