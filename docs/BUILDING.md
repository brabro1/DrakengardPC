# Building and developing DrakengardPC

This page is for people who want to build the port by hand, change it, or help debug it.
If you only want to play, follow the steps in the [README](../README.md): the launcher does
everything below for you (through `tools/build_game.py`).

## How the build works

1. **The recompiler** (`external/PS2Recomp`, vendored with this project's changes) is built
   once.
2. **`tools/recomp.py`** reads your disc image, checks that it is Drakengard (USA) v1.00 (boot
   ELF SHA-1 `652d95f1cc7f788e219948d52fc21ba2fffaefaa`), finds every function in the game's
   main program and translates it to C++ (8,315 functions, about 150 MB). It also translates the
   game's 53 vector-unit (VU0/VU1) microprograms with `tools/vu_recomp.py`. Everything goes to
   `work/`, which is never committed.
3. **The game** is compiled from that generated code plus the runtime into
   `build/game/DrakengardPC.exe`. The toolchain DLLs it needs are copied next to it
   (`cmake/copy_runtime_dlls.cmake`), so it runs without MSYS2 on `PATH`.

The same CMake project also builds **`DrakengardPC Launcher.exe`** (`src/launcher/`, Dear ImGui
on raylib). It contains no game code and builds even before any game code was generated.
`tools/build_game.py` is the scripted version of all steps: `--launcher` builds only the
launcher, `--iso <ISO>` runs the whole pipeline (the launcher's Build page), `--check` lists the
tools. If the CLANG64 packages are missing but MSYS2 is installed, it installs them with
`pacman` first.

## Requirements

- Windows 10/11 x64, a CPU with AVX2 (configure with `-DDRAKENGARD_HOST_AVX2=OFF` for
  SSE4.1-only CPUs), a GPU with a current Vulkan driver.
- [MSYS2](https://www.msys2.org/) with the CLANG64 toolchain, CMake, Ninja and FFmpeg:

```bash
pacman -S --needed mingw-w64-clang-x86_64-toolchain mingw-w64-clang-x86_64-cmake mingw-w64-clang-x86_64-ninja mingw-w64-clang-x86_64-pkgconf mingw-w64-clang-x86_64-ffmpeg
```

- Python 3.11+ and Git for Windows.

Releases: `python -I tools/package_launcher.py --version <v>` packs the launcher, its read-me
and the licenses into `dist/DrakengardPC-Launcher-<v>.zip`.
- About 3 GB free for `work/` and `build/`.

## Manual build

From a Git Bash shell in the repository, with the CLANG64 tools on `PATH` (adjust the MSYS2
location; `MSYS2_CLANG64_BIN` is also honoured by the scripts):

```bash
export PATH=/c/msys64/clang64/bin:$PATH
```

```bash
git submodule update --init external/parallel-gs
```

```bash
cd external/parallel-gs && bash checkout_submodules.sh && cd ../..
```

(Only Granite, volk and Vulkan-Headers are needed. A recursive checkout pulls all of
Granite's third-party code and fails on Windows path-length limits.)

```bash
cmake -S external/PS2Recomp -B build/ps2recomp -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_FLAGS=-msse4.1 -DCMAKE_CXX_FLAGS=-msse4.1 -DPS2X_BUILD_STUDIO=OFF -DPS2X_ENABLE_SCCACHE=OFF -DFETCHCONTENT_BASE_DIR=build/_deps
```

```bash
cmake --build build/ps2recomp --target ps2_recomp ps2_analyzer ps2x_tests
```

```bash
python -I tools/recomp.py "path/to/Drakengard (USA).iso"
```

```bash
cmake -S . -B build/game -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ "-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG" -DFETCHCONTENT_BASE_DIR=build/_deps
```

```bash
cmake --build build/game --target DrakengardPC
```

```bash
build/game/DrakengardPC.exe --iso "path/to/Drakengard (USA).iso" --data work
```

The launcher has a test mode for screenshots: `DKG_LAUNCHER_SCREENSHOT=<png>` and
`DKG_LAUNCHER_PAGE=<0-8>` save one frame of a page and exit.

A full game build takes about 10 minutes on 12 threads. Changing `ps2_runtime.h` or any header
it includes rebuilds all generated code.

Build options: `-DDRAKENGARD_GPU_GS=OFF` (software renderer only), `-DDRAKENGARD_HOST_AVX2=OFF`,
`-DDRAKENGARD_TRACE_CALLS=ON` (logs every guest call; very slow).

## Command line and environment

`DrakengardPC.exe` reads **`DrakengardPC.ini`** (written by the launcher; looked up with
`--settings`, else in the exe folder or up to two folders above it). Every option in it maps to
an environment switch below; a switch already set in the environment wins.

| Option | Effect |
|---|---|
| `--settings <file>` | settings file (default: `DrakengardPC.ini`, see above) |
| `--iso <file>` | disc image (default: from the settings file) |
| `--data <folder>` | where generated data and memory cards live (`work`) |
| `--max-seconds N` | quit after N seconds (automated tests) |
| `PS2X_WINDOW_MODE` / `PS2X_WINDOW_SIZE` | `windowed`/`borderless`/`fullscreen`; `1280x960` |
| `PS2X_ASPECT` / `PS2X_FILTER` / `PS2X_SHOW_FPS` | `4:3`/`stretch`/`pixels`; `linear`/`nearest`; `1` |
| `PS2X_GAMEPAD` / `PS2X_PAD_DEADZONE` / `PS2X_KEYMAP` | `auto`/`off`/`0-3`; `0.15`; `up=265,cross=88,...` (raylib key codes) |
| `PS2X_WINDOW_SCREENSHOT=<png>:<frame>` | save the game window at a host frame (tests) |
| `PS2X_GS_RENDERER=cpu` | software GS renderer instead of the GPU (paraLLEl-GS) |
| `PS2X_GS_SSAA=N` | GPU renderer supersampling (1/2/4/8/16) |
| `PS2X_VU_AOT=0` | interpret VU microprograms instead of running the compiled ones |
| `PS2X_VU1_THREAD=0` | run VIF1/VU1 on the game thread (`=sync`: worker, but wait after every DMA kick) |
| `PS2X_FRAME_DUMP_DIR=<folder>` | save every 60th frame as PNG (folder must exist; `PS2X_FRAME_DUMP_EVERY=N`) |
| `PS2X_PAD_SCRIPT="tick:button[+button][:hold],..."` | scripted button presses at VSync ticks |
| `DKG_PROFILE=<file>` | sampling profile (`DKG_PROFILE_DELAY=<s>`); summarise with `tools/profile_report.py` |

The full list of diagnostics (tracing, GS draw log, VU dumps, pad scripts that reach the first
mission) is in [CLAUDE.md](../CLAUDE.md).

## Tests

```bash
python -I -m unittest discover -s tests -v
```

```bash
cd external/PS2Recomp && ../../build/ps2recomp/ps2xTest/ps2x_tests.exe
```

## Repository layout

| Path | Contents |
|---|---|
| `src/` | the port: start-up, disc reader and identification, settings, game-specific overrides |
| `src/launcher/` | DrakengardPC Launcher |
| `cmake/` | build helpers (copying runtime DLLs) |
| `tools/` | `build_game.py` (used by the launcher), translation pipeline (`recomp.py`, `vu_recomp.py`), analysis/debug tools |
| `tests/` | pipeline tests |
| `external/PS2Recomp/` | recompiler and PS2 runtime (vendored, with this project's changes) |
| `external/parallel-gs/` | GPU graphics renderer (git submodule) |
| `docs/` | architecture, progress log, features, compatibility, research notes, modding |
| `work/` | created locally from your disc; never committed |

## Rules for contributions

- Never commit anything from the disc or generated from it (`work/`, logs, frame dumps,
  saves). Check `git status` before committing.
- Label inferred addresses and symbols as inferred and record the evidence in
  [RESEARCH.md](RESEARCH.md).
- Native replacements of game functions go in `src/overrides/drakengard_overrides.cpp` with a
  reason string; they are logged at start-up.
- Report results with evidence (logs, measurements); see [PROGRESS.md](PROGRESS.md) for the
  format.
