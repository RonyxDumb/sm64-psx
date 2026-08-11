#include "gfx_internal.h"
#include "port/gfx/gfx.h"
#include <port/psx/scratchpad_call.h>
#include <game/game_init.h>
#include <assert.h>

#if defined(TARGET_PSX) && !defined(NO_KERNEL_RAM)
#define GLOBAL_DL_BUFFER_START ((void*) 16)
#define GLOBAL_DL_BUFFER_END ((void*) (65536 - TESSELLATION_QUEUE_SIZE_BYTES))
#else
#ifdef TARGET_PSX
#warning kernel ram global dl disabled
#endif
static dl_t global_dl_buf[16384];
#define GLOBAL_DL_BUFFER_START ((void*) global_dl_buf)
#define GLOBAL_DL_BUFFER_END ((void*) global_dl_buf + sizeof(global_dl_buf))
#endif

scratchpad static dl_t* global_dl;
static dl_t* global_dl_right;

void gfx_init_global_dl() {
	global_dl = GLOBAL_DL_BUFFER_START;
	global_dl_right = GLOBAL_DL_BUFFER_END;
}

void gfx_flush_global_dl() {
	if(global_dl != GLOBAL_DL_BUFFER_START) {
		*(global_dl++) = DL_PACK_OP(DL_CMD_END);
		gfx_init_global_dl();
		scratchpad_call(gfx_run_compiled_dl, global_dl);
	}
}

void* gfx_alloc_in_global_dl(u32 size) {
	u32 word_count = (size + 3) / 4;
	global_dl_right -= word_count;
	assert((uintptr_t) global_dl_right > (uintptr_t) global_dl);
	return global_dl_right;
}

void* alloc_display_list(u32 size) { // get rid of this one when the converted display list is finally working
	return gfx_alloc_in_global_dl(size);
}

void gfx_emit_call(void* target) {
	if(gfx_compile_rsp(target, false)) {
		*(global_dl++) = DL_PACK_OP(DL_CMD_CALL) | DL_PACK_PTR(target);
	}
	assert((uintptr_t) global_dl_right > (uintptr_t) global_dl);
}

static TexHeader* last_tex = NULL;

void gfx_emit_tex(void* tex_header) {
	if(tex_header) {
		gfx_load_texture(tex_header);
	}
	last_tex = tex_header;
	*(global_dl++) = DL_PACK_OP(DL_CMD_TEX) | DL_PACK_PTR(tex_header);
	assert((uintptr_t) global_dl_right > (uintptr_t) global_dl);
}

void gfx_emit_sprite(s32 x, s32 y) {
	*(global_dl++) = DL_PACK_OP(DL_CMD_SPRITE) | (y & 0xFFF) << 12 | (x & 0xFFF);
	assert((uintptr_t) global_dl_right > (uintptr_t) global_dl);
}

void gfx_emit_vertices_native(void* ptr) {
	*(global_dl++) = DL_PACK_OP(DL_CMD_VTX) | DL_PACK_PTR(ptr);
	assert((uintptr_t) global_dl_right > (uintptr_t) global_dl);
}

void gfx_emit_vertices_n64(void* ptr, u32 count) {
	ensure_vertices_converted(ptr, count);
	gfx_emit_vertices_native(((VtxList*) ptr)->psx);
}

void gfx_emit_tri(u32 i0, u32 i1, u32 i2, u32 flags) {
	*(global_dl++) = DL_PACK_OP(DL_CMD_TRI) | i0 << 20 | i1 << 16 | i2 << 12 | flags;
	assert((uintptr_t) global_dl_right > (uintptr_t) global_dl);
}

void gfx_emit_quad(u32 i0, u32 i1, u32 i2, u32 i3, u32 flags) {
	*(global_dl++) = DL_PACK_OP(DL_CMD_QUAD) | i0 << 20 | i1 << 16 | i2 << 12 | i3 << 8 | flags;
	assert((uintptr_t) global_dl_right > (uintptr_t) global_dl);
}

void gfx_emit_env_color_alpha_full(u32 color) {
	*(global_dl++) = DL_PACK_OP(DL_CMD_ENV_COLOR_ALPHA_FULL) | (color & 0xFFFFFF);
	assert((uintptr_t) global_dl_right > (uintptr_t) global_dl);
}

void gfx_emit_env_color_alpha_half(u32 color) {
	*(global_dl++) = DL_PACK_OP(DL_CMD_ENV_COLOR_ALPHA_HALF) | (color & 0xFFFFFF);
	assert((uintptr_t) global_dl_right > (uintptr_t) global_dl);
}

void gfx_emit_env_color_alpha_0(u32 color) {
	*(global_dl++) = DL_PACK_OP(DL_CMD_ENV_COLOR_ALPHA_0) | (color & 0xFFFFFF);
	assert((uintptr_t) global_dl_right > (uintptr_t) global_dl);
}

