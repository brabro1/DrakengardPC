"""Automated boot regression run for the native build.

Usage: python -I tools/boot_test.py <Drakengard (USA).iso> [--seconds 330] [--exe build/game/DrakengardPC.exe]

Boots with scripted input (title -> Start -> New Game -> Yes), dumps a frame every
60 presented frames into work/boot_test/frames, and checks evidence from the log:

  boot      IRX modules loaded and archives found at real sectors
  title     the game kept presenting frames past the title-screen tick
  progress  the guest VSync clock kept advancing until the end of the run
  stall     no [guest-branch:missing-target] and the last samples are not frozen

Prints a PASS/FAIL line per check and the final [run:sample] lines. Frames are
game-derived and stay under work/ (never committed).
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = "1300:start:10,1600:cross:10,1800:left:10,1950:cross:10"
TITLE_TICK = 1093


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("iso")
    ap.add_argument("--seconds", type=int, default=330)
    ap.add_argument("--exe", default=os.path.join(ROOT, "build", "game", "DrakengardPC.exe"))
    args = ap.parse_args()

    out_dir = os.path.join(ROOT, "work", "boot_test")
    frames = os.path.join(out_dir, "frames")
    shutil.rmtree(out_dir, ignore_errors=True)
    os.makedirs(frames)
    env = dict(os.environ)
    on_path = shutil.which("clang")
    for candidate in (os.environ.get("MSYS2_CLANG64_BIN", ""), os.path.dirname(on_path) if on_path else "",
                      r"C:\msys64\clang64\bin"):
        if candidate and os.path.isdir(candidate):
            env["PATH"] = candidate + os.pathsep + env.get("PATH", "")
            break
    env["PS2X_PAD_SCRIPT"] = SCRIPT
    env["PS2X_FRAME_DUMP_DIR"] = frames
    env["PS2X_FRAME_DUMP_EVERY"] = "60"
    log_path = os.path.join(out_dir, "run.log")
    with open(log_path, "w", encoding="utf-8", errors="replace") as log:
        subprocess.call([args.exe, "--iso", args.iso, "--data", os.path.join(ROOT, "work"),
                         "--max-seconds", str(args.seconds)], stdout=log, stderr=subprocess.STDOUT, env=env,
                        timeout=args.seconds + 120)
    text = open(log_path, encoding="utf-8", errors="replace").read()

    samples = re.findall(r"\[run:sample\] t=(\d+)s vsync=(\d+) \((\d+)/s\)[^\[]*", text)
    sample_lines = re.findall(r"\[run:sample\][^\[\n]*", text)
    ticks = sorted(int(m) for m in re.findall(r"frame_\d+_tick(\d+)\.png", " ".join(os.listdir(frames))))

    checks = {
        "boot": text.count("[SIF module] load-emulated") >= 8 and "[sceCdSearchFile:ok] path=\"\\IMAGE.BIN;1\"" in text,
        "title": bool(ticks) and ticks[-1] > TITLE_TICK,
        "progress": len(samples) >= 2 and int(samples[-1][1]) > int(samples[-2][1]),
        "stall": "missing-target" not in text and not (len(samples) >= 2 and samples[-1][2] == "0"),
    }
    for name, ok in checks.items():
        print(f"{'PASS' if ok else 'FAIL'}  {name}")
    print(f"frames dumped: {len(ticks)} (last tick {ticks[-1] if ticks else '-'})")
    for line in sample_lines[-3:]:
        print(line.strip())
    print(f"log: {log_path}")
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
