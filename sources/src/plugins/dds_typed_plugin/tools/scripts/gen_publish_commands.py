#!/usr/bin/env python3
"""
gen_publish_commands.py — generate a ready-to-paste `DDS_TYPED.CMD >
PUBLISH <topic> <payload>` line (with a realistic sample payload) for
every topic struct in a family, e.g. `Alarms`.

Why this reads the .idl (via gen_typed_adapter.py's own parser) rather
than the generated <family>_adapter.c: the adapter's decode()/encode()
were themselves generated from the IDL's field names/types/nesting, so
the IDL is the source of truth and the .c is a step further from it.
Importing gen_typed_adapter's TypeRegistry/parse_idl directly also means
this script can never drift out of sync with what the adapter actually
expects (same get_type_kind() classification — 'primitive' / 'string' /
'char_seq' / 'enum' / 'struct' / 'sequence' / 'array' — that the C
generator itself uses), rather than re-deriving that from C text.

Usage:
    python3 gen_publish_commands.py --family Alarms
    python3 gen_publish_commands.py --family Alarms --topic Alarm_Category
    python3 gen_publish_commands.py --family Alarms --format script -o alarms_publish.txt
"""
import argparse
import os
import sys
from typing import Dict, List, Optional, Set

import gen_typed_adapter as gta


# ---------------------------------------------------------------------
# Sample-value generation
# ---------------------------------------------------------------------

class SampleGen:
    """Deterministic (given --seed) generator of small, readable sample
    values, so re-running with the same seed reproduces the same output."""

    def __init__(self, registry: gta.TypeRegistry, seed: int, seq_elems: int, array_cap: int, max_depth: int):
        self.registry = registry
        self.counter = seed
        self.seq_elems = seq_elems
        self.array_cap = array_cap
        self.max_depth = max_depth

    def next_n(self) -> int:
        self.counter += 1
        return self.counter

    def char_seq_bound(self, type_str: str, module: str) -> Optional[int]:
        """Resolves a char_seq field's typedef chain down to the literal
        `sequence<CharType, N>` and returns N, so sample text never exceeds
        the schema's own bound (e.g. T_ShortString's 20)."""
        current, cur_module, seen = type_str, module, set()
        while True:
            if (current, cur_module) in seen:
                return None
            seen.add((current, cur_module))
            canonical = current.replace("::", "_") if "::" in current else f"{cur_module}_{current}"
            if "::" in current:
                cur_module = current.rsplit("::", 1)[0]
            if canonical in self.registry.typedefs:
                td = self.registry.typedefs[canonical]
                m = __import__("re").match(r"sequence\s*<\s*[\w:]+\s*,\s*(\w+)\s*>", td.raw_type)
                if m:
                    try:
                        return int(m.group(1))
                    except ValueError:
                        return None
                current, cur_module = td.raw_type, td.module
                continue
            return None

    def primitive_value(self, base_prim: str) -> str:
        n = self.next_n()
        if base_prim == "boolean":
            return "true" if n % 2 else "false"
        if base_prim in ("float", "double"):
            return f"{n}.5"
        if base_prim == "char":
            return chr(ord('a') + (n % 26))
        return str(n)

    def char_seq_value(self, field_name: str, type_str: str, module: str) -> str:
        n = self.next_n()
        bound = self.char_seq_bound(type_str, module)
        text = f"sample{n}"
        if bound is not None and len(text) > bound:
            text = text[:bound]
        return text

    def enum_value(self, e: "gta.EnumDecl") -> str:
        return e.values[0] if e.values else "0"

    def struct_value(self, sd: "gta.StructDecl", depth: int) -> str:
        if depth > self.max_depth:
            return "{}"  # cycle/too-deep guard
        parts = [f"{f.name}={self.field_value(f, sd.module, depth + 1)}" for f in sd.fields]
        return "{" + ";".join(parts) + "}"

    def field_value(self, f: "gta.Field", module: str, depth: int) -> str:
        elem_type_str = f.element_type or f.raw_type
        elem_kind = self.registry.get_type_kind(elem_type_str, module)

        if f.is_sequence or f.is_array:
            count = int(f.array_len) if (f.is_array and f.array_len and f.array_len.isdigit()) else self.seq_elems
            count = min(count, self.array_cap) if f.is_array else min(count, self.seq_elems)
            count = max(count, 0)
            elems = [self._elem_value(elem_kind, elem_type_str, module, depth) for _ in range(count)]
            return "[" + ",".join(elems) + "]"

        return self._scalar_value(f.raw_type, elem_kind, f.name, module, depth)

    def _elem_value(self, kind: str, type_str: str, module: str, depth: int) -> str:
        return self._scalar_value(type_str, kind, "elem", module, depth)

    def _scalar_value(self, type_str: str, kind: str, field_name: str, module: str, depth: int) -> str:
        if kind == "struct":
            canonical = self.registry.resolve_type_name(type_str) if "::" not in type_str else type_str.replace("::", "_")
            sd = self.registry.structs.get(canonical) or self._find_struct(type_str, module)
            if sd is None:
                return "{}"
            return self.struct_value(sd, depth)
        if kind == "enum":
            e = self._find_enum(type_str, module)
            return self.enum_value(e) if e else "0"
        if kind == "char_seq":
            return self.char_seq_value(field_name, type_str, module)
        if kind == "string":
            return f"text{self.next_n()}"
        base_prim = self.registry.get_base_primitive_type(type_str, module)
        if base_prim:
            return self.primitive_value(base_prim)
        return "0"  # unknown/unsupported — leave a harmless placeholder

    def _find_struct(self, type_str: str, module: str) -> Optional["gta.StructDecl"]:
        canonical = type_str.replace("::", "_") if "::" in type_str else f"{module}_{type_str}"
        return self.registry.structs.get(canonical)

    def _find_enum(self, type_str: str, module: str) -> Optional["gta.EnumDecl"]:
        canonical = type_str.replace("::", "_") if "::" in type_str else f"{module}_{type_str}"
        return self.registry.enums.get(canonical)


