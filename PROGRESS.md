# Progress

## Milestones

| Milestone | State | Evidence |
|---|---|---|
| A — Inspection and feasibility | **done** | RESEARCH.md: build identity, disc inventory, ELF layout, ISA census, tool audit, architecture choice |
| B — Native execution foundation | **done** | 8,315 functions translated (0 decode failures); `DrakengardPC.exe` executes original game code through IOP init, disc search and interrupt setup into the frame loop (logs below) |
| C — Boot and title screen | **reached, with known issues** | Square Enix → Cavia → Project Dragonsphere logos → title "PRESS START BUTTON"; scripted input opens the main menu and the New Game difficulty prompt (session 2). Known issues: game runs at ~38% speed, no audio (interlaced presentation fixed in session 4) |
| D — Playable vertical slice | in progress | New Game → prologue → Chapter 1 → briefing → first mission intro (HUD, radio message, objective map, hint) at ~2 VSync/s; 3D ground renders as a flat grey area and Caim is not visible yet |
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

### 2026-11-03 — session 3 (speed, New Game hang)

New tools: `DKG_PROFILE` sampling profiler (+ `tools/profile_report.py`), wall-time shares and
VSync/s in `[run:sample]`, `DKG_TRACE_MEM` pointer indirection and float display,
`tools/boot_test.py` automated regression (boot / title / progress / stall).

