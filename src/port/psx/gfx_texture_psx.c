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
// the last pages in the first row are taken up by mario's unreasonably large animation data
// here we dedicate pages on the lower half of vram to specific texture sizes, and cram the palettes after them

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
// Width here is already measured in 16-bit VRAM pixels (4 texels per pixel).
// Do not apply the 4-bpp conversion a second time: it overbooks VRAM pages.
#define COUNT_IN_PAGE(w, h) ((64 / (w)) * (256 / (h)))

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
#define SLOTS_8x16 (PAGE_SLOTS_8x16 * PAGES_8x16)
#define FIRST_CLUT_8x16 (FIRST_CLUT_16x16 + SLOTS_16x16)

#define TEX_SLOTS (SLOTS_128x32 + SLOTS_64x64 + SLOTS_64x32 + SLOTS_32x32 + SLOTS_16x16 + SLOTS_8x16)

STATIC_ASSERT(CLUT_SLOTS >= TEX_SLOTS, "not enough clut slots for the amount of texture slots");
STATIC_ASSERT(PAGE_SLOTS_128x32 == 16, "invalid 128x32 page capacity");
STATIC_ASSERT(PAGE_SLOTS_64x64 == 16, "invalid 64x64 page capacity");
STATIC_ASSERT(PAGE_SLOTS_64x32 == 32, "invalid 64x32 page capacity");
STATIC_ASSERT(PAGE_SLOTS_32x32 == 64, "invalid 32x32 page capacity");
STATIC_ASSERT(PAGE_SLOTS_16x16 == 256, "invalid 16x16 page capacity");
STATIC_ASSERT(PAGE_SLOTS_8x16 == 512, "invalid 8x16 page capacity");

// The CD archive is packed into 8-KiB banks, each containing multiple textures.
// A small LRU cache avoids repeating physical seeks/reads for neighboring images.
// Uploaded textures remain resident in VRAM; this cache stores only CD staging data.
#define TEX_BANK_SECTORS 4
#define TEX_BANK_BYTES (TEX_BANK_SECTORS * 2048)
#define TEX_CACHE_COUNT 2
#define TEX_CLUT_BYTES 32

typedef struct {
	u32 data[TEX_BANK_BYTES / sizeof(u32)]; // aligned for CD and GPU DMA
	u32 last_used;
	u16 first_sector;
	bool valid;
} TextureBank;

static TextureBank texture_banks[TEX_CACHE_COUNT];
static u32 texture_bank_clock;
static TexHeader* vram_slots[TEX_SLOTS];

typedef struct {
	u16 width, height;
	u16 first_slot, count;
	u8 first_page, page_count;
} TextureSlotClass;

static const TextureSlotClass slot_classes[] = {
	{ 8, 16, FIRST_CLUT_8x16, SLOTS_8x16, FIRST_PAGE_8x16, PAGES_8x16 },
	{ 16, 16, FIRST_CLUT_16x16, SLOTS_16x16, FIRST_PAGE_16x16, PAGES_16x16 },
	{ 32, 32, FIRST_CLUT_32x32, SLOTS_32x32, FIRST_PAGE_32x32, PAGES_32x32 },
	{ 64, 32, FIRST_CLUT_64x32, SLOTS_64x32, FIRST_PAGE_64x32, PAGES_64x32 },
	{ 64, 64, FIRST_CLUT_64x64, SLOTS_64x64, FIRST_PAGE_64x64, PAGES_64x64 },
	{ 128, 32, FIRST_CLUT_128x32, SLOTS_128x32, FIRST_PAGE_128x32, PAGES_128x32 },
};

void gfx_release_textures_in_range(const void* start, const void* end) {
	if(start == end) return;
	for(u32 i = 0; i < TEX_SLOTS; i++) {
		TexHeader* texture = vram_slots[i];
		if((uintptr_t) texture >= (uintptr_t) start && (uintptr_t) texture < (uintptr_t) end) {
			texture->window_cmd = 0;
			vram_slots[i] = NULL;
		}
	}
}

extern u8 _texture_data_segment[];
UNUSED extern u8 _texture_data_segment_end[];

