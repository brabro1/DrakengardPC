"""Build DrakengardPC from the player's own disc image (used by the launcher).

Usage: python -I tools/build_game.py --iso "<Drakengard (USA).iso>"
       python -I tools/build_game.py --launcher     (build only the launcher)
       python -I tools/build_game.py --check        (report the build tools)

Steps: find the MSYS2 CLANG64 tools, fetch the GPU renderer source (git submodule), build the
recompiler, translate the game from the disc (tools/recomp.py), compile DrakengardPC.exe.
Lines starting with "@@" are progress markers for the launcher:
  @@step <n>/<total> <text>    @@error <text>    @@done
--check only reports which tools are present (@@tool <name> <ok|missing> [path]).
Everything is written under build/ and work/ in this folder; the ISO is only read.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PACKAGES = ["mingw-w64-clang-x86_64-toolchain", "mingw-w64-clang-x86_64-cmake",
            "mingw-w64-clang-x86_64-ninja", "mingw-w64-clang-x86_64-pkgconf",
            "mingw-w64-clang-x86_64-ffmpeg"]
TOTAL = 5


def say(line: str) -> None:
    print(line, flush=True)


def clang64_bin() -> str | None:
    candidates = [os.environ.get("MSYS2_CLANG64_BIN", "")]
    on_path = shutil.which("clang++")
    if on_path:
        candidates.append(os.path.dirname(on_path))
    for drive in "CDEFG":
        candidates.append(f"{drive}:\\msys64\\clang64\\bin")
    for c in candidates:
        if c and os.path.isfile(os.path.join(c, "clang++.exe")):
            return c
    return None


def msys2_root() -> str | None:
    for drive in "CDEFG":
        root = f"{drive}:\\msys64"
        if os.path.isfile(os.path.join(root, "usr", "bin", "bash.exe")):
            return root
    return None


def check() -> int:
    bin_dir = clang64_bin()
    tools = {
        "msys2": msys2_root(),
        "clang": bin_dir,
        "cmake": bin_dir if bin_dir and os.path.isfile(os.path.join(bin_dir, "cmake.exe")) else None,
        "ninja": bin_dir if bin_dir and os.path.isfile(os.path.join(bin_dir, "ninja.exe")) else None,
        "ffmpeg": bin_dir if bin_dir and any(n.startswith("avcodec-") for n in os.listdir(bin_dir)) else None,
        "git": shutil.which("git"),
        "python": sys.executable,
    }
    for name, where in tools.items():
        say(f"@@tool {name} {'ok' if where else 'missing'} {where or ''}".rstrip())
    return 0 if all(tools.values()) else 1


def run(cmd: list[str], env: dict) -> None:
    say("+ " + " ".join(f'"{c}"' if " " in c else c for c in cmd))
    # Windows looks programs up in this process's PATH, not the child's: resolve them here.
    exe = shutil.which(cmd[0], path=env.get("PATH"))
    if not exe:
        raise RuntimeError(f"{cmd[0]} was not found")
    try:
        proc = subprocess.run([exe] + cmd[1:], cwd=ROOT, env=env)
    except OSError as e:
        raise RuntimeError(f"could not run {cmd[0]}: {e}") from e
    if proc.returncode != 0:
        raise RuntimeError(f"command failed ({proc.returncode}): {cmd[0]}")


def install_packages() -> bool:
    """Installs the CLANG64 compiler, CMake, Ninja and FFmpeg into an existing MSYS2."""
    root = msys2_root()
    if not root:
        return False
    say("@@step 0 Installing the build tools into MSYS2 (about 2 GB download, once)")
    bash = os.path.join(root, "usr", "bin", "bash.exe")
    proc = subprocess.run([bash, "-lc", "pacman -S --needed --noconfirm " + " ".join(PACKAGES)])
    return proc.returncode == 0


def tool_env() -> dict | None:
    if not (clang64_bin() and os.path.isfile(os.path.join(clang64_bin(), "cmake.exe"))):
        install_packages()
    bin_dir = clang64_bin()
    if not bin_dir or not os.path.isfile(os.path.join(bin_dir, "cmake.exe")):
        return None
    env = dict(os.environ)
    env["PATH"] = bin_dir + os.pathsep + env.get("PATH", "")
    env["MSYS2_CLANG64_BIN"] = bin_dir
    return env


def configure_game(env: dict) -> None:
    run(["cmake", "-S", ".", "-B", "build/game", "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
         "-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++",
         "-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG", "-DFETCHCONTENT_BASE_DIR=build/_deps"], env)


def fetch_renderer(env: dict) -> bool:
    if os.path.isfile(os.path.join(ROOT, "external", "parallel-gs", "CMakeLists.txt")):
        return True
    if not os.path.isdir(os.path.join(ROOT, ".git")):
        say("@@error This folder is not a git clone, so the GPU renderer source cannot be fetched. "
            "Download DrakengardPC with: git clone --recursive https://github.com/brabro1/DrakengardPC.git")
        return False
    run(["git", "submodule", "update", "--init", "--recursive"], env)
    return True


def build_launcher() -> int:
    env = tool_env()
    if not env:
        say("@@error The build tools are missing: install MSYS2 from https://www.msys2.org (see README, step 1).")
        return 2
    try:
        say("@@step 1/2 Configuring")
        if not fetch_renderer(env):
            return 2
        configure_game(env)
        say("@@step 2/2 Building DrakengardPC Launcher.exe")
        run(["cmake", "--build", "build/game", "--target", "DrakengardPCLauncher"], env)
    except RuntimeError as e:
        say(f"@@error {e}")
        return 1
    say("Launcher: " + os.path.join(ROOT, "build", "game", "DrakengardPC Launcher.exe"))
    say("@@done")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--launcher", action="store_true", help="build only DrakengardPC Launcher.exe")
    args = ap.parse_args()
    if args.check:
        return check()
    if args.launcher:
        return build_launcher()
    if not args.iso or not os.path.isfile(args.iso):
        say(f"@@error Disc image not found: {args.iso}")
        return 2

    env = tool_env()
    if not env:
        say("@@error The MSYS2 CLANG64 build tools are missing. Install them from the launcher's Build page.")
        return 2
    try:
        say(f"@@step 1/{TOTAL} Getting the GPU renderer source")
        if not fetch_renderer(env):
            return 2

        say(f"@@step 2/{TOTAL} Building the recompiler")
        if not os.path.isfile(os.path.join(ROOT, "build", "ps2recomp", "build.ninja")):
            run(["cmake", "-S", "external/PS2Recomp", "-B", "build/ps2recomp", "-G", "Ninja",
                 "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++",
                 "-DCMAKE_C_FLAGS=-msse4.1", "-DCMAKE_CXX_FLAGS=-msse4.1", "-DPS2X_BUILD_STUDIO=OFF",
                 "-DPS2X_ENABLE_SCCACHE=OFF", "-DFETCHCONTENT_BASE_DIR=build/_deps"], env)
        run(["cmake", "--build", "build/ps2recomp", "--target", "ps2_recomp", "ps2_analyzer"], env)

        say(f"@@step 3/{TOTAL} Translating the game from your disc")
        run([sys.executable, "-I", "tools/recomp.py", args.iso], env)

        say(f"@@step 4/{TOTAL} Configuring the game build")
        # Re-run configure: the game target only exists once the translated code is present.
        configure_game(env)

        say(f"@@step 5/{TOTAL} Compiling the game (the first time takes 10-20 minutes)")
        run(["cmake", "--build", "build/game", "--target", "DrakengardPC"], env)
    except RuntimeError as e:
        say(f"@@error {e}")
        return 1
    say("@@done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
