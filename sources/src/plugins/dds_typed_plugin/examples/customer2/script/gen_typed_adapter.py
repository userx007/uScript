#!/usr/bin/env python3
"""
gen_typed_adapter.py — generate a dds_typed_plugin <customer>_adapter.c
from Cyclone DDS IDL files.

Supports:
  - Namespaced types (e.g., P_LDM_Common::T_IdentifierType)
  - typedefs (including typedefs to sequences/structs/primitives)
  - Nested structs (recursive decode/encode)
  - Sequences of primitives, strings, enums, and simple structs
  - Fixed-size arrays
  - sequence<char, N> treated as string

Usage:
    python3 gen_typed_adapter.py LDM_Common.idl Alarms_PSM.idl -o P_Alarms_PSM_adapter.c

This script parses the IDL, resolves all types (including those in included
files if you manually merge them or pass multiple files), and generates the
C adapter code.
"""

import argparse
import os
import re
import sys
from dataclasses import dataclass, field
from typing import Optional, Dict, List, Set

# ---------------------------------------------------------------------
# Type Definitions & Registry
# ---------------------------------------------------------------------

@dataclass
class EnumDecl:
    name: str
    module: str
    values: list = field(default_factory=list)

@dataclass
class StructDecl:
    name: str
    module: str
    is_topic: bool = False
    topic_name: Optional[str] = None
    fields: list = field(default_factory=list)

@dataclass
class TypedefDecl:
    name: str
    module: str
    raw_type: str  # The string from the IDL, e.g., "sequence <T_Char, 20>"

@dataclass
class Field:
    name: str
    raw_type: str
    is_sequence: bool = False
    is_array: bool = False
    array_len: Optional[str] = None
    element_type: Optional[str] = None  # Resolved element type string

