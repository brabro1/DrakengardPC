# DrakengardPC

An unofficial **native Windows port of Drakengard** (PlayStation 2, NTSC-U `SLUS-20732`
v1.00). The game's own code is translated ahead of time into C++ and compiled into a normal
Windows program that runs on your CPU, and its graphics are drawn by your GPU. It is not an
emulator that interprets the game while it runs.

Not affiliated with or endorsed by Square Enix or Cavia.

> **No game files are included.** You need your own disc image of Drakengard (USA). The
> build reads it locally and writes everything derived from it (the extracted program,
> the generated C++, saves) to `work/`, which is never committed or shared.

---

## Status

Early but real: the game boots, the menus work and the first mission runs at **full speed**
on the reference PC (Intel i5-12400F, RTX 3060): the PS2's 60 VSyncs per second, with the game
logic, the vector-unit programs and the GPU renderer each on their own CPU thread.

| Area | State |
|---|---|
| Boot, logos, title screen, menus, New Game, story intro | working, full speed |
| First mission: walking, attacking, HUD, map | working, full speed with ~40% CPU headroom left (13% at the busiest spot measured) |
| Rest of the game (later missions, dragon flight, story, endings) | not yet verified |
| Audio, movies (FMV), memory card saves | not yet |
| High resolution, widescreen, uncapped frame rate, DLSS/FSR | planned (see [docs/FEATURES.md](docs/FEATURES.md)) |

Details, measurements and the next steps are in [docs/PROGRESS.md](docs/PROGRESS.md).

## How it works

The PlayStation 2 has several processors. Each one is handled like this:

