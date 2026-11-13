#include "psx_profile.h"
#include <assert.h>
#include <port/gfx/gfx_internal.h>
#include <ps1/gpu.h>
#include <ps1/gpucmd.h>
#include <ps1/registers.h>

static u8 selected_fb = 0;
static u32 ot[OT_LEN];
static u32 packet_pool[PACKET_POOL_LEN];
static u32 frame_start_packet[2][8];

/* A normal textured, shaded quad needs at most 14 GPU words. A decal has
 * an extra untextured pass; reserve 64 words per packet to cover both passes
 * and shadows. If a busy scene exhausts the fixed 64 KiB command buffer,
 * write the packet here and discard it instead of corrupting RAM. */
#define PACKET_RESERVE_WORDS 64
/* Keep 6 KiB of the shared packet pool for HUD, menus and dialog. Large
 * 3D scenes previously exhausted all 64 KiB and made the HUD disappear
 * intermittently; 2D packets now have priority without allocating more RAM. */
#define PACKET_HUD_RESERVE_WORDS 1536
static u32 discarded_packet[PACKET_RESERVE_WORDS];
STATIC_ASSERT(PACKET_POOL_LEN > PACKET_RESERVE_WORDS + PACKET_HUD_RESERVE_WORDS, "HUD reserve exceeds GPU command buffer");
scratchpad static u32* next_packet;
static u32* ui_tail;

static bool frame_open = false;  // CPU owns the OT/arena for a frame being built
static bool frame_in_flight = false;

void gfx_init_buffers() {
	selected_fb = 0;
	frame_open = false;
	frame_in_flight = false;

	for(u32 buffer = 0; buffer < 2; buffer++) {
		u32 x = buffer * XRES;
		u32* start = frame_start_packet[buffer];
		start[1] = gp0_texpage(0, true, false);
		start[2] = gp0_fbOffset1(x, 0);
		start[3] = gp0_fbOffset2(x + XRES - 1, YRES - 2);
		start[4] = gp0_fbOrigin(x, 0);
		start[5] = gp0_rgb(0, 0, 0) | gp0_vramFill();
		start[6] = gp0_xy(x, 0);
		start[7] = gp0_xy(XRES, YRES);
	}
	gfx_swap_buffers(false);
}

static OSTime last_frame_time_us = 0;
void audio_backend_vblank(void);

/* CPU/GPU overlap. A submitted frame is drawn by the GPU while the CPU runs
 * the next frame's game logic; the shared OT and packet arena are reclaimed
 * only when the next frame starts producing packets (geo traversal or DL
 * flush). The frame shown at a 30 Hz swap is therefore the previous one,
 * which is complete by then: one frame of display latency, like the N64. */

static void wait_for_gpu_frame(void) {
	if(!frame_in_flight) return;
	PROF_BEGIN(gpu);
	waitForDMADone();
	waitForGP0Ready(); // GP0 idle: the last primitives have been drawn
	PROF_END(gpu, PROF_GPU_WAIT);
	frame_in_flight = false;
}

void gfx_frame_begin(void) {
	if(frame_open) return;
	wait_for_gpu_frame();
	startOrderingTableClear(ot, OT_LEN);
	next_packet = packet_pool;
	ui_tail = NULL;
	awaitOrderingTableClear();
	frame_open = true;
}

static void wait_for_30hz_vblank(void) {
	/* Never switch the displayed PS1 framebuffer in the middle of a
	 * scanout. Acknowledge any stale VSync IRQ, then wait for at least one
	 * new vertical blank; use a second only if the 30 Hz target is early. */
	PROF_BEGIN(vsync);
	IRQ_STAT = ~(1 << IRQ_VSYNC);
	do {
		waitForVSync();
		audio_backend_vblank();
	} while(last_frame_time_us != 0 && osGetTime() - last_frame_time_us < 1000000 / 30);
	last_frame_time_us = osGetTime();
	PROF_END(vsync, PROF_VSYNC);
}

