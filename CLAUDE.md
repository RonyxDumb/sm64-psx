# CLAUDE.md — SM64-PSX Port Development Guide

## 0. Mission

You are working on **SM64-PSX**, a port of Super Mario 64 targeting the original Sony PlayStation.

Your primary objective is:

> **Make the PSX build stable, correct, and progressively optimized until it is comfortably playable on original retail PlayStation hardware.**

The priority order is strict:

1. **Correctness**
2. **Boot reliability**
3. **Retail hardware compatibility**
4. **Memory safety**
5. **Rendering correctness**
6. **Stable frame pacing**
7. **Performance**
8. **Loading time**
9. **Visual fidelity**
10. **Optional enhancements**

Do not sacrifice correctness or retail compatibility merely to make a benchmark look faster.

---

# 1. Fundamental rules

## 1.1 Analyze before modifying

Before changing anything:

- inspect the relevant source files;
- inspect the Makefiles involved;
- inspect compile-time defines;
- inspect the caller and callee chain;
- inspect target-specific `#ifdef`s;
- inspect generated files only to understand them, not to patch them permanently;
- inspect recent Git changes;
- determine whether the problem exists in:
  - common code,
  - PSX-specific code,
  - generated asset code,
  - build tooling,
  - linker configuration,
  - CD-ROM loading,
  - renderer,
  - audio,
  - memory layout.

Never modify several unrelated subsystems at once.

If the reason for a failure is not known, instrument or bisect first.

---

## 1.2 Preserve known-good behavior

A build that boots is more valuable than a theoretically cleaner build that does not.

Before a risky modification:

```bash
git status --short
git diff --stat
git diff
```

If the working tree contains valuable uncommitted work:

```bash
git diff > before_change.patch
```

Prefer committing known-good states:

```bash
git add -A
git commit -m "checkpoint: known-good PSX build"
```

Do not overwrite working code merely because an upstream implementation looks cleaner.

---

## 1.3 One hypothesis at a time

Every change must answer a specific hypothesis.

Good:

> "The renderer stalls because the ordering table exceeds the intended memory range. Add bounds instrumentation and test."

Bad:

> "Rewrite renderer, CD code, startup code, linker script and scratchpad code together."

For every non-trivial change, record:

- problem;
- hypothesis;
- files changed;
- expected effect;
- observed effect;
- whether the hypothesis was confirmed.

---

# 2. Current project context

This project contains multiple build paths and experimental code.

Important target distinction:

- **PSX target** = primary target.
- **PC target** = useful for debugging and validation, but must not dictate PSX architecture.
- Other platform code may exist, but must not be allowed to contaminate the PSX build.

Important directories/files previously involved in PSX work include:

```text
Makefile
Makefile.psx.mk
ext_files_elf.ld

ps1-bare-metal/
    executable.ld
    libc/crt0.c

src/engine/
    math_util.c

src/game/
    game_init.c
    ingame_menu.c

src/port/
    gfx/
        gfx.c
        gfx.h
        gfx_global_dl.c
        gfx_internal.h
        gfx_math.c
        gfx_rsp_jit.c

    psx/
        cd_psx.c
        gfx_backend_psx.c
        gfx_dl_exec_psx.c
        gfx_framebuffers_psx.c
        scratchpad_call.h

tools/
    preprocess_graphics.py
    verify_psx_retail.py
```

Do not assume every file listed above is currently broken. They are simply high-impact areas.

---

# 3. Target hardware assumptions

The production target is **original retail PlayStation hardware**, not an emulator-only environment.

Design for the real hardware constraints:

- MIPS R3000A-class CPU
- approximately 33.8688 MHz CPU clock
- 2 MB main RAM
- 1 MB VRAM
- PS1 GPU command-list architecture
- GTE coprocessor
- CD-ROM with high seek latency relative to RAM
- scratchpad RAM at `0x1F800000`
- no modern MMU/virtual-memory safety net
- no desktop operating system
- strict DMA/cache behavior

