# Third-party notices

## Included in this repository

| Component | Version | License | Location | Notes |
|---|---|---|---|---|
| PS2Recomp (ps2xRecomp, ps2xRuntime, ps2xIOP, ps2xAnalyzer, ps2xTest) | commit `e5636981a1c9de368f6b71e819c3dc188dfedbf4` + local changes | GPL-3.0 | `external/PS2Recomp/` (`LICENSE` there) | © ran-j and contributors. Local modifications are commits in this repository. `android/`, `vita/`, `ps2xStudio/` were not vendored. Its embedded SCE symbol database (`sce_symbol_database_data.h`) is part of upstream. |

## Fetched at build time (not in the repository)

Pulled by PS2Recomp's CMake (`FetchContent`) into `build/_deps`:

| Component | Version | License |
|---|---|---|
| raylib | 5.5 | zlib |
| Dear ImGui | v1.92.7-docking | MIT |
| rlImGui | Raylib_5_5 tag | zlib |
| ELFIO | Release_3.12 | MIT |
| toml11 | v4.4.0 | MIT |
| {fmt} | 12.1.0 | MIT |
| libdwarf | v2.2.0 | LGPL-2.1 (library) |
| rabbitizer | 1.14.3 | MIT |
| nlohmann/json | (as pinned by PS2Recomp) | MIT |

## System dependencies (MSYS2 CLANG64, dynamically linked)

| Component | Version | License |
|---|---|---|
| FFmpeg (libavcodec/format/util, swresample, swscale) | libavcodec 63.1.102 | LGPL-2.1+ (MSYS2 build may enable GPL parts; check before redistributing) |
| LLVM libc++ / compiler-rt / MinGW-w64 runtime | clang 23.1.4 | Apache-2.0 WITH LLVM-exception / ZPL / public domain |

## Obligations before distributing any build

- The combined program includes GPL-3.0 code; binaries must ship with corresponding source
  and the GPL-3.0 text, and the project's own license must be GPL-3.0-compatible.
- FFmpeg: ship its license and note the build configuration of the DLLs actually shipped.
- Never ship game data or anything generated from it: the extracted ELF, `work/`,
  generated C++, or binaries that contain the recompiled game code. The intended
  distribution model is source plus a local generation step from each user's own disc;
  whether a binary containing recompiled code can be distributed is unresolved and must
  not be assumed.
- Future SDKs (NVIDIA DLSS/Streamline, AMD FidelityFX) carry their own license terms;
  review before integrating.

## References used (not included)

- **Drakengard1Recompile** by k1gs (MIT, <https://github.com/k1gs/Drakengard1Recompile>,
  revision `6186ea6`, read 2026-11-05) — a PAL (SLES-52322) recompilation on the same PS2Recomp.
  Its findings and patch designs informed session-4 runtime fixes (64-bit REGIMM/BLEZ/BGTZ
  branches, VF0 write discard, VIF0 UNPACK formats, toSPR chain / VU0-source DMA, empty GIF
  chains, INT+FFMD scanout, T8/CT32 CLUT fast path, vectorised bilinear filter). The code here
  was rewritten against our vendored PS2Recomp; the toSPR chain walk follows its
  `ee_spr_chain.h` closely, so its notice applies:

  > MIT License — Copyright (c) 2026 k1gs. Permission is hereby granted, free of charge, to any
  > person obtaining a copy of this software and associated documentation files (the
  > "Software"), to deal in the Software without restriction, including without limitation the
  > rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
  > the Software, and to permit persons to whom the Software is furnished to do so, subject to
  > the following conditions: The above copyright notice and this permission notice shall be
  > included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED
  > "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
  > WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
  > EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  > LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  > CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

- PCSX2 (GPL-3.0) — behavioural reference and game database facts only.
- Bloodborne PC by Supermedo (GPL-2.0) — product reference only; no code used.
