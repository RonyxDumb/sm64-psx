#include <PR/gbi.h>
#include <port/gfx/gfx_internal.h>
#include <port/psx/scratchpad_call.h>
#include <game/memory.h>
#include <game/game_init.h>
#include <engine/graph_node.h>
#include <engine/math_util.h>
#include <assert.h>

// N64 display list emulation

extern struct GraphNodePerspective* gCurGraphNodeCamFrustum;
extern bool compile_as_ortho;
bool compilation_happened_this_frame = false;

static int texture_scale_x, texture_scale_y;
static bool use_env_color = false;
static bool use_env_alpha = false;
static bool use_color = false;
static bool use_texture = false;
static u32 geometry_mode = 0;
static u32 other_mode_l = 0;
static u32 num_lights = 2; // includes ambient light
static TexHeader* compilation_tex_header = NULL;
static VtxList* compilation_vertices = NULL;
static bool compilation_clamp = false;

void gfx_reset_rsp_jit() {
	texture_scale_x = 1 << 21;
	texture_scale_y = 1 << 21;
	use_env_color = false;
	use_env_alpha = false;
	use_color = false;
	use_texture = false;
	geometry_mode = G_LIGHTING;
	other_mode_l = 0;
	num_lights = 2;
	compilation_tex_header = NULL;
	compilation_vertices = NULL;
	compilation_clamp = false;
}

static int clamp(int x, int a, int b) {
	return x < a? a: (x > b? b: x);
}

void ensure_vertices_converted(VtxList* list, u32 count) {
	TexHeader* tex = compilation_tex_header;
	if(list->tag != COMPILED_TAG) {
		for(u32 i = 0; i < count; i++) {
			Vtx* n64 = &list->n64[i];
			s16 x = n64->v.ob[0];
			s16 y = n64->v.ob[1];
			s16 z = n64->v.ob[2];
			s32 u = (s32) n64->v.tc[(tex && tex->rotated)? 1: 0] * (texture_scale_x >> 8) >> (21 - 8);
			s32 v = (s32) n64->v.tc[(tex && tex->rotated)? 0: 1] * (texture_scale_y >> 8) >> (21 - 8);
			if(tex) {
				if(compilation_clamp) {
					u = clamp(u, 0, tex->width);
					v = clamp(v, 0, tex->height);
				} else {
					if(tex->width <= 32) {
						u = clamp(u + 128, 0, 255);
					} else if(tex->width <= 64) {
						u = clamp(u + 64, 0, 255);
					}
					if(tex->height <= 32) {
						v = clamp(v + 128, 0, 255);
					} else if(tex->height <= 64) {
						v = clamp(v + 64, 0, 255);
					}
				}
			}
			Color color = (Color) {.r = n64->v.cn[0], .g = n64->v.cn[1], .b = n64->v.cn[2], ._pad = 0};

			GfxVtx* psx = &list->psx[i];
			psx->x = x;
			psx->y = y;
			psx->z = z;
			psx->u = u;
			psx->v = v;
			psx->color = color;
		}
		list->tag = COMPILED_TAG;
	}
}

static int min3(int x, int y, int z) {
	if(y < x) {
		x = y;
	}
	return z < x? z: x;
}

static int max3(int x, int y, int z) {
	if(y > x) {
		x = y;
	}
	return z > x? z: x;
}

