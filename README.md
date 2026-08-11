# Super Mario 64 (PS1 Port)

- This is a fork of [the full decompilation of Super Mario 64 (J), (U), (E), and (SH)](https://github.com/n64decomp/sm64).
- It is heavily modified and can no longer target Nintendo 64, only PSX.
- There are still many limitations.
- It can only build from the US version.

This repo does not include all assets necessary for compiling the game.
An original copy of the game is required to extract the assets.

## Features

- Cool "DUAL SHOCK™ Compatible" graphic mimicking the original "振動パック対応" (Rumble Pak Compatible) graphic
- An analog rumble signal is now produced for the DualShock's large motor, in addition to the original modulated digital signal for the small motor and for the SCPH-1150 Dual Analog Controller
- Low-precision soft float implementation specially written for PSX to reduce the performance impact of floats
- Large amounts of code have been adapted to use fixed point math, including the 16-bit integer vectors and matrices that are standard on PSX
- Simplified rewritten render graph walker
- Tessellation (up to 2x) to reduce issues with large polygons
- RSP display lists are compiled just-in-time into a custom display list format that is more compact and faster to process
- Display list preprocessor that removes commands we won't use and optimizes meshes (TODO: make it fix more things)
- Mario's animations are compressed (from 580632 to 190324 bytes) and placed in a corner of VRAM rather than being loaded from storage (we don't have the luxury of a fast cartridge to read from in the middle of a frame)
- Custom profiler
- Custom texture encoder that quantizes all textures to 4 bits per pixel
- Translucent texture shadows replaced with subtractive hexagonal shadows, as the PSX doesn't support arbitrary translucency
- (WIP) Camera system adapted to rotate with the right analog stick
- (WIP) Simplified rewritten Goddard subsystem

## Known issues

- Some of Mario's animations do not play, and may even crash the game
- Music cannot be generated at build time without manually obtaining the tracks
- Sound effects work but sometimes sound odd or are missing notes
- The camera cannot be controlled in many levels due to the unfinished camera control implementation
- Crashes when entering certain levels (due to insufficient memory?)
- Ending sequence crashes on load
- When reaching the bridge in the castle grounds, Mario looks up but Lakitu never comes over (you need to do the Lakitu skip)
- Poles do not go down when pounded
- Textures are loaded individually, causing long stutters and loading times
- Stretched textures due to PSX limitations (the graphics preprocessor could help)
- Tessellation is not good enough to fix all large polygons (the graphics preprocessor could help)
- Some textures are rendered incorrectly (RSP JIT issues?)
- Title screen is unfinished
- Pause menu doesn't work

## Building

1. Place a *Super Mario 64* ROM named exactly `baserom.us.z64` into the repository's root directory. For now, only US ROMs are supported.
2. (Optional) Create a folder named `.local` in the root of the repo and place every track of the soundtrack in it as a .wav file, numbered from 0 to 37 (0.wav, 1.wav, etc). See [here](https://codeberg.org/malucart/sm64-psx/issues/14#issuecomment-20591789) for the full track list. Don't worry too much about this step, you can just skip it and play without background music. One day this will be done automatically, but it will take some work, so it's low priority.
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