class TypeRegistry:
    """Resolves namespaced types and typedefs to their definitions."""
    def __init__(self):
        self.enums: Dict[str, EnumDecl] = {}
        self.structs: Dict[str, StructDecl] = {}
        self.typedefs: Dict[str, TypedefDecl] = {}
        # Map fully qualified name (module_name) to definition
        self.all_types: Dict[str, EnumDecl | StructDecl | TypedefDecl] = {}

    def add_enum(self, e: EnumDecl):
        key = f"{e.module}_{e.name}"
        self.enums[key] = e
        self.all_types[key] = e

    def add_struct(self, s: StructDecl):
        key = f"{s.module}_{s.name}"
        self.structs[key] = s
        self.all_types[key] = s

    def add_typedef(self, t: TypedefDecl):
        key = f"{t.module}_{t.name}"
        self.typedefs[key] = t
        self.all_types[key] = t

    def merge(self, other: 'TypeRegistry'):
        """Merge another registry into this one."""
        self.enums.update(other.enums)
        self.structs.update(other.structs)
        self.typedefs.update(other.typedefs)
        self.all_types.update(other.all_types)

    def resolve_type_name(self, type_str: str) -> str:
        """
        Resolves a type string (e.g., "P_LDM_Common::T_IdentifierType")
        to a canonical key (e.g., "P_LDM_Common_T_IdentifierType").
        Handles typedefs by expanding them until a base type is found.
        """
        # 1. Replace :: with _ for C compatibility
        canonical = type_str.replace("::", "_")
        
        # 2. Check if it's a known typedef
        if canonical in self.typedefs:
            td = self.typedefs[canonical]
            # Recursively resolve the underlying type
            return self.resolve_type_name(td.raw_type)
        
        # 3. If not a typedef, return the canonical name
        return canonical

    def get_type_kind(self, type_str: str, module: str) -> str:
        """
        Returns 'primitive', 'string', 'char_seq', 'enum', 'struct', 'sequence',
        'array', 'unknown'. Resolves typedefs first.

        'string' is a literal IDL `string`/`string<N>` field — Cyclone idlc
        generates these as plain `char*`.
        'char_seq' is a `sequence<char, N>` field (however it got there,
        typically via a typedef like `T_ShortString`) — despite "reading" like
        a bounded string, Cyclone idlc does NOT generate `char*` for these; it
        generates a bounded-sequence struct (`_buffer`/`_length`/`_maximum`/
        `_release`), same shape as any other sequence, just with a char
        element. Callers must not treat 'char_seq' fields as `char*`.
        """
        # First, resolve typedefs to find the base structure.
        # Track the *current module context* alongside the current type text:
        # once we expand a typedef, any unqualified names inside its raw_type
        # must be resolved in the module where that typedef was declared, not
        # in the module of the original caller (they may differ, e.g. a
        # P_Alarms_PSM field typed as P_LDM_Common::T_ShortString expands to
        # "sequence<T_Char, 20>", and T_Char lives in P_LDM_Common).
        current = type_str
        cur_module = module
        seen = set()
        while True:
            if (current, cur_module) in seen:
                return "unknown" # Circular ref
            seen.add((current, cur_module))
            
            # Resolve the name to a canonical key, considering the module context
            if "::" in current:
                cur_module = current.rsplit("::", 1)[0]
                canonical = current.replace("::", "_")
            else:
                canonical = f"{cur_module}_{current}"
            
            if canonical in self.enums:
                return "enum"
            if canonical in self.structs:
                return "struct"
            
            # Check if it's a typedef to expand
            if canonical in self.typedefs:
                td = self.typedefs[canonical]
                current = td.raw_type
                cur_module = td.module
                continue
            
            # Check primitives/strings
            if current in PRIMITIVES:
                return "primitive"
            if re.match(r"^string(?:\s*<\s*\w+\s*>)?$", current):
                return "string"
            
            # Check sequences/arrays
            if re.match(r"^sequence\s*<", current):
                # Check if it's a sequence of char
                elem_match = re.match(r"sequence\s*<\s*([\w:]+)\s*(?:,\s*\w+\s*)?>", current)
                if elem_match:
                    elem_type = elem_match.group(1)
                    # Resolve elem_type to see if it's char
                    # We need to check the resolved type of the element, not just the name
                    # Since T_Char is a typedef for char, we check if the resolved type is 'char'
                    prim = self.get_base_primitive_type(elem_type, cur_module)
                    if prim == "char":
                        # NOT plain "string": idlc generates a bounded-sequence
                        # struct for sequence<char, N>, never a char*. See
                        # docstring above.
                        return "char_seq"
                return "sequence"
            if re.search(r"\[\s*\w+\s*\]$", current):
                return "array"
            
            return "unknown"

    def get_base_primitive_type(self, type_str: str, module: str) -> Optional[str]:
        """
        If the type_str (or its typedef chain) resolves to a primitive,
        return the primitive keyword (e.g., 'long', 'double').
        Otherwise, return None.
        """
        current = type_str
        cur_module = module
        seen = set()
        while True:
            if (current, cur_module) in seen:
                return None  # Circular reference
            seen.add((current, cur_module))
            
            # Check if current is a primitive directly
            if current in PRIMITIVES:
                return current
            
            # Resolve the name to a canonical key, considering the module context
            if "::" in current:
                cur_module = current.rsplit("::", 1)[0]
                canonical = current.replace("::", "_")
            else:
                canonical = f"{cur_module}_{current}"
            
            # Check if it's a typedef
            if canonical in self.typedefs:
                td = self.typedefs[canonical]
                current = td.raw_type
                cur_module = td.module
                continue
            
            # Not a primitive and not a typedef
            return None

    def get_c_type(self, type_str: str, module: str) -> str:
        """Returns the C type name for a type string."""
        # First, resolve the type to its final base type
        current = type_str
        cur_module = module
        seen = set()
        base_type = None
        
        while True:
            if (current, cur_module) in seen:
                break
            seen.add((current, cur_module))
            
            # Resolve the name to a canonical key, considering the module context
            if "::" in current:
                cur_module = current.rsplit("::", 1)[0]
                canonical = current.replace("::", "_")
            else:
                canonical = f"{cur_module}_{current}"
            
            if canonical in self.enums:
                return f"{self.enums[canonical].module}_{self.enums[canonical].name}"
            if canonical in self.structs:
                return f"{self.structs[canonical].module}_{self.structs[canonical].name}"
            
            if canonical in self.typedefs:
                td = self.typedefs[canonical]
                current = td.raw_type
                cur_module = td.module
                continue
            
            base_type = current
            break
        
        if base_type in PRIMITIVES:
            return PRIMITIVES[base_type][0]
        if re.match(r"^string(?:\s*<\s*\w+\s*>)?$", base_type):
            return "char*"
        if re.match(r"^sequence\s*<", base_type):
            elem_type_str = self._get_sequence_elem(base_type)
            elem_c_type = self.get_c_type(elem_type_str, cur_module) # Recursive call, correct module context
            return f"{elem_c_type}_seq"
        if re.search(r"\[\s*\w+\s*\]$", base_type):
            elem_type_str = self._get_array_elem(base_type)
            elem_c_type = self.get_c_type(elem_type_str, cur_module) # Recursive call, correct module context
            return f"{elem_c_type}[]"
            
        return f"/* TODO unknown type {type_str} */"

    def _get_sequence_elem(self, type_str: str) -> str:
        m = re.search(r"sequence\s*<\s*([\w:]+)", type_str)
        return m.group(1) if m else type_str

    def _get_array_elem(self, type_str: str) -> str:
        # Handle T name[N] or T<N> name[N]
        m = re.match(r"^(?P<type>[\w:]+)\s+.*?\[\s*(?P<len>\w+)\s*\]$", type_str)
        if m:
            return m.group("type")
        return type_str


