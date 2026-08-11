#include <port/gfx/gfx_internal.h>
#include <ps1/registers.h>
#include <ps1/cop0.h>
#include <ps1/gte.h>
#include <ps1/gpucmd.h>
#include <ps1/gpu.h>
#include <assert.h>
#include <game/memory.h>

// VRAM is composed of two rows of 16 64x256 pages, 32 pages total
// the first 10 pages (640 pixels) are taken up by the dual 320x240 framebuffers
// here we dedicate the next few to specific texture sizes, and cram the palettes somewhere
// but we also reserve a bunch of pages for mario's unreasonably large animation data

// these values can be configured but must stay in this order
#define FIRST_PAGE_128x32 16
#define FIRST_PAGE_64x64 18
#define FIRST_PAGE_64x32 19
#define FIRST_PAGE_32x32 23
#define FIRST_PAGE_16x16 26
#define FIRST_PAGE_8x16 28
#define FIRST_PAGE_CLUTS 29
#define PAGES_END 32

#define BPP 4
#define COUNT_IN_PAGE(w, h) (16 / BPP * 64 / (w) * (256 / (h)))

#define LOW_CLUTS_PER_ROW ((PAGES_END - FIRST_PAGE_CLUTS) * 64 / (1 << BPP))
#define LOW_CLUT_SLOTS (LOW_CLUTS_PER_ROW * 256)
#define CLUT_PAGES_X (FIRST_PAGE_CLUTS % 16 * 64)
#define CLUT_PAGES_Y (FIRST_PAGE_CLUTS / 16 * 256)
#define HIGH_CLUTS_PER_ROW (XRES * 2 / (1 << BPP))
#define HIGH_CLUT_SLOTS (HIGH_CLUTS_PER_ROW * 16)
#define CLUT_SLOTS (LOW_CLUT_SLOTS + HIGH_CLUT_SLOTS)

#define PAGES_128x32 (FIRST_PAGE_64x64 - FIRST_PAGE_128x32)
#define PAGE_SLOTS_128x32 COUNT_IN_PAGE(128 / BPP, 32)
#define SLOTS_128x32 (PAGE_SLOTS_128x32 * PAGES_128x32)
#define FIRST_CLUT_128x32 0

#define PAGES_64x64 (FIRST_PAGE_64x32 - FIRST_PAGE_64x64)
#define PAGE_SLOTS_64x64 COUNT_IN_PAGE(64 / BPP, 64)
#define SLOTS_64x64 (PAGE_SLOTS_64x64 * PAGES_64x64)
#define FIRST_CLUT_64x64 (FIRST_CLUT_128x32 + SLOTS_128x32)

#define PAGES_64x32 (FIRST_PAGE_32x32 - FIRST_PAGE_64x32)
#define PAGE_SLOTS_64x32 COUNT_IN_PAGE(64 / BPP, 32)
#define SLOTS_64x32 (PAGE_SLOTS_64x32 * PAGES_64x32)
#define FIRST_CLUT_64x32 (FIRST_CLUT_64x64 + SLOTS_64x64)

#define PAGES_32x32 (FIRST_PAGE_16x16 - FIRST_PAGE_32x32)
#define PAGE_SLOTS_32x32 COUNT_IN_PAGE(32 / BPP, 32)
#define SLOTS_32x32 (PAGE_SLOTS_32x32 * PAGES_32x32)
#define FIRST_CLUT_32x32 (FIRST_CLUT_64x32 + SLOTS_64x32)

#define PAGES_16x16 (FIRST_PAGE_8x16 - FIRST_PAGE_16x16)
#define PAGE_SLOTS_16x16 COUNT_IN_PAGE(16 / BPP, 16)
#define SLOTS_16x16 (PAGE_SLOTS_16x16 * PAGES_16x16)
#define FIRST_CLUT_16x16 (FIRST_CLUT_32x32 + SLOTS_32x32)

#define PAGES_8x16 (FIRST_PAGE_CLUTS - FIRST_PAGE_8x16)
#define PAGE_SLOTS_8x16 COUNT_IN_PAGE(8 / BPP, 16)
#define SLOTS_8x16 (PAGE_SLOTS_8x16 * PAGES_8x16 / 64)
#define FIRST_CLUT_8x16 (FIRST_CLUT_16x16 + SLOTS_16x16)

#define TEX_SLOTS (SLOTS_128x32 + SLOTS_64x64 + SLOTS_64x32 + SLOTS_32x32 + SLOTS_16x16 + SLOTS_8x16)

STATIC_ASSERT(CLUT_SLOTS >= TEX_SLOTS, "not enough clut slots for the amount of texture slots");

static void upload_texture(u32 vram_x, u32 vram_y, u32 clut_idx, void* tex_data, u8 vram_width, u8 height, const void* pixel_data) {
	TexHeader* tex = tex_data;
	int clut_x, clut_y;
	if(clut_idx < LOW_CLUT_SLOTS) {
		clut_y = CLUT_PAGES_Y + clut_idx / LOW_CLUTS_PER_ROW;
		clut_x = CLUT_PAGES_X + clut_idx % LOW_CLUTS_PER_ROW * 16;
	} else {
		clut_idx -= LOW_CLUT_SLOTS;
		clut_y = 240 + clut_idx / HIGH_CLUTS_PER_ROW;
		clut_x = clut_idx % HIGH_CLUTS_PER_ROW * 16;
	}
	sendVRAMData(pixel_data, clut_x, clut_y, 16, 1);
	sendVRAMData(pixel_data + 16 * 2, vram_x, vram_y, vram_width, height);
	tex->page_attr = gp0_page(vram_x / 64, vram_y / 256, GP0_BLEND_SEMITRANS, GP0_COLOR_4BPP);
	tex->clut_attr = gp0_clut(clut_x / 16, clut_y);
	tex->offx = vram_x % 64 * (16 / BPP);
	tex->offy = vram_y % 256;
}

