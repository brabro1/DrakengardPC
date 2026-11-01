# Architecture

## Execution model

Drakengard's main game logic runs as **host-compiled C++ produced by static recompilation**
of the original EE executable (`SLUS_207.32`). There is no CPU interpreter or JIT for EE code
anywhere in the execution path. Hardware the EE talked to is provided by supporting
components, listed honestly below.

```
 user's ISO ──► tools/recomp.py ──► work/recomp/generated/*.cpp  (never committed)
                 │  inspect_disc.py   boot ELF out of the ISO, verified by SHA-1
                 │  funcmap.py        function boundaries (.eh_frame FDEs, linkonce, refs)
                 │  ps2_analyzer      SCE SDK signature names, jump tables, MMIO sites
                 │  make_recomp_config.py → drakengard.toml
                 └  ps2_recomp        R5900 → C++ (one function per file, 8,315 functions)

 DrakengardPC.exe = src/ (host, overrides) + generated code + PS2Recomp runtime
```

### Subsystem status (what is recompiled, reconstructed, interpreted, emulated)

| Subsystem | How it runs today | Kind | Target |
|---|---|---|---|
| EE game code (8,050 functions) | translated C++ from `ps2_recomp` | **recompiled** | stays recompiled; hand-reconstructed C++ for hot or patched systems over time |
| SCE SDK (265 functions: libdma, libgraph, libpad, libcdvd, libsif, libc math…) | native runtime handlers bound by address | **reconstructed (HLE)** | keep; extend per evidence |
| Other SDK/libc (436 functions) | translated guest code | recompiled | as needed |
| `sceCdLayerSearchFile` | bound to native `sceCdSearchFile` (`src/overrides`) | reconstructed | — |
| EE kernel (threads, semaphores, alarms, INTC/DMAC handlers, VSync) | PS2Recomp `EeScheduler` + syscall dispatcher | reconstructed | — |
| VU0 macro mode | translated inline (SSE) | recompiled | — |
| VU1 microcode (`.vutext`, `.vudata`, DVP overlays) | PS2Recomp VU1 interpreter | **interpreted** | ahead-of-time translation of the fixed microprogram set |
| VIF1 / GIF / DMAC | PS2Recomp packet processing | emulated | direct submission to the renderer |
| GS | PS2Recomp software GS (`gs_cpu_backend`), raylib/OpenGL window | **emulated** (software) | hardware renderer (see "Rendering") |
| IOP (8 IRX from disc) | ps2xIOP R3000A interpreter + virtual IOP kernel | **interpreted** | HLE or AOT-compiled modules where it pays off |
| SPU2 / sound | not yet producing output (EzSound driver runs on the IOP) | missing | native sound engine driven by LIBSD/EzSound commands |
| IPU / MPEG movies | PS2Recomp IPU stubs + FFmpeg path | emulated/HLE, untested | FFmpeg decode of the disc's MPEG-2 streams |
| Disc | real ISO9660 sectors read from the user's image (see below) | native | — |
| Pads / memory card | PS2Recomp HLE; untested | reconstructed | SDL3 input layer; host save files |

## Why static recompilation (and not the Bloodborne approach)

The Bloodborne PC port executes PS4 code directly because the PS4 CPU is x86-64. The PS2's
EE is a MIPS R5900 with 128-bit MMI registers, a non-IEEE FPU, and two vector units, so
its code cannot run on the host as-is. The options compared:

1. **Static recompilation (chosen)** — every EE function becomes C++. Feasible here
   because the evidence shows a closed program: all code is in one LOAD segment, no EE
   overlays, compiler-provided function bounds for 73% of code, zero calls into the
   middle of known ranges, 0 undecodable instructions. Gives hook points at the level of
   the game's own functions (e.g. `sceVu0ViewScreenMatrix` for widescreen).
2. **Full source reconstruction (decompilation)** — highest end quality, but years of
   work for a 2 MB C++ binary before anything runs. Kept as an incremental path: individual
   recompiled functions can be replaced by hand-written C++ once understood.
3. **Hybrid (the actual plan)** — recompile everything first, then reconstruct the systems
   that need PC features (camera/timing, rendering submission, input, saves) as native
   code, function by function, verified against the recompiled original.

PS2Recomp was chosen as the recompiler after building it and running its tests (446/446
pass on our toolchain), reading its source and issues, and confirming that several
commercial games reached gameplay with it. It is vendored at a pinned commit; our changes
to it are ordinary commits in this repository (see `external/PS2Recomp/UPSTREAM.txt`).

## Function discovery

`tools/funcmap.py` derives boundaries from evidence, strongest first: `.eh_frame` FDEs
(GCC writes exact start/length for every function compiled with exception tables),
`.gnu.linkonce.t.*` sections (exact, with mangled names), the ELF entry, `JAL` targets,
tail-call `J` targets, code pointers in data (vtables/callbacks) and `lui/addiu` address
pairs. Gaps holding code become their own functions. `make_recomp_config.py` then applies
SDK signature names from `ps2_analyzer` and drops signature hits that contradict a
compiler-provided range. Each function in `work/funcmap/funcmap_full.csv` records its
evidence letters.