# ---------------------------------------------------------------------
# Command formatting
# ---------------------------------------------------------------------

def format_shell(topic: str, payload_parts: List[str]) -> str:
    """Matches the hand-written multi-line style with `\\`-continuations —
    readable in a shell/terminal, NOT valid inside a DDS_TYPED.SCRIPT file
    (see format_script for that)."""
    lines = [f"DDS_TYPED.CMD > PUBLISH {topic} \\"]
    for i, part in enumerate(payload_parts):
        is_last = i == len(payload_parts) - 1
        sep = "" if is_last else ";\\"
        lines.append(f"    {part}{sep}")
    return "\n".join(lines)


def format_oneline(topic: str, payload_parts: List[str]) -> str:
    return f"DDS_TYPED.CMD > PUBLISH {topic} " + ";".join(payload_parts)


def format_script(topic: str, payload_parts: List[str]) -> str:
    """One line, no `DDS_TYPED.CMD` prefix and no backslashes — this is
    what a `DDS_TYPED.SCRIPT <file>` line actually has to look like (see
    that command's doc comment: 'each non-empty, non-#-comment line in the
    file is exactly one DDS_TYPED.CMD argument string')."""
    return f"> PUBLISH {topic} " + ";".join(payload_parts)


FORMATTERS = {"shell": format_shell, "oneline": format_oneline, "script": format_script}


# ---------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--family", "--name", dest="family", required=True, metavar="BASE_NAME",
                     help="Base name of a topic family (e.g. 'Alarms'), auto-discovered the same "
                          "way gen_typed_adapter.py --family does.")
    ap.add_argument("--idl-dir", dest="idl_dirs", action="append", default=None,
                     help="Directory to search for the family's IDL files (default: current "
                          "directory). May be given multiple times.")
    ap.add_argument("--topic", metavar="SUBSTRING",
                     help="Only emit commands for topics whose struct name or resolved wire "
                          "topic name contains this substring (case-insensitive).")
    ap.add_argument("--format", choices=sorted(FORMATTERS.keys()), default="shell",
                     help="'shell' (default): multi-line with \\ continuations, for pasting into "
                          "a terminal. 'oneline': same content, single line. 'script': bare "
                          "'> PUBLISH ...' lines with no prefix/continuations, ready to drop "
                          "straight into a DDS_TYPED.SCRIPT file.")
    ap.add_argument("--seed", type=int, default=1, help="Seed for sample values (default: 1). "
                          "Same seed -> same output, for reproducible test scripts.")
    ap.add_argument("--seq-elems", type=int, default=2, help="Sample element count for an "
                          "unbounded sequence<T> field (default: 2).")
    ap.add_argument("--array-cap", type=int, default=3, help="Max sample elements to emit for a "
                          "fixed-size array field, even if its declared length is larger "
                          "(default: 3) — keeps output readable; decode() leaves the rest zeroed.")
    ap.add_argument("--max-depth", type=int, default=8, help="Recursion guard for deeply/circularly "
                          "nested structs (default: 8).")
    ap.add_argument("-o", "--output", help="Write to this file instead of stdout.")
    args = ap.parse_args()

    search_dirs = args.idl_dirs or ["."]
    family_files = gta.discover_family_files(args.family, search_dirs)

    with open(family_files["topic_names"]) as f:
        topic_names = gta.parse_const_string_map(f.read())

    merged = gta.TypeRegistry()
    idl_files = [family_files["ldm_common"], family_files["psm"]]
    for idl_file in idl_files:
        with open(idl_file) as f:
            text = f.read()
        import re
        mod_match = re.search(r"module\s+(\w+)\s*{", text)
        mod = mod_match.group(1) if mod_match else os.path.splitext(os.path.basename(idl_file))[0]
        merged.merge(gta.parse_idl(text, mod))

    gen = SampleGen(merged, args.seed, args.seq_elems, args.array_cap, args.max_depth)
    formatter = FORMATTERS[args.format]

    out_lines = []
    n_emitted = 0
    for name, sd in merged.structs.items():
        if not sd.is_topic:
            continue
        topic = sd.topic_name or topic_names.get(sd.name)
        if not topic:
            print(f"warning: no resolved topic name for struct '{sd.name}', skipping "
                  f"(is it missing from {family_files['topic_names']}?)", file=sys.stderr)
            continue
        if args.topic:
            needle = args.topic.lower()
            if needle not in sd.name.lower() and needle not in topic.lower():
                continue

        payload_parts = [f"{f.name}={gen.field_value(f, sd.module, 1)}" for f in sd.fields]
        out_lines.append(formatter(topic, payload_parts))
        n_emitted += 1

    if n_emitted == 0:
        print("warning: no matching topics found", file=sys.stderr)

    text = "\n\n".join(out_lines) + "\n"
    if args.output:
        with open(args.output, "w") as f:
            f.write(text)
        print(f"wrote {args.output}: {n_emitted} PUBLISH command(s)", file=sys.stderr)
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
