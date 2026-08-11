#include <PR/os_time.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_blendmode.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_timer.h>
#include <port/cd.h>
#include <port/gfx/gfx_internal.h>
#include <game/memory.h>
#include <stdlib.h>

SDL_Window* window;
SDL_Renderer* renderer;

void gfx_backend_init() {
	window = SDL_CreateWindow("name", 960, 720, SDL_WINDOW_HIGH_PIXEL_DENSITY);
	renderer = SDL_CreateRenderer(window, NULL);
	SDL_ShowWindow(window);
	SDL_SetRenderLogicalPresentation(renderer, 320, 240, SDL_LOGICAL_PRESENTATION_LETTERBOX);
	SDL_SetRenderVSync(renderer, 1);
}

void gfx_init_buffers() {}

extern u8 _texture_data_segment[];
UNUSED extern u8 _texture_data_segment_end[];

static u32 psx_to_rgba(u16 psx, bool has_translucency) {
	if(psx == 0) {
		return 0;
	}
	u32 r = psx & 0x1F;
	r = r << 3 | r >> 2;
	u32 g = psx >> 5 & 0x1F;
	g = g << 3 | g >> 2;
	u32 b = psx >> 10 & 0x1F;
	b = b << 3 | b >> 2;
	return b << 8 | g << 16 | r << 24 | ((psx & 0x8000) && has_translucency? 0x7F: 0xFF);
}

void gfx_load_texture(void* tex_ptr) {
	TexHeader* tex_header = tex_ptr;
	if(tex_header->sdl_tex_ptr) {
		return;
	}
	u32 size = tex_header->pixel_data_sector_count * SECTOR_SIZE;
	u8 buf[size];
	u8* start = _texture_data_segment + tex_header->pixel_data_sector * SECTOR_SIZE;
	dma_read(buf, start, start + size);
	SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC, tex_header->width, tex_header->height);
	SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
	SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	u32 rgba[tex_header->width * tex_header->height];
	u32 i = 0;
	for(u32 y = 0; y < tex_header->height; y++) {
		for(u32 x = 0; x < tex_header->width; x += 2) {
			u8 two_indices = buf[32 + i / 2];
			u16 psx_pix_lo = buf[(two_indices & 0xF) * 2] | (u32) buf[(two_indices & 0xF) * 2 + 1] << 8;
			u16 psx_pix_hi = buf[(two_indices >> 4) * 2] | (u32) buf[(two_indices >> 4) * 2 + 1] << 8;
			rgba[i] = psx_to_rgba(psx_pix_lo, tex_header->has_translucency);
			rgba[i + 1] = psx_to_rgba(psx_pix_hi, tex_header->has_translucency);
			i += 2;
		}
	}
	SDL_UpdateTexture(texture, NULL, rgba, tex_header->width * 4);
	tex_header->sdl_tex_ptr = (uintptr_t) texture;
}

bool close_requested = false;

u64 last_frame_time = 0;

static bool is_holding_tab() {
	const bool* keys = SDL_GetKeyboardState(NULL);
	return keys[SDL_SCANCODE_TAB];
}

void flush_pc_ot();

void gfx_swap_buffers(bool vsync_30fps) {
	flush_pc_ot();
	if(vsync_30fps) {
		u64 t = SDL_GetTicksNS();
		if(last_frame_time == 0 || is_holding_tab()) {
			last_frame_time = t;
		} else {
			u64 next_frame_time = last_frame_time + 1000000000ull / 30;
			u64 close_enough_to_busy_wait = next_frame_time - 2000000ull;
			while(t < close_enough_to_busy_wait) {
				SDL_Delay(1);
				t = SDL_GetTicksNS();
			}
			while(t < next_frame_time) {
				t = SDL_GetTicksNS();
			}
			last_frame_time = next_frame_time;
		}
	}
	SDL_RenderPresent(renderer);
	SDL_RenderClear(renderer);
	SDL_Event event;
	while(SDL_PollEvent(&event)) {
		switch(event.type) {
			case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
				close_requested = true;
				break;
		}
	}
}

void gfx_discard_frame() {
}
