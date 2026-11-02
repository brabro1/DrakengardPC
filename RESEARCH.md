# Research notes

Confidence labels: **Observed** = read directly from the disc/ELF or a tool's output in
this repo; **Inferred** = reasoned from observations, not yet confirmed by execution;
**External** = taken from a third-party source (linked).

## Game build (Observed)

| Item | Value | How obtained |
|---|---|---|
| Title / region | Drakengard (USA), NTSC | PCSX2 `RedumpDatabase.yaml` match on MD5 |
| Serial | SLUS-20732, version 1.00 | `SYSTEM.CNF`: `BOOT2 = cdrom0:\SLUS_207.32;1`, `VER = 1.00`, `VMODE = NTSC` |
| Disc image | 4,401,659,904 bytes, 2,149,248 ISO blocks, DVD-5 | `tools/inspect_disc.py` |
| Disc MD5 | `300f66970511bf6133cf3af11ffd2a13` (matches Redump) | `md5sum` |
| Disc SHA-1 | `3d63d8e2a5e46790befcc1f1e4486783f54d0667` | `sha1sum` |
| Disc SHA-256 | `987373cdfae150c4fee7c750114e03b97222d70e30b178ae35798c8431e6815d` | `sha256sum` |
| Volume creation date | 2003-11-26 | ISO PVD |
| Boot ELF | `SLUS_207.32`, 4,853,316 bytes | ISO directory |
| Boot ELF SHA-1 | `652d95f1cc7f788e219948d52fc21ba2fffaefaa` | `tools/inspect_disc.py` |
| Boot ELF MD5 | `f2a795e6f103b258f80e61e128dbeebe` | same |
| PCSX2 ELF CRC | `9679D44C` (XOR of all words; matches PCSX2 GameIndex/patch name) | `src/game_identity.cpp` / script |

Only this revision is supported. Generated code, addresses and patches must not be assumed
valid for SLES-52322 (Europe), the Japanese *Drag-On Dragoon*, or the SLUS-29087 demo.

## Disc contents (Observed)

18 files, one directory (`MODULES`):

| File | Size | Notes |
|---|---:|---|
| `MOVIE0UC.BIN` | 1,303,599,104 | movie archive (ELF string `movie/movie0uc.bin`) |
| `MOVIE1UC.BIN` | 1,112,881,152 | movie archive |
| `IMAGE.BIN` | 941,780,992 | main data archive (ELF strings `\IMAGE.BIN;1`, `image.bin`, `filelist.bin`) |
| `SNDIMGUC.BIN` | 470,888,448 | sound archive |
| `FACEUC.BIN` | 406,740,992 | (Inferred) face/portrait or cutscene archive |
| `VOICEUC.BIN` | 159,518,720 | voice archive |
| `SLUS_207.32` | 4,853,316 | EE boot ELF |
| `MODULES/IOPRP280.IMG`, `IOPRP243.IMG` | 269,889 / 253,201 | IOP replacement images (IOP reboot) |
| `MODULES/*.IRX` | 265,212 total | `SIO2MAN`, `PADMAN`, `MCMAN`, `MCSERV`, `CDVDSTM`, `LIBSD`, `SDRDRV`, `EZSOUND` |

Archive formats are not yet analysed. External: TCRF notes a custom `.zim` image format and an
`.fpk` container inside `IMAGE.BIN` (PAL disc) — <https://tcrf.net/Drakengard>. Unverified for
this disc.

## Boot ELF layout (Observed, `tools/inspect_elf.py`)

- Entry `0x00120008`; flags `0x20924001`; no `.symtab` (stripped); 1,763 sections.
- LOAD 1: `0x00120000`, 2,234,904 bytes, R-X. LOAD 2: `0x00341A80`, file 2,272,885 / mem 3,615,124 bytes, RW-.
- Sections: `.text` `0x120000` (2,129,132 B); `.vutext` `0x33EE00` (8 KB, **VU microcode, flagged AX — must not be decoded as EE code**);
  `.data` `0x341A80`; `.ctors`/`.dtors`; `.eh_frame` `0x5189A8` (134,724 B); `.vudata` `0x547200`;
  `.rodata` `0x555900`; `.gcc_except_table`; `.lit4`; `.sdata`; `.sbss` `0x56C900`; `.bss` to `0x6B4414`;
  `.stack` `0x100000` (128 KB); 57 `.DVP.overlay..*` sections (VU microprogram overlays, not in a LOAD segment).
- 1,346 `.gnu.linkonce.t.*` sections (inline/template functions, **with GCC 2.x mangled names and exact ranges**);
  342 `.gnu.linkonce.d.*` (named vtables such as `_vt$13CDraTaskVSync`).