Do not rely on:

- 8 MB emulator RAM;
- emulator timing quirks;
- undefined memory contents;
- unaligned behavior that happens to work in one emulator;
- excessive stack usage;
- desktop-style dynamic allocation patterns;
- filesystem assumptions that do not exist on real PSX;
- extremely frequent CD seeks.

`BENCH=1` or other high-memory debugging configurations must never be treated as proof of retail compatibility.

---

# 4. Build workflow

## 4.1 Normal PSX build

Use the repository's PSX build path.

Typical workflow:

```bash
make
```

Expected PSX outputs commonly include:

```text
build/us_psx/sm64.cue
build/us_psx/sm64.iso
build/us_psx/sm64.exe
build/us_psx/sm64.elf
build/us_psx/sm64.map
```

When a clean rebuild is genuinely required:

```bash
make clean
make
```

Do not clean on every iteration.

Incremental builds are preferred during development.

---

## 4.2 Never mix PC build artifacts with PSX assumptions

The PC target may use:

- SDL3;
- MinGW/UCRT;
- LLVM ELF helpers;
- PC-specific generated asset ELF files.

Those requirements are not automatically applicable to PSX.

Do not "fix" a PSX failure by importing PC-only assumptions into common headers.

In particular, be cautious around:

```text
uintptr_t
intptr_t
ptrdiff_t
size_t
pointer width
ELF target width
generated asset ELF architecture
```

The PC host may be x86-64 while original game/asset structures may use 32-bit addresses.

---

# 5. Regression protocol

The project previously had PSX builds that compiled and ran correctly.

Therefore, when a previously working build stops booting, assume a **regression** until proven otherwise.

The first objective is to identify which local change introduced the regression without altering repository history.

## 5.1 First-line regression isolation

Use read-only inspection:

```bash
git status --short
git diff --stat
git diff
git log --oneline --decorate -20
```

Group modified files by subsystem:

```text
startup / linker
CD-ROM
renderer
framebuffer
display-list execution
common graphics
scratchpad
math
game initialization
build/preprocessing
```

Then test hypotheses by manually reverting only the smallest recent experimental change that is clearly understood.

Do not use Git restore/reset as the default debugging method.

---

## 5.2 If the repository becomes unrecoverably messy

Only after multiple unsuccessful repair attempts may a restart from a known baseline be considered.

Before doing so:

1. stop making new experimental changes;
2. inspect all modified and untracked files;
3. save a complete external patch;
4. copy any important untracked files outside the repository;
5. identify exactly which baseline should be restored;
6. explain what local work would be lost or preserved.

Backup command:

```bash
git diff > ../sm64-psx-recovery-backup.patch
```

If untracked files contain work, copy them explicitly to a safe external directory.

Only then may a targeted restore be considered.

Never perform a repository-wide destructive reset automatically.

---

## 5.3 Git bisect

Do not use `git bisect` by default.

It may only be useful if:

- meaningful local commit history exists;
- a specific known-good commit exists;
- a specific known-bad commit exists;
- the user has agreed that checking out historical states will not endanger local work.

If those conditions are not met, use manual subsystem isolation instead.


---

# 6. Boot debugging strategy

A black screen does **not** automatically mean "GPU bug".

Possible causes include:

- executable did not start;
- startup code failed;
- stack or GP corruption;
- BSS initialization failure;
- CD-ROM loader deadlock;
- invalid asset data;
- decompression failure;
- invalid pointer;
- renderer never initialized;
- display remained blanked;
- GPU command list corrupted;
- frame loop never reached;
- level script failed;
- memory overwrite occurred earlier.

Determine the stage first.

---

## 6.1 Use the ELF and MAP files

The PSX ELF is a major debugging artifact.

Useful commands:

```bash
mipsel-none-elf-readelf -h build/us_psx/sm64.elf
mipsel-none-elf-readelf -S build/us_psx/sm64.elf
mipsel-none-elf-nm -n build/us_psx/sm64.elf
mipsel-none-elf-objdump -d build/us_psx/sm64.elf
```

