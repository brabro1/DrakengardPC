# DrakengardPC — notes for future sessions

Native Windows port of Drakengard (USA, SLUS-20732 v1.00) by static recompilation with a
vendored PS2Recomp (`external/PS2Recomp`, base `e5636981a`). Read docs/PROGRESS.md first: it
has the current state and next actions. Machine-specific paths (ISO, MSYS2, PCSX2) are in
`CLAUDE.local.md` (not committed).

## Rules

- Never commit or upload anything from the disc or generated from it: `work/` (ELF,
  generated C++, logs, frame dumps), `ps2_log.txt`, saves. Check `git status` before commits.
- The user's ISO is read-only.
- Only the ELF with SHA-1 `652d95f1cc7f788e219948d52fc21ba2fffaefaa` is supported.
- Label inferred addresses/symbols as inferred; record evidence in docs/RESEARCH.md.
- Native replacements of guest functions go in `src/overrides/drakengard_overrides.cpp`
  with a reason string; they are logged at start-up.
- GitHub: `brabro1/DrakengardPC`, private. Do not make it public.

## Toolchain (MSYS2 CLANG64)

Put the CLANG64 `bin` directory on `PATH` in every build shell (Git Bash:
`export PATH=<msys64>/clang64/bin:$PATH`), or the exe fails with a missing `avcodec-63.dll`.

```bash
cmake --build build/ps2recomp --target ps2_recomp ps2_analyzer ps2x_tests
python -I tools/recomp.py "<path to Drakengard (USA).iso>"
cmake --build build/game --target DrakengardPC
```

Full game build ~10 min; changing `ps2_runtime.h` or any header it includes (the generated
code's PCH, e.g. `ps2_vu1.h`, `gs_frontend.h`) rebuilds everything — prefer `.cpp` changes or
new headers. VU microprograms: `python -I tools/vu_recomp.py work/disc/SLUS_207.32 work/recomp/vu1`
(also run by `recomp.py`); only the `drakengard_vu1` library rebuilds.

## Run / debug

```bash
./build/game/DrakengardPC.exe --iso "<iso>" --data work --max-seconds 40 > work/runN.log 2>&1
```

- `[run:sample]` (every 300 host frames): pc/ra, VSync rate, wall-time shares of IOP/VU1/GIF,
  interpreted VU pairs, compiled VU cycles, `aot-entry-stalls` (should be 0), and host wait
  time (`pacing` = idle headroom at full speed).
- `[run:tick] pc=… ra=…`: map an address with `work/recomp/funcmap_named.csv`, read code in
  `work/recomp/disasm.txt` (`python -I tools/disasm_listing.py work/recomp/generated work/recomp/disasm.txt`).
- `[irq:add]`, `[gs:irq]`, `[sceCdSearchFile:ok]` are the key boot diagnostics.
- `PS2X_FRAME_DUMP_DIR=work/frames` (directory must exist) saves presented frames;
  `PS2X_FRAME_DUMP_EVERY=N`. `PS2X_GS_DEBUG_NOFOG=1` disables fog.
- Pad script `PS2X_PAD_SCRIPT="tick:button[+button][:hold],..."`:
  title → New Game → first mission → hint: `1300:start:10,1600:cross:10,1800:left:10,1950:cross:10,2400:cross:10,2700:cross:10,3000:cross:10,3300:start:10`;
  gameplay (dismiss hint, walk, attack): append `4900:cross:10,5050:cross:10,5200:cross:10,5400:up:3000`
  and `square:8` presses every 25 ticks from 5600.
- Renderer: paraLLEl-GS (Vulkan compute) by default; `PS2X_GS_RENDERER=cpu` selects the
  software GS, `PS2X_GS_SSAA=N` supersampling. Compiled VU programs: `PS2X_VU_AOT=0` disables
  them (interpreter only); `PS2X_VU_AOT_ENTRY_LOG=1` logs unpredicted entry pcs.
- VIF1/VU1/GIF worker thread (`src/lib/ps2_vif1_worker.cpp`): `PS2X_VU1_THREAD=0` runs VIF1 and
  VU1 on the EE thread again, `=sync` waits for the worker after every DMA kick (A/B tests).
  Anything on the EE side that reads VIF1/GIF registers, VU1 memory, GS CSR or GS readback must
  call `PS2Memory::syncVif1Worker()` first. Measure game speed with DMA kicks per VSync
  (`dma=` in `[run:sample]`, ~166 in the first mission), not only VSync/s.
  `PS2X_GS_FINISH_TRACE=1` logs which GIF path delivers SIGNAL/FINISH.
- Rendering diagnostics (CPU renderer; all off unless set):
  `PS2X_GS_DRAW_LOG=<firstTick>:<lastTick>:<file>`, `PS2X_GS_PROBE=x,y`,
  `PS2X_VU_CLIP_TRACE=<file>[:<perSite>]`, `PS2X_VU_DUMP=<pcHex>:<dir>`
  (read with `python -I tools/vu_disasm.py <dir>/vu1_code.bin [start end]`),
  `PS2X_VIF_WATCH=<qword>`, `PS2X_VU1_DEBUG_NO_CLIP=1`, `PS2X_VU_UPLOAD_DUMP=<dir>`.
- Native stubs replace guest functions by name: any library helper with a non-C register
  convention must stay guest code (see `tools/make_recomp_config.py`, `_sceVu0ecossin`).
- `DKG_TRACE_FUNCS=0x2f11e4,... DKG_TRACE_MEM=0x5089c0,... DKG_TRACE_SKIP=N DKG_TRACE_LIMIT=N`
  wraps recompiled functions (args, return, memory words) without regenerating code;
  `DKG_TRACE_MEM` accepts `A>O` (word at [A]+O) and prints floats.
- Profiling: `DKG_PROFILE=work/profile.txt [DKG_PROFILE_DELAY=<s>]` samples every thread
  (1 ms) and also records the caller of samples inside DLLs;
  `python -I tools/profile_report.py work/profile.txt build/game/DrakengardPC.exe`.
- PCSX2 2.7 is the behavioural reference; k1gs/Drakengard1Recompile (PAL, MIT) is a useful
  cross-check for runtime bugs (docs/RESEARCH.md).
- Run experiments one at a time: a hidden or minimised window stalls presentation.

## Tests

```bash
python -I -m unittest discover -s tests -v
cd external/PS2Recomp && ../../build/ps2recomp/ps2xTest/ps2x_tests.exe
```
