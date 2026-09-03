#!/usr/bin/env python3
"""
gen_typed_adapter.py — generate a dds_typed_plugin <customer>_adapter.c
from a Cyclone DDS IDL file.

Usage:
    python3 gen_typed_adapter.py customer1.idl -o customer1_adapter.c

What it parses (a pragmatic, regex-based subset of OMG IDL — not a full
IDL grammar; good enough for the flat, idlc-friendly IDL this project's
customer types use):
  - a single top-level `module`
  - `enum Name { A, B, C };`
  - `struct Name { ... };` with optional @final/@appendable/@mutable
  - fields: primitive types, `string`/`string<N>`, previously-declared
    enum/struct type names (nested structs), `sequence<T>` /
    `sequence<T, N>`, and fixed-size arrays `T name[N];`
  - an optional `// @topic <topic/name>` comment line immediately above
    a struct marks it as a DDS topic type with that name. If no struct
    in the file has this annotation, every top-level struct (one not
    used as a field's type inside another struct) is treated as a
    topic, with a placeholder topic name you MUST edit.

What it generates, per topic struct:
  - alloc_sample/free_sample wrappers around idlc's
    `<mod>_<Struct>__alloc` / `<mod>_<Struct>_free`
  - decode()/encode() built on the shared idl_kv.h "key=value" tree
    parser (see runtime/idl_kv.h) — full support for nested structs,
    sequences, and arrays of the types below
  - a DdsTypeEntry table + DdsTypePlugin + dds_type_plugin_get(), same
    shape as examples/customer1/src/customer1_adapter.c

What it CANNOT auto-generate (emits a clearly marked TODO stub instead,
and the file will not compile until you fill it in):
  - unions
  - sequences/arrays of struct-typed elements (only sequences/arrays of
    primitives, strings, and enums are auto-generated)
  - multi-dimensional arrays (`T name[N][M];`)
  - typedefs, `#include`d IDL, bitmasks, or forward-declared/recursive
    struct types
  - any field whose type isn't a primitive, string, or a struct/enum
    declared earlier in the same file

This is meant to remove the repetitive 80% of writing a new adapter
(the alloc/free wrappers, the KV plumbing, the DdsTypeEntry/DdsTypePlugin
boilerplate) — always read the generated file, fill in the TODOs, and
skim the diff against examples/customer1 before trusting it.
"""
import argparse
import re
import sys
from dataclasses import dataclass, field
from typing import Optional

PRIMITIVES = {
    # idl keyword -> (c type, kv accessor, printf fmt)
    "boolean": ("bool", "bool", "%d"),
    "octet": ("uint8_t", "i64", "%u"),
    "char": ("char", "i64", "%c"),
    "short": ("int16_t", "i64", "%d"),
    "unsigned short": ("uint16_t", "i64", "%u"),
    "long": ("int32_t", "i64", "%d"),
    "unsigned long": ("uint32_t", "i64", "%u"),
    "long long": ("int64_t", "i64", "%lld"),
    "unsigned long long": ("uint64_t", "i64", "%llu"),
    "float": ("float", "double", "%f"),
    "double": ("double", "double", "%f"),
}
# Longest-keyword-first so "unsigned long long" matches before "unsigned long"/"long"
PRIMITIVE_KEYWORDS = sorted(PRIMITIVES.keys(), key=len, reverse=True)


@dataclass
class EnumDecl:
    name: str
    values: list


@dataclass
class Field:
    name: str
    raw_type: str
    is_sequence: bool = False
    is_array: bool = False
    array_len: Optional[str] = None
    element_type: Optional[str] = None  # element type for sequence/array


@dataclass
class StructDecl:
    name: str
    fields: list = field(default_factory=list)
    is_topic: bool = False
    topic_name: Optional[str] = None