Resolve a real virtual address:

```bash
mipsel-none-elf-addr2line \
    -e build/us_psx/sm64.elf \
    -f -C 0xADDRESS
```

Do not blindly convert an emulator log offset into an ELF virtual address.

First establish what the emulator value represents:

- PC;
- EPC;
- virtual address;
- physical address;
- RAM offset;
- JIT host address;
- DMA destination;
- fault address.

---

# 7. CD-ROM system

The CD system is performance-critical and correctness-critical.

Likely relevant file:

```text
src/port/psx/cd_psx.c
```

## 7.1 Correctness first

A failed CD read must never silently return corrupted or incomplete data to the caller.

If a lower-level function can fail, propagate the failure.

Avoid logic equivalent to:

```c
try_read();
printf("error");
return_as_if_successful();
```

Instead:

- return a status;
- retry only when safe;
- abort the current load if recovery fails;
- preserve command synchronization.

---

## 7.2 Avoid pathological seeking

The PS1 CD-ROM is slow at random seeking.

Optimization goals:

- batch adjacent reads;
- preload commonly used tables;
- preserve locality in `EXT.DAT`;
- avoid repeatedly reopening/searching the ISO filesystem;
- cache the LBA of frequently used files;
- avoid `Init` unless genuinely required;
- avoid repeated `Setloc -> ReadN -> Pause` for tiny reads when a larger contiguous transfer is possible.

Measure first.

Do not rewrite the whole loader without evidence.

---

## 7.3 Instrument CD reads

Useful temporary counters:

```text
number of reads/frame
number of Setloc commands
number of seeks
bytes transferred
largest transfer
smallest transfer
average transfer
LBA seek distance
retries
timeouts
```

A PSX-optimized loader should minimize random head movement.

---

# 8. RAM budget

Retail PS1 RAM is extremely limited.

Treat RAM as a budget, not an infinite resource.

Track at minimum:

```text
.text
.rodata
.data
.bss
stack
heap
framebuffers if stored in RAM
display lists / ordering tables
level assets
audio buffers
CD staging buffers
scratch buffers
JIT/interpreter tables
```

Useful inspection:

```bash
mipsel-none-elf-size build/us_psx/sm64.elf
mipsel-none-elf-readelf -S build/us_psx/sm64.elf
```

Inspect:

```text
build/us_psx/sm64.map
```

The linker script must protect against:

- `.bss` overlapping stack;
- stack overlapping heap;
- large static buffers exceeding 2 MB;
- accidental duplicate assets in RAM;
- debug buffers remaining in release builds.

---

# 9. Stack discipline

PS1 stack space is limited.

Avoid:

```c
u8 giant_buffer[100000];
```

inside functions.

Prefer:

- static storage where appropriate;
- shared scratch buffers;
- scratchpad memory;
- streaming processing;
- bounded buffers.

Watch recursion.

Use linker assertions where possible.

---

# 10. Scratchpad strategy

The PS1 scratchpad is fast but tiny.

Relevant area:

```text
0x1F800000
```

Possible project file:

```text
src/port/psx/scratchpad_call.h
```

Use scratchpad only for:

- very hot temporary data;
- small matrix/vector workspaces;
- tight renderer/JIT helpers;
- short-lived structures.

Never store persistent game state there.

Never assume emulator behavior proves correct scratchpad synchronization.

Keep allocations explicit and documented.

Recommended layout comment:

```c
/*
 * PSX scratchpad layout
 *
 * 0x1F800000 - 0x1F8000XX : ...
 * 0x1F8000XX - 0x1F8000YY : ...
 *
 * Total <= 1024 bytes
 */
```

---

# 11. Renderer optimization

Likely relevant files:

```text
src/port/psx/gfx_backend_psx.c
src/port/psx/gfx_dl_exec_psx.c
src/port/psx/gfx_framebuffers_psx.c

src/port/gfx/gfx.c
src/port/gfx/gfx_rsp_jit.c
```