**Speed** (details and table in RESEARCH.md "Host performance"): profiling showed the guest
thread ~99% busy with the software GS (67%) and IOP *scheduling bookkeeping* (22%; the IOP
interpreter itself ~2%). Added an IOP idle fast path (skips scheduling while no IOP thread is
ready, invalidated by any external input): IOP 22% → 5%. Built the GS sources with
`-O3 -mavx2` and inlined an exact `lround` (exhaustively checked against `lroundf`).
Net: ~+40% game-clock progress at equal wall time; the title phase still runs at
~16 VSync/s because the GS takes ~80% of the guest thread. A single-page texture-buffer
experiment showed no gain and was reverted (upstream models the GS's 8 KB texture buffer).

**New Game hang fixed.** Root cause: the game seeds a nearest-object search (`0x137214`)
with `0x7FFFFFFF` as the largest distance. On the PS2 that is the largest float; on the host
it is NaN, and PS2Recomp's `c.lt.s` used IEEE comparison, so no object was ever assigned and
a comb sort received n = 0 (`n − 1` unsigned never terminates). EE FPU compares now follow
PS2 rules (exponent-255 values are large numbers, denormals are zero, nothing unordered),
with a fast path for ordinary operands; new upstream-style test. Result: Yes → prologue text
("It is an ancient time…") → ✕ → "Chapter 1 — Beginnings" → mission briefing ("Caim and his
Union soldiers battle toward the castle…", Weapon / Continue) → Start → "NOW LOADING…".

**libvu0 now runs as recompiled guest code.** 41 `sceVu0*` functions had been replaced by
native stubs (host float math; upstream #361 disputes one's operand order). The recompiler
binds handlers by name, so the config generator renames them `guest_*`; 8,091 functions are
now recompiled, 224 native. Did not change the loading stall below.

**First loading screen stall: `cvt.w.s` rounding.** On "NOW LOADING…" the main thread
looped in a comb sort (`0x24DC4C`) of visible objects. Tracing showed the visibility test
passing for several objects, so the list was not empty (first hypothesis wrong). The sort
computes its gap as `cvt.w.s(gap / 1.3)`; PS2Recomp translated `cvt.w.s` with `nearbyintf`
(round to nearest), but the R5900 truncates toward zero and saturates out-of-range values.
With rounding, gap 2 → 2/1.3 = 1.54 → 2, so the gap never reaches 1 and the sort never
exits. Fixed (PCSX2's formulation) with a new test. Result: loading completes and the
first mission starts: VU1 microcode runs for the first time (~16% of the guest thread), a
dark scene with a faint circular HUD element fades in (tick ~3267), but game time drops to
~0.2 VSync/s (GS ~82%, VU1 ~16%), far too slow to verify combat.

Tests: PS2Recomp `ps2x_tests` 448/448; pipeline tests 6/6; `tools/boot_test.py` 4/4 checks.

### 2026-11-05 — session 4 (fixes ported from k1gs/Drakengard1Recompile)

Reviewed k1gs/Drakengard1Recompile (MIT; PAL SLES-52322 on PS2Recomp `eb8688b2`, one commit
behind ours; see RESEARCH.md "Existing community work"). Its game addresses do not apply to our
ELF, but its PS2Recomp fixes do. Each was checked against our vendored copy, rewritten in place
(not as build-time text patches) and given a regression test:

- **64-bit signed branches.** BLTZ/BGEZ/BLEZ/BGTZ (+likely/link forms) were emitted as
  `GPR_S32(...) op 0`; the R5900 tests the 64-bit register. 2,710 sites in our code, now
  `GPR_S64`.
- **VU0 macro MAC flags and VF0.** Macro-mode FMAC instructions never updated MAC/status
  flags, and writes to VF0 (the constant (0,0,0,1)) were stored. The game's visibility/clip
  test is `vsub.xyzw vf0, a, b; cfc2 $vi17` (e.g. `0x1CB7BC`/`0x1CB7D4`, sign bits `& 0xE0`),
  so it read stale flags and its next VF0 reads saw garbage. The translator now inserts
  `PS2_VU0_SET_MAC` (Z/S/O per lane, sticky status bits) for ADD/SUB/MUL/MADD/MSUB/OPMSUB and
  their bc/q/i/ACC forms (7,920 sites) and drops every VF0 store (114 macro sites + 1 LQC2)
  while still evaluating the value. The VU interpreter's `applyDest` also discards VF0 writes so
  a paired lower instruction cannot read a transient value. U (underflow) is not reported:
  results arrive already flushed by `ps2_vfinish`.
- **VIF0 UNPACK.** Only V*-32 was decoded; 16-, 8- and 5-bit formats were skipped and the
  address used VIF1's mask and TOPS. One shared decoder now serves VIF0 (256-QW mask, absolute
  address) and VIF1. Its V4-5 expansion was also wrong in VIF1 (5-bit values were not shifted to
  8 bits, PCSX2 `UNPACK_V4_5`).
- **Scratchpad DMA.** toSPR from VU0 data memory (`0x11004000`) and toSPR source-chain mode
  were refused silently; both are implemented. Remaining unsupported cases log
  `[spr-dma:unsupported]`.
- **Empty GIF/VIF chains** now end the transfer and raise the channel interrupt.
- **INT+FFMD presentation.** With SMODE2 = INT|FFMD the framebuffer holds one field, so the
  display reads half the lines and line-doubles them. Upstream read the full height, which is
  why both field buffers appeared stacked.
- **GS speed.** T8/CT32/CSA0 texels read the CLUT directly; the bilinear filter does four
  channels with SSE2.

**Result (run 48, same scripted input as session 3, AVX2 build).** The first mission is
reached at ~300 s host time (540 s before) and runs at ~2 VSync/s (was ~0.2; VU1 interpreter
now ~70% of the guest thread, GS ~40%). The mission intro now plays: dragon silhouette during
the fade-in, full HUD (health and magic gauges, radar, combo counter), the radio message
"Lord Caim! The Empire has surrounded the castle!" with portrait, the objective map ("The
goddess Furiae's castle is surrounded. Kill the commanders marked with the TARGET symbol…",
enemy markers) and the R3 map hint (run 49). The window shows one 448-line image (no stacked
field buffers). No `[spr-dma:unsupported]` lines in either run.

**Still wrong in the 3D scene.** Below the horizon most of the view is a flat grey-brown
area; walls and distant terrain above it are textured. Caim is not visible. Run 49 with the
new `PS2X_GS_DEBUG_NOFOG=1` shows the same area, so it is not the fog blend. Not yet
diagnosed (candidates: VU1 microprogram output, GS depth/alpha test, texture state). The last
display line has scattered coloured pixels. Movement/attack input was not exercised: the
script pressed it while the objective map and hint were up.

Tests: PS2Recomp `ps2x_tests` 455/455 (7 new); pipeline tests 6/6.

## Main blockers

1. **3D scene correctness**: in the first mission the ground is a flat grey area and Caim is
   not drawn (not fog; see session 4). Blocks verifying gameplay.
2. **Speed**: the first mission runs at ~2 VSync/s (VU1 interpreter ~70%, software GS ~40%
   of the guest thread); the title ~16-20 VSync/s. Needs faster VU1 (ahead-of-time translation
   of the fixed microprogram set), the GS off the guest thread, then the hardware renderer.
3. **Timing catch-up**: after long stalls the scheduler delivered missed VBlanks back to back
   (~187 VSync/s once); VBlank pacing should resynchronise to wall time.
4. **Audio**: none reaches the host.

## Next actions

1. Diagnose the grey ground / missing Caim: dump one mission frame's GS draws (PRIM, TEX0,
   TEST, ZBUF, vertex ranges) and compare against PCSX2's GS dump of the same scene.
2. Extend the pad script past the hint (✕) and drive movement/attack to reach control.
3. Profile VU1 in the mission; plan ahead-of-time translation of its microprograms, then the
   asynchronous GS (queue GIF packets; sync on FINISH/SIGNAL, readback, presentation).
4. Resynchronise VBlank deadlines after stalls.
5. Audio (EzSound on the IOP → host mixer).