def strip_comments(text: str):
    """Remove /*...*/ and // comments, but keep `// @topic <name>` lines
    (as a separate list of (line_index_in_stripped_text, topic_name))."""
    topic_annotations = []  # list of (char_offset_in_output, topic_name)
    out = []
    i = 0
    n = len(text)
    while i < n:
        if text[i : i + 2] == "/*":
            end = text.find("*/", i + 2)
            i = n if end == -1 else end + 2
            continue
        if text[i : i + 2] == "//":
            eol = text.find("\n", i)
            line = text[i : eol if eol != -1 else n]
            m = re.match(r"//\s*@topic\s+(\S+)", line)
            if m:
                topic_annotations.append((len(out), m.group(1)))
            i = eol if eol != -1 else n
            continue
        out.append(text[i])
        i += 1
    return "".join(out), topic_annotations


def parse_idl(text: str):
    stripped, topic_annotations = strip_comments(text)

    mod_match = re.search(r"module\s+(\w+)\s*{", stripped)
    if not mod_match:
        raise ValueError("no top-level `module` found")
    module = mod_match.group(1)

    enums = {}
    structs = {}
    struct_order = []

    # Enums
    for m in re.finditer(r"enum\s+(\w+)\s*{([^}]*)}\s*;", stripped):
        name = m.group(1)
        values = [v.strip() for v in m.group(2).split(",") if v.strip()]
        enums[name] = EnumDecl(name, values)

    # Structs (also capture the leading offset for @topic lookup)
    for m in re.finditer(
        r"(?:@\w+\s*)*struct\s+(\w+)\s*{([^}]*)}\s*;", stripped
    ):
        name = m.group(1)
        body = m.group(2)
        struct_offset = m.start()
        sd = StructDecl(name=name)

        for annot_offset, topic_name in topic_annotations:
            # crude proximity heuristic: annotation must appear on the
            # line(s) immediately preceding this struct's `struct` kw
            if 0 <= struct_offset - annot_offset < 200:
                sd.is_topic = True
                sd.topic_name = topic_name

        for stmt in body.split(";"):
            stmt = stmt.strip()
            if not stmt:
                continue
            f = parse_field(stmt)
            if f:
                sd.fields.append(f)

        structs[name] = sd
        struct_order.append(name)

    # Default topic selection: if nothing was @topic-annotated, every
    # struct not referenced as a field's element/raw type inside another
    # struct is a topic candidate.
    if not any(s.is_topic for s in structs.values()):
        referenced = set()
        for s in structs.values():
            for f in s.fields:
                t = f.element_type or f.raw_type
                if t in structs:
                    referenced.add(t)
        for name in struct_order:
            if name not in referenced:
                structs[name].is_topic = True
                structs[name].topic_name = None  # placeholder, filled later

    return module, enums, structs, struct_order


ARRAY_DECL_RE = re.compile(r"^(?P<type>[\w:<>,\s]+?)\s+(?P<name>\w+)\s*\[\s*(?P<len>\w+)\s*\]$")
SEQ_TYPE_RE = re.compile(r"^sequence\s*<\s*(?P<elem>[\w:]+)\s*(?:,\s*(?P<bound>\w+)\s*)?>$")
STRING_TYPE_RE = re.compile(r"^string(?:\s*<\s*\w+\s*>)?$")


def parse_field(stmt: str) -> Optional[Field]:
    stmt = re.sub(r"\s+", " ", stmt.strip())
    if not stmt:
        return None

    m = ARRAY_DECL_RE.match(stmt)
    if m:
        return Field(
            name=m.group("name"),
            raw_type=m.group("type").strip(),
            is_array=True,
            array_len=m.group("len"),
            element_type=m.group("type").strip(),
        )

    parts = stmt.rsplit(" ", 1)
    if len(parts) != 2:
        return None
    type_str, name = parts[0].strip(), parts[1].strip()

    seq_m = SEQ_TYPE_RE.match(type_str)
    if seq_m:
        return Field(
            name=name,
            raw_type=type_str,
            is_sequence=True,
            element_type=seq_m.group("elem"),
        )

    return Field(name=name, raw_type=type_str)


