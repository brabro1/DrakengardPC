# Progress

## Milestones

| Milestone | State | Evidence |
|---|---|---|
| A — Inspection and feasibility | **done** | RESEARCH.md: build identity, disc inventory, ELF layout, ISA census, tool audit, architecture choice |
| B — Native execution foundation | **done** | 8,315 functions translated (0 decode failures); `DrakengardPC.exe` executes original game code through IOP init, disc search and interrupt setup into the frame loop (logs below) |
| C — Boot and title screen | **reached, with known issues** | Square Enix → Cavia → Project Dragonsphere logos → title "PRESS START BUTTON"; scripted input opens the main menu and the New Game difficulty prompt (session 2). Known issues: no audio (speed fixed in session 5) |
| D — Playable vertical slice | in progress | first mission at full speed (60 VSync/s) on the reference PC: walking and sword attacks verified (session 5); fighting enemies, audio and saves not yet |
| E — Compatibility and timing | not started | timing architecture sketched in ARCHITECTURE.md |
| F — Modern rendering and mods | not started | |
| G — Complete-game verification | not started | |

## Session log

### 2026-11-02 — session 1

**Inspected**
- Workspace empty; Windows 11 Pro 26300; i5-12400F (6C/12T); RTX 3060 (driver 32.0.16.2101);
  31.8 GB RAM.
- Tools present: git 2.54, GitHub CLI, Python 3.13, Node 24, Java 8. No C/C++ compiler,
  CMake or Ninja.
- Disc: Redump-verified Drakengard (USA) SLUS-20732 v1.00 (MD5 `300f6697…`); boot ELF SHA-1
  `652d95f1…`, PCSX2 CRC `9679D44C`. Full inventory in RESEARCH.md.
- PS2Recomp, the Bloodborne PC reference, PCSX2's game database and patches, and five
  PS2Recomp-based ports (structure and approach).

**Installed** (no admin, no license prompts)
- MSYS2 base 2026-09-27 (SHA-256 checked against the release) with CLANG64
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

**3D scene diagnosed and fixed: `_sceVu0ecossin` was still a native stub.** Chain of evidence
(runs 50–59, new diagnostics listed in CLAUDE.md):

1. GS draw log (`PS2X_GS_DRAW_LOG`, `PS2X_GS_PROBE`): the 3D scene renders into a 640×480
   buffer (FBP 80, Z at 230) that 20 sprites copy into the 224-line field. A probe pixel in the
   grey area only ever received the frame clear; ~9,060 primitives per frame, but rows 260–420
   of the scene buffer got almost none. The grey was the clear colour, not a bad draw.
2. ADC-skipped primitives (logged by the frontend hook): 5,996 per frame, mostly off-screen
   guard-band rejects, plus large near-ground triangles.
3. A/B switch `PS2X_VU1_DEBUG_NO_CLIP=1`: the ground (road, trees) appears, so the geometry
   reaches VU1 and its clip test rejects it.
4. VU clip trace + VU1 dump + `tools/vu_disasm.py`: the ground program (VU1 `0x0AC0`) transforms
   vertices by vf19–vf22 loaded from VU qwords 959–962, then clips at `0x0B38`. The matrix held
   (1,0,1,0)/(0,0,1,0)/…; transformed vertices had w = 0, so every vertex was "outside".
5. `PS2X_VIF_WATCH=959`: the matrix is uploaded by `UNPACK V4-32 num 4 addr 959`, and most
   uploads carried 1e26–1e29 values. The EE packet builder `sub_00251DEC` copies it from
   `func_264344(0x480B00)` (camera object, inferred), which builds it with
   `sceVu0RotMatrixX` (`0x2FCB30`).