- EE code occupies two ranges: `0x00120000-0x0033ED28` and `0x00340E00-0x003419B0` (type_info/bad_cast after `.vutext`).
- Compiler: GCC 2.9x for the EE (old `__tf`/`_$_` mangling, `eh` CIE augmentation, `.mdebug.eabi64`) — Inferred from mangling/augmentation.
- SDK: `PsIIlib* 2800` strings (libdma, libpad, libgraph, libvu0, libmpeg, libipu, libpc, libcdvd, libsdr, libscf, libkernl),
  `libmc 2810`; IOP module path string `/home/ps2/sce281/iop/modules`.
- Middleware/engine names from RTTI: `abes::Scene*` scene graph, `CDragonMPEG`, `CEzMPEG`, `CDen2Stream`, `CVideoStream`,
  `CCamera`, `CCharaCtrlBase`, `CSky*` (aerial), `CEne*Ctrl` (enemy controllers).

## Function map (Observed, `tools/funcmap.py`)

| Measure | Value |
|---|---:|
| `.eh_frame` CIEs / FDEs | 358 / 3,840 (augmentation `""`×340, `"eh"`×18) |
| FDEs rejected (outside EE code or malformed) | 44 |
| Functions with exact compiler ranges (FDE or linkonce) | 4,871 |
| Code bytes covered by exact ranges | 73.1 % |
| JAL call sites / distinct targets | 19,682 / 4,897 |
| **JAL targets strictly inside an exact range** | **0** |
| Tail-call (`J`) targets accepted | 614 |
| Data words pointing into code | 7,468 (5,047 accepted as function starts; the rest are jump-table case labels or rejected because they fall in straight-line code — 132 rejected, e.g. the value 0x00142900) |
| Total functions (all evidence) | 8,140, plus 175 split at SDK signatures = 8,315 (first pass, before the mid-code rule: 8,442) |

The zero count of calls into the middle of compiler-provided ranges is the main evidence that
`.eh_frame` FDEs are reliable function boundaries for this binary.

`ps2_analyzer` (embedded SCE symbol database) matched 708 SDK/libc functions by signature.
Two matches (`sceVu0CopyMatrix@0x32DA84`, `@0x334F08`) fall 4 bytes inside FDE ranges and were
dropped as false positives (`tools/make_recomp_config.py`).

## Instruction census (Observed, `tools/isa_census.py`)

556,555 instructions in mapped functions. COP1 (FPU) 39,810 + LWC1/SWC1 35,853; COP2 (VU0 macro)
12,730 + LQC2/SQC2 7,410; MMI 2,204; LQ/SQ 5,283; COP0 89 (EI/DI/ERET/Status — SDK kernel code);
CACHE 17; SYNC 83; SYSCALL 168 sites (one per libkernel stub); JR not via `$ra` 215 (jump tables);
JALR 1,651 (virtual calls / callbacks). No undecodable instruction (ps2_recomp report: 0 decode
failures, 0 unhandled instructions).

## Requirements assessment

| Area | Finding | Status |
|---|---|---|
| FPU semantics | Heavy FPU use. **External**: PCSX2 GameIndex sets `eeClampMode: 3` (full clamping) for SLUS-20732, comment "Characters are visible in-game" — non-IEEE PS2 float behaviour matters. PS2Recomp emulates PS2 float semantics since upstream #272 (in our base). | to validate in-game |
| VU0 macro | ~20k instructions; matrix/vector math (`sceVu0*`). Translated inline by ps2_recomp. Clamping behaviour must match. | translated |
| VU1 microcode | `.vutext` (8 KB) + `.vudata` (57 KB) + 57 DVP overlays. Runs on the PS2Recomp VU1 interpreter initially. Candidate for ahead-of-time translation (precedent: God of War port). | interpreted (planned AOT) |
| Indirect control flow | 118 jump tables (analyzer), 215 computed JR, 1,651 JALR; 120,626 resumable entry points emitted. | handled by runtime dispatch |
| Self-modifying / overlays | No EE overlays found: all code is in LOAD 1. CACHE ops are SDK flushes. | Inferred — confirm at runtime |
| C++ exceptions | libgcc EH runtime present (`__throw`, `__sjthrow`, `find_exception_handler`, `__frame_state_for`). Whether game code throws is not yet known; throwing would need landing-pad entry support. | open |
| IOP | Reboots IOP with `IOPRP280.IMG`; loads 8 IRX from `cdrom0:\MODULES`. `EZSOUND.IRX` is a custom sound driver on top of `LIBSD`/`SDRDRV`. ps2xIOP runs IRX on an R3000A interpreter. | interpreted |
| Disc | Game uses `sceCdSearchFile` on `\IMAGE.BIN;1` etc. and streams via `CDVDSTM`. Real ISO LBNs implemented (see ARCHITECTURE "Disc"). | implemented, unverified |
| Movies | `libmpeg`/`libipu` (`CDragonMPEG`, `CEzMPEG`) — IPU MPEG-2. Runtime has an FFmpeg-backed path. | untested |
| Pads / memory card | libpad (`PADMAN`), libmc 2810 (`MCMAN`/`MCSERV`). | untested |