def classify(type_str: str, enums, structs):
    """Return one of 'primitive', 'string', 'enum', 'struct', 'unknown'."""
    if STRING_TYPE_RE.match(type_str):
        return "string"
    if type_str in PRIMITIVES:
        return "primitive"
    if type_str in enums:
        return "enum"
    if type_str in structs:
        return "struct"
    return "unknown"


def c_type_for(type_str: str, enums, structs, module: str) -> str:
    kind = classify(type_str, enums, structs)
    if kind == "primitive":
        return PRIMITIVES[type_str][0]
    if kind == "string":
        return "char*"
    if kind == "enum":
        return f"{module}_{type_str}"
    if kind == "struct":
        return f"{module}_{type_str}"
    return f"/* TODO unknown type */ {type_str}"


# ---------------------------------------------------------------------
# Code generation
# ---------------------------------------------------------------------

def gen_enum_helpers(module: str, e: EnumDecl) -> str:
    from_lines = "\n".join(
        f'    if (strcmp(s, "{v}") == 0) {{ *out = {module}_{v}; return true; }}'
        for v in e.values
    )
    to_lines = "\n".join(
        f'        case {module}_{v}: return "{v}";' for v in e.values
    )
    return f"""
static bool {module}_{e.name}_from_string(const char* s, {module}_{e.name}* out) {{
{from_lines}
    return false;
}}

static const char* {module}_{e.name}_to_string({module}_{e.name} v) {{
    switch (v) {{
{to_lines}
        default: return "?";
    }}
}}
"""


def _field_node_expr(src_node_expr: str, field_name: str) -> str:
    """kv_get() call that fetches `field_name`'s own child node out of the
    container node `src_node_expr` refers to (the document root, or a
    nested struct's node)."""
    container = "root" if src_node_expr == "root" else src_node_expr
    return f'kv_get({container}, "{field_name}")'