6. `sceVu0RotMatrixX/Y/Z` became guest code in session 3, but they call libvu0's internal
   `_sceVu0ecossin` (`0x2FC9C8`, angle in vf6.x, cos/sin returned in vf4.xy, vf5 zeroed), which stayed a
   native C-ABI stub: it read `f12`, wrote {cos, sin} to memory at `a0` (the caller's matrix)
   and left vf4/vf5 stale.

Fix: the config generator keeps `_sceVu0*` as guest code too (42 functions); a pipeline test
asserts no libvu0 function is a native stub. Result (run 59): the ground, hills and Caim with
his sword render in the first mission. Also fixed: macro-mode `VCLIPw` put −x in bit 0 and
compared against signed w (hardware: +x bit 0, limit |w|).

Run 60 (after the VCLIP fix, 1,500 s, pad script past the hint): the scene stays correct; while
d-pad up is held the view pitches up to the cloud sky and castle skyline and Caim's pose
changes, and ✕/□ presses are delivered. Frames every 30 ticks do not establish walking or an
attack animation. `boot_test.py` 4/4; `ps2x_tests` 455/455; pipeline tests 7/7.

### 2026-11-05 — session 5 (full speed: GPU GS, compiled VU code, threads)

Goal: the first mission at full speed (60 VSync/s, the NTSC field rate) on the reference PC.
All speeds are `[run:sample]` VSync/s in the first mission (scripted with the pad script in
CLAUDE.md); "slack" is the host time the EE scheduler spent waiting for wall-clock pacing,
i.e. idle headroom at full speed.

**What changed, in order, with the measured mission speed after each step**

| Step | Mission VSync/s |
|---|---|
| Start of session (session-4 runtime, software GS, interpreted VU1) | ~2 |
| GS on the GPU: paraLLEl-GS (Vulkan compute, LGPL-3.0+) as the default renderer. Raw GIF packets per path, privileged registers copied at VSync, scanout through `vsync()`, 3-slot readback ring. `GsSetCrt`/`sceGsResetGraph` now program SMODE1 (paraLLEl-GS produced no image with SMODE1 = 0). | 4 |
| VU1 microprograms compiled ahead of time (`tools/vu_recomp.py`): every MPG payload in `.vudata` becomes one C++ function (53 programs, 7,484 pairs, 0 unsupported), matched at upload by address + FNV-1a hash. Timing mirrors `VU1Interpreter` (per-lane ready cycles, 4-cycle flag latency, Q/P latencies, branch-delay VI hazard, E-bit, XGKICK). | 15–19 |
| VU0 microprograms from `.vutext` compiled the same way | 22 |
| Frame wait loop at `0x27DE1C` (inferred: polls the frame-done flag) charges 2,048 EE cycles per pass (`ps2x::registerEeWaitLoop`) instead of re-running for every timer tick; EE timers accrue in batches | 27–36 |
| GS backend on its own thread (like PCSX2's MTGS), pipelined readback | 36–40 |
| FMAC flag fast path for normal results | 40–44 |
| No allocation per GIF packet (arbiter slot reuse, byte-batch GS queue) | 46–48 |
| Static stall elimination in the generated VU code (see below) | 49–53 |
| `resetScheduler` skips clearing clean interpreter pipelines (it ran on every MSCAL); SSE-only rounding mode switch; GS frontend only scans for SIGNAL/FINISH/LABEL/TRX registers in raw-GIF mode; VIF UNPACK fast path; MPG upload matching by memcmp | **59–60**, ~10% slack |
| MAC/status flag liveness in VU1 programs | 59–60, 14–18% slack |
| VIF1/VU1/GIF worker thread with exact FINISH latching (below) | 59–60, 43–45% slack |

**Static stall elimination.** Each pair issues at least one cycle after the previous one, so a
read of a register written ≥ latency pairs earlier on the same straight-line path cannot stall,
and a read ≥ 3 pairs after the run's entry point cannot see an older in-flight write (VF/VI
latencies are ≤ 4; ACC latency is 1, so ACC never stalls). The generator computes the entry
points of each program (program start, static branch targets, BAL/JALR return addresses,
E-bit resume points, other programs' end addresses and cross-program branch targets) and drops
provably satisfied checks: 26,437 → 9,423 run-time checks. A dispatch to any other pc first waits
for every in-flight write (`vuaot::stallAll`); `[run:sample] aot-entry-stalls` counts those, and
it is 0 in the mission (a first version missed a branch from program 34 back into program 33 and
showed ~43,000 per 5 s, with ~12% more VU cycles).

**Flag liveness.** A census of all 53 programs: no program reads the status flag; VU1 programs
read MAC only with FMAND/FMEQ/FMOR (≤ 5 per program); the EE cannot read VU1 flags. A VU1 FMAC's
MAC/status result is dead when a later FMAC on the same path overwrites it before any MAC read
or departure can observe it (4-pair window). 1,959 of 3,576 flag computations are removed; the
zero/denormal/overflow value fix-up stays. VU0 programs keep all flags (the EE reads them with
CFC2). Compiled VU1 cycles per VSync were unchanged by both steps (~0.95 M per VSync in the mission
intro), as expected when only provably redundant work is removed.

**Gameplay.** The pad script now dismisses the R3 hint (✕ at ticks 4900–5200), holds d-pad up
and presses □ every 25 ticks: Caim walks toward the fortress and swings his sword, with the HUD,
trees and fortress drawn (frames at ticks 8253–13756). No enemy was reached.

**Measured (i5-12400F, RTX 3060, idle machine, before the VIF1 worker).** Title, menus and intro: 60. First
mission while walking and attacking: 59–60 with 600–900 ms slack per 5 s. Approaching the
fortress gate: the game thread is fully busy and the rate swings between 56 and 62 (average
~59). Profile of the game thread in the mission: compiled VU1 code ~55%, VIF/GIF/GS frontend
~8%, EE game code and runtime the rest.

**VIF1/VU1 worker thread** (`ps2xRuntime/src/lib/ps2_vif1_worker.cpp`, like PCSX2's MTVU).
VIF1 DMA data (UNPACK, MPG, MSCAL → compiled VU1 programs, XGKICK, DIRECT) and GIF PATH3 data
are copied at DMA kick time and processed on one worker thread in kick order; DMA completion is
still reported at kick time. The EE waits for the worker before it reads or writes VIF1/GIF
registers or FIFOs, VU1 memory (EE loads/stores, DMA from VU memory), GS CSR/SIGLBLID, GS
readback, EE-side GS register writes, the native GIF fast paths, and when it is only waiting
(registered wait loop, scheduler idle, start of VBlank).

The first version kept full speed in VSync/s but the game advanced ~18% slower: DMA kicks per
VSync fell from 166.0 to 137.5 and Caim was visibly behind at the same tick. A/B evidence:
`PS2X_VU1_THREAD=0` → 166.0, `=sync` (worker, wait after every kick) → 166.0, asynchronous →
137.5. `PS2X_GS_FINISH_TRACE=1` showed the game's GS FINISH (unmasked, IMR `0x7D00`) arrives
once per frame on PATH2 in a 32-byte VIF1 DIRECT packet; when VIF1 ran on the EE, FINISH was
latched during the kick and the interrupt handler ran right after it. Waiting for the worker at
that kick restored 166 DMA/VSync but removed all overlap (the FINISH kick ends the frame).
Final design (`runtime/vif1_scan.h`): the EE parses every chunk it queues with the same rules as
`processVIF1Data` (STCYCL for UNPACK sizes, DIRECT image continuation) and the GS frontend's GIF
walk, latches each PATH2 FINISH in CSR at kick time and records a credit; the frontend consumes
the credit instead of latching that FINISH again. SIGNAL/LABEL and PATH3 FINISH make the EE wait
instead. The DIRECT payloads sit at 8 mod 16 inside DMA chain copies (tag VIF codes), which a
first quadword-aligned pattern scan missed.

Measured with another emulator using ~3 of the 12 hardware threads the whole time (absolute
headroom is lower than on an idle machine, so runs were interleaved): worker on 166.0
DMA/VSync, 43–45% idle; worker off 165.9, 11–18% idle. A 240 s gameplay run with the worker:
59–60 VSync/s throughout, minimum 13% idle at the fortress gate (where the synchronous build
dropped to 56 with no idle time); frames correct up to the fortress gate.

Also: the GS backend thread now registers with Granite (each run logged 80,000–130,000
"Thread does not exist in thread manager" lines before).

Tests: `ps2x_tests` 455/455; pipeline tests 7/7. A test-only build error was fixed (runtime
includes were inside `#if PS2X_HAVE_PARALLEL_GS`).

Repository: docs moved to `docs/`, README rewritten for players and builders, personal paths
removed from tracked files (machine notes go to the ignored `CLAUDE.local.md`), GPL-3.0
`LICENSE` added, third-party notices for paraLLEl-GS, Granite, volk and Vulkan-Headers.

## Main blockers

1. **Audio**: none reaches the host (EzSound runs on the IOP; no output path).
2. **Gameplay coverage**: only the first mission's opening is verified; enemies, dragon flight,
   later missions, FMV and saves are untested.
3. **Speed headroom in big battles**: unmeasured. The worker build leaves ~40% idle time in
   the first mission; the next lever is SIMD code generation for VU FMAC lanes.
4. **Timing catch-up**: after long host stalls (e.g. a hidden window) VBlanks are delivered
   back to back (69/s seen after a stall); pacing should resynchronise to wall time.

## Next actions

1. Drive the pad script into combat (enemies near the fortress) and measure speed there.
2. Audio (EzSound on the IOP → host mixer).
3. Analog-stick scripting in `PS2X_PAD_SCRIPT` for movement tests.
4. Resynchronise VBlank deadlines after stalls.
5. Rendering features on the GPU path: internal resolution, widescreen
   (`sceVu0ViewScreenMatrix`), then a draw-level renderer for DLSS/FSR.
