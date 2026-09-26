#include "kvparser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

typedef struct {
    const char* p;
} Cursor;

static void skip_ws(Cursor* pC) { while (*pC->p == ' ' || *pC->p == '\t') pC->p++; }

static char* dup_trimmed(const char* pstrStart, const char* pstrEnd) {
    while (pstrStart < pstrEnd && (*pstrStart == ' ' || *pstrStart == '\t')) pstrStart++;
    while (pstrEnd > pstrStart && (pstrEnd[-1] == ' ' || pstrEnd[-1] == '\t')) pstrEnd--;
    size_t n = (size_t)(pstrEnd - pstrStart);
    char* s = (char*)malloc(n + 1);
    if (!s) return NULL;
    memcpy(s, pstrStart, n);
    s[n] = '\0';
    return s;
}

static KvNode* new_node(KvKind kind) {
    KvNode* n = (KvNode*)calloc(1, sizeof(KvNode));
    if (n) n->kind = kind;
    return n;
}

static bool push_child(KvNode* psParent, KvNode* psChild) {
    KvNode** grown = (KvNode**)realloc(psParent->children, (psParent->n_children + 1) * sizeof(KvNode*));
    if (!grown) return false;
    psParent->children = grown;
    psParent->children[psParent->n_children++] = psChild;
    return true;
}

static KvNode* parse_value(Cursor* pC);

static KvNode* parse_pairs(Cursor* pC, char terminator) {
    KvNode* obj = new_node(KV_OBJECT);
    if (!obj) return NULL;
    skip_ws(pC);
    if (*pC->p == terminator || *pC->p == '\0') return obj; /* empty object */

    for (;;) {
        skip_ws(pC);
        const char* key_start = pC->p;
        while (*pC->p && *pC->p != '=' && *pC->p != ';' && *pC->p != terminator) pC->p++;
        if (*pC->p != '=') { kv_free(obj); return NULL; } /* malformed: no '=' */
        char* key = dup_trimmed(key_start, pC->p);
        pC->p++; /* consume '=' */

        KvNode* val = parse_value(pC);
        if (!key || !val) { free(key); kv_free(val); kv_free(obj); return NULL; }
        val->key = key;
        if (!push_child(obj, val)) { kv_free(val); kv_free(obj); return NULL; }

        skip_ws(pC);
        if (*pC->p == ';') { pC->p++; continue; }
        break;
    }
    return obj;
}

static KvNode* parse_array(Cursor* pC) {
    KvNode* arr = new_node(KV_ARRAY);
    if (!arr) return NULL;
    skip_ws(pC);
    if (*pC->p == ']') { pC->p++; return arr; }
    for (;;) {
        KvNode* val = parse_value(pC);
        if (!val) { kv_free(arr); return NULL; }
        if (!push_child(arr, val)) { kv_free(val); kv_free(arr); return NULL; }
        skip_ws(pC);
        if (*pC->p == ',') { pC->p++; continue; }
        if (*pC->p == ']') { pC->p++; break; }
        kv_free(arr);
        return NULL; /* malformed */
    }
    return arr;
}

static KvNode* parse_value(Cursor* pC) {
    skip_ws(pC);
    if (*pC->p == '{') {
        pC->p++;
        KvNode* obj = parse_pairs(pC, '}');
        if (!obj) return NULL;
        skip_ws(pC);
        if (*pC->p != '}') { kv_free(obj); return NULL; }
        pC->p++;
        return obj;
    }
    if (*pC->p == '[') {
        pC->p++;
        return parse_array(pC);
    }
    const char* start = pC->p;
    while (*pC->p && *pC->p != ';' && *pC->p != ',' && *pC->p != '}' && *pC->p != ']') pC->p++;
    char* s = dup_trimmed(start, pC->p);
    if (!s) return NULL;
    KvNode* n = new_node(KV_SCALAR);
    if (!n) { free(s); return NULL; }
    n->scalar = s;
    return n;
}

KvNode* kv_parse(const char* pstrText) {
    if (!pstrText) return NULL;
    Cursor c = { .p = pstrText };
    KvNode* obj = parse_pairs(&c, '\0');
    if (!obj) return NULL;
    skip_ws(&c);
    if (*c.p != '\0') { kv_free(obj); return NULL; } /* trailing garbage */
    return obj;
}

const KvNode* kv_get(const KvNode* psNode, const char* pstrKey) {
    if (!psNode || psNode->kind != KV_OBJECT) return NULL;
    for (size_t i = 0; i < psNode->n_children; i++) {
        if (psNode->children[i]->pstrKey && strcmp(psNode->children[i]->pstrKey, pstrKey) == 0) {
            return psNode->children[i];
        }
    }
    return NULL;
}

const char* kv_as_str(const KvNode* psNode) {
    return (psNode && psNode->kind == KV_SCALAR) ? psNode->scalar : NULL;
}

bool kv_as_i64(const KvNode* psNode, long long* out) {
    const char* s = kv_as_str(psNode);
    if (!s || *s == '\0') return false;
    char* end = NULL;
    long long v = strtoll(s, &end, 10);
    if (end == s || *end != '\0') return false;
    *out = v;
    return true;
}

bool kv_as_double(const KvNode* psNode, double* pOut) {
    const char* s = kv_as_str(psNode);
    if (!s || *s == '\0') return false;
    char* end = NULL;
    double v = strtod(s, &end);
    if (end == s || *end != '\0') return false;
    *pOut = v;
    return true;
}

bool kv_as_bool(const KvNode* psNode, bool* pbOut) {
    const char* s = kv_as_str(psNode);
    if (!s) return false;
    if (strcmp(s, "true") == 0 || strcmp(s, "1") == 0) { *pbOut = true; return true; }
    if (strcmp(s, "false") == 0 || strcmp(s, "0") == 0) { *pbOut = false; return true; }
    return false;
}

void kv_free(KvNode* psNode) {
    if (!psNode) return;
    for (size_t i = 0; i < psNode->n_children; i++) kv_free(psNode->children[i]);
    free(psNode->children);
    free(psNode->key);
    free(psNode->scalar);
    free(psNode);
}

/* --- Writer ------------------------------------------------------------ */

void kv_writer_init(KvWriter* pW, char* pstrBuf, size_t cap) {
    pW->pstrBuf = pstrBuf;
    pW->cap = cap;
    pW->len = 0;
    pW->overflow = false;
    if (cap > 0) pstrBuf[0] = '\0';
}

void kv_write(KvWriter* pW, const char* pstrFmt, ...) {
    if (pW->overflow) return;
    va_list ap;
    va_start(ap, pstrFmt);
    int n = vsnprintf(pW->buf + pW->len, pW->cap - pW->len, pstrFmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= pW->cap - pW->len) {
        pW->overflow = true;
        return;
    }
    pW->len += (size_t)n;
}
