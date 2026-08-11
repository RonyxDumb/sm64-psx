#include <port/gfx/gfx_internal.h>
#include <ps1/gpu.h>
#include <ps1/gpucmd.h>
#include <ps1/registers.h>
#include <ps1/gte.h>
#include <ps1/cop0.h>
#include <stdio.h>

void gfx_backend_init() {
	initSerialIO(518400);
	setupGPU((GPU_GP1 & GP1_STAT_FB_MODE_BITMASK) == GP1_STAT_FB_MODE_PAL? GP1_MODE_PAL: GP1_MODE_NTSC, XRES, YRES);
	GPU_GP1 = gp1_dispBlank(true); // disable the display until a frame is rendered
	cop0_setReg(COP0_SR, COP0_SR_CU2); // enable GTE while disabling all other COP0 features
	gte_setControlReg(GTE_OFX, XRES / 2 << 16); // graphics origin at the screen center
	gte_setControlReg(GTE_OFY, YRES / 2 << 16);
	gte_setControlReg(GTE_H, 1); // this will be overwritten every frame by the actual fov
	gte_setControlReg(GTE_ZSF3, ONE / (3 * MAX_Z / Z_BUCKETS));
	gte_setControlReg(GTE_ZSF4, ONE / (4 * MAX_Z / Z_BUCKETS));
	gte_setControlReg(GTE_RFC, 0);
	gte_setControlReg(GTE_GFC, 0);
	gte_setControlReg(GTE_BFC, 0);
	gte_setControlReg(GTE_LC11LC12, 0);
	gte_setControlReg(GTE_LC13LC21, 0);
	gte_setControlReg(GTE_LC22LC23, 0);
	gte_setControlReg(GTE_LC31LC32, 0);
	gte_setControlReg(GTE_LC33, 0);
	DMA_DPCR |= DMA_DPCR_ENABLE << (DMA_GPU << 2); // enable DMA for GPU commands and VRAM
	DMA_DPCR |= DMA_DPCR_ENABLE << (DMA_OTC << 2); // enable DMA for clearing ordering table
	DMA_DPCR |= DMA_DPCR_ENABLE << (DMA_SPU << 2); // enable DMA for SPU
	BIU_DEV4_CTRL = BIU_CTRL_DMA_DELAY | BIU_CTRL_RECOVERY | BIU_CTRL_WIDTH_16 | BIU_CTRL_AUTO_INCR // setup SPU bus
		| (2 << 24 & BIU_CTRL_DMA_DELAY_BITMASK)
		| (9 << 16 & BIU_CTRL_SIZE_BITMASK)
		| (14 << 4 & BIU_CTRL_READ_DELAY_BITMASK)
		| (1 << 0 & BIU_CTRL_WRITE_DELAY_BITMASK);
	SPU_CTRL = SPU_CTRL_ENABLE | SPU_CTRL_UNMUTE; // enable SPU
	GPU_GP1 = gp1_dmaRequestMode(GP1_DREQ_GP0_WRITE); // allow DMA to the display
}

void gfx_fade_to_color(Color color, u8 alpha) {
	Packet packet = gfx_packet_begin();
	gfx_packet_append(&packet, gp0_texpage(gp0_page(0, 0, GP0_BLEND_SUBTRACT, 0), true, false));
	gfx_packet_append(&packet, (u32) alpha << 16 | (u32) alpha << 8 | alpha | gp0_rectangle(false, true, true));
	gfx_packet_append(&packet, gp0_xy(0, 0));
	gfx_packet_append(&packet, gp0_xy(XRES, YRES));
	gfx_packet_append(&packet, gp0_texpage(gp0_page(0, 0, GP0_BLEND_ADD, 0), true, false));
	gfx_packet_append(&packet, ((u32) color.r * alpha / 256) << 16 | ((u32) color.g * alpha / 256) << 8 | ((u32) color.b * alpha / 256) | gp0_rectangle(false, true, true));
	gfx_packet_append(&packet, gp0_xy(0, 0));
	gfx_packet_append(&packet, gp0_xy(XRES, YRES));
	gfx_packet_end(packet, 0);
}