Primary goal:

> Reduce CPU cost per rendered frame while preserving correct PSX GPU output.

---

## 11.1 Profile before optimizing

Break frame time into:

```text
game logic
animation
collision
camera
display-list generation
display-list interpretation
transform/GTE work
clipping
GPU command generation
ordering-table construction
GPU wait
VBlank wait
CD activity
audio
```

Do not optimize based on intuition alone.

---

## 11.2 Display-list interpreter/JIT

If the port dynamically interprets N64-style display lists, this can become a major CPU cost.

Investigate:

- repeated command decoding;
- redundant state transitions;
- repeated segmented-address translation;
- repeated texture-state setup;
- repeated matrix loads;
- unnecessary branches;
- expensive function-pointer dispatch;
- conversion work repeated every frame for static display lists.

Potential optimizations:

### Static display-list preprocessing

Where safe, convert static display lists during the build rather than every frame.

### Command specialization

Convert generic command paths into PSX-specific fast paths.

### State caching

Avoid resending GPU state when unchanged.

Track:

```text
texture
draw mode
semi-transparency
CLUT
texture page
geometry mode
matrix
lighting state
```

### Reduce function-call overhead

Hot inner loops on R3000A should avoid excessive abstraction.

But do not sacrifice correctness until profiling confirms the function is hot.

---

# 12. GTE usage

The PlayStation GTE should be used aggressively for suitable 3D math.

Candidates:

- vertex transforms;
- projection;
- depth;
- lighting where compatible;
- vector/matrix operations;
- clipping support.

Avoid doing expensive transform math in generic C when the GTE can do it substantially faster.

However:

- preserve correct fixed-point ranges;
- handle overflow;
- respect GTE pipeline hazards;
- verify results against reference output;
- do not blindly replace numerically sensitive gameplay math with fixed point.

Rendering math and gameplay math do not necessarily require the same precision model.

---

# 13. Floating-point elimination

R3000A has no fast general-purpose hardware floating-point pipeline comparable to modern CPUs.

Locate hot float usage:

```bash
grep -RniE '\bf32\b|\bfloat\b|sqrtf|sinf|cosf|atan2f' src
```

Do not mechanically remove all floating point.

Instead classify:

```text
gameplay-critical
cold-path
load-time
render-hot-path
physics-hot-path
camera
animation
```

Prioritize replacing floating point in high-frequency PSX-only rendering paths.

Possible approaches:

- fixed-point;
- lookup tables;
- GTE;
- integer approximations;
- cached results.

Validate behavior carefully.

---

# 14. GPU optimization

The PS1 GPU has different bottlenecks from modern GPUs.

Avoid:

- excessive overdraw;
- giant full-screen translucent primitives;
- unnecessary state changes;
- excessive texture-page switching;
- excessive CLUT changes;
- tiny fragmented primitives;
- redundant clears;
- unnecessary VRAM copies.

Measure primitive count per frame.

Track:

```text
triangles
quads
sprites
lines
GPU packets
texture-page switches
CLUT switches
semi-transparent primitives
```

---

# 15. Framebuffer strategy

Relevant file may include:

```text
src/port/psx/gfx_framebuffers_psx.c
```

Verify:

- display area;
- draw area;
- framebuffer positions;
- double buffering;
- VRAM non-overlap;
- texture/CLUT non-overlap;
- display enable/disable logic;
- VBlank synchronization.

A black screen can be caused by valid rendering into the wrong VRAM page.

Do not assume `GPU=0%` in an emulator log directly identifies the game-side GPU bug.

---

# 16. Geometry simplification

Performance optimization may eventually require content-side adjustments.

Only after renderer optimization is understood, consider PSX-specific LOD.

Candidates:

- distant scenery;
- high-poly decorative objects;
- particles;
- enemies far from camera;
- decorative transparency.

Possible PSX-only changes:

