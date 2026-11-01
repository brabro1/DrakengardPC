# Progress

## Milestones

| Milestone | State | Evidence |
|---|---|---|
| A — Inspection and feasibility | **done** | RESEARCH.md: build identity, disc inventory, ELF layout, ISA census, tool audit, architecture choice |
| B — Native execution foundation | **done** | 8,315 functions translated (0 decode failures); `DrakengardPC.exe` executes original game code through IOP init, disc search and interrupt setup into the frame loop (logs below) |
| C — Boot and title screen | in progress | the game's own memory-card warning dialog renders (first real screen, ~4 s after boot); no input yet, so title not reached |
| D — Playable vertical slice | not started | |
| E — Compatibility and timing | not started | timing architecture sketched in ARCHITECTURE.md |
| F — Modern rendering and mods | not started | |
| G — Complete-game verification | not started | |

## Session log

### 2026-11-02 — session 1

**Inspected**
- Workspace empty; Windows 11 Pro 26300; i5-12400F (6C/12T); RTX 3060 (driver 32.0.16.2101);
  31.8 GB RAM; C: 5.3 GB free, D: 86.9 GB free.
- Tools present: git 2.54, gh (logged in as `brabro1`, `repo` scope), Python 3.13, Node 24,
  Java 8. No C/C++ compiler, CMake or Ninja.
- Disc: Redump-verified Drakengard (USA) SLUS-20732 v1.00 (MD5 `300f6697…`); boot ELF SHA-1
  `652d95f1…`, PCSX2 CRC `9679D44C`. Full inventory in RESEARCH.md.
- PS2Recomp, the Bloodborne PC reference, PCSX2's game database and patches, and five
  PS2Recomp-based ports (structure and approach).

**Installed** (on D:, no admin, no license prompts)
- MSYS2 base 2026-09-27 (`D:\msys64`, SHA-256 checked against the release) with CLANG64
  toolchain: clang 23.1.4, lld, CMake 4.4.5, Ninja 1.13.2, FFmpeg (libavcodec 63.1.102).

**Built and ran**
- PS2Recomp tools at `e5636981a` with clang/MinGW (two small CMake fixes for MinGW):
  `ps2x_tests` **446/446 passed**.
- Analysis pipeline (`tools/`): function map 8,268 functions (4,871 with compiler-exact
  ranges; 0 of 4,897 JAL targets inside exact ranges), analyzer matched 708 SDK functions,
  merged config → `ps2_recomp`: 8,442 functions, 8,177 recompiled, 265 native stubs,
  0 decode failures, 0 unhandled instructions, 150 MB C++.
- `DrakengardPC.exe` (79 MB) built from all generated code + runtime.
- Pipeline tests: `python -I -m unittest discover -s tests` — 6/6 passed.

**Boot runs** (each bounded with `--max-seconds`)
1. Run 1: stopped at `pc=0x3064E0` in guest `sceCdLayerSearchFile` — libcdvd retry-binding
   SID `0x80000597` (CDVDFSV search RPC, an IOP BIOS service). Fix: bind it to the native
   `sceCdSearchFile` (`src/overrides/drakengard_overrides.cpp`).
2. Run 2: all six archives found at real LSNs (`IMAGE.BIN` `0xCCA1E` = ISO directory value);
   game draws GS sprite strips and sends VIF1/GIF DMA; then each frame waits ~8 s at
   `0x1270A4` (spin timeout on the frame-done flag `obj+0x124`).
3. Run 3 (diagnostics): registered handlers logged — DMAC 1/2 → `0x27E19C`, INTC 0 (GS) →
   `0x27E1F4`, INTC 9 → `0x12156C`, INTC 11 → `0x324EA0`. Both DMAC and GS handlers drive
   the frame task's DMA state machine (`0x126F84`). The runtime never raised the GS interrupt.
4. Run 4: added `ps2PollGsInterrupt` (raise INTC 0 on unmasked `CSR.FINISH/SIGNAL`; game's
   `IMR=0x7D00`). FINISH interrupts now fire; DMA submissions went from ~6 per 8 s to ~14 per
   2 s; the main thread runs the task loop (`0x27DE1C`, `0x27800C`); the game starts streaming
   64 KB blocks to IOP RAM (`sceSifSetDma` → IOP `0x125180`).
5. Run 5 (75 s, frame dump): one presented frame (boot-time dark clear, tick 5); no later
   frame was latched for presentation.

6. Discovered that PS2Recomp's default "aggressive logs" compile a call tracker into every
   generated function and wrote a 1.2 GB `ps2_log.txt` per run; made opt-in
   (`DRAKENGARD_TRACE_CALLS`). Run 6 with it off: the game presented a frame on almost every
   60 Hz tick (2,129 frames in 40 s), still the dark boot field, then stopped at
   `[guest-branch:missing-target] target=0x142904`.
7. Root cause: a data word equal to `0x00142900` had created a false function start inside
   the function at `0x1428FC` (8 JAL callers), splitting it between a branch and its delay
   slot. `funcmap.py` now accepts data-pointer/materialized-address starts only right after
   a function end (`jr`/`j`/`b` + delay slot, padding, or an exact range end): 128 false
   starts rejected, 8,315 functions, 0 decode failures, pipeline tests 6/6.
8. Run 7 (60 s): no crash. Black → fade-in at tick 185 → **the game's memory-card warning
   dialog** from tick 245 ("There is no memory card (8MB)… Are you sure you want to start the
   game? Yes/No", ✕ Confirm prompt), rendered by the recompiled game code with its own font
   and UI art. The game polls the card (`[MC] GetDir port=0 '/BASLUS-20732/*'`, repeated
   `GetInfo`) and waits for input.

## Main blockers

1. **Input**: no host input reaches the game yet, so the dialog cannot be answered.
2. **Memory card result codes**: the runtime's MC layer answers `GetInfo type=2 free=8192
   format=1 result=0`, yet the game reports no card. The libmc 2810 result semantics the game
   expects need checking.
3. **Presentation**: the window shows the raw 640×512 frame-buffer region (both field buffers)
   instead of the `DISPLAY1` rectangle; interlaced field rendering (640×224 fields) is not
   yet handled.

## Next actions

1. Wire keyboard/controller input to the pad HLE (`scePadRead` stub) and confirm the dialog
   reacts (Yes/No, ✕).
2. Trace the game's memory-card check (`sceMcGetInfo` call sites and how the result/type
   values are interpreted) against libmc semantics; aim for "card present, no save" → title.
3. Fix presentation to crop `DISPLAY1` and merge interlaced fields (compare with PCSX2).
4. Measure timing: presented frames per second vs. 59.94 Hz; then audio (EzSound) and the
   MPEG intro (`MOVIE0UC.BIN` at LSN `0xBE7`).
