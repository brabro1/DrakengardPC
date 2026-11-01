# Progress

## Milestones

| Milestone | State | Evidence |
|---|---|---|
| A — Inspection and feasibility | **done** | RESEARCH.md: build identity, disc inventory, ELF layout, ISA census, tool audit, architecture choice |
| B — Native execution foundation | **done** | 8,315 functions translated (0 decode failures); `DrakengardPC.exe` executes original game code through IOP init, disc search and interrupt setup into the frame loop (logs below) |
| C — Boot and title screen | **reached, with known issues** | Square Enix → Cavia → Project Dragonsphere logos → title "PRESS START BUTTON"; scripted input opens the main menu and the New Game difficulty prompt (session 2). Known issues: game runs at ~38% speed, window shows both interlaced field buffers, no audio |
| D — Playable vertical slice | blocked | New Game hangs after the fade-out (see Main blockers) |
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

### 2026-11-02 — session 2 (input, memory card, title screen)

All findings came from new tooling: `DKG_TRACE_FUNCS` / `DKG_TRACE_MEM` / `DKG_TRACE_SKIP`
function trace hooks (wrap any recompiled function, log arguments, return value and guest
memory words; no code regeneration needed), `PS2X_PAD_SCRIPT` scripted input, and an
always-on `[run:sample]` position sample every 300 host frames.

1. **Memory card** (game said "There is no memory card"). Traced the game's card state
   machine (`0x2F11E4`, object `0x508300`, state at `+0x6C0`): states went 0→1→2 (PS2 card,
   formatted, 8,192 free)→3, then **8 = no card**. Cause: the game asks GetEntSpace for its
   save folder `/BASLUS-20732` and expects **−4 when the folder does not exist**; the runtime
   always answered 1024, so the game believed a folder existed, then GetDir failed and the
   mismatch became "no card". Fixed GetEntSpace per ps2sdk `mcman_getentspace` (−4 for a
   missing directory, else free entry slots). Also made the first GetInfo after init report
   −1 ("formatted card inserted since last call", libmc.h), unformatted cards −2.
   Result: logos → title instead of the warning.
2. **Speed**: `[run:sample]` shows the guest VSync clock advancing ~23/s instead of 59.94
   (≈38% speed); boot to the title takes ~110 s of host time. Not yet investigated.
3. **IOP heap**: after the third logo the game looped on `sceSifAllocIopHeap(0x40000)`
   failing ("Cannot allocate %d bytes of IOP memory" string at `0x555BB0`). The game needs
   64 K + 128 K + 128 K + 256 K + 256 K; the IOP heap was capped at 832 KB
   (`0x120000–0x1F0000`) and never reused freed blocks (searched only above a high-water
   cursor). Fixed: first-fit allocation, largest-hole `QueryMaxFreeMemSize`, heap base
   `0x90000` (modules here end near `0x5C000`), failure logging. → **title screen** at tick
   ~1093: "Drakengard — PRESS START BUTTON — ©2003, 2004 cavia Inc./SQUARE ENIX".
4. **Input** (Start ignored at the title). The game reads pads every frame into
   `0x342860`/`0x342900`, but its controller manager (`0x128704`) **only accepts port-0 data
   whose controller ID is `0x79`** (DUALSHOCK 2 pressure-sensitive mode) and otherwise
   substitutes neutral data. Two runtime gaps prevented that: the pad mode table listed only
   digital mode (so the game never called `scePadSetMainMode` to switch to analog), and
   pressure mode never reported ID `0x79`. Fixed both (table = {4 digital, 7 analog}; ID 0x79
   when pressure mode is on). The game now locks analog mode, enters pressure mode, and
   scripted presses work: Start → main menu (NEW GAME / LOAD GAME / OPTIONS); ✕ → difficulty
   prompt ("…set to NORMAL… Begin game with the current setting? Yes/No").
5. **New Game**: ← then ✕ selects Yes; the screen fades to black and the game hangs in a
   comb sort (`0x137478`) called from `0x137B5C` with **n = 0**: its unsigned bound `n − gap`
   wraps to 0xFFFFFFFF. The same code would hang on hardware, so the list it sorts (scene
   objects whose `+0xE0` matches a category, built in `0x138038`) must not be empty there:
   something the first stage should have created or loaded is missing.

Tests: PS2Recomp `ps2x_tests` 446/446 after updating four upstream tests to the libmc /
DUALSHOCK 2 semantics above (first GetInfo −1; fixed mode table; IOP heap base). Pipeline
tests 6/6.

## Main blockers

1. **New Game hangs** after the fade-out (n = 0 sort above). Next: find what populates the
   object list built at `0x138038` (callers `0x13560C`, `0x135428`, `0x137214`) and which
   load or creation step fails before it; compare with PCSX2 at the same point.
2. **Speed ~38%**: the guest VSync clock runs slow; boot-to-title ≈110 s host time.
3. **Presentation**: the window shows the raw 640×512 region (both interlaced field buffers)
   instead of the `DISPLAY1` rectangle.
4. **Audio**: the EzSound driver runs on the IOP but nothing reaches the host.

## Next actions

1. Trace the stage-start path (`0x138038` and its object-list builders) with the trace hooks;
   check for failed file reads or allocations just before the hang.
2. Profile where host time goes (VU1 interpreter, software GS, IOP interpreter) and why the
   VSync clock lags; target real-time 59.94 Hz before timing work.
3. Crop/merge the interlaced display for presentation.
4. Add an automated boot test script that runs the scripted path to the title and checks
   frame-dump hashes.
