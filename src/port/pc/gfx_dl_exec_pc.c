#include "engine/graph_node.h"
#include "port/gfx/gfx.h"
#include <types.h>
#include <port/gfx/gfx_internal.h>
#include <SDL3/SDL.h>
#include <assert.h>
#include <engine/math_util.h>

void gfx_begin_queueing_for_tessellation(const GfxVtx* v0, const GfxVtx* v1, const GfxVtx* v2, const GfxVtx* v3, u8 flags) {}
void gfx_finish_queueing_for_tessellation(u32 rgb0, u32 rgb1, u32 rgb2, u32 rgb3) {}

u32 debug_processed_poly_count = 0;
void* tex_ptr = NULL;
static Color env_color;

void gfx_fade_to_color(Color color, u8 alpha) {}

static u8 env_alpha;
static GfxVtx* vertices;
static u32 multiplier = 1;
static bool is_ortho;
static bool is_2d_background;
static u8 foreground_z;
static Color ambient_color;
static ShortVec light_directions[2];
static Color light_colors[2];

void gfx_reset_dl_exec() {
	tex_ptr = NULL;
	multiplier = 1;
	is_ortho = false;
	is_2d_background = true;
	foreground_z = FOREGROUND_BUCKETS;
	env_color.as_u32 = 0xFFFFFF;
	env_alpha = 255;
	gfx_modelview_identity();
	ambient_color.as_u32 = 0;
	light_directions[0] = (ShortVec) {.vx_vy = 0, .vz_pad = 0};
	light_directions[1] = (ShortVec) {.vx_vy = 0, .vz_pad = 0};
	light_colors[0].as_u32 = 0;
	light_colors[1].as_u32 = 0;
}

extern SDL_Renderer* renderer;

typedef struct SdlSpritePacket {
	SDL_FRect rect;
	SDL_Texture* sdl_tex;
} SdlSpritePacket;

static SdlSpritePacket fg_sprites[4096];
static u32 fg_sprite_count = 0;

ALWAYS_INLINE static void draw_sprite(s16 x, s16 y) {
	if(tex_ptr) {
		TexHeader* header = tex_ptr;
		SDL_Texture* sdl_tex = (void*) header->sdl_tex_ptr;
		if(is_2d_background) {
			SDL_RenderTexture(renderer, sdl_tex, NULL, &(SDL_FRect) {.x = x, .y = y, .w = header->width, .h = header->height});
		} else {
			fg_sprites[fg_sprite_count++] = (SdlSpritePacket) {
				.rect = {.x = x, .y = y, .w = header->width, .h = header->height},
				.sdl_tex = sdl_tex
			};
			assert(fg_sprite_count < ARRAY_COUNT(fg_sprites) - 1);
		}
	}
}

static int clamp(int x, int a, int b) {
	return x < a? a: (x > b? b: x);
}

static Color apply_directional_light(u8 i, s8* n) {
	s32 intensity = 0;
	s32 x = (s32) n[0] * light_directions[i].vx / 128;
	if(x > 0) intensity += x;
	s32 y = (s32) n[1] * light_directions[i].vy / 128;
	if(y > 0) intensity += y;
	s32 z = (s32) n[2] * light_directions[i].vz / 128;
	if(z > 0) intensity += z;
	if(intensity <= 0) {
		return (Color) {.as_u32 = 0};
	}
	Color col = light_colors[i];
	col.r = clamp(col.r * intensity / ONE, 0, 255);
	col.g = clamp(col.g * intensity / ONE, 0, 255);
	col.b = clamp(col.b * intensity / ONE, 0, 255);
	return col;
}

static SDL_FColor light_from_normal(s8* normal) {
	Color l0 = apply_directional_light(0, normal);
	Color l1 = apply_directional_light(1, normal);
	return (SDL_FColor) {
		.r = clamp((u32) ambient_color.r + l0.r + l1.r, 0, 255) / 255.f,
		.g = clamp((u32) ambient_color.g + l0.g + l1.g, 0, 255) / 255.f,
		.b = clamp((u32) ambient_color.b + l0.b + l1.b, 0, 255) / 255.f,
		.a = 1
	};
}