void gfx_emit_light_ambient(u32 color) {
	*(global_dl++) = DL_PACK_OP(DL_CMD_LIGHT_AMBIENT) | (color & 0xFFFFFF);
	assert((uintptr_t) global_dl_right > (uintptr_t) global_dl);
}

void gfx_emit_light_directional0(Light_t* light) {
	*(global_dl++) = DL_PACK_OP(DL_CMD_LIGHT_DIRECTIONAL0) | DL_PACK_PTR(light);
	assert((uintptr_t) global_dl_right > (uintptr_t) global_dl);
}

void gfx_emit_light_directional1(Light_t* light) {
	*(global_dl++) = DL_PACK_OP(DL_CMD_LIGHT_DIRECTIONAL1) | DL_PACK_PTR(light);
	assert((uintptr_t) global_dl_right > (uintptr_t) global_dl);
}

bool compile_as_ortho = false;

void gfx_emit_screen_quad(s16 x0, s16 y0, s16 x1, s16 y1) {
	VtxList* list = gfx_alloc_in_global_dl(4 + sizeof(GfxVtx) * 4);
	// not setting the tag, it won't be checked
	list->psx[0].x = x0;
	list->psx[0].y = y0;
	list->psx[1].x = x0;
	list->psx[1].y = y1;
	list->psx[2].x = x1;
	list->psx[2].y = y0;
	list->psx[3].x = x1;
	list->psx[3].y = y1;
	u32 flags = PRIM_FLAG_ENV_COLOR;
#ifdef PRIM_FLAG_ENV_ALPHA
	flags |= PRIM_FLAG_ENV_ALPHA;
#endif
	if(last_tex) {
		list->psx[0].u = 0;
		list->psx[0].v = 0;
		list->psx[1 + last_tex->rotated].u = 0;
		list->psx[1 + last_tex->rotated].v = last_tex->height - 1;
		list->psx[2 - last_tex->rotated].u = last_tex->width - 1;
		list->psx[2 - last_tex->rotated].v = 0;
		list->psx[3].u = last_tex->width - 1;
		list->psx[3].v = last_tex->height - 1;
		flags |= PRIM_FLAG_TEXTURED;
	}
	*(global_dl++) = DL_PACK_OP(DL_CMD_VTX) | DL_PACK_PTR(list->psx);
	if(!compile_as_ortho) {
		gfx_emit_set_ortho(true);
	}
	gfx_emit_quad(0, 1, 2, 3, flags);
	if(!compile_as_ortho) {
		gfx_emit_set_ortho(false);
	}
}

void gfx_emit_multiplier(u32 multiplier) {
	*(global_dl++) = DL_PACK_OP(DL_CMD_MULTIPLIER) | (multiplier & 0xFFFFFF);
	assert((uintptr_t) global_dl_right > (uintptr_t) global_dl);
}

void gfx_emit_mtx_set(ShortMatrix* mtx_ptr) {
	*(global_dl++) = DL_PACK_OP(DL_CMD_MTX_SET) | DL_PACK_PTR(mtx_ptr);
	assert((uintptr_t) global_dl_right > (uintptr_t) global_dl);
}

void gfx_emit_mtx_mul(ShortMatrix* mtx_ptr) {
	*(global_dl++) = DL_PACK_OP(DL_CMD_MTX_MUL) | DL_PACK_PTR(mtx_ptr);
	assert((uintptr_t) global_dl_right > (uintptr_t) global_dl);
}

void gfx_emit_mtx_push() {
	*(global_dl++) = DL_PACK_OP(DL_CMD_MTX_PUSH);
	assert((uintptr_t) global_dl_right > (uintptr_t) global_dl);
}

void gfx_emit_mtx_pop() {
	*(global_dl++) = DL_PACK_OP(DL_CMD_MTX_POP);
	assert((uintptr_t) global_dl_right > (uintptr_t) global_dl);
}

void gfx_emit_set_background(bool is_background) {
	*(global_dl++) = DL_PACK_OP(DL_CMD_SET_BACKGROUND) | (is_background? 1: 0);
	assert((uintptr_t) global_dl_right > (uintptr_t) global_dl);
}

void gfx_emit_set_ortho(bool is_ortho) {
	*(global_dl++) = DL_PACK_OP(DL_CMD_SET_ORTHO) | is_ortho;
	compile_as_ortho = is_ortho;
	assert((uintptr_t) global_dl_right > (uintptr_t) global_dl);
}

void gfx_emit_shadow(bool is_square, s16 radius, u8 opacity) {
	*(global_dl++) = (is_square? DL_PACK_OP(DL_CMD_SQUARE_SHADOW): DL_PACK_OP(DL_CMD_CIRCLE_SHADOW)) | (u32) opacity << 16 | (u32) (u16) radius;
}