static const u8* get_packed_texture(const TexHeader* header) {
	u32 sector = header->pixel_data_bank_sector;
	u32 offset = header->pixel_data_bank_offset;
	u32 bytes = TEX_CLUT_BYTES + (((u32) header->width + 3) / 4) * header->height * 2;
	if((sector % TEX_BANK_SECTORS) || (offset & 3)
		|| bytes > TEX_BANK_BYTES || offset > TEX_BANK_BYTES - bytes) {
		abortf("invalid packed texture bank: sector %u offset %u size %u\n", sector, offset, bytes);
	}

	TextureBank* bank = NULL;
	TextureBank* victim = &texture_banks[0];
	for(u32 i = 0; i < TEX_CACHE_COUNT; i++) {
		TextureBank* entry = &texture_banks[i];
		if(entry->valid && entry->first_sector == sector) {
			bank = entry;
			break;
		}
		if(!entry->valid || (victim->valid && entry->last_used < victim->last_used)) {
			victim = entry;
		}
	}
	if(!bank) {
		bank = victim;
		// sendVRAMData() is asynchronous. Do not overwrite a bank until the
		// GPU has finished reading CLUT/texture pixels from its RAM buffer.
		waitForDMADone();
		bool prev_can_show_screen_message = can_show_screen_message;
		can_show_screen_message = false;
		const u8* start = _texture_data_segment + sector * 2048;
		dma_read((u8*) bank->data, start, start + TEX_BANK_BYTES);
		can_show_screen_message = prev_can_show_screen_message;
		bank->first_sector = sector;
		bank->valid = true;
	}
	bank->last_used = ++texture_bank_clock;
	return (const u8*) bank->data + offset;
}

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

	sendVRAMData(pixel_data, clut_x, clut_y, 16, 1); // upload clut
	sendVRAMData(pixel_data + 16 * 2, vram_x, vram_y, vram_width, height); // upload pixels

	tex->page_attr = gp0_page(vram_x / 64, vram_y / 256, GP0_BLEND_SEMITRANS, GP0_COLOR_4BPP);
	tex->clut_attr = gp0_clut(clut_x / 16, clut_y);
	tex->offx = vram_x % 64 * (16 / BPP);
	tex->offy = vram_y % 256;
}

[[gnu::noinline]] void gfx_load_texture(void* tex_ptr) {
	assert(tex_ptr);
	TexHeader* tex = tex_ptr;
	if((tex->window_cmd >> 24) == ((u32) GP0_CMD_TEXWINDOW >> 24)) {
		return; // already loaded
	}

	u32 width = tex->width;
	u32 height = tex->height;
	for(u32 type = 0; type < sizeof(slot_classes) / sizeof(slot_classes[0]); type++) {
		const TextureSlotClass* slot_class = &slot_classes[type];
		if(width > slot_class->width || height > slot_class->height) continue;
		u32 class_word_width = slot_class->width / (16 / BPP);
		u32 slots_in_row = (64 / class_word_width) * slot_class->page_count;
		for(u32 i = 0; i < slot_class->count; i++) {
			u32 slot = slot_class->first_slot + i;
			if(vram_slots[slot]) continue;
			u32 x = (slot_class->first_page % 16) * 64 + (i % slots_in_row) * class_word_width;
			u32 y = (slot_class->first_page / 16) * 256 + (i / slots_in_row) * slot_class->height;
			const u8* pixels = get_packed_texture(tex);
			upload_texture(x, y, slot, tex, (width + 3) / 4, height, pixels);
			// The window follows the texture size, including when a small texture
			// uses a larger slot. The GPU window works in eight-texel steps.
			u32 window_width = 8;
			u32 window_height = 8;
			while(window_width < width) window_width *= 2;
			while(window_height < height) window_height *= 2;
			tex->window_cmd = gp0_texwindow(tex->offx / 8, tex->offy / 8,
				~(window_width / 8 - 1), ~(window_height / 8 - 1));
			vram_slots[slot] = tex;
			return;
		}
	}
	abortf("no PS1 VRAM slot for texture %ux%u\n", width, height);
}