| PS2 part | What it does in the game | In DrakengardPC |
|---|---|---|
| EE (main CPU, MIPS R5900) | game logic, AI, physics | **translated to C++** ahead of time (8,315 functions) and compiled natively |
| VU0 / VU1 (vector units) | transforms, lighting, building draw lists | **microprograms translated to C++** ahead of time (53 programs) |
| GS (graphics chip) | rasterising triangles | drawn on the **GPU** with [paraLLEl-GS](https://github.com/Arntzen-Software/parallel-gs) (Vulkan compute) |
| IOP (I/O CPU) | disc, sound and controller drivers | small interpreter (an emulated support component) |
| DMA, timers, interrupts | glue between the chips | C++ runtime ([PS2Recomp](https://github.com/ran-j/PS2Recomp), extended here) |

More in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Requirements

- Windows 10/11, 64-bit; a CPU with AVX2 (Intel Haswell / AMD Zen or newer).
- A GPU with an up-to-date Vulkan driver (tested: NVIDIA RTX 3060; paraLLEl-GS uses Vulkan
  compute shaders).
- Your own `Drakengard (USA).iso` (Redump MD5 `300f66970511bf6133cf3af11ffd2a13`). Other
  releases (PAL, Japanese) are refused.
- To build: [MSYS2](https://www.msys2.org/) with the CLANG64 toolchain, and Python 3.11+.
- About 3 GB of free disk space for `work/` and `build/`.

## Building

All commands run in a Git Bash or MSYS2 shell from the repository folder.

**1. Get the source and the toolchain**

```bash
git clone --recursive https://github.com/brabro1/DrakengardPC.git
```

In an MSYS2 shell:

```bash
pacman -S --needed mingw-w64-clang-x86_64-toolchain mingw-w64-clang-x86_64-cmake mingw-w64-clang-x86_64-ninja mingw-w64-clang-x86_64-pkgconf mingw-w64-clang-x86_64-ffmpeg
```

Then put the CLANG64 tools on `PATH` in the shell you build from (adjust the MSYS2 location):

```bash
export PATH=/c/msys64/clang64/bin:$PATH
```

**2. Build the recompiler (once)**

```bash
cmake -S external/PS2Recomp -B build/ps2recomp -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_FLAGS=-msse4.1 -DCMAKE_CXX_FLAGS=-msse4.1 -DPS2X_BUILD_STUDIO=OFF -DPS2X_ENABLE_SCCACHE=OFF -DFETCHCONTENT_BASE_DIR=build/_deps
```

```bash
cmake --build build/ps2recomp --target ps2_recomp ps2_analyzer ps2x_tests
```

**3. Translate the game from your disc** (writes about 150 MB of C++ to `work/`)

```bash
python -I tools/recomp.py "path/to/Drakengard (USA).iso"
```

**4. Build the game** (about 10 minutes on 12 threads)

```bash
cmake -S . -B build/game -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ "-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG" -DFETCHCONTENT_BASE_DIR=build/_deps
```

```bash
cmake --build build/game --target DrakengardPC
```

## Playing

```bash
build/game/DrakengardPC.exe --iso "path/to/Drakengard (USA).iso" --data work
```

The CLANG64 `bin` folder must be on `PATH` (the game uses its FFmpeg and C++ runtime DLLs).

**Controls.** A connected gamepad is used when present. Keyboard: arrows or WASD = D-pad,
X or Space = ✕, C = ○, Z = □, V = △, Enter = Start, Tab = Select, Q / E = L1 / R1,
Shift = L2 / R2.

**Options.** `--max-seconds N` quits after N seconds. Useful environment variables:

| Variable | Effect |
|---|---|
| `PS2X_GS_RENDERER=cpu` | use the software renderer instead of the GPU |
| `PS2X_GS_SSAA=N` | supersample the GPU renderer N× |
| `PS2X_FRAME_DUMP_DIR=<folder>` | save every 60th frame as PNG (`PS2X_FRAME_DUMP_EVERY=N` to change) |
| `PS2X_PAD_SCRIPT="1300:start:10,..."` | press buttons at given VSync ticks (automated tests) |
| `PS2X_VU_AOT=0` | run vector-unit code in the interpreter (slow; for comparison) |
| `DKG_PROFILE=<file>` | write a sampling profile (`tools/profile_report.py` summarises it) |

Developer diagnostics (tracing, rendering probes, profiling) are listed in [CLAUDE.md](CLAUDE.md).

## Troubleshooting

| Problem | Fix |
|---|---|
| "avcodec-63.dll was not found" | put the MSYS2 `clang64/bin` folder on `PATH` |
| `[disc] unsupported revision` at start-up | only Drakengard (USA) v1.00 is supported (boot ELF SHA-1 `652d95f1…`) |
| black window, Vulkan errors | update the GPU driver, or try `PS2X_GS_RENDERER=cpu` |
| `No generated code in work/recomp/generated` when configuring | run step 3 first |

## Repository layout

| Path | Contents |
|---|---|
| `src/` | the port: start-up, disc reader, game identity check, game-specific overrides |
| `tools/` | the translation pipeline (`recomp.py`, `vu_recomp.py`) and analysis/debug tools |
| `tests/` | pipeline tests |
| `external/PS2Recomp/` | recompiler and PS2 runtime (vendored, with this project's changes) |
| `external/parallel-gs/` | GPU graphics renderer (git submodule) |
| `docs/` | architecture, progress log, features, compatibility, research notes, modding |
| `work/` | created locally from your disc; never committed |

## Tests

```bash
python -I -m unittest discover -s tests -v
```

```bash
cd external/PS2Recomp && ../../build/ps2recomp/ps2xTest/ps2x_tests.exe
```

## Documentation

- [docs/PROGRESS.md](docs/PROGRESS.md): milestones, measurements, session log
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): how the port is built and why
- [docs/FEATURES.md](docs/FEATURES.md): feature matrix (planned / implemented / verified)
- [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md): known issues and tested setups
- [docs/RESEARCH.md](docs/RESEARCH.md): evidence for addresses, formats and findings
- [docs/MODDING.md](docs/MODDING.md): plans for mod support

## License

DrakengardPC is free software under the **GNU General Public License v3.0** ([LICENSE](LICENSE)),
the same license as PS2Recomp, which it includes. paraLLEl-GS is LGPL-3.0-or-later. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for every component and its license.

The license covers this project's source code only. Drakengard and all of its content are the
property of their owners; nothing from the game is distributed here.
