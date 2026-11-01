# Compatibility

## Game revisions

| Release | Serial | Boot ELF SHA-1 | Status |
|---|---|---|---|
| Drakengard (USA) v1.00 | SLUS-20732 | `652d95f1cc7f788e219948d52fc21ba2fffaefaa` | **supported target** (boot in progress) |
| Drakengard (Europe) | SLES-52322 | unknown | not supported — refused at start |
| Drag-On Dragoon (Japan) | SLPM-65… | unknown | not supported |
| Square Enix Sampler Disc Vol. 1 (demo) | SLUS-29087 | unknown | not supported |

Addresses, function maps and overrides are specific to the supported ELF.

## Tested scenarios

| Date | Scenario | Result | Build / evidence |
|---|---|---|---|
| 2026-11-02 | Cold boot to IOP module load | pass — 8 IRX loaded | session 1, run 1 |
| 2026-11-02 | Archive lookup (`IMAGE.BIN`, `SNDIMGUC.BIN`, `FACEUC.BIN`, `VOICEUC.BIN`, `MOVIE0UC.BIN`, `MOVIE1UC.BIN`) | pass — real LSNs | run 2 |
| 2026-11-02 | Frame loop / GS FINISH interrupts | partial — loop runs, slow | run 4 |
| 2026-11-02 | First visible screen | fail — only boot clear presented | run 5 |
| 2026-11-02 | Boot to memory-card warning dialog | pass — dialog renders at tick 245; presentation shows both field buffers | run 7, after function-map fix |
| 2026-11-02 | Answer dialog / reach title | not possible yet — no input | run 7 |

## Host

| Machine | OS | CPU | GPU | Result |
|---|---|---|---|---|
| reference | Windows 11 Pro 26300 | i5-12400F | RTX 3060 (32.0.16.2101) | builds; boots to the memory-card warning dialog |
