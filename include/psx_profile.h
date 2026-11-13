#pragma once
/*
 * Lightweight PS1 frame-time profiler (build with `make PROFILE=1`).
 *
 * Each category accumulates root-counter-1 ticks (HBlank clock, ~63.6 us)
 * into psx_prof.ticks[]; psx_prof.frames counts game frames. Nothing is
 * printed: read the `psx_prof` symbol from RAM (emulator save state or
 * debugger) and divide by frames. Without PSX_PROFILE every macro is empty,
 * so release builds are unaffected.
 */
#include <types.h>

enum {
	PROF_FRAME,     // whole game-loop iteration
	PROF_OBJECTS,   // update_objects(): behaviours, collision, Mario
	PROF_GEO,       // geo_process_root(): scene graph, matrices, DL emission
	PROF_DL,        // gfx_flush_global_dl(): DL execution, GTE, GPU packets
	PROF_GPU_WAIT,  // waiting for the previous frame's GPU DMA
	PROF_VSYNC,     // waiting for VBlank / 30 Hz pacing
	PROF_AUDIO,     // audio_game_loop_tick()
	PROF_CD,        // synchronous CD reads
	PROF_CLIP,      // (inside DL) frustum clipper
	PROF_EMIT,      // (inside DL) GPU packet emission
	PROF_COUNT
};

enum {
	PROF_N_POLYS,      // draw_poly() calls (incl. tessellation children)
	PROF_N_PACKETS,    // GPU polygon packets emitted
	PROF_N_CLIPPED,    // polygons sent through the frustum clipper
	PROF_N_TESSELLATED,// polygons split by near-camera tessellation
	PROF_N_CULLED,     // vertex groups rejected by the sphere test
	PROF_N_CULL_TESTS, // vertex-group sphere tests
	PROF_N_COUNT
};

typedef struct {
	u32 frames;
	u32 ticks[PROF_COUNT];
	u32 max_frame_ticks;
	u32 counts[PROF_N_COUNT];
} PsxProfile;

#ifdef PSX_PROFILE
extern PsxProfile psx_prof;
static inline u16 psx_prof_now(void) {
	return *(volatile u16*) 0xBF801110; // root counter 1 value (HBlank clocked, set up by osGetTime)
}
#define PROF_BEGIN(name) u16 _prof_start_##name = psx_prof_now()
#define PROF_END(name, cat) (psx_prof.ticks[cat] += (u16) (psx_prof_now() - _prof_start_##name))
#define PROF_COUNT(cat) (psx_prof.counts[cat]++)
#else
#define PROF_BEGIN(name) ((void) 0)
#define PROF_END(name, cat) ((void) 0)
#define PROF_COUNT(cat) ((void) 0)
#endif