```text
distance-based draw culling
lower-poly models
lower animation update frequency
reduced particles
simplified effects
lower draw distance
```

Do not degrade visuals globally before establishing the actual bottleneck.

---

# 17. Culling

Aggressive culling is likely one of the most valuable optimizations.

Investigate:

- frustum culling;
- distance culling;
- room/area culling;
- object visibility;
- particle culling;
- backface rejection.

Avoid transforming vertices for objects guaranteed not to be visible.

Target:

> Reject invisible work before expensive display-list processing and GTE transformation.

---

# 18. Object/update optimization

SM64 may update many objects each frame.

Profile:

- object count;
- active object count;
- behavior execution;
- collision checks;
- distance checks;
- animation updates.

Possible optimization:

```text
far objects:
    skip expensive animation
    update every 2/4 frames
    skip collision if irrelevant
    skip rendering
```

Never change gameplay-visible behavior without validating it.

---

# 19. Collision

Collision can become CPU-heavy.

Investigate:

- floor queries;
- wall queries;
- ceiling queries;
- repeated triangle traversal;
- spatial partitioning;
- object collision.

Potential improvements:

- cache common floor results;
- improve spatial filtering;
- reduce queries for inactive/far objects;
- avoid repeated identical queries within one frame.

Preserve SM64 behavior.

---

# 20. Audio

Audio must not destabilize the frame loop.

Track:

- mixing cost;
- buffer size;
- update frequency;
- CD contention;
- DMA usage;
- underruns.

If music is unavailable during development, distinguish:

```text
build warning
missing assets
disabled music
actual runtime failure
```

Do not treat:

```text
music not found!
```

as the cause of unrelated linker/runtime bugs without evidence.

---

# 21. Asset preprocessing

Likely relevant:

```text
tools/preprocess_graphics.py
extract_assets.py
ext_files_elf.ld
```

Generated `.processed.c`, `.o2`, `.elf`, asset tables, and external data files are products of the build system.

Never permanently hand-edit generated files unless the build system explicitly expects it.

Fix the generator or source input.

When generated symbols disappear:

- determine source asset;
- determine preprocessing condition;
- determine region;
- determine `#ifdef`;
- determine whether stale build artifacts exist.

---

# 22. Compiler optimization

Do not immediately enable the most aggressive global optimization flags.

Compare:

```text
-O2
-O3
-Os
```

on PSX.

For each configuration measure:

```text
ELF size
RAM usage
boot success
frame time
level stability
compiler bugs / UB sensitivity
```

A smaller instruction cache footprint may outperform a theoretically more optimized binary.

Use function-level attributes only after profiling.

---

# 23. Undefined behavior

Legacy N64-derived code can contain assumptions that modern compilers optimize aggressively.

Potential hazards:

```text
signed overflow
pointer aliasing
invalid shifts
unaligned access
out-of-bounds reads
type punning
pointer/integer conversion
uninitialized data
```

If an optimization level causes a regression:

1. identify the exact UB;
2. fix it if practical;
3. otherwise isolate the affected function;
4. avoid globally disabling optimization unless necessary.

---

# 24. Cache behavior

The R3000A has small caches.

Optimization must consider code/data locality.

Prefer:

- compact hot loops;
- contiguous tables;
- fewer indirect calls;
- fewer large generic handlers;
- smaller working sets.

Avoid enormous hot functions created by uncontrolled inlining.

---

# 25. DMA

DMA is valuable but dangerous.

When using DMA:

- verify source/destination alignment;
- verify transfer size;
- verify channel state;
- verify cache coherency requirements;
- do not reuse memory until transfer completion;
- avoid blocking CPU unnecessarily.

Use DMA to overlap work where safe.

---

# 26. Performance measurement

Never claim "faster" without measurement.

For each optimization record:

```text
Test:
Level:
Camera:
Emulator/hardware:
Build:
FPS:
Frame time:
CPU:
Primitive count:
RAM:
Notes:
```