## Existing community work

- PCSX2 bundles `SLUS-20732_9679D44C.pnach` (widescreen by InfiniteNine, ported from ElHecht's PAL
  code). Its "original value" comments match this ELF exactly (`0x2FD034/40/44 = 0`,
  `0x2640A4 = 3C0143A0`, `0x129FC0 = 3C014420` — checked). The FOV change sits inside
  `sceVu0ViewScreenMatrix` (`0x2FCFD0`), which gives a native hook point for widescreen.
  Its no-interlace codes write `0x4F2668/74/84` (display setup data).
- No Drakengard-specific recompilation, decompilation or file-format tooling was found
  (searches of GitHub, PS2Recomp issues, web). TCRF page: <https://tcrf.net/Drakengard>.
- PCSX2 wiki reports the game is playable; rare slowdowns in aerial missions:
  <https://wiki.pcsx2.net/Drakengard>.

## Execution-approach research (External, read 2026-11-02)

- **Bloodborne PC** (<https://github.com/Supermedo/bloodborne_pc>, GPL-2.0): a shadPS4-derived
  port. PS4 game code is x86-64 and runs directly on the host CPU; GNM graphics are translated
  to Vulkan. This only works because guest and host share an ISA. **It does not transfer to the
  PS2**, whose EE is a MIPS R5900 with VU0/VU1 coprocessors and a GS rasteriser. What does
  transfer is the product shape: launcher, guided first start, upscalers, frame-rate options,
  input remapping, mod loading, crash reports.
- **PS2Recomp** (<https://github.com/ran-j/PS2Recomp>, GPL-3.0, base commit `e5636981a`):
  static recompiler (R5900 → C++, MMI, VU0 macro, PS2 float semantics), runtime (memory,
  syscalls, EE scheduler, GS software backend, VU1 interpreter, VIF1, IPU/MPEG via FFmpeg),
  ps2xIOP (R3000A IRX interpreter + virtual IOP kernel). 446/446 unit tests pass on our toolchain.
  216 open issues at the time of reading, many about semantics gaps — see its tracker.
- **Ports built on it** (all GPL-3.0): KillzoneRecomp (links PCSX2's GS/microVU/IPU/SPU2 as
  libraries; upscaling, 120+ FPS), god-of-war-recomp / Waffle565 runtime (VU1 microprograms and
  IRX modules compiled ahead of time to C++, native OpenGL GS renderer, native sound engine,
  real disc sectors), PS2Recomp-Fixer (PCSX2 hardware GS DLL, microVU, SPU2, SIO2, IPU, ~150 fixes,
  real ISO9660 LBNs). These show which subsystems commonly need replacement after the EE is
  recompiled.
- **PCSX2** (<https://github.com/PCSX2/pcsx2>): used here only as a behavioural reference and as
  a source of game-specific facts (GameIndex, patches). Not part of the execution path.

## Game behaviour established in session 2 (Observed, via trace hooks)

Addresses are for SLUS-20732 v1.00 only. Names in quotes are descriptive, not original symbols.

| Address | What it is | Evidence |
|---|---|---|
| `0x2EF5AC` / `0x2EF8A0` / `0x2EF3DC` | memory-card request dispatcher / poller / issue-and-wait wrapper; −10000 = pending, −4000 = sync command mismatch | disassembly; traced returns |
| `0x2EF7B4` | wires GetInfo outputs: type → `obj+0x24`, free → `+0x1C`, format → `+0x20` | disassembly |
| `0x2F11E4` | "card check" state machine on object `0x508300`; state `+0x6C0` (0 start, 2 formatted PS2 card, 3 entry space read, 4 OK/no save, 7 unformatted, 8 no card); reads GetInfo 3× and requires agreement | memory watch of `0x5089C0..` |
| — | game expects `GetEntSpace("/BASLUS-20732")` = −4 when no save folder exists; free-space need computed as `free − 2 − Σ⌈size/1 KB⌉ − ⌈(files+1)/2⌉` clusters | disassembly at `0x2F1458..0x2F1508` |
| `0x1269B8` | loops on `sceSifAllocIopHeap(0x40000)` with "Cannot allocate %d bytes of IOP memory" | disassembly, string at `0x555BB0` |
| `0x286214` | IOP buffer allocations seen: 0x10000, 0x20000, 0x20000, 0x40000, 0x40000 | trace of `sceSifAllocIopHeap` |
| `0x122AE4` | opens pad ports 0/1 with DMA buffers `0x56CD80` / `0x56CEC0` | trace of `scePadPortOpen` |
| `0x122F40` | per-port pad update (object via `gp−0x3E2C`): state `+0x108`, ID `+0x132`, input-mask countdown `+0x114` | disassembly; memory watch |
| `0x122E2C` / `0x122F0C` | `scePadSetMainMode(port, 0, 1, 3)` (analog, locked) / `scePadEnterPressMode` | traces |
| `0x128704` | controller manager: port-0 data accepted only with ID `0x79` (pressure mode); otherwise replaced by neutral data and flag `+0x94` = 1; pad buffers `0x342860` / `0x342900` | disassembly; memory watch |
| `0x122A58..0x122AE0` | confirm/cancel decode: ✕ or Start → 0x20, ○ → 0x10 | disassembly |
| `0x137478` | comb sort of 8-byte `(ptr, float key)` records; bound `n − gap` is unsigned, so n = 0 never terminates | disassembly; trace (n = 0 after New Game) |
| `0x137B5C` / `0x138038` | collect scene objects whose `+0xE0` matches a category and sort by float at `+0xE4` | disassembly |

Boot sequence (observed VSync ticks at ~38% speed): fade-in 185 → Square Enix logo ~305 →
Cavia ~605 → Project Dragonsphere ~845 → title ~1093.

## Host performance (Observed, session 3; i5-12400F, title-screen phase)

Measured with `DKG_PROFILE` (1 ms sampling of every thread) and the `[run:sample]` wall-time
shares. The process is CPU-bound on one guest thread (~1.1 cores used). VSync events fire
only when both wall time and the guest's charged EE cycles reach the deadline, so host time
spent in GS/IOP work slows the game clock.

| Change | Guest-thread share before → after | VSync reached at host frame 2700 |
|---|---|---|
| baseline | GS 67%, IOP 22% (IOP: `IopKernel::beginNextReady` 11%, `nextWakeCycle` 6%, timers 2%; interpreter itself ~2%) | 632 |
| IOP idle fast path (skip scheduling while no IOP thread is ready; invalidated by every external input) | IOP 22% → 5% | 793 |
| GS sources at `-O3 -mavx2` | — | 896 |
| inline `lround` in bilinear filtering (exhaustively equal to `lroundf` for \|x\| < 2³¹) | `ucrtbase` 13% → 4.5% | 858–896 (noise ±5%) |
| direct texel reads instead of the 8 KB texture-page snapshot (reverted: upstream models the GS texture buffer) | — | 884 (no gain) |

Remaining profile at the title: `SampleTexture` (+ fetch lambda) ~36%, `WritePixel` 15%,
`ReadTexture` 8%, `Submit` 6.5%, EE `checkpointDue` 8% and `advanceEeTimers` 6.5% (a hash-map
lookup of `GIF_STAT` per cycle charge). The game clock runs at ~16 VSync/s (27% of 59.94 Hz)
in this phase; boot to title is ~100 s. Real-time speed needs the software GS off the guest
thread (asynchronous GS like PCSX2's MTGS) or the planned hardware renderer.

## New-game hang root cause (Observed, session 3)

`0x137214` assigns each scene object (list built in `0x138038`) to the nearest "category"
object within a maximum distance; the distance is computed with VU0 macro ops (`vsub`,
`vmul`, `vaddy/z`, `vsqrt`, `vaddq`). The initial maximum is the float at `0x56A2F8`
= `0x7FFFFFFF`: the PS2's largest value, but an IEEE NaN on the host. PS2Recomp's
`c.lt.s` used host float comparison, so `distance < NaN` was always false, no object got a
category (`+0xE0 = 0`), and the comb sort in `0x137478` was called with n = 0, whose
unsigned bound `n − 1` never terminates. Memory dump at the call: both list objects and the
category at the origin (distance 0). Fix: EE FPU compares use PS2 semantics (exponent-255
values are large numbers, denormals are zero, nothing is unordered).

## First-loading-screen stall (Observed, session 3)

`0x24DD08` builds a list of objects that pass a visibility test (`0x24DA94` →
`0x25C324` → `0x275F5C`; traced: several pass) and sorts it by distance with a comb sort
at `0x24DC4C`. The gap is `cvt.w.s((float)gap / 1.3f)` (constant at `gp−0x43D0`), with the
Combsort11 rule (gaps 9 and 10 become 11) and exit when gap < 2 and no swaps. PS2Recomp
translated `cvt.w.s` as `nearbyintf` (round to nearest): 2 / 1.3 = 1.54 → 2, so the gap
never reached 1. The R5900 `cvt.w.s` truncates toward zero and saturates |x| ≥ 2³¹ by sign
(PCSX2: `(x & 0x7F800000) <= 0x4E800000 ? (s32)x : sign ? 0x80000000 : 0x7FFFFFFF`).