static SDL_Vertex transform_vertex(const GfxVtx* v, s32* min_z, s32* max_z, u32 flags) {
	ShortVec p = gfx_modelview_apply((ShortVec*) &v->x);
	//p.vz = -p.vz;
	if(p.vz < *min_z) {
		*min_z = p.vz;
	}
	if(p.vz > *max_z) {
		*max_z = p.vz;
	}
	return (SDL_Vertex) {
		.position.x = is_ortho? p.vx: (float) p.vx * multiplier / p.vz + XRES / 2,
		.position.y = is_ortho? p.vy: (float) p.vy * multiplier / p.vz + YRES / 2,
		.tex_coord.x = tex_ptr? (float) v->u / (float) ((TexHeader*) tex_ptr)->width: 0.f,
		.tex_coord.y = tex_ptr? (float) v->v / (float) ((TexHeader*) tex_ptr)->height: 0.f,
		.color =
			(flags & PRIM_FLAG_LIGHTED)? light_from_normal((s8*) v->color.elems):
			(flags & PRIM_FLAG_ENV_COLOR)? (SDL_FColor) {env_color.r / 255.f, env_color.g / 255.f, env_color.b / 255.f, 1}:
			(SDL_FColor) {v->color.r / 255.f, v->color.g / 255.f, v->color.b / 255.f, 1}
	};
}

typedef struct SdlPacket {
	struct SdlPacket* next;
	SDL_Vertex vertices[4];
	bool is_quad;
	u8 flags;
	SDL_Texture* sdl_tex;
} SdlPacket;

static SdlPacket packet_buf[65536];
static u32 packet_buf_count = 0;
static SdlPacket* ot[OT_LEN] = {NULL};
static SdlPacket* ortho_fg_packet = NULL;
static SdlPacket* ortho_fg_packet_last = NULL;

void flush_pc_ot() {
	for(s32 i = OT_LEN - 1; i >= 0; i--) {
		SdlPacket* packet = ot[i];
		while(packet) {
			if(packet->flags & PRIM_FLAG_DECAL) {
				SDL_RenderGeometry(renderer, NULL, packet->vertices, packet->is_quad? 4: 3, (const int[]) {0, 1, 2, 2, 1, 3}, packet->is_quad? 6: 3);
			}
			SDL_RenderGeometry(renderer, packet->sdl_tex, packet->vertices, packet->is_quad? 4: 3, (const int[]) {0, 1, 2, 2, 1, 3}, packet->is_quad? 6: 3);
			packet = packet->next;
		}
		ot[i] = NULL;
	}
	while(ortho_fg_packet) {
		SDL_RenderGeometry(renderer, ortho_fg_packet->sdl_tex, ortho_fg_packet->vertices, ortho_fg_packet->is_quad? 4: 3, (const int[]) {0, 1, 2, 2, 1, 3}, ortho_fg_packet->is_quad? 6: 3);
		ortho_fg_packet = ortho_fg_packet->next;
	}
	ortho_fg_packet_last = NULL;
	packet_buf_count = 0;

	for(u32 i = 0; i < fg_sprite_count; i++) {
		SDL_RenderTexture(renderer, fg_sprites[i].sdl_tex, NULL, &fg_sprites[i].rect);
	}
	fg_sprite_count = 0;
}

