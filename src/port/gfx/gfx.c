#include <port/gfx/gfx_internal.h>
#include <stdbool.h>
#include <PR/gbi.h>
#include <game/game_init.h>
#include <game/camera.h>
#include <game/print.h>

extern struct TextLabel* sTextLabels[52];
extern s16 sTextLabelsCount;
static const char* showing_msg = NULL;
void render_text_labels();

void gfx_backend_init();
void audio_backend_init();
void audio_backend_tick();

void gfx_init() {
	gfx_backend_init();
	gfx_init_global_dl();
	gfx_init_buffers();
	audio_backend_init();
}

u32 gNumVblanks = 0;

void gfx_end_frame(bool vsync_30fps) {
	gfx_flush_global_dl();
	gfx_swap_buffers(vsync_30fps);
	gfx_reset_rsp_jit();
	gfx_reset_dl_exec();

	gfx_modelview_identity();
	showing_msg = NULL;
	if(vsync_30fps) {
		audio_backend_tick();
		gNumVblanks += 2;
	}
}

bool can_show_screen_message = false;

void gfx_show_message_screen(const char* msg, const char* second_line, const char* third_line) {
	if(showing_msg == msg || !can_show_screen_message) {
		return;
	}
	showing_msg = msg;
	gfx_discard_frame();
	for(int i = 0; i < sTextLabelsCount; i++) {
		mem_pool_free(gEffectsMemoryPool, sTextLabels[i]);
	}
	sTextLabelsCount = 0;
	print_text_centered(XRES / 2, YRES / 2, msg);
	print_text_centered(XRES / 2, YRES / 2 - 32, second_line);
	print_text_centered(XRES / 2, YRES / 2 - 64, third_line);
	render_text_labels();
	gfx_end_frame(false);
}
