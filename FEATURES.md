# Feature matrix

States: **planned** (designed, not built) · **implemented** (code exists, not validated in
game) · **verified** (observed working on the tested setup, with evidence in PROGRESS.md).

Tested setup: Windows 11 Pro 26300, i5-12400F, RTX 3060, Drakengard (USA) SLUS-20732 v1.00.

## Core

| Feature | State | Notes |
|---|---|---|
| Disc identity check (SYSTEM.CNF + ELF SHA-1) | verified | refuses other releases |
| Static recompilation of the EE executable | verified | 8,315 functions, 0 decode failures |
| Reproducible regeneration from the user's disc | implemented | `tools/recomp.py`; individual steps verified |
| Boot: crt0, kernel init, IOP module loading | verified | 8 IRX loaded and started |
| Disc access at real ISO sectors (EE + IOP) | verified (EE search) | IOP search path implemented, not yet exercised |
| Interrupts: DMAC, GS FINISH/SIGNAL, timers, VSync | implemented | GS interrupt added this session; timers/VSync from PS2Recomp |
| First game screen (memory-card warning dialog) | verified | renders ~4 s after boot (run 7) |
| Title screen | planned | blocked on input + memory card result codes |
| Ground combat, dragon flight, story, endings | planned | |
| Audio (EzSound on IOP → host audio) | planned | driver runs on IOP; no output path |
| FMV playback (IPU MPEG-2) | planned | FFmpeg linked; untested |
| Memory card saves + backups | planned | host folder `work/memcard/mc0` configured; game currently reports "no memory card" |

## Display

| Feature | State | Notes |
|---|---|---|
| Windowed output (640×448, software GS) | implemented | correctness reference only |
| Higher internal resolution (1080p/1440p/4K) | planned | needs hardware GS renderer |
| Separate internal/output resolution | planned | |
| Widescreen / ultrawide | planned | hook point identified: `sceVu0ViewScreenMatrix` (`0x2FCFD0`) |
| HUD positioning/scaling | planned | |
| Borderless / fullscreen, VSync, frame cap, VRR | planned | |

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
| Controller (DualShock 2 semantics) | planned | PS2Recomp pad HLE present, untested |
| Keyboard/mouse with rebinding | planned | |
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
| Pipeline invariant tests | verified | `tests/test_pipeline.py` (6 tests) |
