#include <assert.h>
#include <port/gfx/gfx_internal.h>
#include <ps1/gpu.h>
#include <ps1/gpucmd.h>
#include <ps1/registers.h>

static u8 selected_fb = 0;
static Framebuffer fb[2];

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
scratchpad static u32* ot;
scratchpad static u32* next_packet;

void gfx_init_buffers() {
	startOrderingTableClear(fb[0].ot, OT_LEN);
	awaitOrderingTableClear();
	startOrderingTableClear(fb[1].ot, OT_LEN);
	awaitOrderingTableClear();
	selected_fb = 0;
	ot = fb[selected_fb].ot;
	next_packet = fb[selected_fb].packet_pool;

	// set the rendering area
	fb[0].frame_start_packet[1] = gp0_texpage(0, true, false);
	fb[0].frame_start_packet[2] = gp0_fbOffset1(0, 0);
	fb[0].frame_start_packet[3] = gp0_fbOffset2(XRES - 1, YRES - 2);
	fb[0].frame_start_packet[4] = gp0_fbOrigin(0, 0);
	// clear the rendering area
	fb[0].frame_start_packet[5] = gp0_rgb(0, 0, 0) | gp0_vramFill();
	fb[0].frame_start_packet[6] = gp0_xy(0, 0);
	fb[0].frame_start_packet[7] = gp0_xy(XRES, YRES);

	// set the rendering area
	fb[1].frame_start_packet[1] = gp0_texpage(0, true, false);
	fb[1].frame_start_packet[2] = gp0_fbOffset1(XRES, 0);
	fb[1].frame_start_packet[3] = gp0_fbOffset2(XRES * 2 - 1, YRES - 2);
	fb[1].frame_start_packet[4] = gp0_fbOrigin(XRES, 0);
	// clear the rendering area
	fb[1].frame_start_packet[5] = gp0_rgb(0, 0, 0) | gp0_vramFill();
	fb[1].frame_start_packet[6] = gp0_xy(XRES, 0);
	fb[1].frame_start_packet[7] = gp0_xy(XRES, YRES);
	gfx_swap_buffers(false);
}

static OSTime last_frame_time_us = 0;

void gfx_swap_buffers(bool vsync_30fps) {
	u32 prev_ot_entry = ot[OT_LEN - 1];
	ot[OT_LEN - 1] = gp0_tag(0, fb[selected_fb].frame_start_packet);
	fb[selected_fb].frame_start_packet[0] = gp0_tag(7, (void*) prev_ot_entry);

	sendLinkedList(&ot[OT_LEN - 1]);
	GPU_GP1 = gp1_fbOffset(selected_fb? 0: XRES, 0);

	selected_fb ^= 1;
	ot = fb[selected_fb].ot;
	next_packet = fb[selected_fb].packet_pool;
	startOrderingTableClear(ot, OT_LEN);

	if(vsync_30fps) {
		// Wait only for the remaining portion of this 30 Hz frame.
		// Two *additional* VBlanks after a partially rendered frame could
		// turn a ~20 ms frame into ~50 ms, causing avoidable stutter.
		if(last_frame_time_us != 0) {
			while(osGetTime() - last_frame_time_us < 1000000 / 30) {
				waitForVSync();
			}
		}
		last_frame_time_us = osGetTime();

		GPU_GP1 = gp1_dispBlank(false); // ensure the display has been turned back on
	}

	awaitOrderingTableClear();
}

void gfx_discard_frame() {
	startOrderingTableClear(ot, OT_LEN);
	next_packet = fb[selected_fb].packet_pool;
	gfx_init_global_dl();
	gfx_reset_rsp_jit();
	gfx_reset_dl_exec();
	awaitOrderingTableClear();
}

ALWAYS_INLINE static Packet gfx_packet_begin_with_reserve(u32 reserve) {
	u32* pool_end = fb[selected_fb].packet_pool + PACKET_POOL_LEN;
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

ALWAYS_INLINE void gfx_packet_end(Packet packet, u32 ot_z) {
	if(packet.header == discarded_packet) {
		return; // drop safely, do not link this packet into the GPU OT
	}
	/* Each GP0 packet must fit in the reserved space, with a valid depth. */
	assert(packet.end <= fb[selected_fb].packet_pool + PACKET_POOL_LEN);
	assert(ot_z < OT_LEN);
	u32 packet_size = (u32) packet.end - (u32) packet.start;
	*packet.header = ot[ot_z] | packet_size << 22;
	ot[ot_z] = (u32) next_packet & 0x00FFFFFF;
	next_packet = packet.end;
}