PRIMITIVES = {
    "boolean": ("bool", "i64", "%d"),
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
PRIMITIVE_KEYWORDS = sorted(PRIMITIVES.keys(), key=len, reverse=True)

# ---------------------------------------------------------------------
# Parsing Logic
# ---------------------------------------------------------------------

def strip_comments(text: str):
    """Remove /*...*/ and // comments, but keep `// @topic <name>` lines."""
    topic_annotations = []
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

def parse_idl(text: str, module_name: str) -> TypeRegistry:
    """Parse IDL text into enums, structs, typedefs."""
    stripped, topic_annotations = strip_comments(text)
    
    registry = TypeRegistry()

    # 1. Parse Enums
    for m in re.finditer(r"enum\s+(\w+)\s*{([^}]*)}\s*;", stripped):
        name = m.group(1)
        values = [v.strip() for v in m.group(2).split(",") if v.strip()]
        mod_match = re.search(r"module\s+(\w+)\s*{", stripped)
        mod = mod_match.group(1) if mod_match else module_name
        e = EnumDecl(name=name, module=mod, values=values)
        registry.add_enum(e)

    # 2. Parse Structs
    for m in re.finditer(
        r"(?:@\w+\s*)*struct\s+(\w+)\s*{([^}]*)}\s*;", stripped
    ):
        name = m.group(1)
        body = m.group(2)
        struct_offset = m.start()
        mod_match = re.search(r"module\s+(\w+)\s*{", stripped)
        mod = mod_match.group(1) if mod_match else module_name
        sd = StructDecl(name=name, module=mod)

        for annot_offset, topic_name in topic_annotations:
            if 0 <= struct_offset - annot_offset < 200:
                sd.is_topic = True
                sd.topic_name = topic_name

        # Parse fields
        for stmt in body.split(";"):
            stmt = stmt.strip()
            if not stmt:
                continue
            f = parse_field(stmt)
            if f:
                sd.fields.append(f)

        registry.add_struct(sd)

    # 2b. Parse `#pragma keylist <StructName> <key.field> ...` directives.
    # This is Cyclone DDS's standard way of marking a struct as a top-level
    # topic type (with its key fields) in .idl files that don't use a
    # `// @topic <name>` comment convention.
    for m in re.finditer(r"#pragma\s+keylist\s+(\w+)([^\n]*)", stripped):
        struct_name = m.group(1)
        key_fields = m.group(2).split()
        mod_match = re.search(r"module\s+(\w+)\s*{", stripped)
        mod = mod_match.group(1) if mod_match else module_name
        key = f"{mod}_{struct_name}"
        if key in registry.structs:
            registry.structs[key].is_topic = True
            # No explicit wire name is given by #pragma keylist (unlike
            # `// @topic <name>`), so topic_name stays None and the
            # generator will fall back to a placeholder name that's
            # flagged for the user to confirm/replace.

    # 3. Parse Typedefs
    for m in re.finditer(r"typedef\s+(.+?)\s+(\w+)\s*;", stripped):
        raw_type = m.group(1).strip()
        name = m.group(2)
        mod_match = re.search(r"module\s+(\w+)\s*{", stripped)
        mod = mod_match.group(1) if mod_match else module_name
        td = TypedefDecl(name=name, module=mod, raw_type=raw_type)
        registry.add_typedef(td)

    return registry

def parse_field(stmt: str) -> Optional[Field]:
    stmt = re.sub(r"\s+", " ", stmt.strip())
    if not stmt:
        return None

    # Check for array: Type name[N]
    array_match = re.search(r"\s+(\w+)\s*\[\s*(\w+)\s*\]\s*$", stmt)
    if array_match:
        name = array_match.group(1)
        length = array_match.group(2)
        type_part = stmt[:array_match.start()].strip()
        return Field(
            name=name,
            raw_type=type_part,
            is_array=True,
            array_len=length,
            element_type=type_part
        )

    # Check for sequence: sequence < Type > name
    seq_match = re.match(r"sequence\s*<\s*([\w:]+)\s*(?:,\s*\w+\s*)?>\s+(\w+)", stmt)
    if seq_match:
        return Field(
            name=seq_match.group(2),
            raw_type=seq_match.group(0),
            is_sequence=True,
            element_type=seq_match.group(1)
        )

    # Standard: Type name
    parts = stmt.rsplit(" ", 1)
    if len(parts) == 2:
        type_str, name = parts[0].strip(), parts[1].strip()
        return Field(name=name, raw_type=type_str)
    
    return None

# ---------------------------------------------------------------------
# Topic-family file discovery ("<Base>_PSM.idl", "<Base>_topicNames.idl",
# "<Base>_topicTypeNames.idl", "LDM_Common.idl") and const-string parsing
# ---------------------------------------------------------------------

# Filename suffixes that make up a "topic family" for a given base name
# (e.g. base "Alarms" -> Alarms_PSM.idl, Alarms_topicNames.idl, ...).
FAMILY_SUFFIXES = {
    "psm": "_PSM.idl",
    "topic_names": "_topicNames.idl",
    "topic_type_names": "_topicTypeNames.idl",
}
# Shared/common file required alongside every family, regardless of base name.
FAMILY_COMMON_FILE = "LDM_Common.idl"


def discover_family_files(base_name: str, search_dirs: List[str]) -> Dict[str, str]:
    """
    Given a base name (e.g. "Alarms"), locate:
      <base_name>_PSM.idl
      <base_name>_topicNames.idl
      <base_name>_topicTypeNames.idl
      LDM_Common.idl
    by searching search_dirs (in order). Returns a dict keyed by
    "psm" / "topic_names" / "topic_type_names" / "ldm_common" -> full path.
    Exits with an error listing everything missing if any file can't be found.
    """
    wanted = {key: f"{base_name}{suffix}" for key, suffix in FAMILY_SUFFIXES.items()}
    wanted["ldm_common"] = FAMILY_COMMON_FILE

    found: Dict[str, str] = {}
    missing: List[str] = []
    for key, fname in wanted.items():
        hit = None
        for d in search_dirs:
            candidate = os.path.join(d, fname)
            if os.path.isfile(candidate):
                hit = candidate
                break
        if hit:
            found[key] = hit
        else:
            missing.append(fname)

    if missing:
        dirs_str = ", ".join(search_dirs)
        print(
            f"error: could not find required IDL file(s) for family '{base_name}' "
            f"(searched: {dirs_str}): {', '.join(missing)}",
            file=sys.stderr,
        )
        sys.exit(1)

    return found


def parse_const_string_map(text: str) -> Dict[str, str]:
    """
    Parses `const string NAME = "VALUE";` (and the `const String ...` variant
    seen in *_topicTypeNames.idl) declarations into a {NAME: VALUE} dict.
    Used to read *_topicNames.idl and *_topicTypeNames.idl, whose sole
    purpose is to hand out the wire topic name / qualified type name for
    each topic struct as a string constant.
    """
    stripped, _ = strip_comments(text)
    result: Dict[str, str] = {}
    for m in re.finditer(
        r'const\s+[Ss]tring\s+(\w+)\s*=\s*"((?:[^"\\]|\\.)*)"\s*;', stripped
    ):
        result[m.group(1)] = m.group(2)
    return result


# ---------------------------------------------------------------------
# Code Generation
# ---------------------------------------------------------------------

def gen_enum_helpers(registry: TypeRegistry, e: EnumDecl) -> str:
    from_lines = "\n".join(
        f'    if (strcmp(s, "{v}") == 0) {{ *out = {e.module}_{v}; return true; }}'
        for v in e.values
    )
    to_lines = "\n".join(
        f'        case {e.module}_{v}: return "{v}";' for v in e.values
    )
    return f"""
static bool {e.module}_{e.name}_from_string(const char* s, {e.module}_{e.name}* out) {{
{from_lines}
    return false;
}}

static const char* {e.module}_{e.name}_to_string({e.module}_{e.name} v) {{
    switch (v) {{
{to_lines}
        default: return "?";
    }}
}}
"""

def _field_node_expr(src_node_expr: str, field_name: str) -> str:
    container = "root" if src_node_expr == "root" else src_node_expr
    return f'kv_get({container}, "{field_name}")'

def _gen_char_seq_decode(dst_expr: str, node_expr: str, indent: str) -> List[str]:
    """
    Populates a bounded char-sequence struct (the `_buffer`/`_length`/
    `_maximum`/`_release` shape idlc generates for `sequence<char, N>`,
    e.g. via a typedef like T_ShortString) from a KV scalar node.
    NOT used for real IDL `string` fields (plain char*) — see
    TypeRegistry.get_type_kind's 'char_seq' docstring for why the two
    need different C code despite both "reading" like strings in the IDL.
    """
    return [
        f"{indent}{{ const char* tmp = kv_as_str({node_expr});",
        f"{indent}  if (tmp) {{",
        f"{indent}    size_t tmp_len = strlen(tmp);",
        f"{indent}    char* tmp_buf = (char*)malloc(tmp_len + 1);",
        f"{indent}    if (tmp_buf) {{",
        f"{indent}      memcpy(tmp_buf, tmp, tmp_len + 1);",
        f"{indent}      free({dst_expr}._buffer);",
        f"{indent}      {dst_expr}._buffer = tmp_buf;",
        f"{indent}      {dst_expr}._length = (uint32_t)(tmp_len + 1);",
        f"{indent}      {dst_expr}._maximum = (uint32_t)(tmp_len + 1);",
        f"{indent}      {dst_expr}._release = true;",
        f"{indent}    }}",
        f"{indent}  }}",
        f"{indent}}}",
    ]

def _gen_char_seq_encode(src_expr: str, indent: str, literal_prefix: str = "", literal_suffix: str = "") -> List[str]:
    """Writes a bounded char-sequence struct (see _gen_char_seq_decode) back
    out as text. `_buffer` may or may not be NUL-terminated (only our own
    decode above guarantees that); strip at most one trailing NUL so a
    round-tripped value doesn't grow one extra byte per cycle."""
    return [
        f"{indent}{{",
        f"{indent}    uint32_t cs_len = {src_expr}._length;",
        f"{indent}    const char* cs_buf = {src_expr}._buffer;",
        f"{indent}    if (cs_len > 0 && cs_buf && cs_buf[cs_len - 1] == '\\0') cs_len -= 1;",
        f'{indent}    kv_write(w, "{literal_prefix}%.*s{literal_suffix}", (int)cs_len, cs_buf ? cs_buf : "");',
        f"{indent}}}",
    ]

def _gen_seq_grow(dst_expr: str, idx_var: str, indent: str) -> List[str]:
    """
    Grows an unbounded/heap-backed sequence's `_buffer` (realloc, doubling)
    the moment `idx_var` reaches `_maximum`. A freshly dds_alloc()'d sample
    always starts with `_buffer == NULL` / `_maximum == 0` / `_length == 0`
    (idlc represents every `sequence<T>` and `sequence<T,N>` field this way,
    with no static backing array), so without this, decode() could never
    actually write a single element into any non-array sequence field — the
    old `if (idx >= _maximum) break;` bound check just silently discarded
    everything on the very first element. Fixed-size arrays (`f.is_array`)
    don't need this: they're backed by an inline C array of the declared
    length, already fully usable as-is. Uses sizeof(*_buffer) so this works
    for whatever the element's real C type is without the caller having to
    name it.
    """
    return [
        f"{indent}if ({idx_var} >= {dst_expr}._maximum) {{",
        f"{indent}    uint32_t grown_max = {dst_expr}._maximum ? {dst_expr}._maximum * 2 : 4;",
        f"{indent}    void* grown_buf = realloc({dst_expr}._buffer, grown_max * sizeof(*{dst_expr}._buffer));",
        f"{indent}    if (!grown_buf) break;",
        f"{indent}    memset((char*)grown_buf + {dst_expr}._maximum * sizeof(*{dst_expr}._buffer), 0,",
        f"{indent}           (size_t)(grown_max - {dst_expr}._maximum) * sizeof(*{dst_expr}._buffer));",
        f"{indent}    {dst_expr}._buffer = grown_buf;",
        f"{indent}    {dst_expr}._maximum = grown_max;",
        f"{indent}    {dst_expr}._release = true;",
        f"{indent}}}",
    ]

def gen_decode_field(registry: TypeRegistry, f: Field, dst_expr: str, src_node_expr: str, indent: str, module: str) -> str:
    lines = []
    
    # Resolve element type for sequences/arrays
    elem_type_str = f.element_type or f.raw_type
    elem_kind = registry.get_type_kind(elem_type_str, module)
    
    # Handle Sequence/Array
    if f.is_sequence or f.is_array:
        if elem_kind == "struct":
            # Support for sequence of structs
            elem_c_type = registry.get_c_type(elem_type_str, module)
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
                elem_dst = f"{dst_expr}._buffer[{idx}]"
                lines.extend(_gen_seq_grow(dst_expr, idx, indent + "        "))
            
            lines.append(f"{indent}        {elem_c_type}_decode_fields({elem_node}, &{elem_dst});")
            
            if not f.is_array:
                lines.append(f"{indent}        {dst_expr}._length = {idx} + 1;")
            lines.append(f"{indent}    }}")
            lines.append(f"{indent}}}")
            return "\n".join(lines)
        elif elem_kind in ("primitive", "enum", "string", "char_seq"):
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
                elem_dst = f"{dst_expr}._buffer[{idx}]"
                lines.extend(_gen_seq_grow(dst_expr, idx, indent + "        "))
            
            if elem_kind == "char_seq":
                # sequence/array of a bounded-char-sequence typedef (e.g.
                # sequence<T_ShortString>) — each element is itself a
                # _buffer/_length/_maximum/_release struct, not a char*.
                lines.extend(_gen_char_seq_decode(elem_dst, elem_node, indent + "        "))
            elif elem_kind == "string":
                # A genuine IDL `string` element (char*) inside a sequence/array.
                lines.append(f"{indent}        {{ const char* tmp = kv_as_str({elem_node}); if (tmp) {elem_dst} = strdup(tmp); }}")
            elif elem_kind == "primitive":
                _, accessor, _ = PRIMITIVES[elem_type_str]
                if accessor == "i64":
                    lines.append(f"{indent}        {{ long long tmp; if (kv_as_i64({elem_node}, &tmp)) {elem_dst} = ({registry.get_c_type(elem_type_str, module)})tmp; }}")
                elif accessor == "double":
                    lines.append(f"{indent}        {{ double tmp; if (kv_as_double({elem_node}, &tmp)) {elem_dst} = ({registry.get_c_type(elem_type_str, module)})tmp; }}")
                else:
                    lines.append(f"{indent}        {{ bool tmp; if (kv_as_bool({elem_node}, &tmp)) {elem_dst} = tmp; }}")
            elif elem_kind == "enum":
                lines.append(f"{indent}        {{ const char* tmp = kv_as_str({elem_node}); "
                              f"if (tmp) {registry.get_c_type(elem_type_str, module)}_from_string(tmp, &{elem_dst}); }}")
            
            if not f.is_array:
                lines.append(f"{indent}        {dst_expr}._length = {idx} + 1;")
            lines.append(f"{indent}    }}")
            lines.append(f"{indent}}}")
            return "\n".join(lines)
        else:
            lines.append(f"{indent}/* TODO: unsupported sequence/array element type '{elem_type_str}' */")
            return "\n".join(lines)

    # Handle Single Field
    kind = registry.get_type_kind(f.raw_type, module)
    node = _field_node_expr(src_node_expr, f.name)

    # Check if it's a primitive (directly or via typedef)
    base_prim = registry.get_base_primitive_type(f.raw_type, module)
    if base_prim:
        _, accessor, _ = PRIMITIVES[base_prim]
        c_type = registry.get_c_type(f.raw_type, module)
        if accessor == "i64":
            lines.append(f"{indent}{{ long long tmp; if (kv_as_i64({node}, &tmp)) {dst_expr} = ({c_type})tmp; }}")
        elif accessor == "double":
            lines.append(f"{indent}{{ double tmp; if (kv_as_double({node}, &tmp)) {dst_expr} = ({c_type})tmp; }}")
        else:
            lines.append(f"{indent}{{ bool tmp; if (kv_as_bool({node}, &tmp)) {dst_expr} = tmp; }}")
    elif kind == "char_seq":
        # sequence<char, N> (typically via a typedef like T_ShortString) —
        # idlc generates a bounded-sequence struct here, NOT char*.
        lines.extend(_gen_char_seq_decode(dst_expr, node, indent))
    elif kind == "string":
        # A genuine IDL `string`/`string<N>` field — idlc generates plain char*.
        lines.append(f"{indent}{{ const char* tmp = kv_as_str({node}); if (tmp) {{ free({dst_expr}); {dst_expr} = strdup(tmp); }} }}")
    elif kind == "enum":
        c_type = registry.get_c_type(f.raw_type, module)
        lines.append(f"{indent}{{ const char* tmp = kv_as_str({node}); if (tmp) {c_type}_from_string(tmp, &{dst_expr}); }}")
    elif kind == "struct":
        c_type = registry.get_c_type(f.raw_type, module)
        lines.append(f"{indent}{c_type}_decode_fields({node}, &{dst_expr});")
    else:
        lines.append(f"{indent}/* TODO: unknown/unsupported field type '{f.raw_type}' for '{f.name}' */")
    
    return "\n".join(lines)

def gen_encode_field(registry: TypeRegistry, f: Field, src_expr: str, indent: str, is_last: bool, module: str) -> str:
    sep = "" if is_last else ";"
    lines = []
    
    elem_type_str = f.element_type or f.raw_type
    elem_kind = registry.get_type_kind(elem_type_str, module)

    if f.is_sequence or f.is_array:
        if elem_kind == "struct":
            elem_c_type = registry.get_c_type(elem_type_str, module)
            count_expr = f.array_len if f.is_array else f"{src_expr}._length"
            elem_get = (lambda i: f"{src_expr}[{i}]") if f.is_array else (lambda i: f"{src_expr}._buffer[{i}]")
            
            lines.append(f"{indent}kv_write(w, \"{f.name}=[\");")
            idx = f"i_{f.name}"
            lines.append(f"{indent}for (size_t {idx} = 0; {idx} < (size_t)({count_expr}); {idx}++) {{")
            lines.append(f"{indent}    if ({idx} > 0) kv_write(w, \",\");")
            e = elem_get(idx)
            
            lines.append(f'{indent}    kv_write(w, "{{");')
            lines.append(f"{indent}    {elem_c_type}_encode_fields(&{e}, w);")
            lines.append(f'{indent}    kv_write(w, "}}");')
            
            lines.append(f"{indent}}}")
            lines.append(f'{indent}kv_write(w, "]{sep}");')
            return "\n".join(lines)
        elif elem_kind in ("primitive", "enum", "string", "char_seq"):
            count_expr = f.array_len if f.is_array else f"{src_expr}._length"
            elem_get = (lambda i: f"{src_expr}[{i}]") if f.is_array else (lambda i: f"{src_expr}._buffer[{i}]")
            
            lines.append(f"{indent}kv_write(w, \"{f.name}=[\");")
            idx = f"i_{f.name}"
            lines.append(f"{indent}for (size_t {idx} = 0; {idx} < (size_t)({count_expr}); {idx}++) {{")
            lines.append(f"{indent}    if ({idx} > 0) kv_write(w, \",\");")
            e = elem_get(idx)
            
            if elem_kind == "char_seq":
                # sequence/array of a bounded-char-sequence typedef (e.g.
                # sequence<T_ShortString>) — each element is a
                # _buffer/_length/_maximum/_release struct, not a char*.
                lines.extend(_gen_char_seq_encode(e, indent + "    "))
            elif elem_kind == "string":
                # A genuine IDL `string` element (char*) inside a sequence/array.
                lines.append(f'{indent}    kv_write(w, "%s", {e} ? {e} : "");')
            elif elem_kind == "primitive":
                _, _, fmt = PRIMITIVES[elem_type_str]
                cast = "(double)" if fmt == "%f" else ""
                lines.append(f'{indent}    kv_write(w, "{fmt}", {cast}{e});')
            elif elem_kind == "enum":
                c_type = registry.get_c_type(elem_type_str, module)
                lines.append(f'{indent}    kv_write(w, "%s", {c_type}_to_string({e}));')
            
            lines.append(f"{indent}}}")
            lines.append(f'{indent}kv_write(w, "]{sep}");')
            return "\n".join(lines)
        else:
            lines.append(f'{indent}/* TODO: unsupported sequence/array element type \'{elem_type_str}\' */')
            return "\n".join(lines)

    kind = registry.get_type_kind(f.raw_type, module)
    
    base_prim = registry.get_base_primitive_type(f.raw_type, module)
    if base_prim:
        _, _, fmt = PRIMITIVES[base_prim]
        cast = "(double)" if fmt == "%f" else ""
        c_type = registry.get_c_type(f.raw_type, module)
        lines.append(f'{indent}kv_write(w, "{f.name}=' + fmt + f'{sep}", {cast}{src_expr});')
    elif kind == "char_seq":
        # sequence<char, N> (typically via a typedef like T_ShortString) —
        # idlc generates a bounded-sequence struct here, NOT char*.
        lines.extend(_gen_char_seq_encode(src_expr, indent, literal_prefix=f"{f.name}=", literal_suffix=sep))
    elif kind == "string":
        # A genuine IDL `string`/`string<N>` field — idlc generates plain char*.
        lines.append(f'{indent}kv_write(w, "{f.name}=%s{sep}", {src_expr} ? {src_expr} : "");')
    elif kind == "enum":
        c_type = registry.get_c_type(f.raw_type, module)
        lines.append(f'{indent}kv_write(w, "{f.name}=%s{sep}", {c_type}_to_string({src_expr}));')
    elif kind == "struct":
        c_type = registry.get_c_type(f.raw_type, module)
        lines.append(f'{indent}kv_write(w, "{f.name}={{");')
        lines.append(f"{indent}{c_type}_encode_fields(&{src_expr}, w);")
        lines.append(f'{indent}kv_write(w, "}}{sep}");')
    else:
        lines.append(f"{indent}/* TODO: unknown/unsupported field type '{f.raw_type}' for '{f.name}' */")
    return "\n".join(lines)

def gen_struct_helpers(registry: TypeRegistry, sd: StructDecl) -> str:
    ctype = f"{sd.module}_{sd.name}"
    decode_body = []
    for f in sd.fields:
        decode_body.append(gen_decode_field(registry, f, f"out->{f.name}", "node", "    ", sd.module))
    
    encode_body = []
    for i, f in enumerate(sd.fields):
        encode_body.append(gen_encode_field(registry, f, f"v->{f.name}", "    ", i == len(sd.fields) - 1, sd.module))

    return f"""
static void {ctype}_decode_fields(const KvNode* node, {ctype}* out) {{
{chr(10).join(decode_body) if decode_body else "    (void)node; (void)out;"}
}}

static void {ctype}_encode_fields(const {ctype}* v, KvWriter* w) {{
{chr(10).join(encode_body) if encode_body else "    (void)v; (void)w;"}
}}
"""

def gen_topic_entry(registry: TypeRegistry, sd: StructDecl, topic_names: Optional[Dict[str, str]] = None) -> tuple:
    ctype = f"{sd.module}_{sd.name}"
    
    decode_body = []
    for f in sd.fields:
        decode_body.append(gen_decode_field(registry, f, f"v->{f.name}", "root", "    ", sd.module))
    
    encode_body = []
    for i, f in enumerate(sd.fields):
        encode_body.append(gen_encode_field(registry, f, f"v->{f.name}", "    ", i == len(sd.fields) - 1, sd.module))

    topic_names = topic_names or {}
    if sd.topic_name:
        # Explicit `// @topic <name>` annotation in the source .idl wins.
        topic_name = sd.topic_name
        topic_name_comment = ""
    elif sd.name in topic_names:
        # Resolved from the matching *_topicNames.idl const string, keyed
        # by struct name (e.g. C_Crew_Role_In_Mission_State ->
        # "Alarms__Crew_Role_In_Mission_State").
        topic_name = topic_names[sd.name]
        topic_name_comment = ""
    else:
        topic_name = f"{sd.module}/{sd.name.lower()}"
        topic_name_comment = "  /* TODO: placeholder — set the real wire topic name */"

    return f"""
static void* {sd.module}_{sd.name}_sample_alloc(void) {{ return {ctype}__alloc(); }}
static void  {sd.module}_{sd.name}_sample_free(void* d, dds_free_op_t op) {{ {ctype}_free(({ctype}*)d, op); }}

static bool {sd.module}_{sd.name}_decode(const char* text, void* out_sample) {{
    {ctype}* v = ({ctype}*)out_sample;
    KvNode* root = kv_parse(text);
    if (!root) return false;
{chr(10).join(decode_body) if decode_body else "    (void)v;"}
    kv_free(root);
    return true;
}}

static bool {sd.module}_{sd.name}_encode(const void* sample, char* out_buf, size_t out_cap) {{
    const {ctype}* v = (const {ctype}*)sample;
    KvWriter writer;
    kv_writer_init(&writer, out_buf, out_cap);
    KvWriter* w = &writer;
{chr(10).join(encode_body) if encode_body else "    (void)v; (void)w;"}
    return !writer.overflow;
}}
""", topic_name, topic_name_comment


def generate(registry: TypeRegistry, module: str, topic_names: Optional[Dict[str, str]] = None,
             topic_type_names: Optional[Dict[str, str]] = None) -> str:
    out = []
    out.append(f"""/*
 * GENERATED by gen_typed_adapter.py — a dds_typed_plugin type adapter for
 * module `{module}`.
 *
 * Review before building:
 *   - grep for "TODO" below.
 *   - topic_name defaults are placeholders where the .idl had no
 *     `// @topic <name>` annotation above the struct — set the real ones.
 */
#include "DdsTypePluginAbi.h"
#include "{module}.h"
#include "idl_kv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
""")

    for e in registry.enums.values():
        out.append(gen_enum_helpers(registry, e))

    for name, s in registry.structs.items():
        if not s.is_topic:
            out.append(gen_struct_helpers(registry, s))

    entries = []
    for name, s in registry.structs.items():
        if s.is_topic:
            code, topic_name, comment = gen_topic_entry(registry, s, topic_names)
            out.append(code)
            entries.append((s, topic_name, comment))

            # Cross-check against *_topicTypeNames.idl, if supplied: it hands
            # out the fully-qualified type name ("Module::Struct") for each
            # topic const. This doesn't feed into the generated C (the C type
            # name is already known from the .idl struct itself), it's just
            # a consistency check against the topic-family files.
            if topic_type_names is not None and not s.topic_name:
                expected = f"{s.module}::{s.name}"
                actual = topic_type_names.get(s.name)
                if actual is None:
                    print(f"warning: '{s.name}' has no matching entry in the "
                          f"*_topicTypeNames.idl file", file=sys.stderr)
                elif actual != expected:
                    print(f"warning: '{s.name}' topic type name mismatch: "
                          f"*_topicTypeNames.idl says '{actual}', expected '{expected}'",
                          file=sys.stderr)

    kTypes_lines = []
    for sd, topic_name, comment in entries:
        kTypes_lines.append(f"""    {{
        .topic_name = "{topic_name}",{comment}
        .descriptor = &{sd.module}_{sd.name}_desc,
        .alloc_sample = {sd.module}_{sd.name}_sample_alloc,
        .free_sample = {sd.module}_{sd.name}_sample_free,
        .decode = {sd.module}_{sd.name}_decode,
        .encode = {sd.module}_{sd.name}_encode,
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
    ap.add_argument("idl_file", nargs="*", help="IDL files to parse explicitly (order matters for includes). "
                                                 "Omit this and use --family instead to auto-discover them.")
    ap.add_argument("--family", "--name", dest="family", metavar="BASE_NAME",
                     help="Base name of a topic family (e.g. 'Alarms') to auto-discover "
                          "<BASE_NAME>_PSM.idl, <BASE_NAME>_topicNames.idl, "
                          "<BASE_NAME>_topicTypeNames.idl and LDM_Common.idl in --idl-dir, "
                          "and use the *_topicNames.idl content to resolve real wire topic "
                          "names (no more topic_name TODO placeholders).")
    ap.add_argument("--idl-dir", dest="idl_dirs", action="append", default=None,
                     help="Directory to search for --family auto-discovery (default: current "
                          "directory). May be given multiple times to search several directories.")
    ap.add_argument("-o", "--output",
                     help="Output .c path. With --family BASE_NAME this defaults to "
                          "'<BASE_NAME>_adapter.c'; otherwise required.")
    ap.add_argument("--header", help="idlc-generated header name to #include (default: <module>.h)")
    args = ap.parse_args()

    topic_names: Dict[str, str] = {}
    topic_type_names: Optional[Dict[str, str]] = None

    if args.family:
        if args.idl_file:
            ap.error("pass either explicit IDL files or --family, not both")
        search_dirs = args.idl_dirs or ["."]
        family_files = discover_family_files(args.family, search_dirs)
        # LDM_Common.idl first (defines shared types referenced by the PSM
        # file), the family's *_PSM.idl last (its filename determines the
        # generated module/customer name).
        idl_files = [family_files["ldm_common"], family_files["psm"]]

        with open(family_files["topic_names"]) as f:
            topic_names = parse_const_string_map(f.read())
        with open(family_files["topic_type_names"]) as f:
            topic_type_names = parse_const_string_map(f.read())

        if not args.output:
            args.output = f"{args.family}_adapter.c"
    else:
        if not args.idl_file:
            ap.error("either provide one or more IDL files, or use --family BASE_NAME")
        idl_files = args.idl_file
        if not args.output:
            ap.error("-o/--output is required unless --family is used")

    full_text = ""
    for idl_file in idl_files:
        with open(idl_file) as f:
            full_text += f.read() + "\n"

    primary_module = os.path.basename(idl_files[-1])
    if primary_module.endswith('.idl'):
        primary_module = primary_module[:-4]

    merged_registry = TypeRegistry()
    
    for idl_file in idl_files:
        with open(idl_file) as f:
            text = f.read()
        
        mod_match = re.search(r"module\s+(\w+)\s*{", text)
        if mod_match:
            mod = mod_match.group(1)
        else:
            mod = os.path.basename(idl_file)
            if mod.endswith('.idl'):
                mod = mod[:-4]
        
        registry = parse_idl(text, mod)
        merged_registry.merge(registry)

    if not any(s.is_topic for s in merged_registry.structs.values()):
        referenced = set()
        for s in merged_registry.structs.values():
            for f in s.fields:
                t = f.element_type or f.raw_type
                if merged_registry.get_type_kind(t, primary_module) == "struct":
                    referenced.add(merged_registry.resolve_type_name(t))
        
        for name, s in merged_registry.structs.items():
            if name not in referenced:
                s.is_topic = True
                s.topic_name = None

    header_name = args.header or f"{primary_module}.h"
    code = generate(merged_registry, primary_module, topic_names, topic_type_names)

    with open(args.output, "w") as f:
        f.write(code)

    n_topics = sum(1 for s in merged_registry.structs.values() if s.is_topic)
    n_todo = code.count("TODO")
    print(f"wrote {args.output}: module={primary_module}, {len(merged_registry.structs)} struct(s), "
          f"{n_topics} topic type(s), {n_todo} TODO marker(s) to review", file=sys.stderr)


if __name__ == "__main__":
    main()