Preferred test scenes:

```text
boot/title
castle exterior
castle interior
Bob-omb Battlefield
high object-count scene
high geometry scene
transparent/effect-heavy scene
Bowser scene
water-heavy scene
```

Keep camera position reproducible when possible.

---

# 27. Original hardware testing

Emulator success is necessary but insufficient.

Test on real PS1 when possible.

Real-hardware checklist:

```text
boots consistently
no random lockup
no CD read deadlock
no audio stutter
no framebuffer corruption
no texture corruption
no scratchpad corruption
no stack overflow
stable controller input
stable 30/20 FPS behavior
reasonable load times
```

Record console model if relevant.

---

# 28. Performance target

Desired final target:

> **Stable, playable frame rate on a stock retail PlayStation with acceptable frame pacing.**

Preferred target where realistically achievable:

```text
30 FPS
```

If a scene cannot sustainably reach 30 FPS, prioritize:

1. stable frame pacing;
2. predictable lower frame rate;
3. input responsiveness;
4. consistent simulation;
5. visual compromises.

A stable 20 FPS is better than wildly oscillating 15–30 FPS.

Do not alter game simulation speed unintentionally.

---

# 29. Optimization order

Follow this order unless profiling proves otherwise.

## Phase A — Restore stability

- boot;
- CD reads;
- renderer;
- level loading;
- memory safety;
- retail 2 MB operation.

## Phase B — Establish metrics

- frame timer;
- subsystem timers;
- object counts;
- primitive counts;
- CD metrics;
- RAM map.

## Phase C — Remove major CPU waste

- display-list interpreter overhead;
- transform/GTE work;
- redundant render state;
- invisible object processing;
- hot float math.

## Phase D — GPU workload

- overdraw;
- primitive count;
- texture switching;
- transparency;
- LOD/culling.

## Phase E — Streaming/loading

- seek reduction;
- larger sequential reads;
- asset layout;
- caching.

## Phase F — Secondary optimization

- collision;
- behaviors;
- animation;
- audio;
- compiler tuning.

---

# 30. Do not perform these actions without strong justification

Never casually:

- rewrite `crt0.c`;
- rewrite linker scripts;
- move `$gp`;
- change stack location;
- change global pointer assumptions;
- change pointer widths;
- change asset ELF width;
- change CD interrupt state machine;
- change framebuffer VRAM layout;
- change scratchpad ABI;
- rewrite the display-list interpreter;
- change common N64 graphics macros;
- change generated asset symbol conventions.

These are high-risk changes.

If one is required:

1. explain exactly why;
2. preserve the old implementation;
3. create a checkpoint;
4. make the smallest possible patch;
5. test boot immediately;
6. test at least one level immediately.

---

# 31. Coding style for optimization work

Favor explicit, low-overhead C appropriate for embedded hardware.

Good:

```c
if (unlikely_condition) {
    ...
}
```

when supported and measured.

Avoid excessive abstraction in hot PSX code.

Document fixed-point formats:

```c
/* 12.4 fixed point */
s16 x;
```

Document hardware registers and magic constants.

Never leave unexplained:

```c
0x1F800000
0x1F801810
0x1F801814
```

Use named constants where practical.

---

# 32. Debug code policy

Debug instrumentation must be easy to disable.

Prefer:

```c
#ifdef PSX_PROFILE
...
#endif
```

or:

```c
#ifdef PSX_DEBUG
...
#endif
```

Do not leave frequent `printf()` calls in performance-critical release paths.

Serial output can heavily disturb timing.

---

# 33. Performance instrumentation design

Create lightweight instrumentation instead of verbose logging.

Potential API:

```c
psx_profiler_begin(PROF_RENDER);
psx_profiler_end(PROF_RENDER);
```

Categories:

```text
PROF_FRAME
PROF_GAME
PROF_OBJECTS
PROF_COLLISION
PROF_RENDER
PROF_GFX_DL
PROF_GTE
PROF_GPU_WAIT
PROF_AUDIO
PROF_CD
```