def gen_decode_field(module, f: Field, enums, structs, dst_expr: str, src_node_expr: str, indent: str) -> str:
    """Generate C statements that decode one field from the container node
    `src_node_expr` refers to, into `dst_expr` (an lvalue, e.g. 'v->id')."""
    lines = []
    if f.is_sequence or f.is_array:
        elem_kind = classify(f.element_type, enums, structs)
        if elem_kind == "struct":
            lines.append(f"{indent}/* TODO: sequence/array of struct '{f.element_type}' — decode each element "
                          f"by hand (this generator only auto-fills primitive/string/enum elements). */")
            return "\n".join(lines)
        node = f"{f.name}_node"
        lines.append(f"{indent}const KvNode* {node} = {_field_node_expr(src_node_expr, f.name)};")
        lines.append(f"{indent}if ({node} && {node}->kind == KV_ARRAY) {{")
        idx = f"i_{f.name}"
        lines.append(f"{indent}    for (size_t {idx} = 0; {idx} < {node}->n_children"
                      + (f" && {idx} < {f.array_len}" if f.is_array else "")
                      + f"; {idx}++) {{")
        elem_node = f"{node}->children[{idx}]"
        if f.is_array:
            elem_dst = f"{dst_expr}[{idx}]"
        else:
            # sequence: idlc generates a dds_sequence_t-like {n, m, buf, release}
            elem_dst = f"{dst_expr}._buffer[{idx}]"
            lines.append(f"{indent}        if ({idx} >= {dst_expr}._maximum) break; /* TODO: grow sequence buffer if needed */")
        if elem_kind == "primitive":
            _, accessor, _ = PRIMITIVES[f.element_type]
            if accessor == "i64":
                lines.append(f"{indent}        {{ long long tmp; if (kv_as_i64({elem_node}, &tmp)) {elem_dst} = ({c_type_for(f.element_type, enums, structs, module)})tmp; }}")
            elif accessor == "double":
                lines.append(f"{indent}        {{ double tmp; if (kv_as_double({elem_node}, &tmp)) {elem_dst} = ({c_type_for(f.element_type, enums, structs, module)})tmp; }}")
            else:
                lines.append(f"{indent}        {{ bool tmp; if (kv_as_bool({elem_node}, &tmp)) {elem_dst} = tmp; }}")
        elif elem_kind == "string":
            lines.append(f"{indent}        {{ const char* tmp = kv_as_str({elem_node}); if (tmp) {elem_dst} = strdup(tmp); }}")
        elif elem_kind == "enum":
            lines.append(f"{indent}        {{ const char* tmp = kv_as_str({elem_node}); "
                          f"if (tmp) {module}_{f.element_type}_from_string(tmp, &{elem_dst}); }}")
        if not f.is_array:
            lines.append(f"{indent}        {dst_expr}._length = {idx} + 1;")
        lines.append(f"{indent}    }}")
        lines.append(f"{indent}}}")
        return "\n".join(lines)

    kind = classify(f.raw_type, enums, structs)
    node = _field_node_expr(src_node_expr, f.name)

    if kind == "primitive":
        _, accessor, _ = PRIMITIVES[f.raw_type]
        if accessor == "i64":
            lines.append(f"{indent}{{ long long tmp; if (kv_as_i64({node}, &tmp)) {dst_expr} = ({c_type_for(f.raw_type, enums, structs, module)})tmp; }}")
        elif accessor == "double":
            lines.append(f"{indent}{{ double tmp; if (kv_as_double({node}, &tmp)) {dst_expr} = ({c_type_for(f.raw_type, enums, structs, module)})tmp; }}")
        else:
            lines.append(f"{indent}{{ bool tmp; if (kv_as_bool({node}, &tmp)) {dst_expr} = tmp; }}")
    elif kind == "string":
        lines.append(f"{indent}{{ const char* tmp = kv_as_str({node}); if (tmp) {{ free({dst_expr}); {dst_expr} = strdup(tmp); }} }}")
    elif kind == "enum":
        lines.append(f"{indent}{{ const char* tmp = kv_as_str({node}); if (tmp) {module}_{f.raw_type}_from_string(tmp, &{dst_expr}); }}")
    elif kind == "struct":
        lines.append(f"{indent}{module}_{f.raw_type}_decode_fields({node}, &{dst_expr});")
    else:
        lines.append(f"{indent}/* TODO: unknown/unsupported field type '{f.raw_type}' for '{f.name}' */")
    return "\n".join(lines)


