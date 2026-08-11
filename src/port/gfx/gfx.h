#pragma once
#include <PR/gbi.h>
#include <types.h>

#define XRES 320
#define YRES 240

typedef union __attribute__((aligned(4))) {
	struct {
		u8 r;
		u8 g;
		u8 b;
		u8 _pad;
	};
	u8 elems[4];
	u32 as_u32;
} Color;

// render distance in world space, anything farther will be clipped
#define MAX_Z 8000 // for performance, should be equal to Z_BUCKETS multiplied by a power of two
#define MAX_TESSELLATION_Z 1000
#define MAX_HIGH_TESSELLATION_Z 500

void gfx_init();
void gfx_end_frame(bool vsync_30fps);
void gfx_show_message_screen(const char* msg, const char* second_line, const char* third_line);
void gfx_fade_to_color(Color color, u8 alpha);
extern bool can_show_screen_message;

// generic short math functions
ShortMatrix mtx_identity();
void mtx_set_cell(ShortMatrix* dst, int i, int j, s32 val);
s32 mtx_get_cell(const ShortMatrix* dst, int i, int j);
void mtx_transpose(ShortMatrix* mtx);
ShortMatrix mtx_mul(const ShortMatrix* a, const ShortMatrix* b);
ShortVec mtx_apply_without_translation(const ShortVec* v, const ShortMatrix* mtx);
ShortVec mtx_apply(const ShortVec* v, const ShortMatrix* mtx);
ShortMatrix mtx_rotation_zxy(const short angles[3]);
ShortMatrix mtx_rotation_zxy_and_translation(const short translation[3], const short angles[3]);
ShortMatrix mtx_rotation_xyz(const short angles[3]);
ShortMatrix mtx_scalingq(const q32 scale[3]);
ShortMatrix mtx_translationi(const s16 off[3]);
void mtx_scaleq(ShortMatrix* mtx, const q32 scale[3]);

q32 vec_distance_sq(const ShortVec* v0, const ShortVec* v1);

// stateful graphical-math-backend functions
scratchpad extern ShortMatrix scratch_mtx;
void gfx_modelview_identity(void);
void gfx_modelview_rotate(short axisx, short axisy, short axisz, short angle);
void gfx_modelview_rotate_xyz(const short angles[3]);
void gfx_modelview_rotate_zxy(const short angles[3]);
void gfx_modelview_translatei(const s16 off[3]);
void gfx_modelview_scaleq(const q32 scale[3]);
void gfx_modelview_scale_byq(q32 scale);
void gfx_modelview_set(const ShortMatrix* mtx);
ShortMatrix gfx_modelview_get();
void gfx_modelview_get_rotation_into(ShortMatrix* mtx);
void gfx_modelview_get_translation_into(ShortMatrix* mtx);
void gfx_modelview_set_rotation_from(const ShortMatrix* mtx);
void gfx_modelview_set_translation_from(const ShortMatrix* mtx);
void gfx_modelview_push();
void gfx_modelview_pop();
void gfx_modelview_mul(const ShortMatrix* mtx);
ShortVec gfx_modelview_apply_without_translation(const ShortVec* v);
ShortVec gfx_modelview_apply(const ShortVec* v);
#define DECAL_Z_BIAS 2 // in ordering table indices, not world coordinates, so 2 is probably ideal

// textures
void gfx_load_texture(void* tex_ptr);

// rsp compiler
void gfx_reset_rsp_jit();
bool gfx_compile_rsp(Gfx* cmd, bool nested);

#define ALPHA_TRANSLUCENT 32
#define ALPHA_OPAQUE 192

// display list execution
extern u32 debug_processed_poly_count;

#ifdef TARGET_PC
typedef u64 dl_t;
#define DL_PACK_OP(op) ((dl_t) (op) << 56)
#define DL_UNPACK_OP(cmd) ((dl_t) (cmd) >> 56)
#define DL_PACK_PTR(ptr) ((dl_t) (ptr) & 0xFFFFFFFFFFFFFF)
#define DL_UNPACK_PTR(cmd) ((void*) ((s64) ((cmd) << 8) >> 8))
#else
typedef u32 dl_t;
#define DL_PACK_OP(op) ((dl_t) (op) << 24)
#define DL_UNPACK_OP(cmd) ((dl_t) (cmd) >> 24)
#define DL_PACK_PTR(ptr) ((dl_t) (ptr) & 0xFFFFFF)
#define DL_UNPACK_PTR(cmd) ((void*) ((cmd) & 0xFFFFFF))
#endif

