# Feature matrix

States: **planned** (designed, not built) · **implemented** (code exists, not validated in
game) · **verified** (observed working on the tested setup, with evidence in PROGRESS.md).

Tested setup: Windows 11 Pro 26300, i5-12400F, RTX 3060, Drakengard (USA) SLUS-20732 v1.00.

## Core

| Feature | State | Notes |
|---|---|---|
| Disc identity check (SYSTEM.CNF + ELF SHA-1) | verified | refuses other releases |
| Static recompilation of the EE executable | verified | 8,315 functions, 0 decode failures |
| Reproducible regeneration from the user's disc | verified | launcher Build page / `tools/build_game.py --iso` runs every step (tested with the real ISO) |
| Launcher (settings, disc check, build, Play) | verified | `DrakengardPC Launcher.exe`; all pages rendered and checked by screenshot; Play uses the same command line as the tested direct start |
| Game exe starts directly | verified | `DrakengardPC.exe` without arguments reads `DrakengardPC.ini`; runs from any folder without MSYS2 on PATH (DLLs next to it); logs to `work/logs/game.log` |
| Disc identification (USA / Europe / Japan) | implemented | USA v1.00 verified as supported; EU (`SLES_523.22`, inferred from serial SLES-52322) and Japanese discs are recognised by boot ELF name only and reported as not supported (no images to test) |
| Europe and Japan releases | planned | each needs its own translation, overrides and testing; needs those disc images |
| Boot: crt0, kernel init, IOP module loading | verified | 8 IRX loaded and started |
| Disc access at real ISO sectors (EE + IOP) | verified (EE search) | IOP search path implemented, not yet exercised |
| Interrupts: DMAC, GS FINISH/SIGNAL, timers, VSync | verified | boot to gameplay depends on all of them |
| First game screen (memory-card warning dialog) | verified | renders ~4 s after boot (run 7) |
| Boot logos (Square Enix, Cavia, Project Dragonsphere) | verified | session 2 |
| Title screen | verified | reached at VSync tick ~1093 (≈110 s host time) |
| Main menu and New Game difficulty prompt | verified | via scripted input |
| Starting a new game | verified | prologue text, "Chapter 1 — Beginnings", mission briefing (session 3) |
| First mission (ground combat) | verified (partly) | walking and sword attacks with HUD and map at full speed (session 5); fighting enemies not yet exercised |
| Ground combat, dragon flight, story, endings | planned | |
| Audio (EzSound on IOP → host audio) | planned | driver runs on IOP; no output path |
| FMV playback (IPU MPEG-2) | planned | FFmpeg linked; untested |
| Memory card detection (empty card, no save yet) | verified | libmc first-probe and GetEntSpace semantics fixed |
| Memory card saves + backups | planned | host folder `work/memcard/mc0`; no save written yet |

## Native execution and speed

| Feature | State | Notes |
|---|---|---|
| EE program as native code | verified | 8,315 recompiled functions |
| VU1/VU0 microprograms as native code | verified | `tools/vu_recomp.py`: 53 programs, 7,484 instruction pairs, 0 unsupported; cycle model of PS2Recomp's VU interpreter, with static stall and flag-liveness elimination (session 5) |
| GS on the GPU | verified | paraLLEl-GS (Vulkan compute) on its own thread; `PS2X_GS_RENDERER=cpu` keeps the software GS |
| VIF1/VU1 on a worker thread | verified | game speed unchanged (166 DMA kicks/VSync), ~3× the idle headroom; GS FINISH latched at kick time as before; `PS2X_VU1_THREAD=0` disables (session 5) |
| Full speed (60 VSync/s) | verified (first mission) | menus, title, intro and first mission; see PROGRESS.md for measurements |

## Display

| Feature | State | Notes |
|---|---|---|
| Windowed output | verified | window size from settings (default 1280×960), resizable; GPU renderer by default |
| Aspect ratio 4:3 / stretch / original pixels, smooth or sharp filtering, FPS counter | verified (4:3, FPS) | window screenshot at 1280×960 (session 6) |
| Higher internal resolution (1080p/1440p/4K) | planned | paraLLEl-GS supersampling (`PS2X_GS_SSAA`) exists; true upscaling needs a draw-level renderer |
| Separate internal/output resolution | planned | |
| Widescreen / ultrawide | planned | hook point identified: `sceVu0ViewScreenMatrix` (`0x2FCFD0`) |
| HUD positioning/scaling | planned | |
| Borderless fullscreen, exclusive fullscreen, F11 toggle | verified (borderless) | borderless screenshot at 1920×1080 with pillarboxed 4:3; exclusive fullscreen and F11 not tested |
| VSync options, frame cap, VRR | planned | |

## Frame rate and timing

| Feature | State | Notes |
|---|---|---|
| Original timing baseline | planned | field-based task scheduler identified (`0x27ED50`) |
| 60/120/144+ FPS rendering | planned | requires a validated timing layer |
| Fixed-step simulation + interpolation | planned | |

## Upscaling and frame generation

| Feature | State | Notes |
|---|---|---|
| DLSS Super Resolution | planned | needs depth, motion vectors, jitter (scene-level hooks) |
| FSR upscaling | planned | |
| DLSS / FSR frame generation | planned | DLSS FG requires RTX 40+; not available on the test GPU |
| Runtime hardware capability checks | planned | |

## Input

| Feature | State | Notes |
|---|---|---|
| Controller (DualShock 2 analog + pressure modes) | verified | game locks analog mode and enables pressure mode; menus respond |
| Keyboard with rebinding | implemented | every PS2 button and the left stick; launcher Controls page; not tested with real key presses |
| Gamepad choice and stick dead zone | implemented | launcher Controls page |
| Scripted input for tests | verified | `PS2X_PAD_SCRIPT` |
| Gamepad remapping, mouse camera | planned | |
| Dead zones, button prompts, mouse camera | planned | |

## Mods

| Feature | State | Notes |
|---|---|---|
| Asset override folder + manifest | planned | needs archive formats (`IMAGE.BIN` etc.) |
| Load order / conflict report | planned | |
| Scripting / gameplay hooks | planned | native function overrides exist (`src/overrides`) |

## Tooling and diagnostics

| Feature | State | Notes |
|---|---|---|
| Function map from `.eh_frame`/linkonce/references | verified | `tools/funcmap.py` |
| Whole-program disassembly listing | verified | `tools/disasm_listing.py` |
| ISA census | verified | `tools/isa_census.py` |
| Interrupt handler registration log | verified | `[irq:add]` |
| Frame dump to PNG | verified | `PS2X_FRAME_DUMP_DIR` |
| Bounded automated runs | verified | `--max-seconds` |
| Pipeline invariant tests | verified | `tests/test_pipeline.py` (7 tests) |
| Function trace hooks + memory watch | verified | `DKG_TRACE_FUNCS`, `DKG_TRACE_MEM`, `DKG_TRACE_SKIP` |
| Position samples without call tracing | verified | `[run:sample]` every 300 host frames, with VSync/s and IOP/VU1/GS wall-time shares |
| Sampling profiler | verified | `DKG_PROFILE` (+ `DKG_PROFILE_DELAY`, DLL caller capture) + `tools/profile_report.py` |
| Automated boot regression | verified | `tools/boot_test.py` (boot, title, progress, stall checks) |
| GS draw log, pixel probe, VU clip trace/dump, VIF watch | verified | env switches in CLAUDE.md; `tools/vu_disasm.py` |
