"""Merge the evidence function map with ps2_analyzer output into a ps2_recomp config.

Usage: python -I tools/make_recomp_config.py <work_dir>

Reads  <work_dir>/disc/SLUS_207.32
       <work_dir>/funcmap/funcmap_full.csv   (tools/funcmap.py)
       <work_dir>/recomp/analyzer.toml       (ps2_analyzer: SCE signature names,
                                              jump tables, MMIO sites)
Writes <work_dir>/recomp/funcmap_named.csv   Name,Start,End,Size
       <work_dir>/recomp/drakengard.toml     ps2_recomp configuration
       <work_dir>/recomp/merge_report.txt

Names: SCE/libc signature matches win, then the linkonce mangled name, then
sub_XXXXXXXX. Duplicate names get an address suffix so every C++ symbol is unique.
"""
from __future__ import annotations

import bisect
import csv
import os
import re
import sys
import tomllib


def ident(name: str) -> str:
    s = re.sub(r"[^A-Za-z0-9_]", "_", name)
    if not s or s[0].isdigit():
        s = "_" + s
    return s


def selector_split(sel: str) -> tuple[str, int | None]:
    if "@" in sel:
        name, addr = sel.split("@", 1)
        return name, int(addr, 16)
    return sel, None


def main() -> int:
    work = os.path.abspath(sys.argv[1])
    elf = os.path.join(work, "disc", "SLUS_207.32")
    rec_dir = os.path.join(work, "recomp")
    analyzer = tomllib.load(open(os.path.join(rec_dir, "analyzer.toml"), "rb"))
    general = analyzer["general"]

    funcs = {}
    with open(os.path.join(work, "funcmap", "funcmap_full.csv")) as f:
        for row in csv.DictReader(f):
            s = int(row["Start"], 16)
            funcs[s] = {"end": int(row["End"], 16), "ev": row["Evidence"], "mangled": row["MangledName"], "sce": ""}

    report = []
    starts = sorted(funcs)
    sce_named = sce_new = sce_conflict = 0
    kept: dict[str, list[str]] = {"stubs": [], "entry_points": []}
    for key in ("stubs", "entry_points"):
        for sel in general.get(key, []):
            name, addr = selector_split(sel)
            if addr is None:
                kept[key].append(sel)
                continue
            if addr in funcs:
                funcs[addr]["sce"] = funcs[addr]["sce"] or name
                sce_named += 1
                kept[key].append(sel)
                continue
            i = bisect.bisect_right(starts, addr) - 1
            host = starts[i] if i >= 0 else None
            if host is not None and host < addr < funcs[host]["end"] and ("F" in funcs[host]["ev"] or "L" in funcs[host]["ev"]):
                # The compiler's own FDE/linkonce range outranks a signature match:
                # drop the selector so no handler is bound to a mid-function address.
                sce_conflict += 1
                report.append(f"dropped {key} selector {name}@0x{addr:08x}: inside exact range "
                              f"0x{host:08x}-0x{funcs[host]['end']:08x}")
                continue
            kept[key].append(sel)
            # Signature match in uncovered code: split the host range here.
            end = funcs[host]["end"] if host is not None and host < addr < funcs[host]["end"] else addr + 4
            if host is not None and host < addr < funcs[host]["end"]:
                funcs[host]["end"] = addr
            funcs[addr] = {"end": end, "ev": "S", "mangled": "", "sce": name}
            starts = sorted(funcs)
            sce_new += 1
    report.insert(0, f"SCE signature names applied to existing starts: {sce_named}, new starts: {sce_new}, "
                     f"conflicts with exact ranges: {sce_conflict}")

    # Keep libvu0 as recompiled guest code. Its functions are plain VU0 macro-mode
    # code that the recompiler translates exactly (PS2 float rules included); the
    # runtime's native replacements use host float math and at least one has a
    # disputed operand order (upstream issue #361). Drakengard's visibility culling
    # runs through sceVu0ApplyMatrix and friends.
    guest_prefixes = ("sceVu0",)
    # ps2_recomp binds runtime handlers by function name as well as by selector, so
    # these functions are renamed with a guest_ prefix in the map and selectors.
    moved = [s for s in kept["stubs"] if selector_split(s)[0].startswith(guest_prefixes)]
    kept["stubs"] = [s for s in kept["stubs"] if s not in moved]
    guest_renamed = set()
    for i, sel in enumerate(kept["entry_points"]):
        name, addr = selector_split(sel)
        if name.startswith(guest_prefixes) and addr is not None:
            kept["entry_points"][i] = f"guest_{name}@0x{addr:08X}"
            guest_renamed.add(addr)
    for sel in moved:
        name, addr = selector_split(sel)
        if addr is not None:
            kept["entry_points"].append(f"guest_{name}@0x{addr:08X}")
            guest_renamed.add(addr)
    for addr in guest_renamed:
        if addr in funcs and funcs[addr]["sce"] and not funcs[addr]["sce"].startswith("guest_"):
            funcs[addr]["sce"] = "guest_" + funcs[addr]["sce"]
    report.append(f"kept as recompiled guest code instead of native stubs: {len(moved)} ({', '.join(guest_prefixes)}*)")

    used: dict[str, int] = {}
    rows = []
    for s in sorted(funcs):
        f = funcs[s]
        base = f["sce"] or (ident(f["mangled"]) if f["mangled"] else f"sub_{s:08x}")
        name = base
        if name in used:
            name = f"{base}_{s:08x}"
        used[name] = s
        rows.append((name, s, f["end"]))

    named_csv = os.path.join(rec_dir, "funcmap_named.csv")
    with open(named_csv, "w", newline="\n") as out:
        out.write("Name,Start,End,Size\n")
        for name, s, e in rows:
            out.write(f"{name},0x{s:08X},0x{e:08X},{e - s}\n")

    def toml_str(v: str) -> str:
        return '"' + v.replace("\\", "/").replace('"', '\\"') + '"'

    lines = [
        "# ps2_recomp configuration for Drakengard (USA) SLUS-20732, ELF CRC 9679D44C.",
        "# Generated by tools/make_recomp_config.py; do not edit by hand.",
        "",
        "[general]",
        f"input = {toml_str(elf)}",
        f"ghidra_output = {toml_str(named_csv)}",
        # tools/recomp.py syncs generated_new -> generated, touching only changed files.
        f"output = {toml_str(os.path.join(rec_dir, 'generated_new'))}",
        "single_file_output = false",
        "patch_syscalls = false",
        "patch_cop0 = true",
        "patch_cache = true",
        "stubs = [",
        *[f"  {toml_str(s)}," for s in kept["stubs"]],
        "]",
        "entry_points = [",
        *[f"  {toml_str(s)}," for s in kept["entry_points"]],
        "]",
        "skip = []",
        "",
        "[mmio]",
        *[f'"{k}" = "{v}"' for k, v in analyzer.get("mmio", {}).items()],
        "",
        "[jump_tables]",
    ]
    for t in analyzer.get("jump_tables", {}).get("table", []):
        lines.append("[[jump_tables.table]]")
        lines.append(f'address = "{t["address"]}"')
        if "base_register" in t:
            br = t["base_register"]
            lines.append(f"base_register = {br}" if isinstance(br, int) else f'base_register = "{br}"')
        lines.append("entries = [")
        for e in t.get("entries", []):
            parts = []
            for k in ("index", "target"):
                if k in e:
                    v = e[k]
                    parts.append(f"{k} = {v}" if isinstance(v, int) else f'{k} = "{v}"')
            lines.append("  { " + ", ".join(parts) + " },")
        lines.append("]")
    with open(os.path.join(rec_dir, "drakengard.toml"), "w", newline="\n") as out:
        out.write("\n".join(lines) + "\n")

    report.append(f"functions written: {len(rows)}; stubs: {len(kept['stubs'])}; "
                  f"entry points: {len(kept['entry_points'])}; "
                  f"jump tables: {len(analyzer.get('jump_tables', {}).get('table', []))}")
    with open(os.path.join(rec_dir, "merge_report.txt"), "w") as out:
        out.write("\n".join(report) + "\n")
    print("\n".join(report[:40]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
