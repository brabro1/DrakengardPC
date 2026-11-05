"""Package DrakengardPC Launcher.exe for a release download.

Usage: python -I tools/package_launcher.py [--version 0.1.0-preview]

Writes dist/DrakengardPC-Launcher-<version>.zip: the launcher (no game code), a short read-me,
the GPL-3.0 license and the licenses of the libraries linked into the launcher. Build the
launcher first (python -I tools/build_game.py --launcher).
"""
from __future__ import annotations

import argparse
import os
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEPS = os.path.join(ROOT, "build", "_deps")

README = """DrakengardPC Launcher {version}
==============================

DrakengardPC turns Drakengard (PlayStation 2, USA release SLUS-20732) into a native Windows PC
game. This launcher contains no game code: it downloads DrakengardPC, builds the PC version
from YOUR OWN disc image on your PC, and starts it.

1. Install these free tools (once):
   - Git:    https://git-scm.com/download/win
   - Python: https://www.python.org/downloads/windows/  (tick "Add python.exe to PATH")
   - MSYS2:  https://www.msys2.org/  (default folder C:\\msys64)
2. Put "DrakengardPC Launcher.exe" in an empty folder (for example Documents\\DrakengardPC)
   and start it. Press "Download DrakengardPC".
3. Game page: choose your Drakengard (USA) disc image (.iso).
4. Build page: "Install the build tools" if any are missing, then "Build the game"
   (15-30 minutes the first time).
5. Press PLAY.

Status: preview. The first mission is playable at full speed; there is no sound yet.
Project page and source code: https://github.com/brabro1/DrakengardPC

Unofficial fan project, not affiliated with Square Enix or Cavia. Drakengard is (c) Square Enix.
DrakengardPC is free software under the GNU GPL v3.0 (LICENSE.txt); its source code is at the
project page above. The libraries inside the launcher keep their own licenses (licenses\\).
"""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", default="0.1.0-preview")
    args = ap.parse_args()

    exe = os.path.join(ROOT, "build", "game", "DrakengardPC Launcher.exe")
    if not os.path.isfile(exe):
        print("Build the launcher first: python -I tools/build_game.py --launcher")
        return 1
    licenses = {
        "licenses/raylib.txt": os.path.join(DEPS, "raylib-src", "LICENSE"),
        "licenses/dear-imgui.txt": os.path.join(DEPS, "imgui-src", "LICENSE.txt"),
        "licenses/rlImGui.txt": os.path.join(DEPS, "rlimgui-src", "LICENSE"),
        "licenses/font-awesome.txt": os.path.join(DEPS, "rlimgui-src", "extras", "FontAwsome_LICENSE.txt"),
    }
    missing = [src for src in licenses.values() if not os.path.isfile(src)]
    if missing:
        print("missing license files:", *missing, sep="\n  ")
        return 1

    out_dir = os.path.join(ROOT, "dist")
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, f"DrakengardPC-Launcher-{args.version}.zip")
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(exe, "DrakengardPC Launcher.exe")
        z.writestr("README.txt", README.format(version=args.version).replace("\n", "\r\n"))
        z.write(os.path.join(ROOT, "LICENSE"), "LICENSE.txt")
        z.write(os.path.join(ROOT, "THIRD_PARTY_NOTICES.md"), "THIRD_PARTY_NOTICES.md")
        for name, src in licenses.items():
            z.write(src, name)
    print(out, os.path.getsize(out), "bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