Store counters in RAM and print only occasionally.

Never print every primitive/frame over serial during real performance testing.

---

# 34. Suggested optimization experiments

Only perform after stability is restored.

Potential experiments:

### Experiment 1 — Display-list command histogram

Count every command type over 300 frames.

Find the most frequent commands.

Optimize those first.

### Experiment 2 — Segmented-address cache

Measure repeated segmented-to-virtual translations.

Cache safely if beneficial.

### Experiment 3 — Static DL preprocessing

Identify display lists invariant across frames.

Preconvert them.

### Experiment 4 — GTE transform batching

Process vertices in batches to reduce setup overhead.

### Experiment 5 — Object distance throttling

Update far objects less frequently.

Measure gameplay impact.

### Experiment 6 — CD asset ordering

Reorder `EXT.DAT` based on actual level loading sequence.

Measure seek reduction.

### Experiment 7 — Compiler configuration

Benchmark `-O2`, `-O3`, `-Os`.

Do not assume `-O3` wins.

---

# 35. File-specific caution

## `ps1-bare-metal/libc/crt0.c`

Startup-critical.

Any regression here can prevent the entire game from initializing.

Touch only with proof.

---

## `ps1-bare-metal/executable.ld`

Memory-layout-critical.

Before changing, inspect:

```bash
mipsel-none-elf-readelf -S build/us_psx/sm64.elf
mipsel-none-elf-size build/us_psx/sm64.elf
```

---

## `src/port/psx/cd_psx.c`

State-machine-critical.

Preserve interrupt sequencing.

Never return success after failed reads.

---

## `src/port/psx/gfx_backend_psx.c`

GPU initialization-critical.

Verify display mode, resolution, display enable and draw environment.

---

## `src/port/psx/gfx_framebuffers_psx.c`

VRAM-layout-critical.

Check framebuffer/texture/CLUT overlap before changing coordinates.

---

## `src/port/psx/gfx_dl_exec_psx.c`

Likely major performance hotspot.

Profile heavily.

Optimize incrementally.

---

## `src/port/gfx/gfx_rsp_jit.c`

Potentially very high-impact.

Do not rewrite until command-frequency data exists.

---

## `src/engine/math_util.c`

Shared gameplay math.

PSX-specific optimization here can cause gameplay regressions.

Prefer target-specific fast paths where necessary.

---

# 36. Required reasoning format before major patches

Before editing a major subsystem, produce a short engineering note:

```text
Problem:
Evidence:
Likely root cause:
Alternative explanations:
Files involved:
Minimal proposed change:
Risks:
How to test:
Expected result:
Rollback plan:
```

Do this before touching more than one high-risk file.

---

# 37. Required validation after each optimization

After every optimization:

```text
[ ] builds successfully
[ ] boots
[ ] reaches title/menu
[ ] enters level
[ ] renders correctly
[ ] controller works
[ ] audio still works
[ ] no new CD errors
[ ] no obvious memory corruption
[ ] performance measured
[ ] result compared with baseline
```

If any correctness regression appears, revert before continuing.

---

# 38. Git policy

Git is primarily an **inspection and emergency-recovery tool** for this project.

Normal autonomous work must not create repository history.

Do not automatically run:

```text
git add
git commit
git commit --amend
git stash
git checkout
git switch
git restore
git reset
git clean
git rebase
git merge
git cherry-pick
git push
git pull
git fetch
```

Normal Git use should be limited to read-only commands such as:

```bash
git status --short
git diff
git diff --stat
git log
git show
```

Keep optimization experiments small enough that the agent can undo its own latest change manually if it performs worse.

If, after several failed attempts, the repository must be returned to a known baseline, first create an external backup and then ask for approval before any destructive Git operation.

No GitHub remote, cloud backup or published repository may be assumed to exist.


---

# 39. When a change makes performance worse

Revert it.

Do not keep a complicated optimization because it "should" be faster.