## Disc

The runtime's original design serves `cdrom0:` files from an extracted folder with
synthetic sector numbers. Drakengard computes sector offsets into its archives, and the
host must not depend on that arithmetic staying inside a synthetic range, so:

- `main.cpp` verifies the ISO (`SYSTEM.CNF` boot name + ELF SHA-1) and stages only the
  small files the IOP opens by name (ELF, `MODULES/*`) under `work/disc_root`.
- EE `sceCdSearchFile` (runtime `Support.h`) and IOP `sceCdSearchFile` (`iop_cdvd.cpp`)
  resolve names through the image's ISO9660 directory and return **real LSNs**; reads go
  to the image at those LSNs. Observed: `IMAGE.BIN` → LSN `0xCCA1E` (838174), matching the
  ISO directory.
- The image is opened read-only.

## Interrupts and frame pacing (current understanding)

Observed handler registrations: DMAC ch1/ch2 → `0x27E19C`; INTC 0 (GS) → `0x27E1F4`;
INTC 9 (TIMER0) → `0x12156C`; INTC 11 (TIMER2) → `0x324EA0` (SDK alarms). The game's frame
task (`CDraTaskVSync`, vptr at `+0x120`) advances a DMA state machine (`0x126F84`, jump
table `0x555D10`) from the DMAC and GS handlers and sets a "frame done" flag at `+0x124`
that the main thread polls with a spin timeout (`0x127098`). The runtime did not raise the
GS interrupt; `ps2PollGsInterrupt` now raises INTC cause 0 on newly latched, unmasked
`CSR.SIGNAL/FINISH` (game sets `IMR=0x7D00`, i.e. FINISH unmasked). The VSync callback
(`0x27EC38` → `0x27ED50`) tracks fields from `CSR` bit 13.

Implication for high frame rates: game logic is paced by this field-based task scheduler.
Unlocking the frame rate will need a timing layer at that scheduler (fixed-step simulation
with interpolated rendering or a scaled tick), designed after the timing dependencies are
mapped. Nothing has been changed there yet.

## Rendering (plan)

Today: GS register/primitive stream → PS2Recomp software rasteriser → 640×448 texture in
a raylib/OpenGL window. That is a correctness reference only.

Planned stages, each measurable:

1. **Hardware GS renderer** (D3D12 or Vulkan): GS primitives rendered on the GPU at an
   internal-resolution multiplier (true higher-resolution rendering, not output scaling);
   separate internal and output resolution; handles the game's frame-buffer effects
   (PCSX2 GameIndex notes shuffle/post effects, `textureInsideRT`, `mergeSprite`).
2. **Scene-level hooks**: because game functions are native, the renderer can read the
   game's matrices and per-object draw calls (e.g. `sceVu0ViewScreenMatrix`, the `abes::Scene*`
   graph) to get camera/object motion vectors, depth, a jitterable projection, history
   resets on cuts, and HUD separation — inputs DLSS/FSR need and a GS stream alone lacks.
3. **Upscalers / frame generation** after stage 2 provides those inputs, gated by runtime
   hardware checks (RTX 3060 available locally for DLSS SR testing; DLSS FG needs RTX 40+).

API choice (D3D12 vs Vulkan) is deferred until the SDK requirements are checked against
the toolchain: DLSS/Streamline ship MSVC-built libraries; FSR 3.1/4 ship for D3D12 and
Vulkan. This may require an MSVC/clang-cl build (see "Toolchain").

## Toolchain

- MSYS2 CLANG64: clang 23.1.4, lld, CMake 4.4.5, Ninja 1.13.2, FFmpeg 8 (libavcodec 63) —
  installed at `D:\msys64` (not C:, which has ~5 GB free).
- Python 3.13 for the analysis pipeline.
- MSVC is PS2Recomp upstream's primary Windows compiler; our build keeps the code
  compatible with it. Moving to MSVC/clang-cl later (likely needed for DLSS/Streamline
  static libraries) requires Visual Studio Build Tools and accepting Microsoft's license
  terms — the owner's decision.

## Data layout

| Path | Contents | In git? |
|---|---|---|
| user's ISO | original disc image, read-only | never |
| `work/disc/` | extracted ELF, SYSTEM.CNF, listings, reports | no (ignored) |
| `work/funcmap/`, `work/recomp/` | function map, analyzer output, config, generated C++ | no |
| `work/disc_root/` | staged boot files for the IOP | no |
| `work/memcard/` | memory card saves | no |
| `build/` | build trees | no |
| `config/`, `src/`, `tools/`, docs | project code and metadata | yes |
