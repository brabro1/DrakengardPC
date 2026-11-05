# DrakengardPC

An unofficial native Windows port of **Drakengard** (PlayStation 2, NTSC-U `SLUS-20732`
v1.00), built by statically recompiling the game's original MIPS R5900 executable to C++
with [PS2Recomp](https://github.com/ran-j/PS2Recomp). Not affiliated with Square Enix or
Cavia.

> **No game files are included or distributed.** You need your own disc image of
> Drakengard (USA). Everything derived from it (extracted ELF, generated C++, saves)
> is created locally under `work/` and is never committed.

## Status: early bring-up (title screen and menus reached; no gameplay yet)

What is verified on the reference machine (see [PROGRESS.md](PROGRESS.md)):

- The whole EE executable is translated: 8,315 functions, 0 decode failures, 0 unhandled
  instructions; `DrakengardPC.exe` links all of it.
- The native executable boots the original game code: crt0 → kernel init → the game
  loads all 8 IOP modules from the disc (run by the IOP interpreter), finds its data
  archives at their real disc sectors, registers its interrupt handlers, runs its
  frame/task loop, shows the Square Enix, Cavia and Project Dragonsphere logos, and reaches
  the **title screen**. Controller input works (the game switches the virtual DualShock 2
  to analog + pressure mode). New Game plays the prologue, "Chapter 1 — Beginnings" and the
  mission briefing, loads, and plays the first mission's intro (HUD, radio message,
  objective map).
- **Not yet**: a correct 3D scene in the mission (the ground is drawn as a flat grey area
  and Caim is not visible), playable speed (~2 VSync/s in the mission: interpreted VU1 and a
  single-threaded software GS), audio, movies, saves. There is no playable gameplay. See [PROGRESS.md](PROGRESS.md) for the blockers.

## Requirements

- Windows 10/11 x64, a CPU with AVX2 by default (Haswell / Zen 1 or newer; configure with
  `-DDRAKENGARD_HOST_AVX2=OFF` for SSE4.1-only CPUs). Developed on an i5-12400F, RTX 3060.
- Your own `Drakengard (USA).iso` (Redump MD5 `300f66970511bf6133cf3af11ffd2a13`).
- To build: MSYS2 with the CLANG64 toolchain, CMake, Ninja, FFmpeg; Python 3.11+.

## Build

```bash
pacman -S --needed mingw-w64-clang-x86_64-toolchain mingw-w64-clang-x86_64-cmake mingw-w64-clang-x86_64-ninja mingw-w64-clang-x86_64-pkgconf mingw-w64-clang-x86_64-ffmpeg
```

1. Build the recompiler tools (once):

```bash
cmake -S external/PS2Recomp -B build/ps2recomp -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_FLAGS=-msse4.1 -DCMAKE_CXX_FLAGS=-msse4.1 -DPS2X_BUILD_STUDIO=OFF -DPS2X_ENABLE_SCCACHE=OFF -DFETCHCONTENT_BASE_DIR=build/_deps
```

```bash
cmake --build build/ps2recomp --target ps2_recomp ps2_analyzer ps2x_tests
```

2. Generate the game code from your disc (writes `work/`, ~150 MB of C++):

```bash
python -I tools/recomp.py "D:/ps2/games/Drakengard (USA).iso"
```

3. Build the game (about 10 minutes on 12 threads):

```bash
cmake -S . -B build/game -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ "-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG" -DFETCHCONTENT_BASE_DIR=build/_deps
```

```bash
cmake --build build/game --target DrakengardPC
```

## Run

FFmpeg DLLs come from MSYS2, so put `D:\msys64\clang64\bin` on `PATH` (packaging comes later).

```bash
build/game/DrakengardPC.exe --iso "D:/ps2/games/Drakengard (USA).iso" --data work
```

Controls (keyboard): arrows/WASD = d-pad, X or Space = ✕, C = ○, Z = □, V = △, Enter = Start,
Tab = Select, Q/E = L1/R1, Shift = L2/R2. A connected gamepad is used instead when present.

Options: `--max-seconds N` exits after N seconds (automated boot tests). Environment:
`PS2X_FRAME_DUMP_DIR=<dir>` (and `PS2X_FRAME_DUMP_EVERY=N`) saves presented frames as PNG;
`PS2X_PAD_SCRIPT="1300:start:10,1600:cross:10"` presses buttons at given VSync ticks;
`PS2X_GS_DEBUG_NOFOG=1` ignores the GS fog enable (diagnostic);
`DKG_TRACE_FUNCS=0xADDR,...` (with `DKG_TRACE_MEM`, `DKG_TRACE_SKIP`, `DKG_TRACE_LIMIT`)
logs calls to recompiled functions; `DKG_PROFILE=<file>` writes a sampling profile
(summarise with `tools/profile_report.py`).
Configure with `-DDRAKENGARD_TRACE_CALLS=ON` to log every guest call (slow, huge output).

The launcher verifies the disc's boot ELF (SHA-1 `652d95f1…`) and refuses any other release.

## Tests

```bash
python -I -m unittest discover -s tests -v
```

```bash
cd external/PS2Recomp && ../../build/ps2recomp/ps2xTest/ps2x_tests.exe
```

## Documentation

[ARCHITECTURE.md](ARCHITECTURE.md) · [PROGRESS.md](PROGRESS.md) · [FEATURES.md](FEATURES.md) ·
[COMPATIBILITY.md](COMPATIBILITY.md) · [RESEARCH.md](RESEARCH.md) · [MODDING.md](MODDING.md) ·
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)

## License

Not yet chosen by the project owner. This repository includes PS2Recomp (GPL-3.0) under
`external/PS2Recomp`, so any distributed build must comply with the GPL-3.0. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
