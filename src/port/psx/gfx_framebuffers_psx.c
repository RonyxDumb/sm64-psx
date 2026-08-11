#include <port/gfx/gfx_internal.h>
#include <ps1/gpu.h>
#include <ps1/gpucmd.h>
#include <ps1/registers.h>

static u8 selected_fb = 0;
static Framebuffer fb[2];
scratchpad static u32* ot;
scratchpad static u32* next_packet;

void gfx_init_buffers() {
	clearOrderingTable(fb[0].ot, OT_LEN);
	clearOrderingTable(fb[1].ot, OT_LEN);
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
	if(vsync_30fps) {
		OSTime frame_time = osGetTime() - last_frame_time_us;
		if(frame_time < 1000000 / 30) {
			waitForVSync();
			waitForVSync();
		}
		last_frame_time_us = osGetTime();

		GPU_GP1 = gp1_dispBlank(false); // ensure the display has been turned back on
	}

	selected_fb ^= 1;
	ot = fb[selected_fb].ot;
	next_packet = fb[selected_fb].packet_pool;
	clearOrderingTable(ot, OT_LEN);
}

void gfx_discard_frame() {
	clearOrderingTable(ot, OT_LEN);
	next_packet = fb[selected_fb].packet_pool;
	gfx_init_global_dl();
	gfx_reset_rsp_jit();
	gfx_reset_dl_exec();
}

ALWAYS_INLINE Packet gfx_packet_begin() {
	return (Packet) {
		.header = next_packet,
		.start = next_packet + 1,
		.end = next_packet + 1
	};
}

ALWAYS_INLINE void gfx_packet_append(Packet* packet, u32 cmd) {
	*(packet->end++) = cmd;
}

ALWAYS_INLINE void gfx_packet_end(Packet packet, u32 ot_z) {
	u32 packet_size = (u32) packet.end - (u32) packet.start;
	*packet.header = ot[ot_z] | packet_size << 22;
	ot[ot_z] = (u32) next_packet & 0x00FFFFFF;
	next_packet = packet.end;
}
