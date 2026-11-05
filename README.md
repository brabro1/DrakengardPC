<div align="center">

# DrakengardPC

**Drakengard (PlayStation 2, 2003) running as a native Windows PC game.**

*Unofficial fan project · work in progress · not affiliated with Square Enix or Cavia*

**Status: the first mission is playable at full speed. No sound yet.**

</div>

---

## What is this?

DrakengardPC turns the original PlayStation 2 game **Drakengard** into a real Windows program.

Most people play PS2 games on PC with an **emulator**: a program that pretends to be a
PlayStation 2 and reads the game's instructions one by one while it runs. DrakengardPC works
differently. Before you play, the game's code is **translated once into PC code** and compiled
like any other PC game. When you play, your **CPU runs the game directly** and your **graphics
card draws it**.

Why that matters:

- **Speed.** The game runs at full speed with a lot of CPU power left over.
- **Room to grow.** A game that is real PC code can get PC features: higher resolutions,
  widescreen, higher frame rates, modern upscaling and mods (see [What's coming](#whats-coming)).
- **Your copy, your PC.** You build it from your own disc. No game files are included here or
  downloaded from anywhere.

## Can I play it?

Partly. The project is young, and this is exactly what works today on the test PC (Intel
i5-12400F, NVIDIA RTX 3060):

| | Part of the game | State |
|:-:|---|---|
| ✅ | Start-up, logos, title screen, menus | works, full speed |
| ✅ | New Game, prologue, Chapter 1 briefing | works |
| ✅ | First mission: walking, sword attacks, HUD, map | works, full speed |
| ✅ | Gamepad and keyboard | works |
| 🟡 | Fighting enemies, the rest of the first mission | not tested yet |
| 🟡 | Dragon flight, later missions, story, endings | not tested yet |
| 🟡 | Movies (cutscene videos) | not tested yet |
| 🟡 | Saving and loading | memory card is detected; saving not tested yet |
| ❌ | Sound and music | not working yet |

**In short:** it is a preview for the curious, not yet a way to play through the game. If you
want to finish Drakengard today, use your PS2 or an emulator. Progress is tracked in
[docs/PROGRESS.md](docs/PROGRESS.md).

## Features

**Working now**

- Native PC code for the whole game program and its graphics programs (no emulated CPU)
- Graphics drawn on your GPU (Vulkan)
- Full speed (the PS2's 60 frames per second display rate) in everything tested so far
- Game logic, graphics processing and drawing each on their own CPU thread
- Gamepad (detected automatically), plus a keyboard fallback
- Only reads your disc image; never changes it

## What's coming

Planned, in rough order. None of these exist yet.

- 🔊 Sound and music
- 💾 Saving, with automatic backups
- 🖥️ Higher resolutions (1080p, 1440p, 4K) and fullscreen / borderless window
- ↔️ Widescreen and ultrawide
- ⚡ Higher frame rates (120/144 FPS and more)
- ✨ DLSS and FSR upscaling
- 🎮 Button remapping and better keyboard/mouse controls
- 🧩 Mod support (replacing textures and other files)

Details: [docs/FEATURES.md](docs/FEATURES.md).

---

## What you need

| | |
|---|---|
| **Your own copy of the game** | *Drakengard*, **USA version** (serial SLUS-20732), as a disc image (`.iso`) made from your own disc. European and Japanese versions are **not** supported. |
| **A Windows PC** | Windows 10 or 11 (64-bit) |
| **Processor** | Intel 4th generation (Haswell) / AMD Ryzen or newer (AVX2 needed) |
| **Graphics card** | Any recent NVIDIA, AMD or Intel GPU with up-to-date drivers (Vulkan) |
| **Disk space** | about 3 GB, plus a few GB for the free build tools |
| **Time** | plan for about an hour the first time (installing the tools and building); after that, starting the game takes seconds |

> **Why isn't there a simple download of the game?**
> The finished program contains Drakengard's code (translated). That belongs to Square Enix,
> so it can't be shared. Instead, you get the *tools* here, and they turn **your own copy** into
> the PC version on your own computer. Two double-click scripts do all the work.

## How to install and play

### Step 1 — Install the free tools (once)

1. **Git** — download from [git-scm.com](https://git-scm.com/download/win) and install with
   the default options.
2. **Python** — download from [python.org](https://www.python.org/downloads/windows/). In the
   installer, **tick "Add python.exe to PATH"** before you click *Install*.
3. **MSYS2** (the compiler) — download from [msys2.org](https://www.msys2.org/) and install it
   to the default folder `C:\msys64`. Then open **"MSYS2 MSYS"** from the Start menu, paste this
   line, press **Enter**, and answer **Y**:

   ```bash
   pacman -S --needed mingw-w64-clang-x86_64-toolchain mingw-w64-clang-x86_64-cmake mingw-w64-clang-x86_64-ninja mingw-w64-clang-x86_64-pkgconf mingw-w64-clang-x86_64-ffmpeg
   ```

### Step 2 — Download DrakengardPC (once)

Open **Command Prompt** in the folder where you want it (for example your Documents folder)
and run:

```bash
git clone --recursive https://github.com/brabro1/DrakengardPC.git
```

This creates a `DrakengardPC` folder. (The green *Download ZIP* button doesn't work for this
project, because it leaves out the graphics renderer.)

### Step 3 — Build the game from your disc (once)

Open the `DrakengardPC` folder and **drag your `Drakengard (USA).iso` file onto `build.bat`**.

A window shows the progress. It checks that your disc is the right version, translates the
game, and compiles it. The first time takes a while (about 15–30 minutes on a modern PC). When it
says **Done**, you're ready.

### Step 4 — Play

**Double-click `play.bat`.** That's it. From now on this is the only step.

## Controls

A connected gamepad is used automatically. Without one, use the keyboard:

| PS2 button | Keyboard |
|---|---|
| D-pad (move) | Arrow keys or W A S D |
| ✕ | X or Space |
| ○ | C |
| □ (attack) | Z |
| △ | V |
| Start | Enter |
| Select | Tab |
| L1 / R1 | Q / E |
| L2 / R2 | Shift |

The keys can't be changed yet.

## Questions

**Is this an emulator?**
No. An emulator imitates the PS2 while the game runs. Here the game's code has been translated
into PC code before you play, so your PC runs it directly. A few small support parts still
imitate PS2 hardware, mainly the console's input/output processor that runs the disc, sound and
controller drivers; they are listed in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

**Does this include the game?**
No. Nothing from the game is in this project. You need your own copy of Drakengard (USA).

**Which version of the game do I need?**
Drakengard for PlayStation 2, **USA release** (SLUS-20732). `build.bat` checks your disc
image and refuses other versions, because every version's code is different.

**Will it run on my PC?**
It has only been tested on one PC so far (Intel Core i5-12400F, NVIDIA RTX 3060), where it
runs at full speed with plenty of power to spare. Slower PCs haven't been tried yet; reports
are welcome.

**Where are my saves?**
In `work\memcard\mc0` inside the DrakengardPC folder, once saving works.

**Can I change the resolution or play in widescreen?**
Not yet. The game draws its original 640×448 picture, shown in a window you can resize. Higher
resolutions and widescreen are planned.

**Is there a Mac or Linux version?**
Not at the moment. Windows only.

**How can I help?**
Testing on different PCs and reporting what happens is very useful. Developers: see
[docs/BUILDING.md](docs/BUILDING.md).

## Troubleshooting

| Problem | What to do |
|---|---|
| `build.bat` says MSYS2 was not found | Install MSYS2 to `C:\msys64` and run the `pacman` line from Step 1. If you installed it somewhere else, set the environment variable `MSYS2_CLANG64_BIN` to your `msys64\clang64\bin` folder. |
| `build.bat` says Python or Git was not found | Install it (Step 1). For Python, re-run the installer and tick "Add python.exe to PATH". |
| It says the disc is the wrong version / `unsupported revision` | Only Drakengard **USA** (SLUS-20732) works. Make the image from the USA disc. |
| "avcodec-63.dll was not found" | Start the game with `play.bat`, not the `.exe` directly. |
| The window stays black or closes at once | Update your graphics card driver. |
| You moved your `.iso` file | Drag it onto `build.bat` again (this time it only takes a moment). |
| Something else | Note what the window said and open an issue on the project's GitHub page. |

## Updating

Open Command Prompt in the `DrakengardPC` folder and run `git pull`, then drag your ISO onto
`build.bat` again.

---

## Learn more

- [docs/PROGRESS.md](docs/PROGRESS.md) — what has been done, with measurements
- [docs/FEATURES.md](docs/FEATURES.md) — every feature, planned or done
- [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) — what was tested, and on which PCs
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — how the port works inside
- [docs/BUILDING.md](docs/BUILDING.md) — building by hand, options, tests (for developers)

## Credits

- [PS2Recomp](https://github.com/ran-j/PS2Recomp) by ran-j and contributors: the
  recompiler this port is built on
- [paraLLEl-GS](https://github.com/Arntzen-Software/parallel-gs) by Arntzen Software: the GPU
  graphics renderer
- [Drakengard1Recompile](https://github.com/k1gs/Drakengard1Recompile) by k1gs: a related
  project whose fixes helped this one
- [PCSX2](https://pcsx2.net/): used as the reference for how the PS2 behaves

## License and legal

DrakengardPC's source code is free software under the **GNU General Public License v3.0**
([LICENSE](LICENSE)). Other components keep their own licenses
([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).

Drakengard is © Square Enix. This is an unofficial fan project. It is not made, approved or
supported by Square Enix or Cavia, and it contains no part of the game. Please buy the game.