// returns true if the compiled display list is not empty
[[gnu::flatten]] bool gfx_compile_rsp(Gfx* cmd, bool nested) {
	u8 first_op = DL_UNPACK_OP(*(dl_t*) cmd);
	if(first_op >= _DL_CMD_ENUM_START && first_op <= _DL_CMD_ENUM_END) {
		return first_op != DL_CMD_END;
	}
	if(!nested) {
		gfx_reset_rsp_jit();
	}
	compilation_happened_this_frame = true;
	dl_t* out_start = (dl_t*) cmd;
	dl_t* out = out_start;
	while(true) {
		u8 opcode = cmd->words.w0 >> 24;
		// those G_* constants HAVE to be cast to u8, because they are defined in a weird way. don't remove the casts
		switch(opcode) {
			case (u8) G_VTX: {
				u8 count = (cmd->words.w0 & 0xFFFF) / sizeof(Vtx);
				VtxList* vtx_list = segmented_to_virtual((void*) cmd->words.w1);
				assert((cmd->words.w0 >> 16 & 0xF) == 0);
				assert(count <= 16);
				ensure_vertices_converted(vtx_list, count);
				compilation_vertices = vtx_list;
				*(out++) = DL_PACK_OP(DL_CMD_VTX) | DL_PACK_PTR(vtx_list->psx);
				break;
			}
			case (u8) G_TRI1: {
				u32 i0 = (cmd->words.w1 >> 16 & 0xFF) / 10;
				u32 i1 = (cmd->words.w1 >> 8 & 0xFF) / 10;
				u32 i2 = (cmd->words.w1 & 0xFF) / 10;
				goto process_poly_cmd;
			case (u8) G_PORT_QUAD:
				i0 = cmd->words.w1 >> 24 & 0xF;
				i1 = cmd->words.w1 >> 16 & 0xF;
				i2 = cmd->words.w1 >> 8 & 0xF;
				u32 i3 = cmd->words.w1 & 0xF;
				GfxVtx* v3 = &compilation_vertices->psx[i3];
				goto process_poly_cmd;
			case (u8) G_PORT_TRI2:
				i0 = cmd->words.w0 >> 16 & 0xF;
				i1 = cmd->words.w0 >> 8 & 0xF;
				i2 = cmd->words.w0 & 0xF;
			process_poly_cmd:
				GfxVtx* v0 = &compilation_vertices->psx[i0];
				GfxVtx* v1 = &compilation_vertices->psx[i1];
				GfxVtx* v2 = &compilation_vertices->psx[i2];
				u32 flags = 0;
				if(use_texture && compilation_tex_header) flags |= PRIM_FLAG_TEXTURED;
				if(use_env_color) flags |= PRIM_FLAG_ENV_COLOR;
#ifdef PRIM_FLAG_ENV_ALPHA
				if(use_env_alpha) flags |= PRIM_FLAG_ENV_ALPHA;
#endif
				if(!compile_as_ortho) {
					if(geometry_mode & G_LIGHTING) {
						flags |= PRIM_FLAG_LIGHTED;
					}
					if((other_mode_l & ZMODE_DEC) == ZMODE_DEC) {
						flags |= PRIM_FLAG_DECAL;
					} else if(use_texture && compilation_tex_header) {
						s32 min_x = min3(v0->x, v1->x, v2->x);
						s32 min_y = min3(v0->y, v1->y, v2->y);
						s32 min_z = min3(v0->z, v1->z, v2->z);
						s32 max_x = max3(v0->x, v1->x, v2->x);
						s32 max_y = max3(v0->y, v1->y, v2->y);
						s32 max_z = max3(v0->z, v1->z, v2->z);
						if(opcode == G_PORT_QUAD) {
							if(v3->x < min_x) {
								min_x = v3->x;
							} else if(v3->x > max_x) {
								max_x = v3->x;
							}
							if(v3->y < min_y) {
								min_y = v3->y;
							} else if(v3->y > max_y) {
								max_y = v3->y;
							}
							if(v3->z < min_z) {
								min_z = v3->z;
							} else if(v3->z > max_z) {
								max_z = v3->z;
							}
						}
						u32 width = max_x - min_x;
						u32 height = max_y - min_y;
						u32 depth = max_z - min_z;
						u32 size_heuristic = sqrtu(width * width + height * height + depth * depth);
						if(size_heuristic > 256) {
							flags |= PRIM_FLAG_TESSELLATE;//_LOW;
							//if(size_heuristic > 1024) {
							//	flags |= PRIM_FLAG_TESSELLATE_HIGH;
							//}
						}
					}
				}
				if(opcode == G_PORT_QUAD) {
					*(out++) = DL_PACK_OP(DL_CMD_QUAD) | i0 << 20 | i1 << 16 | i2 << 12 | i3 << 8 | flags;
				} else if(opcode == G_PORT_TRI2) {
					u32 t1i0 = cmd->words.w1 >> 24 & 0xFF;
					u32 t1i1 = cmd->words.w1 >> 16 & 0xFF;
					u32 t1i2 = cmd->words.w1 >> 8 & 0xFF;
					*(out++) = DL_PACK_OP(DL_CMD_TRI) | i0 << 20 | i1 << 16 | i2 << 12 | flags;
					*(out++) = DL_PACK_OP(DL_CMD_TRI) | t1i0 << 20 | t1i1 << 16 | t1i2 << 12 | flags;
				} else {
					*(out++) = DL_PACK_OP(DL_CMD_TRI) | i0 << 20 | i1 << 16 | i2 << 12 | flags;
				}
				break;
			}
			case (u8) G_MTX: {
				u8 params = (cmd->words.w0 >> 16) & 0xFF;
				if(params & G_MTX_PROJECTION) {
					// we don't need a projection matrix! yep playstation is a rebel like that
				} else {
					// mess with modelview matrix
					s32* addr = segmented_to_virtual((s32*) cmd->words.w1);
					if(params & G_MTX_PUSH) {
						*(out++) = DL_PACK_OP(DL_CMD_MTX_PUSH);
					}
					if(params & G_MTX_LOAD) {
						*(out++) = DL_PACK_OP(DL_CMD_MTX_N64_SET) | DL_PACK_PTR(addr);
					} else {
						*(out++) = DL_PACK_OP(DL_CMD_MTX_N64_MUL) | DL_PACK_PTR(addr);
					}
				}
				break;
			}
			case (u8) G_POPMTX: {
				*(out++) = DL_PACK_OP(DL_CMD_MTX_POP);
				break;
			}
			case (u8) G_MOVEWORD: {
				u8 index = (cmd->words.w0 >> 16) & 0xFF;
				switch(index) {
					case G_MW_NUMLIGHT: {
						num_lights = (u32) (cmd->words.w1 - 0x80000000) / 32;
						break;
					}
				}
				break;
			}
			case (u8) G_MOVEMEM: {
				u8 index = (cmd->words.w0 >> 16) & 0xFF;
				switch(index) {
					case G_MV_L0: case G_MV_L1: case G_MV_L2: {
						const Light_t* n64light = (const Light_t*) segmented_to_virtual((void*) cmd->words.w1);
						switch(num_lights - 1 - (index - G_MV_L0) / 2) {
							case 0: {
								*(out++) = DL_PACK_OP(DL_CMD_LIGHT_AMBIENT) | (u32) n64light->col[2] << 16 | (u32) n64light->col[1] << 8 | (u32) n64light->col[0];
								break;
							}
							case 1: {
								*(out++) = DL_PACK_OP(DL_CMD_LIGHT_DIRECTIONAL0) | DL_PACK_PTR(n64light);
								break;
							}
							case 2: {
								*(out++) = DL_PACK_OP(DL_CMD_LIGHT_DIRECTIONAL1) | DL_PACK_PTR(n64light);
								break;
							}
						}
						break;
					}
				}
				break;
			}
			case (u8) G_TEXTURE: {
				texture_scale_x = cmd->words.w1 >> 16 & 0xFFFF;
				texture_scale_y = cmd->words.w1 & 0xFFFF;
				break;
			}
			case (u8) G_SETGEOMETRYMODE: {
				geometry_mode |= cmd->words.w1;
				break;
			}
			case (u8) G_CLEARGEOMETRYMODE: {
				geometry_mode &= ~cmd->words.w1;
				break;
			}
			case (u8) G_SETOTHERMODE_L: {
				const u8 bits = (cmd->words.w0 & 0xFF) + 1;
				const u8 shift = 32 - (cmd->words.w0 >> 8 & 0xFF) - bits;
				const u32 mask = ((1 << bits) - 1) << shift;
				other_mode_l = (other_mode_l & ~mask) | (cmd->words.w1 & mask);
				break;
			}
			case (u8) G_SETENVCOLOR: {
				u32 rgb = cmd->words.w1 >> 8;
				u8 alpha = cmd->words.w1;
				if(alpha >= ALPHA_OPAQUE) {
					*(out++) = DL_PACK_OP(DL_CMD_ENV_COLOR_ALPHA_FULL) | rgb;
				} else if(alpha >= ALPHA_TRANSLUCENT) {
					*(out++) = DL_PACK_OP(DL_CMD_ENV_COLOR_ALPHA_HALF) | rgb;
				} else {
					*(out++) = DL_PACK_OP(DL_CMD_ENV_COLOR_ALPHA_0) | rgb;
				}
				break;
			}
			case (u8) G_TEXRECT: {
				u32 x0 = (cmd->words.w1 >> 12 & 0xFFF) / 4;
				u32 y0 = (cmd->words.w1 >> 0 & 0xFFF) / 4;
				*(out++) = DL_PACK_OP(DL_CMD_SPRITE) | (y0 & 0xFFF) << 12 | (x0 & 0xFFF);
				break;
			}
			case (u8) G_SETCOMBINE: {
				u8 a_color_src = cmd->words.w0 >> 20 & 0x0F;
				u8 b_color_src = cmd->words.w1 >> 28 & 0x0F;
				u8 c_color_src = cmd->words.w0 >> 15 & 0x1F;
				u8 d_color_src = cmd->words.w1 >> 15 & 0x07;
				u8 c_alpha_src = cmd->words.w0 >> 9 & 0x07;
				u8 d_alpha_src = cmd->words.w1 >> 9 & 0x07;
				use_env_color = c_color_src == G_CCMUX_ENVIRONMENT || d_color_src == G_CCMUX_ENVIRONMENT;
				use_env_alpha = c_alpha_src == G_CCMUX_ENVIRONMENT || d_alpha_src == G_CCMUX_ENVIRONMENT;
				//assert(use_env_color == use_env_alpha);
				use_color = !use_env_color && (a_color_src == G_CCMUX_SHADE || b_color_src == G_CCMUX_SHADE || c_color_src == G_CCMUX_SHADE || d_color_src == G_CCMUX_SHADE);
				use_texture = a_color_src == G_CCMUX_TEXEL0 || b_color_src == G_CCMUX_TEXEL0 || c_color_src == G_CCMUX_TEXEL0 || d_color_src == G_CCMUX_TEXEL0;
				if(b_color_src == d_color_src) {
					other_mode_l |= ZMODE_DEC;
					// according to the DSi port this hides the overlay on the mario head since we can't do the blending it expects
					// but can we? can't test that yet, so this is for later
					if(a_color_src == G_CCMUX_PRIMITIVE) {
						use_texture = false;
						use_env_color = true;
						use_env_alpha = true;
					}
				} else {
					other_mode_l &= ~ZMODE_DEC;
				}
				break;
			}
			case (u8) G_SETTILE: {
				compilation_clamp = cmd->words.w1 & (G_TX_CLAMP << 18);
				break;
			}
			case (u8) G_SETTIMG: {
				compilation_tex_header = segmented_to_virtual((u8*) cmd->words.w1);
				if(compilation_tex_header) {
					gfx_load_texture(compilation_tex_header);
				}
				*(out++) = DL_PACK_OP(DL_CMD_TEX) | DL_PACK_PTR(compilation_tex_header);
				break;
			}
			case (u8) G_DL: {
				void* target = segmented_to_virtual((void*) cmd->words.w1);
				if(cmd->words.w0 & (1 << 16)) { // jump/tail call
					if(gfx_compile_rsp(target, true)) {
						*(out++) = DL_PACK_OP(DL_CMD_JUMP) | DL_PACK_PTR(target);
						return true;
					} else {
						goto end;
					}
				} else { // call and continue
					if(gfx_compile_rsp(target, true)) {
						*(out++) = DL_PACK_OP(DL_CMD_CALL) | DL_PACK_PTR(target);
					}
				}
				break;
			}
			case (u8) G_ENDDL: {
			end:
				bool was_useful = out != out_start;
				*(out++) = DL_PACK_OP(DL_CMD_END);
				return was_useful;
			}
		}
		cmd++;
	}
}