static void draw_poly(const GfxVtx* v0, const GfxVtx* v1, const GfxVtx* v2, const GfxVtx* v3, u32 flags) {
	s32 min_z = MAX_Z, max_z = 0;
	SdlPacket* packet = &packet_buf[packet_buf_count];
	packet->vertices[0] = transform_vertex(v0, &min_z, &max_z, flags);
	packet->vertices[1] = transform_vertex(v1, &min_z, &max_z, flags);
	packet->vertices[2] = transform_vertex(v2, &min_z, &max_z, flags);
	if(v3) packet->vertices[3] = transform_vertex(v3, &min_z, &max_z, flags);
	if(is_ortho) {
		if(is_2d_background) {
			packet->next = ot[Z_BUCKETS + 1];
			ot[Z_BUCKETS + 1] = packet;
		} else {
			if(ortho_fg_packet_last) {
				ortho_fg_packet_last->next = packet;
			} else {
				ortho_fg_packet = packet;
			}
			packet->next = NULL;
			ortho_fg_packet_last = packet;
		}
	} else if(min_z <= 0 || min_z >= MAX_Z || max_z <= 0 || max_z >= MAX_Z) {
		return;
	} else {
		s32 nclip = packet->vertices[0].position.x * packet->vertices[1].position.y
			+ packet->vertices[1].position.x * packet->vertices[2].position.y
			+ packet->vertices[2].position.x * packet->vertices[0].position.y
			- packet->vertices[0].position.x * packet->vertices[2].position.y
			- packet->vertices[1].position.x * packet->vertices[0].position.y
			- packet->vertices[2].position.x * packet->vertices[1].position.y;
		if(nclip >= 0) {
			return;
		}
		u32 otz = max_z / (MAX_Z / Z_BUCKETS);
		packet->next = ot[otz];
		ot[otz] = packet;
	}
	packet->is_quad = v3 != NULL;
	packet->flags = flags;
	packet->sdl_tex = (flags & PRIM_FLAG_TEXTURED) && tex_ptr? (void*) ((TexHeader*) tex_ptr)->sdl_tex_ptr: NULL;
	packet_buf_count++;
	assert(packet_buf_count < ARRAY_COUNT(packet_buf) - 1);
}

static void draw_shadow(s32 radius, u8 opacity) {
	GfxVtx vertices[4] = {
		{.x = -radius, .y = 0, .z = -radius},
		{.x = -radius, .y = 0, .z = radius},
		{.x = radius, .y = 0, .z = -radius},
		{.x = radius, .y = 0, .z = radius}
	};
	draw_poly(&vertices[0], &vertices[1], &vertices[2], &vertices[3], 0);
	return;
	s32 min_z = MAX_Z, max_z = 0;
	SdlPacket* packet = &packet_buf[packet_buf_count];
	packet->vertices[0] = transform_vertex(&vertices[0], &min_z, &max_z, 0);
	packet->vertices[1] = transform_vertex(&vertices[1], &min_z, &max_z, 0);
	packet->vertices[2] = transform_vertex(&vertices[2], &min_z, &max_z, 0);
	packet->vertices[3] = transform_vertex(&vertices[3], &min_z, &max_z, 0);
	if(min_z <= 0 || min_z >= MAX_Z || max_z <= 0 || max_z >= MAX_Z) {
		return;
	}
	u32 otz = max_z / (MAX_Z / Z_BUCKETS);
	packet->next = ot[otz];
	ot[otz] = packet;
	packet->is_quad = true;
	packet->flags = 0;
	packet->sdl_tex = nullptr;
	packet_buf_count++;
	assert(packet_buf_count < ARRAY_COUNT(packet_buf) - 1);
}

