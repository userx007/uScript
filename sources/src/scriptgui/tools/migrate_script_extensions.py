#!/usr/bin/env python3
"""Migrate legacy script files to the .main / .comm extension scheme.

  .main  scripts run directly by the interpreter (and the only INCLUDE targets)
  .comm  communication scripts run by plugins (PLUGIN.SCRIPT <file>, PLUGIN.CMD script <file>)

Earlier versions accepted any extension (usually .txt, .scr, .script).  This tool finds such files
below the given folders, decides what each one is from how it is *used*, renames it and rewrites
the references to it.  It is a DRY RUN unless --apply is given.

Classification (first match wins):
  1. target of an INCLUDE "..." directive                  -> .main
  2. target of PLUGIN.SCRIPT <file> / PLUGIN.<CMD> script <file>   -> .comm
  3. contains LOAD_PLUGIN, or itself references comm scripts / includes  -> .main
  4. anything else                                          -> reported, NOT renamed (decide by hand)
A file that is used both ways is reported as a conflict and left alone.  Existing targets are never
overwritten.  Also renames setup.bat -> setup.cmd (the Windows setup script name).

usage:  migrate_script_extensions.py [--apply] [--git] DIR [DIR ...]
"""
import argparse, os, re, subprocess, sys

LEGACY = {".txt", ".scr", ".script"}
SKIP_NAMES = {"cmakelists.txt", "readme.txt", "license.txt", "requirements.txt", "changelog.txt"}
SKIP_DIRS = {".git", "build", "third_party", "node_modules", "__pycache__"}

RX_INCLUDE = re.compile(r'^\s*INCLUDE\s+"([^"]+)"')
RX_SCRIPT_CMD = re.compile(r'\b[A-Z][A-Z0-9_]*(?::\d+)?\.SCRIPT\s+("?)([^\s"]+)\1')
RX_SCRIPT_ARG = re.compile(r'\b[A-Z][A-Z0-9_]*(?::\d+)?\.[A-Z][A-Z0-9_]*\s+script\s+("?)([^\s"]+)\1')


def iter_files(roots):
    for root in roots:
        for dp, dn, fn in os.walk(root):
            dn[:] = [d for d in dn if d not in SKIP_DIRS]
            for f in fn:
                yield os.path.join(dp, f)


def read(p):
    try:
        with open(p, encoding="utf-8", errors="replace", newline="") as fh:
            return fh.read()
    except OSError:
        return ""


def refs_in(text):
    """Yield (kind, ref_text, line_index) for every reference in a script's text."""
    for i, line in enumerate(text.splitlines()):
        code = line.split("#", 1)[0]
        m = RX_INCLUDE.match(code)
        if m:
            yield "include", m.group(1), i
        for rx in (RX_SCRIPT_CMD, RX_SCRIPT_ARG):
            for m in rx.finditer(code):
                yield "comm", m.group(2), i


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dirs", nargs="+")
    ap.add_argument("--apply", action="store_true", help="really rename / rewrite (default: dry run)")
    ap.add_argument("--git", action="store_true", help="use 'git mv' for the renames")
    a = ap.parse_args()

    files = [p for p in iter_files(a.dirs)]
    by_real = {os.path.realpath(p): p for p in files}
    script_like = [p for p in files
                   if os.path.splitext(p)[1].lower() in (LEGACY | {".main", ".comm"})
                   and os.path.basename(p).lower() not in SKIP_NAMES]

    used_as = {}      # real path -> set of {"main","comm"}
    refs = []         # (referencing file, kind, ref_text, line_idx, resolved real path)
    has_load_plugin = set()
    for p in script_like:
        text = read(p)
        if re.search(r"^\s*LOAD_PLUGIN\b", text, re.M):
            has_load_plugin.add(os.path.realpath(p))
        for kind, ref, li in refs_in(text):
            target = os.path.realpath(os.path.join(os.path.dirname(p), ref))
            if target in by_real:
                refs.append((p, kind, ref, li, target))
                used_as.setdefault(target, set()).add("main" if kind == "include" else "comm")
    referencing = {os.path.realpath(r[0]) for r in refs}

    plan, report = {}, []   # real path -> new real path
    for p in script_like:
        rp = os.path.realpath(p)
        ext = os.path.splitext(p)[1].lower()
        if ext not in LEGACY:
            continue
        uses = used_as.get(rp, set())
        if len(uses) > 1:
            report.append(("CONFLICT (used as both main and comm)", p)); continue
        if uses:
            kind = next(iter(uses))
        elif rp in has_load_plugin or rp in referencing:
            kind = "main"
        else:
            report.append(("UNKNOWN (not referenced, no LOAD_PLUGIN) - rename by hand", p)); continue
        new = os.path.splitext(p)[0] + "." + kind
        if os.path.exists(new):
            report.append(("TARGET EXISTS - skipped", p + " -> " + new)); continue
        plan[rp] = (p, new, kind)

    # setup.bat -> setup.cmd
    for p in files:
        if os.path.basename(p).lower() == "setup.bat":
            new = os.path.join(os.path.dirname(p), "setup.cmd")
            if not os.path.exists(new):
                plan[os.path.realpath(p)] = (p, new, "setup")
            else:
                report.append(("TARGET EXISTS - skipped", p + " -> " + new))

    print(("APPLYING" if a.apply else "DRY RUN (use --apply to make these changes)") + "\n")
    for _, (old, new, kind) in sorted(plan.items(), key=lambda kv: kv[1][0]):
        print(f"  rename [{kind:5}] {old}  ->  {os.path.basename(new)}")
    # reference rewrites (only references whose target is being renamed)
    rewrites = {}   # referencing file -> list of (line_idx, old_ref, new_ref)
    for p, kind, ref, li, target in refs:
        if target in plan:
            new_ref = os.path.splitext(ref)[0] + os.path.splitext(plan[target][1])[1]
            rewrites.setdefault(p, []).append((li, ref, new_ref))
    for p, items in sorted(rewrites.items()):
        for li, o, n in items:
            print(f"  rewrite {p}:{li + 1}  {o}  ->  {n}")
    for why, what in report:
        print(f"  ! {why}: {what}")
    print(f"\n{len(plan)} file(s) to rename, {sum(len(v) for v in rewrites.values())} reference(s) to rewrite, {len(report)} need attention")

    if not a.apply:
        return 0
    # rewrite references first (paths still valid), then rename
    for p, items in rewrites.items():
        text = read(p)
        lines = text.splitlines(True)
        for li, o, n in items:
            lines[li] = lines[li].replace(o, n, 1)
        with open(p, "w", encoding="utf-8", newline="") as fh:
            fh.write("".join(lines))
    for _, (old, new, _k) in plan.items():
        if a.git:
            subprocess.check_call(["git", "mv", old, new])
        else:
            os.rename(old, new)
    return 1 if report else 0


if __name__ == "__main__":
    sys.exit(main())