def gen_encode_field(module, f: Field, enums, structs, src_expr: str, indent: str, is_last: bool) -> str:
    sep = "" if is_last else ";"
    lines = []
    if f.is_sequence or f.is_array:
        elem_kind = classify(f.element_type, enums, structs)
        if elem_kind == "struct":
            lines.append(f'{indent}/* TODO: sequence/array of struct \'{f.element_type}\' — encode each element by hand */')
            return "\n".join(lines)
        count_expr = f.array_len if f.is_array else f"{src_expr}._length"
        elem_get = (lambda i: f"{src_expr}[{i}]") if f.is_array else (lambda i: f"{src_expr}._buffer[{i}]")
        lines.append(f"{indent}kv_write(w, \"{f.name}=[\");")
        idx = f"i_{f.name}"
        lines.append(f"{indent}for (size_t {idx} = 0; {idx} < (size_t)({count_expr}); {idx}++) {{")
        lines.append(f"{indent}    if ({idx} > 0) kv_write(w, \",\");")
        e = elem_get(idx)
        if elem_kind == "primitive":
            _, _, fmt = PRIMITIVES[f.element_type]
            cast = "(double)" if fmt == "%f" else ""
            lines.append(f'{indent}    kv_write(w, "{fmt}", {cast}{e});')
        elif elem_kind == "string":
            lines.append(f'{indent}    kv_write(w, "%s", {e} ? {e} : "");')
        elif elem_kind == "enum":
            lines.append(f'{indent}    kv_write(w, "%s", {module}_{f.element_type}_to_string({e}));')
        lines.append(f"{indent}}}")
        lines.append(f'{indent}kv_write(w, "]{sep}");')
        return "\n".join(lines)

    kind = classify(f.raw_type, enums, structs)
    if kind == "primitive":
        _, _, fmt = PRIMITIVES[f.raw_type]
        cast = "(double)" if fmt == "%f" else ""
        lines.append(f'{indent}kv_write(w, "{f.name}=' + fmt + f'{sep}", {cast}{src_expr});')
    elif kind == "string":
        lines.append(f'{indent}kv_write(w, "{f.name}=%s{sep}", {src_expr} ? {src_expr} : "");')
    elif kind == "enum":
        lines.append(f'{indent}kv_write(w, "{f.name}=%s{sep}", {module}_{f.raw_type}_to_string({src_expr}));')
    elif kind == "struct":
        lines.append(f'{indent}kv_write(w, "{f.name}={{");')
        lines.append(f"{indent}{module}_{f.raw_type}_encode_fields(&{src_expr}, w);")
        lines.append(f'{indent}kv_write(w, "}}{sep}");')
    else:
        lines.append(f"{indent}/* TODO: unknown/unsupported field type '{f.raw_type}' for '{f.name}' */")
    return "\n".join(lines)


def gen_struct_helpers(module, sd: StructDecl, enums, structs) -> str:
    """decode_fields/encode_fields for a NESTED (non-topic) struct type,
    called recursively by parent structs' decode/encode."""
    ctype = f"{module}_{sd.name}"
    decode_body = []
    for f in sd.fields:
        decode_body.append(gen_decode_field(module, f, enums, structs, f"out->{f.name}", "node", "    "))
    encode_body = []
    for i, f in enumerate(sd.fields):
        encode_body.append(gen_encode_field(module, f, enums, structs, f"v->{f.name}", "    ", i == len(sd.fields) - 1))

    return f"""
static void {ctype}_decode_fields(const KvNode* node, {ctype}* out) {{
{chr(10).join(decode_body) if decode_body else "    (void)node; (void)out;"}
}}

static void {ctype}_encode_fields(const {ctype}* v, KvWriter* w) {{
{chr(10).join(encode_body) if encode_body else "    (void)v; (void)w;"}
}}
"""


def gen_topic_entry(module, sd: StructDecl, enums, structs) -> str:
    ctype = f"{module}_{sd.name}"
    decode_body = []
    for f in sd.fields:
        decode_body.append(gen_decode_field(module, f, enums, structs, f"v->{f.name}", "root", "    "))
    encode_body = []
    for i, f in enumerate(sd.fields):
        encode_body.append(gen_encode_field(module, f, enums, structs, f"v->{f.name}", "    ", i == len(sd.fields) - 1))

    topic_name = sd.topic_name or f"{module}/{sd.name.lower()}"
    topic_name_comment = "" if sd.topic_name else "  /* TODO: placeholder — set the real wire topic name */"

    return f"""
static void* {sd.name}_alloc(void) {{ return {ctype}__alloc(); }}
static void  {sd.name}_free(void* d, dds_free_op_t op) {{ {ctype}_free(({ctype}*)d, op); }}

static bool {sd.name}_decode(const char* text, void* out_sample) {{
    {ctype}* v = ({ctype}*)out_sample;
    KvNode* root = kv_parse(text);
    if (!root) return false;
{chr(10).join(decode_body) if decode_body else "    (void)v;"}
    kv_free(root);
    return true;
}}

static bool {sd.name}_encode(const void* sample, char* out_buf, size_t out_cap) {{
    const {ctype}* v = (const {ctype}*)sample;
    KvWriter writer;
    kv_writer_init(&writer, out_buf, out_cap);
    KvWriter* w = &writer;
{chr(10).join(encode_body) if encode_body else "    (void)v; (void)w;"}
    return !writer.overflow;
}}
""", topic_name, topic_name_comment