void gfx_swap_buffers(bool vsync_30fps) {
	gfx_frame_begin(); // an empty frame still needs a valid OT
	u32* start = frame_start_packet[selected_fb];
	u32 prev_ot_entry = ot[OT_LEN - 1];
	ot[OT_LEN - 1] = gp0_tag(0, start);
	start[0] = gp0_tag(7, (void*) prev_ot_entry);

	if(vsync_30fps) {
		// The previous frame (other buffer) was completed when this one
		// reclaimed the arena: present it, then let the GPU draw this one
		// in the background.
		wait_for_gpu_frame();
		wait_for_30hz_vblank();
		GPU_GP1 = gp1_fbOffset(selected_fb ? 0 : XRES, 0);
		GPU_GP1 = gp1_dispBlank(false);
		sendLinkedList(&ot[OT_LEN - 1]);
		frame_in_flight = true;
	} else {
		// Loading/message screens: show this frame as soon as it is drawn.
		sendLinkedList(&ot[OT_LEN - 1]);
		frame_in_flight = true;
		wait_for_gpu_frame();
		GPU_GP1 = gp1_fbOffset(selected_fb ? XRES : 0, 0);
	}
	frame_open = false;
	selected_fb ^= 1;
}

void gfx_discard_frame() {
	frame_open = false;
	gfx_frame_begin();
	gfx_init_global_dl();
	gfx_reset_rsp_jit();
	gfx_reset_dl_exec();
}

ALWAYS_INLINE static Packet gfx_packet_begin_with_reserve(u32 reserve) {
	u32* pool_end = packet_pool + PACKET_POOL_LEN;
	u32* header = next_packet;
	/* Reserve room before writing; never let scenery exhaust the final
	 * 2D packet budget. Each GPU packet itself needs at most 64 words. */
	if(header > pool_end - reserve) {
		header = discarded_packet;
	}
	return (Packet) {
		.header = header,
		.start = header + 1,
		.end = header + 1
	};
}

ALWAYS_INLINE Packet gfx_packet_begin() {
	return gfx_packet_begin_with_reserve(PACKET_RESERVE_WORDS + PACKET_HUD_RESERVE_WORDS);
}

ALWAYS_INLINE Packet gfx_packet_begin_ui() {
	return gfx_packet_begin_with_reserve(PACKET_RESERVE_WORDS);
}

// One inlined copy for callers that choose the budget at run time.
ALWAYS_INLINE Packet gfx_packet_begin_for(bool ui) {
	return gfx_packet_begin_with_reserve(PACKET_RESERVE_WORDS + (ui ? 0 : PACKET_HUD_RESERVE_WORDS));
}

ALWAYS_INLINE void gfx_packet_end(Packet packet, u32 ot_z) {
	if(packet.header == discarded_packet) {
		return; // drop safely, do not link this packet into the GPU OT
	}
	/* Each GP0 packet must fit in the reserved space, with a valid depth. */
	assert(packet.end <= packet_pool + PACKET_POOL_LEN);
	assert(ot_z < OT_LEN);
	u32 packet_size = packet.end - packet.start;
	/* Linked-list DMA checks GPU readiness only once per node: a node must
	 * fit the 16-word GP0 FIFO or words are dropped and the GPU can hang. */
	assert(packet_size <= 16);
	u32 address = (u32) packet.header & 0x00FFFFFF;
	if(ot_z == UI_Z && ui_tail) {
		/* Keep 2D primitives in submission order so a box can be followed
		 * by arbitrarily many glyphs, cursors and icons in the same layer. */
		*packet.header = (*ui_tail & 0x00FFFFFF) | packet_size << 24;
		*ui_tail = (*ui_tail & 0xFF000000) | address;
	} else {
		*packet.header = ot[ot_z] | packet_size << 24;
		ot[ot_z] = address;
	}
	if(ot_z == UI_Z) {
		ui_tail = packet.header;
	}
	next_packet = packet.end;
}