extern u8 _texture_data_segment[];
UNUSED extern u8 _texture_data_segment_end[];

[[gnu::noinline]] void gfx_load_texture(void* tex_ptr) {
	assert(tex_ptr);
	TexHeader* tex_header = tex_ptr;
	if(((u32) tex_header->window_cmd >> 24) == ((u32) GP0_CMD_TEXWINDOW >> 24)) {
		return;
	}
	const void* dma_begin_addr = _texture_data_segment + tex_header->pixel_data_sector * 2048;
	ALIGNED4 u8 pixel_data[tex_header->pixel_data_sector_count * 2048];
	bool prev_can_show_screen_message = can_show_screen_message;
	can_show_screen_message = false;
	dma_read(pixel_data, dma_begin_addr, dma_begin_addr + tex_header->pixel_data_sector_count * 2048);
	can_show_screen_message = prev_can_show_screen_message;
	u16 width = tex_header->width;
	u16 height = tex_header->height;
	u32 aligned_width, aligned_height;
	u32 vram_x, vram_y, slot_idx;
	if(width <= 8 && height <= 16) {
		static u32 cur_slot_8x16 = 0;
		assert(cur_slot_8x16 < SLOTS_8x16);
		u32 slots_in_row = 64 / (16 / (16 / BPP)) * PAGES_8x16;
		vram_x = FIRST_PAGE_8x16 % 16 * 64 + cur_slot_8x16 % slots_in_row * (8 / (16 / BPP));
		vram_y = FIRST_PAGE_8x16 / 16 * 256 + cur_slot_8x16 / slots_in_row * 16;
		slot_idx = FIRST_CLUT_8x16 + cur_slot_8x16++;
		aligned_width = 8;
		aligned_height = 16;
	} else if(width <= 16 && height <= 16) {
		static u32 cur_slot_16x16 = 0;
		assert(cur_slot_16x16 < SLOTS_16x16);
		u32 slots_in_row = 64 / (16 / (16 / BPP)) * PAGES_16x16;
		vram_x = FIRST_PAGE_16x16 % 16 * 64 + cur_slot_16x16 % slots_in_row * (16 / (16 / BPP));
		vram_y = FIRST_PAGE_16x16 / 16 * 256 + cur_slot_16x16 / slots_in_row * 16;
		slot_idx = FIRST_CLUT_16x16 + cur_slot_16x16++;
		aligned_width = 16;
		aligned_height = 16;
	} else if(width <= 32 && height <= 32) {
		static u32 cur_slot_32x32 = 0;
		assert(cur_slot_32x32 < SLOTS_32x32);
		u32 slots_in_row = 64 / (32 / (16 / BPP)) * PAGES_32x32;
		vram_x = FIRST_PAGE_32x32 % 16 * 64 + cur_slot_32x32 % slots_in_row * (32 / (16 / BPP));
		vram_y = FIRST_PAGE_32x32 / 16 * 256 + cur_slot_32x32 / slots_in_row * 32;
		slot_idx = FIRST_CLUT_32x32 + cur_slot_32x32++;
		aligned_width = 32;
		aligned_height = 32;
	} else if(width <= 64 && height <= 32) {
		static u32 cur_slot_64x32 = 0;
		assert(cur_slot_64x32 < SLOTS_64x32);
		u32 slots_in_row = 64 / (64 / (16 / BPP)) * PAGES_64x32;
		vram_x = FIRST_PAGE_64x32 % 16 * 64 + cur_slot_64x32 % slots_in_row * (64 / (16 / BPP));
		vram_y = FIRST_PAGE_64x32 / 16 * 256 + cur_slot_64x32 / slots_in_row * 32;
		slot_idx = FIRST_CLUT_64x32 + cur_slot_64x32++;
		aligned_width = 64;
		aligned_height = 32;
	} else if(width <= 64 && height <= 64) {
		static u32 cur_slot_64x64 = 0;
		assert(cur_slot_64x64 < SLOTS_64x64);
		u32 slots_in_row = 64 / (64 / (16 / BPP)) * PAGES_64x64;
		vram_x = FIRST_PAGE_64x64 % 16 * 64 + cur_slot_64x64 % slots_in_row * (64 / (16 / BPP));
		vram_y = FIRST_PAGE_64x64 / 16 * 256 + cur_slot_64x64 / slots_in_row * 64;
		slot_idx = FIRST_CLUT_64x64 + cur_slot_64x64++;
		aligned_width = 64;
		aligned_height = 64;
	} else if(width <= 128 && height <= 32) {
		static u32 cur_slot_128x32 = 0;
		assert(cur_slot_128x32 < SLOTS_128x32);
		u32 slots_in_row = 64 / (128 / (16 / BPP)) * PAGES_128x32;
		vram_x = FIRST_PAGE_128x32 % 16 * 64 + cur_slot_128x32 % slots_in_row * (128 / (16 / BPP));
		vram_y = FIRST_PAGE_128x32 / 16 * 256 + cur_slot_128x32 / slots_in_row * 32;
		slot_idx = FIRST_CLUT_128x32 + cur_slot_128x32++;
		aligned_width = 128;
		aligned_height = 32;
	} else {
		abortf("unhandled texture size %ux%u\n", width, height);
	}
	upload_texture(vram_x, vram_y, slot_idx, tex_header, width / (16 / BPP), aligned_height, pixel_data);
	tex_header->window_cmd = gp0_texwindow(tex_header->offx / 8, tex_header->offy / 8, ~(aligned_width / 8 - 1), ~(aligned_height / 8 - 1));
}