void gfx_run_compiled_dl(dl_t* dl) {
	dl_t* call_stack[16];
	u32 call_stack_idx = 0;
	while(true) {
		dl_t cmd = *(dl++);
		u8 op = DL_UNPACK_OP(cmd);
		switch(op) {
			case DL_CMD_CALL: case DL_CMD_JUMP: {
				dl_t* target = DL_UNPACK_PTR(cmd);
				if(op == DL_CMD_CALL) {
					call_stack[call_stack_idx++] = dl;
				}
				dl = target;
				break;
			}
			case DL_CMD_END: {
				if(call_stack_idx == 0) {
					return;
				}
				dl = call_stack[--call_stack_idx];
				break;
			}
			case DL_CMD_TEX: {
				tex_ptr = DL_UNPACK_PTR(cmd);
				break;
			}
			case DL_CMD_VTX: {
				vertices = DL_UNPACK_PTR(cmd);
				break;
			}
			case DL_CMD_TRI: case DL_CMD_QUAD: {
				if(!is_ortho) {
					is_2d_background = false;
				}
				draw_poly(
					&vertices[cmd >> 20 & 0xF],
					&vertices[cmd >> 16 & 0xF],
					&vertices[cmd >> 12 & 0xF],
					op == DL_CMD_QUAD? &vertices[cmd >> 8 & 0xF]: NULL,
					cmd & 0xFF
				);
				break;
			}
			case DL_CMD_ENV_COLOR_ALPHA_FULL: {
				env_alpha = 255;
				goto set_just_env_color;
			}
			case DL_CMD_ENV_COLOR_ALPHA_HALF: {
				env_alpha = 127;
				goto set_just_env_color;
			}
			case DL_CMD_ENV_COLOR_ALPHA_0: {
				env_alpha = 0;
			set_just_env_color:
				env_color.as_u32 = cmd & 0xFFFFFF;
				break;
			}
			case DL_CMD_LIGHT_AMBIENT: {
				ambient_color.r = cmd & 0xFF;
				ambient_color.g = cmd >> 8 & 0xFF;
				ambient_color.b = cmd >> 16 & 0xFF;
				break;
			}
			case DL_CMD_LIGHT_DIRECTIONAL0: case DL_CMD_LIGHT_DIRECTIONAL1: {
				Light_t* n64light = DL_UNPACK_PTR(cmd);
				ShortVec n = {
					.vx = (s16) n64light->dir[0],
					.vy = (s16) n64light->dir[1],
					.vz = (s16) n64light->dir[2]
				};
				n = gfx_modelview_apply_without_translation(&n);
				s32 n_len_sq = (s32) n.vx * n.vx + (s32) n.vy * n.vy + (s32) n.vz * n.vz;
				if(n_len_sq > 0) {
					s32 n_len = sqrtu(n_len_sq);
					n.vx = (s32) n.vx * ONE / n_len;
					n.vy = (s32) n.vy * ONE / n_len;
					n.vz = (s32) n.vz * ONE / n_len;
				}
				u32 light_index = op == DL_CMD_LIGHT_DIRECTIONAL1? 1: 0;
				light_directions[light_index] = n;
				light_colors[light_index] = (Color) {.r = n64light->col[0], .g = n64light->col[1], .b = n64light->col[2]};
				break;
			}
			case DL_CMD_MTX_SET: case DL_CMD_MTX_MUL: {
				gfx_flush_tessellation_queue_if_necessary();
				const ShortMatrix* addr = DL_UNPACK_PTR(cmd);
				if(op == DL_CMD_MTX_MUL) {
					gfx_modelview_mul(addr);
				} else {
					gfx_modelview_set(addr);
				}
				break;
			}
			case DL_CMD_MTX_PUSH: {
				gfx_modelview_push();
				break;
			}
			case DL_CMD_MTX_POP: {
				gfx_flush_tessellation_queue_if_necessary();
				gfx_modelview_pop();
				break;
			}
			case DL_CMD_MTX_N64_SET: case DL_CMD_MTX_N64_MUL: {
				gfx_flush_tessellation_queue_if_necessary();
				const u32* addr = DL_UNPACK_PTR(cmd);
				ShortMatrix arg_mtx;
				for(int i = 0; i < 4; i++) {
					for(int j = 0; j < 4; j += 2) {
						u32 int_part = addr[i * 2 + j / 2];
						u32 frac_part = addr[8 + i * 2 + j / 2];
						mtx_set_cell(&arg_mtx, i, j, (s32) (frac_part >> 16 | (int_part & 0xffff0000)) >> 4);
						mtx_set_cell(&arg_mtx, i, j + 1, (s32) (int_part << 16 | (frac_part & 0xffff)) >> 4);
					}
				}
				if(op == DL_CMD_MTX_N64_MUL) {
					gfx_modelview_mul(&arg_mtx);
				} else {
					gfx_modelview_set(&arg_mtx);
				}
				break;
			}
			case DL_CMD_MULTIPLIER: {
				multiplier = cmd & 0xFFFFFF;
				break;
			}
			case DL_CMD_SET_BACKGROUND: {
				is_2d_background = cmd & 1;
				break;
			}
			case DL_CMD_SET_ORTHO: {
				is_ortho = cmd & 1;
				break;
			}
			case DL_CMD_SPRITE: {
				draw_sprite((s32) ((u32) cmd << 20) >> 20, (s32) ((u32) cmd << 8) >> 20);
				break;
			}
			case DL_CMD_SQUARE_SHADOW: case DL_CMD_CIRCLE_SHADOW: {
				draw_shadow((s16) (cmd & 0xFFFF), (u8) (cmd >> 16 & 0xFF));
				break;
			}
			default: abortf("invalid compiled display list opcode %d\n", op);
		}
	}
}
