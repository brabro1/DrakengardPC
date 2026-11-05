"""Regenerate the recompiled game code from the user's own disc image.

Usage: python -I tools/recomp.py <Drakengard (USA).iso> [--work work] [--tools build/ps2recomp]

Steps (each output lands under <work>/, which is git-ignored):
  1. inspect_disc.py   read SYSTEM.CNF, extract the boot ELF, list the disc
  2. verify            refuse any ELF other than SLUS-20732 v1.00
  3. funcmap.py        function boundaries from .eh_frame/linkonce/references
  4. ps2_analyzer      SCE SDK signature names, jump tables, MMIO sites
  5. make_recomp_config.py   merge 3+4 into drakengard.toml
  6. ps2_recomp        translate every function to C++ in <work>/recomp/generated

The recompiler binaries must already be built (see README "Build").
"""
from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
EXPECTED_ELF_SHA1 = "652d95f1cc7f788e219948d52fc21ba2fffaefaa"


def ensure_toolchain_dlls() -> None:
    """MinGW-built tools load libc++/FFmpeg DLLs from the MSYS2 CLANG64 bin directory."""
    if os.name != "nt":
        return
    on_path = shutil.which("clang")
    candidates = [os.environ.get("MSYS2_CLANG64_BIN", ""), os.path.dirname(on_path) if on_path else "",
                  r"C:\msys64\clang64\bin"]
    for c in candidates:
        if c and os.path.isfile(os.path.join(c, "libc++.dll")):
            if c.lower() not in os.environ.get("PATH", "").lower():
                os.environ["PATH"] = c + os.pathsep + os.environ.get("PATH", "")
            return
    print("warning: MSYS2 CLANG64 bin not found; set MSYS2_CLANG64_BIN if the tools fail to start")


def run(cmd: list[str], log: str | None = None, cwd: str | None = None) -> None:
    print("+", " ".join(cmd), flush=True)
    if log:
        with open(log, "w") as out:
            rc = subprocess.call(cmd, stdout=out, stderr=subprocess.STDOUT, cwd=cwd)
    else:
        rc = subprocess.call(cmd, cwd=cwd)
    if rc != 0:
        sys.exit(f"step failed with exit code {rc}: {' '.join(cmd)}" + (f" (log: {log})" if log else ""))


def sync_generated(src: str, dst: str) -> None:
    """Mirror src into dst, rewriting only files whose bytes changed.

    ps2_recomp rewrites every file on each run; keeping untouched files' timestamps
    lets the game build recompile only what a config change actually affected.
    """
    os.makedirs(dst, exist_ok=True)
    new = set(os.listdir(src))
    changed = added = removed = 0
    for name in new:
        s, d = os.path.join(src, name), os.path.join(dst, name)
        with open(s, "rb") as f:
            data = f.read()
        if os.path.exists(d):
            with open(d, "rb") as f:
                if f.read() == data:
                    continue
            changed += 1
        else:
            added += 1
        with open(d, "wb") as f:
            f.write(data)
    for name in set(os.listdir(dst)) - new:
        os.remove(os.path.join(dst, name))
        removed += 1
    shutil.rmtree(src)
    print(f"generated code sync: {changed} changed, {added} added, {removed} removed, "
          f"{len(new) - changed - added} unchanged")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("iso")
    ap.add_argument("--work", default=os.path.join(ROOT, "work"))
    ap.add_argument("--tools", default=os.path.join(ROOT, "build", "ps2recomp"))
    args = ap.parse_args()
    work = os.path.abspath(args.work)
    exe = ".exe" if os.name == "nt" else ""
    analyzer = os.path.join(args.tools, "ps2xAnalyzer", "ps2_analyzer" + exe)
    recomp = os.path.join(args.tools, "ps2xRecomp", "ps2_recomp" + exe)
    for tool in (analyzer, recomp):
        if not os.path.exists(tool):
            sys.exit(f"missing {tool}; build PS2Recomp first (README 'Build')")
    ensure_toolchain_dlls()

    py = [sys.executable, "-I"]
    run(py + [os.path.join(HERE, "inspect_disc.py"), args.iso, os.path.join(work, "disc")],
        log=os.path.join(work, "inspect_disc.log") if os.path.isdir(work) else None)
    elf = os.path.join(work, "disc", "SLUS_207.32")
    sha1 = hashlib.sha1(open(elf, "rb").read()).hexdigest()
    if sha1 != EXPECTED_ELF_SHA1:
        sys.exit(f"unsupported boot ELF (sha1 {sha1}); only Drakengard (USA) SLUS-20732 v1.00 is supported")
    print(f"boot ELF verified: sha1 {sha1}")

    run(py + [os.path.join(HERE, "funcmap.py"), elf, os.path.join(work, "funcmap")])
    rec_dir = os.path.join(work, "recomp")
    os.makedirs(rec_dir, exist_ok=True)
    run([analyzer, os.path.relpath(elf, rec_dir), "analyzer.toml"], log=os.path.join(rec_dir, "analyzer.log"), cwd=rec_dir)
    run(py + [os.path.join(HERE, "make_recomp_config.py"), work])
    gen = os.path.join(rec_dir, "generated")
    gen_new = os.path.join(rec_dir, "generated_new")
    if os.path.isdir(gen_new):
        shutil.rmtree(gen_new)
    run([recomp, "drakengard.toml"], log=os.path.join(rec_dir, "recomp.log"), cwd=rec_dir)
    sync_generated(gen_new, gen)
    run(py + [os.path.join(HERE, "vu_recomp.py"), elf, os.path.join(rec_dir, "vu1")])
    with open(os.path.join(rec_dir, "recomp.log")) as f:
        tail = f.read().split("========== PS2Recomp report ==========")[-1]
    print(tail.strip())
    print(f"\ngenerated code: {gen}\nnext: configure and build the game (README 'Build')")
    return 0


if __name__ == "__main__":
    sys.exit(main())
