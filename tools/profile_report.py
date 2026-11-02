"""Summarise a DKG_PROFILE sample file per thread and by function.

Usage: python -I tools/profile_report.py <profile.txt> <DrakengardPC.exe> [--top 40] [--threads 2]

Threads are ranked by samples outside ntdll/win32u (time not spent waiting).
Executable addresses (image offsets) are rebased to the file's preferred image
base and resolved with llvm-symbolizer (MSYS2 CLANG64).
"""
from __future__ import annotations

import argparse
import collections
import json
import shutil
import struct
import subprocess
import sys

IDLE_MODULES = {"ntdll.dll", "win32u.dll"}


def image_base(exe: str) -> int:
    with open(exe, "rb") as f:
        data = f.read(4096)
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    optional = pe + 24
    if struct.unpack_from("<H", data, optional)[0] == 0x20B:  # PE32+
        return struct.unpack_from("<Q", data, optional + 24)[0]
    return struct.unpack_from("<I", data, optional + 28)[0]


def symbolize(exe: str, addrs: list[int]) -> dict[int, str]:
    tool = shutil.which("llvm-symbolizer") or r"D:\msys64\clang64\bin\llvm-symbolizer.exe"
    out = subprocess.run([tool, "--obj=" + exe, "--demangle", "--no-inlines", "--output-style=JSON"],
                         input="\n".join(f"0x{a:x}" for a in addrs) + "\n",
                         capture_output=True, text=True).stdout
    names: dict[int, str] = {}
    for line, a in zip([l for l in out.splitlines() if l.strip()], addrs):
        try:
            names[a] = json.loads(line)["Symbol"][0].get("FunctionName") or "??"
        except (ValueError, KeyError, IndexError):
            names[a] = "??"
    return names


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("profile")
    ap.add_argument("exe")
    ap.add_argument("--top", type=int, default=40)
    ap.add_argument("--threads", type=int, default=2)
    args = ap.parse_args()

    exe_hits: dict[str, dict[int, int]] = collections.defaultdict(dict)
    mod_hits: dict[str, dict[str, int]] = collections.defaultdict(dict)
    with open(args.profile) as f:
        for line in f:
            p = line.split()
            if len(p) == 5 and p[0] == "t" and p[2] == "exe":
                exe_hits[p[1]][int(p[3], 16)] = int(p[4])
            elif len(p) == 5 and p[0] == "t" and p[2] == "module":
                mod_hits[p[1]][p[3]] = int(p[4])

    def busy(t: str) -> int:
        return sum(exe_hits[t].values()) + sum(c for m, c in mod_hits[t].items() if m.lower() not in IDLE_MODULES)

    def total(t: str) -> int:
        return sum(exe_hits[t].values()) + sum(mod_hits[t].values())

    ranked = sorted(set(exe_hits) | set(mod_hits), key=busy, reverse=True)
    print("threads (busy = samples outside ntdll/win32u):")
    for t in ranked[:6]:
        print(f"  tid {t}: busy {100 * busy(t) / max(1, total(t)):5.1f}% of {total(t)} samples")

    base = image_base(args.exe)
    for t in ranked[: args.threads]:
        hits = exe_hits[t]
        names = symbolize(args.exe, [base + a for a in sorted(hits)])
        by_func: collections.Counter = collections.Counter()
        for a in sorted(hits):
            by_func[names[base + a]] += hits[a]
        for m, c in mod_hits[t].items():
            by_func[f"[{m}]"] += c
        n = total(t)
        print(f"\nthread {t} ({n} samples):")
        for func, count in by_func.most_common(args.top):
            print(f"  {100 * count / n:5.1f}%  {func[:140]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
