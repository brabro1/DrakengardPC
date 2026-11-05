# Modding

**There is no working mod system yet.** This file records what exists and the staged plan,
so nothing here should be read as a supported feature until it is marked verified in
[FEATURES.md](FEATURES.md).

## What exists today

- **Native function overrides** (developer-level): `src/overrides/drakengard_overrides.cpp`
  binds a guest function address to a native handler. This is how the port itself replaces
  SDK code, and later how gameplay hooks will be attached. It requires rebuilding.

## Planned stages

1. **Archive understanding**: document the containers in `IMAGE.BIN`, `SNDIMGUC.BIN`,
   `FACEUC.BIN`, `VOICEUC.BIN` (TCRF mentions `.fpk` containers and `.zim` images; unverified
   for this disc). Write read-only extraction/inspection tools under `tools/`.
2. **First real replacement**: serve one asset (e.g. one texture) from
   `mods/<name>/` instead of the disc at the point the game reads it, without touching the
   ISO. Demonstrate in-game with a before/after frame dump.
3. **Manifest and load order**: `mods/<name>/mod.toml` with id, version, target game
   (`SLUS-20732`, ELF SHA-1), and the files it overrides; deterministic load order and a
   conflict report written at start-up.
4. **Conversion tools** for the formats as they are understood (image ↔ PNG, audio ↔ WAV).
5. **Gameplay hooks**: a stable C API over the native override mechanism, then optional
   scripting.

Original files are never modified; overrides are resolved at read time.