def generate(module, enums, structs, struct_order, header_name: str) -> str:
    out = []
    out.append(f"""/*
 * GENERATED by gen_typed_adapter.py — a dds_typed_plugin type adapter for
 * module `{module}`. See DdsTypePluginAbi.h's doc comment for the ABI this
 * implements, and examples/customer1 for the hand-written original this
 * generator's output is modeled on.
 *
 * Review before building:
 *   - grep for "TODO" below — those spots need a hand-written fallback
 *     (unions, sequences/arrays of structs, multi-dim arrays, unknown types).
 *   - topic_name defaults are placeholders where the .idl had no
 *     `// @topic <name>` annotation above the struct — set the real ones.
 *   - decode()/encode() use the shared idl_kv.h "key=value" grammar
 *     (id=1;label=truck-07;pos={{x=1;y=2}};tags=[a,b,c]) — swap in your
 *     own grammar if the DDS_TYPED.CMD text format needs to match an
 *     existing script/tool.
 */
#include "DdsTypePluginAbi.h"
#include "{header_name}"
#include "idl_kv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
""")

    for name in struct_order:
        e = enums.get(name)
    for e in enums.values():
        out.append(gen_enum_helpers(module, e))

    # nested (non-topic) structs need decode_fields/encode_fields helpers,
    # emitted in declaration order so later structs can call earlier ones.
    for name in struct_order:
        sd = structs[name]
        if not sd.is_topic:
            out.append(gen_struct_helpers(module, sd, enums, structs))

    entries = []
    for name in struct_order:
        sd = structs[name]
        if sd.is_topic:
            code, topic_name, comment = gen_topic_entry(module, sd, enums, structs)
            out.append(code)
            entries.append((sd, topic_name, comment))

    kTypes_lines = []
    for sd, topic_name, comment in entries:
        kTypes_lines.append(f"""    {{
        .topic_name = "{topic_name}",{comment}
        .descriptor = &{module}_{sd.name}_desc,
        .alloc_sample = {sd.name}_alloc,
        .free_sample = {sd.name}_free,
        .decode = {sd.name}_decode,
        .encode = {sd.name}_encode,
    }},""")

    out.append(f"""
static const DdsTypeEntry kTypes[] = {{
{chr(10).join(kTypes_lines)}
}};

static size_t get_type_count(void) {{ return sizeof(kTypes) / sizeof(kTypes[0]); }}
static const DdsTypeEntry* get_type(size_t index) {{ return (index < get_type_count()) ? &kTypes[index] : NULL; }}

static const DdsTypePlugin kPlugin = {{
    .abi_version = DDS_TYPE_PLUGIN_ABI_VERSION,
    .customer_name = "{module}",
    .get_type_count = get_type_count,
    .get_type = get_type,
}};

const DdsTypePlugin* dds_type_plugin_get(void) {{ return &kPlugin; }}
""")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("idl_file")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--header", help="idlc-generated header name to #include (default: <module>.h)")
    args = ap.parse_args()

    with open(args.idl_file) as f:
        text = f.read()

    module, enums, structs, struct_order = parse_idl(text)
    header_name = args.header or f"{module}.h"
    code = generate(module, enums, structs, struct_order, header_name)

    with open(args.output, "w") as f:
        f.write(code)

    n_topics = sum(1 for s in structs.values() if s.is_topic)
    n_todo = code.count("TODO")
    print(f"wrote {args.output}: module={module}, {len(structs)} struct(s), "
          f"{n_topics} topic type(s), {n_todo} TODO marker(s) to review", file=sys.stderr)


if __name__ == "__main__":
    main()