Hardware is the authority.

---

# 40. When emulator and hardware disagree

Original hardware wins.

Use the emulator to:

- inspect registers;
- inspect CD commands;
- use breakpoints;
- inspect memory;
- collect traces.

Use retail hardware to validate:

- timing;
- DMA;
- CD behavior;
- actual performance;
- stability.

---

# 41. Immediate recovery goal

Before aggressive optimization begins:

1. restore a known-good PSX build;
2. confirm it boots normally;
3. confirm at least one level is playable;
4. record the exact working state and relevant diffs without creating a commit;
5. remove accidental experimental/debug modifications;
6. establish baseline FPS and RAM usage;
7. begin optimization from that stable state.

Do not optimize the currently broken build until the regression is understood.

---

# 42. First optimization milestone

After restoring the working build, the first performance milestone should be:

> Obtain a repeatable frame-time breakdown in one representative level without materially changing performance.

Then identify the top two bottlenecks.

Only optimize those.

---

# 43. Long-term objective

The ideal end state is a PSX port that:

- boots reliably on original hardware;
- remains within retail RAM/VRAM limits;
- loads levels reliably from CD;
- has stable frame pacing;
- minimizes unnecessary CD seeking;
- uses GTE effectively;
- minimizes CPU-side display-list overhead;
- performs aggressive but safe culling;
- avoids unnecessary floating-point work in hot PSX paths;
- uses efficient GPU packet generation;
- has no emulator-only assumptions;
- preserves the feel and behavior of Super Mario 64 as much as realistically possible.

Performance improvements must remain measurable, reversible and evidence-driven.

---

# 44. Autonomous execution policy

Work autonomously inside the repository.

Do not ask for confirmation before routine, reversible engineering work such as:

- inspecting source files;
- searching the repository;
- reading build scripts;
- reading Git status/diffs/history;
- compiling;
- running non-destructive tests;
- examining ELF/MAP files;
- inspecting compiler and linker errors;
- adding temporary diagnostics;
- making focused source-code changes;
- fixing compilation errors caused by the current change;
- reverting the agent's own immediately previous experiment manually;
- profiling;
- benchmarking;
- comparing before/after performance;
- continuing to the next clearly supported optimization.

Use this loop:

```text
ANALYZE
-> FORM HYPOTHESIS
-> MAKE ONE FOCUSED CHANGE
-> BUILD
-> TEST
-> MEASURE
-> KEEP OR MANUALLY REVERT
-> CONTINUE
```

Do not stop merely to explain what should be done if the repository and available tools already provide enough information to do it.

Do not ask questions whose answers can be obtained by:

- reading source code;
- searching symbols;
- inspecting build files;
- compiling;
- reading logs;
- examining generated ELF/MAP data;
- comparing current files with the repository baseline.

Stop and ask the user only when:

- physical PS1 testing is required and cannot be automated;
- required information is genuinely absent;
- credentials or external services are required;
- several mutually exclusive architectural choices have major consequences;
- a destructive Git recovery operation is being considered;
- user-owned work may be lost;
- continuing would require guessing rather than engineering evidence.

Git write operations are **not** part of autonomous permission.

The agent must not create commits, branches, stashes, resets, restores, cleans, pushes, pulls or other Git mutations during ordinary work.

If several attempts have failed and restarting from a baseline appears necessary:

1. stop;
2. save an external patch backup;
3. preserve important untracked files;
4. explain the proposed recovery;
5. request user approval before destructive Git operations.


---

# 45. Final instruction to the coding agent

Do not act like a code generator.

Act like an embedded/console porting engineer.

Before every substantial modification:

1. understand the subsystem;
2. inspect the existing implementation;
3. identify the bottleneck or failure with evidence;
4. propose the smallest safe change;
5. compile;
6. test;
7. measure;
8. keep or revert based on results.

The goal is not to produce the most code.

The goal is to make **SM64-PSX run correctly and as efficiently as possible on a real PlayStation**.