void gfx_reset_dl_exec();
void gfx_run_compiled_dl(dl_t* dl);

#define PRIM_FLAG_TEXTURED        0b00000001
#define PRIM_FLAG_LIGHTED         0b00000010
#define PRIM_FLAG_DECAL           0b00000100
#define PRIM_FLAG_FORCE_BLEND     0b00001000
#define PRIM_FLAG_ENV_COLOR       0b00010000
//#define PRIM_FLAG_ENV_ALPHA ...
#define PRIM_FLAG_TESSELLATE  0b01000000
//#define PRIM_FLAG_TESSELLATE_HIGH 0b10000000

enum {
	_DL_CMD_ENUM_START = (u8) G_NOOP + 1,
	DL_CMD_JUMP = _DL_CMD_ENUM_START,
	DL_CMD_CALL,
	DL_CMD_END,
	DL_CMD_TEX,
	DL_CMD_VTX,
	DL_CMD_TRI,
	DL_CMD_QUAD,
	DL_CMD_ENV_COLOR_ALPHA_0,
	DL_CMD_ENV_COLOR_ALPHA_HALF,
	DL_CMD_ENV_COLOR_ALPHA_RESERVED,
	DL_CMD_ENV_COLOR_ALPHA_FULL,
	DL_CMD_LIGHT_AMBIENT,
	DL_CMD_LIGHT_DIRECTIONAL0,
	DL_CMD_LIGHT_DIRECTIONAL1,
	DL_CMD_MTX_SET,
	DL_CMD_MULTIPLIER,
	DL_CMD_SET_BACKGROUND,
	DL_CMD_SET_ORTHO,
	DL_CMD_SPRITE,
	DL_CMD_CIRCLE_SHADOW,
	// the commands below are less common and are split off so that the loop can fit in icache
	_DL_CMD_ENUM_FIRST_EXTRA,
	DL_CMD_MTX_MUL = _DL_CMD_ENUM_FIRST_EXTRA,
	DL_CMD_MTX_PUSH,
	DL_CMD_MTX_POP,
	DL_CMD_MTX_N64_SET,
	DL_CMD_MTX_N64_MUL,
	DL_CMD_SQUARE_SHADOW,
	_DL_CMD_ENUM_POST_END,
	_DL_CMD_ENUM_END = _DL_CMD_ENUM_POST_END - 1,
	_DL_CMD_ENUM_COUNT = _DL_CMD_ENUM_POST_END - _DL_CMD_ENUM_START
};
STATIC_ASSERT(_DL_CMD_ENUM_END < (u8) G_TEXRECT, "too many commands");

// global display list
void gfx_init_global_dl();
void gfx_flush_global_dl();
void* gfx_alloc_in_global_dl(u32 size);
void gfx_emit_call(void* target);
void gfx_emit_tex(void* tex_data);
void gfx_emit_sprite(s32 x, s32 y);
void gfx_emit_vertices_native(void* ptr);
void gfx_emit_vertices_n64(void* ptr, u32 count);
void gfx_emit_tri(u32 i0, u32 i1, u32 i2, u32 flags);
void gfx_emit_quad(u32 i0, u32 i1, u32 i2, u32 i3, u32 flags);
void gfx_emit_env_color_alpha_full(u32 color);
void gfx_emit_env_color_alpha_half(u32 color);
void gfx_emit_env_color_alpha_0(u32 color);
void gfx_emit_light_ambient(u32 color);
void gfx_emit_light_directional0(Light_t* light);
void gfx_emit_light_directional1(Light_t* light);
void gfx_emit_screen_quad(s16 x0, s16 y0, s16 x1, s16 y1);
void gfx_emit_multiplier(u32 multiplier);
void gfx_emit_mtx_set(ShortMatrix* mtx_ptr);
void gfx_emit_mtx_mul(ShortMatrix* mtx_ptr);
void gfx_emit_mtx_push();
void gfx_emit_mtx_pop();
void gfx_emit_set_background(bool is_background);
void gfx_emit_set_ortho(bool is_ortho);
void gfx_emit_shadow(bool is_square, s16 radius, u8 opacity);
