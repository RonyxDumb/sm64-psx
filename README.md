> **Progetto originale:** lo sviluppo upstream di SM64-PSX è stato spostato su [Codeberg](https://codeberg.org/malucart/sm64-psx). **Questo repository GitHub è un fork indipendente** con correzioni aggiuntive. [Confronto dei 10 commit successivi alla migrazione](docs/CODEBERG_UPSTREAM_AUDIT_2026-11.md).

# Super Mario 64 (DualShock Version)

- This is a fork of [the full decompilation of Super Mario 64 (J), (U), (E), and (SH)](https://github.com/n64decomp/sm64).
- It is heavily modified and can no longer target Nintendo 64, only PSX and PC (for debugging).
- For now, it can only build from the US version.

This repo does not include all assets necessary for compiling the game.
An original copy of the game is required to extract the assets.

## Features

- Cool "DUAL SHOCK™ Compatible" graphic mimicking the original "振動パック対応" (Rumble Pak Compatible) graphic
- An analog rumble signal is now produced for the DualShock's large motor, in addition to the original modulated digital signal for the small motor and for the SCPH-1150 Dual Analog Controller
- Low-precision soft float implementation specially written for PSX to reduce the performance impact of floats
- Large amounts of code have been adapted to use fixed point math, including the 16-bit integer vectors and matrices that are standard on PSX
- Simplified rewritten render graph walker
- Static level meshes are compiled into PS1 draw lists where they fit the original overlay allocation. The compiler splits large textured faces and propagates splits across shared edges.
- Dynamic and unsupported display lists use the runtime compiler. The GTE projects the vertices and the GPU draws packets in ordering-table order.
- Mario's animations are compressed (from 580632 to 190324 bytes) and placed in a corner of VRAM rather than being loaded from storage (we don't have the luxury of a fast cartridge to read from in the middle of a frame)
- Custom profiler
- Custom texture encoder that quantizes all textures to 4 bits per pixel
- Translucent texture shadows replaced with subtractive hexagonal shadows, as the PSX doesn't support arbitrary translucency
- The right stick pans the free camera and supplies C-button input in the original stepped camera modes.
- (WIP) Simplified rewritten Goddard subsystem

## PlayStation controls and saves

Mario D starts with 120 stars, the cap switches, the castle doors and the cannons unlocked. The port seeds this progress into an empty D slot at boot. Save changes are currently session-only because the memory card backend is still a stub.

Hold L1 while pressing Start on the title screen to open the level test menu. It uses Mario D and act 6.

## Remaining issues

- The soundtrack still requires WAV inputs. Sound effects can differ from the original in pitch, timing or missing notes.
- Some animations, scripted camera shots and the first-visit Lakitu cutscene still need gameplay validation. The interactive Mario head is unfinished.
- Level starts, the pause menu and the final cake screen can be tested on a 2 MiB emulator. The complete run through every area, cutscene and credits entry still needs regression coverage.
- Some faces still exhibit affine distortion, sorting errors or visible seams. Texture uploads can still pause gameplay when an asset first enters VRAM.

The [Psy-Q run-time overview](https://psx.arthus.net/sdk/Psy-Q/DOCS/LibOver47.pdf) and [PSX-SPX GPU documentation](https://psx-spx.consoledev.net/dev/ps1/gpu/misc/) describe the affine texture interpolation that motivates build-time subdivision.

### GitHub Actions: build images for the PlayStation emulator

The workflow [SM64-PSX emulator builds and RGB555 checks](.github/workflows/light-color-test.yml)
runs on every push to `main` and on manual dispatch. It runs conversion and
MIPS syntax checks, then builds **two PS1 disc images** using the repository's
Dockerfile: RGB888 (default lighting) and RGB555 (`LIGHTS_15BIT=1`).
Each successful build uploads `sm64.iso`, `sm64.cue`, the optional ELF
debug image and SHA-256 checksums as an Actions artifact (retained for 3 days).
Open the `.cue` file from the extracted artifact in DuckStation or PCSX-Redux.

**Required one-time setup:** GitHub runners cannot access an N64 ROM stored in
a local folder or the ChatGPT Library. Add an Actions repository secret named
`SM64_US_ROM_URL` under **Settings → Secrets and variables → Actions**. An
existing Google Drive sharing link is supported via `gdown`; you can also
reuse the previous secret name `SM64_ROM_DRIVE_URL` in this repository.
It must contain a private, directly downloadable URL for *your own* unmodified
US Super Mario 64 `baserom.us.z64`. If authentication requires a header,
optionally define `SM64_US_ROM_AUTH_HEADER` (for example
`Authorization: Bearer <token>`). The workflow validates the file size,
big-endian header and expected US SHA-1 before building. It will **fail
explicitly** if this prerequisite is missing, rather than reporting a false
green build. The source N64 ROM is not committed or uploaded as an artifact.

**Copyright:** the generated PS1 ISO contains game assets. On a public
repository, GitHub Actions artifacts may also be publicly accessible.
Only generate/distribute the image where you have the necessary rights.

### Optional 15-bit lighting (RGB555)

Set `LIGHTS_15BIT=1` to quantize ambient and directional light colors to 5 bits
per RGB channel. The PS1 GTE renderer and the PC reference renderer use the
same RGB555 conversion, and RGB888 remains the default (`LIGHTS_15BIT=0`).

```sh
make LIGHTS_15BIT=1
# For PC debugging:
make PC=1 LIGHTS_15BIT=1
```

The original N64 `Light_t` and `Ambient_t` layouts are **not** changed: this
avoids corrupting light data and pointers from generated assets. Consequently
this optional mode reduces color precision, **not** the size of light assets in
RAM/ROM. The color conversion has host-side regression tests in
`tests/light_rgb555_test.c`. Full game and hardware rendering still require
in-game testing. Run `make clean` before switching modes if reusing an older
build directory.

## Building

1. Place a *Super Mario 64* ROM named exactly `baserom.us.z64` into the repository's root directory. For now, only US ROMs are supported.
2. (Optional) Place WAV tracks numbered `0.wav` through `37.wav` in `.local`. Missing tracks are silent and require no XA streaming. See the [track list](https://codeberg.org/malucart/sm64-psx/issues/14#issuecomment-20591789).
3. Now you need a properly set up build environment. A Dockerfile is provided to simplify this. To use it, ensure you have [Docker](https://www.docker.com) (or a compatible alternative) installed and set up. (if on Linux, don't forget to put it in [rootless mode](https://docs.docker.com/engine/security/rootless/) as well!)
	- If on Linux, I've included a convenience script for invoking a container from a terminal. Run `./idc` to enter a Bash shell in the container, or `./idc <command>` to run one command in it (for example, `./idc make` or `./idc make clean`). If this works for you, go to step 4.
	- If using Zed (any OS), containers are supported right away. Open the repo, press Ctrl+Shift+P, and run the "projects: open dev container" action. The editor will act like it's running inside the container, including the terminal. Open the terminal panel in Zed and go to step 4.
	- If using Visual Studio Code (any OS), you can simply install the Dev Containers extension, open the repository, and click "Reopen in Container" (either from the notification, or from the Ctrl+Shift+P menu). The editor will act like it's running inside the container, including the terminal. Open the terminal panel in VS Code and go to step 4.
	- Alternatively, if you plan to do a lot of PS1 development, you can skip the container and simply have the right things installed on your system, but this is the harder option. You must be using Linux (any). You need FFMPEG's libraries, libpng, xxd, Python 3, meson, GCC or Clang, and version 15 or later of the mipsel-none-elf-gcc toolchain. To build and install mipsel-none-elf-gcc, there is a utility in [this other repo](https://github.com/malucard/poeng). Clone it and run `make install-gcc`. It will take a pretty long time. Then see if step 4 works for you.
4. Simply run `make`, and when it's done, sm64.iso and sm64.cue will be placed in the `build/us_psx/` folder (you can either use the iso file alone, or the cue file if it's required; iso+cue is the same thing as bin+cue). As long as you have a way to boot the game, it should work on regular old PS1 hardware. If you're using an emulator, you may want to enable an 8 MB RAM mode in the settings to reduce crashes, since the game is still so constrained.
	- If you want to debug the game and work on it, run `make DEBUG=1` instead. This mode will require 8 MB of RAM (it will not work on a retail console), and it will be even laggier than normal, but it may catch more bugs in the code instead of crashing mysteriously. (note: don't optimize code based on how it runs in this mode!)
	- On the debugging topic, PCSX-Redux is recommended. First enable the debugger, disable dynarec, and ensure the OpenGL GPU is off. Open sm64.cue from "File > Open Disk Image" first, but then load sm64.elf manually from "File > Load binary" before starting the game, so the emulator can read the debugging symbols. You don't have to reopen the iso/cue every time you recompile (unless the "Preload Disk Image files" option is on), but you do have to reload the elf. See the "Debug" tab for some utilities ("Show Logs", "Show Assembly", and "Show Callstacks" are the most important).
	- If you want to automatically benchmark the game, run `make BENCH=1` instead. This mode also requires 8 MB of RAM (it will not work on a retail console), but it doesn't require a CD, and it will boot directly into a level without requiring any inputs.
	- `make clean` will delete the `build` folder, and `make distclean` will do that but also clean up the `tools` folder (ie. `make -C tools clean`). Basically, run `make distclean` to clean everything and start from scratch if your environment changed or you're having sudden issues.

## Project Structure

	sm64
	├── actors: object behaviors, geo layout, and display lists
	├── assets: animation and demo data
	│   ├── anims: animation data
	│   └── demos: demo data
	├── bin: C files for ordering display lists and textures
	├── build: output directory
	├── data: behavior scripts, misc. data
	├── doxygen: documentation infrastructure
	├── enhancements: example source modifications
	├── include: header files
	├── levels: level scripts, geo layout, and display lists
	├── lib: N64 SDK code
	├── sound: sequences, sound samples, and sound banks
	├── src: C source code for game
	│   ├── audio: audio code
	│   ├── buffers: stacks, heaps, and task buffers
	│   ├── engine: script processing engines and utils
	│   ├── game: behaviors and rest of game source
	│   ├── goddard: rewritten Mario intro screen
	│   ├── goddard_og: backup of original Mario intro screen
	│   ├── menu: title screen and file, act, and debug level selection menus
	│   └── port: port code, audio and video renderer
	├── text: dialog, level names, act names
	├── textures: skybox and generic texture data
	└── tools: build tools

## Contributing

Pull requests are welcome. For major changes, please open an issue first to
discuss what you would like to change.
