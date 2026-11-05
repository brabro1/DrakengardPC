# DrakengardPC — notes for future sessions

Native Windows port of Drakengard (USA, SLUS-20732 v1.00) by static recompilation with a
vendored PS2Recomp (`external/PS2Recomp`, base `e5636981a`). Read PROGRESS.md first: it has
the current blocker and next actions.

## Rules

- Never commit or upload anything from the disc or generated from it: `work/` (ELF,
  generated C++, logs, frame dumps), `ps2_log.txt`, saves. Check `git status` before commits.
- The user's ISO is read-only: `D:\ps2\games\Drakengard (USA).iso`.
- Only the ELF with SHA-1 `652d95f1cc7f788e219948d52fc21ba2fffaefaa` is supported.
- Label inferred addresses/symbols as inferred; record evidence in RESEARCH.md.
- Native replacements of guest functions go in `src/overrides/drakengard_overrides.cpp`
  with a reason string; they are logged at start-up.
- GitHub: `brabro1/DrakengardPC`, private. Do not make it public.

## Toolchain (MSYS2 CLANG64 on D:)

Prefix every build shell with `export PATH=/d/msys64/clang64/bin:$PATH` (Git Bash).

```bash
cmake --build build/ps2recomp --target ps2_recomp ps2_analyzer ps2x_tests
python -I tools/recomp.py "D:/ps2/games/Drakengard (USA).iso"
cmake --build build/game --target DrakengardPC
```

Full game build ~10 min; changing `ps2_runtime.h` or anything in the generated code's PCH
rebuilds everything — prefer new headers or `.cpp` changes in the runtime.

## Run / debug

```bash
./build/game/DrakengardPC.exe --iso "D:/ps2/games/Drakengard (USA).iso" --data D:/DrakengardPC/work --max-seconds 40 > work/runN.log 2>&1
```

- `[run:tick] pc=… ra=…` lines show where the main thread is; map an address to a function
  with `work/recomp/funcmap_named.csv` and read code in `work/recomp/disasm.txt`
  (build it with `python -I tools/disasm_listing.py work/recomp/generated work/recomp/disasm.txt`).
- `[irq:add]`, `[gs:irq]`, `[sceCdSearchFile:ok]` are the key boot diagnostics.
- `PS2X_FRAME_DUMP_DIR=work/frames` saves presented frames. `PS2X_GS_DEBUG_NOFOG=1` disables fog.
- Mission script (reaches the objective map/hint, first mission ~300 s):
  `1300:start:10,1600:cross:10,1800:left:10,1950:cross:10,2400:cross:10,2700:cross:10,3000:cross:10,3300:start:10`.
- k1gs/Drakengard1Recompile (PAL, MIT) is a useful cross-check for runtime bugs; see RESEARCH.md.
- PCSX2 2.7 at `D:\ps2\pcsx2-v2.7.346-windows-x64-Qt` is the behavioural reference.
- `[run:sample]` (every 300 host frames) gives pc/ra/VSync tick in any build.
- `DKG_TRACE_FUNCS=0x2f11e4,... DKG_TRACE_MEM=0x5089c0,... DKG_TRACE_SKIP=N DKG_TRACE_LIMIT=N`
  wraps recompiled functions (args, return, memory words) without regenerating code.
- `PS2X_PAD_SCRIPT="1300:start:10,1600:cross:10,1800:left:10,1950:cross:10"` reaches
  title → menu → New Game → Yes (title appears around VSync tick 1093; ~110 s host time).
- Profiling: `DKG_PROFILE=work/profile.txt` samples every thread (1 ms) and writes on exit;
  `python -I tools/profile_report.py work/profile.txt build/game/DrakengardPC.exe` ranks the
  busy thread's functions. `[run:sample]` also prints VSync/s and wall-time shares of the IOP,
  VU1 and GIF/GS paths. `DKG_TRACE_MEM` accepts `A>O` (word at [A]+O) and prints floats.
- Shell needs `export PATH=/d/msys64/clang64/bin:$PATH` or the exe fails with a missing
  `avcodec-63.dll`.

## Tests

```bash
python -I -m unittest discover -s tests -v
cd external/PS2Recomp && ../../build/ps2recomp/ps2xTest/ps2x_tests.exe
```
