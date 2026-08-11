#include <port/gfx/gfx.h>
#include <ps1/registers.h>
#include <port/gfx/gfx_internal.h>
#include <ps1/gpucmd.h>
#include <ps1/gte.h>
#include <engine/math_util.h>
#include <game/game_init.h>

// check the .map file and ensure the section is smaller than 0x1000 so it actually fits in icache!!!
#define DL_EXEC_ICACHE_FUNC [[gnu::section(".dl_exec")]] [[gnu::noinline]]

u32 debug_processed_poly_count;
scratchpad void* tex_ptr;
scratchpad static GfxVtx* vertices;
scratchpad static Color env_color;
scratchpad static bool is_ortho;
static bool is_2d_background;
static u8 foreground_z;

void gfx_reset_dl_exec() {
	tex_ptr = NULL;
	is_2d_background = true;
	foreground_z = FOREGROUND_BUCKETS;
	env_color.as_u32 = 0xFFFFFFFF;
	is_ortho = false;
	gfx_modelview_identity();
	gte_setControlReg(GTE_RBK, 0);
	gte_setControlReg(GTE_GBK, 0);
	gte_setControlReg(GTE_BBK, 0);
	gte_setControlReg(GTE_L11L12, 0);
	gte_setControlReg(GTE_L13L21, 0);
	gte_setControlReg(GTE_L22L23, 0);
	gte_setControlReg(GTE_L31L32, 0);
	gte_setControlReg(GTE_L33, 0);
	gte_setControlReg(GTE_LC11LC12, 0);
	gte_setControlReg(GTE_LC13LC21, 0);
	gte_setControlReg(GTE_LC22LC23, 0);
	gte_setControlReg(GTE_LC31LC32, 0);
	gte_setControlReg(GTE_LC33, 0);
}

typedef union {
	struct {
		u8 idx0;
		u8 idx1;
		u16 weight;
	};
	u32 data;
} Interpolated;

typedef struct {
	//u32 v0data;
	//u32 v1data;
	//u32 v2data;
	//u32 v3data;
	GfxVtx v0;
	GfxVtx v1;
	GfxVtx v2;
	GfxVtx v3;
	bool is_quad;
} SubPoly;

#define POLY_QUEUE_MAX 3

typedef struct {
	u32 count;
	SubPoly sub_polys[POLY_QUEUE_MAX];
} PolyQueue;

static GfxVtx between(const GfxVtx* v0, const GfxVtx* v1) {
	return (GfxVtx) {
		.x = ((s32) v0->x + v1->x) / 2, .y = ((s32) v0->y + v1->y) / 2, .z = ((s32) v0->z + v1->z) / 2,
		.u = (v0->u + v1->u) / 2, .v = (v0->v + v1->v) / 2,
		.color.as_u32 = ((v0->color.as_u32 & 0xFEFEFE) + (v1->color.as_u32 & 0xFEFEFE)) / 2,
	};
}

[[gnu::flatten]] ALWAYS_INLINE static void draw_poly(PolyQueue* queue, u32 flags) {
	SubPoly* poly = &queue->sub_polys[--queue->count];
	GfxVtx* v0 = &poly->v0;
	GfxVtx* v1 = &poly->v1;
	GfxVtx* v2 = &poly->v2;
	GfxVtx* v3 = poly->is_quad? &poly->v3: NULL;

	gte_loadDataRegM(GTE_VXY0, (const u32*) &v0->xy);
	gte_loadDataRegM(GTE_VZ0, &v0->zuv);
	gte_loadDataRegM(GTE_VXY1, (const u32*) &v1->xy);
	gte_loadDataRegM(GTE_VZ1, &v1->zuv);
	gte_loadDataRegM(GTE_VXY2, (const u32*) &v2->xy);
	gte_loadDataRegM(GTE_VZ2, &v2->zuv);

#ifdef PRIM_FLAG_ENV_ALPHA
	if((flags & PRIM_FLAG_ENV_ALPHA) && env_alpha._pad < ALPHA_OPAQUE) {
#else
	if((flags & PRIM_FLAG_ENV_COLOR) && env_color._pad < ALPHA_OPAQUE) {
#endif
		if(env_color._pad < ALPHA_TRANSLUCENT) {
			return;
		}
		flags |= PRIM_FLAG_FORCE_BLEND;
	} else if((flags & PRIM_FLAG_TEXTURED) && ((TexHeader*) tex_ptr)->has_translucency) {
		flags |= PRIM_FLAG_FORCE_BLEND;
	}

	u32 sxy0, sxy1, sxy2, sxy3;
	s32 z, min_z;
	bool gte_errored;
	if(is_ortho) {
		s16 ortho_z = -1;
		ortho_z = is_2d_background? BACKGROUND_Z: (foreground_z? --foreground_z: 0);
		//if((u32) ortho_z >= MAX_Z) return;
		gte_errored = false;
		z = ortho_z;
		min_z = ortho_z;
		gte_setControlReg(GTE_RT31RT32, 0);
		gte_setControlReg(GTE_RT33, 0);
		gte_commandNoNop(GTE_CMD_MVMVA | GTE_SF | GTE_V_V0 | GTE_MX_RT | GTE_CV_TR);
		//debug_processed_poly_count++;
		sxy0 = (gte_getDataReg(GTE_IR1) & 0xFFFF) | gte_getDataReg(GTE_IR2) << 16;
		gte_commandNoNop(GTE_CMD_MVMVA | GTE_SF | GTE_V_V1 | GTE_MX_RT | GTE_CV_TR);
		if(v3) {
			gte_loadDataRegM(GTE_VXY0, (u32*) &v3->xy);
			gte_loadDataRegM(GTE_VZ0, (u32*) &v3->z);
		}
		sxy1 = (gte_getDataReg(GTE_IR1) & 0xFFFF) | gte_getDataReg(GTE_IR2) << 16;
		gte_commandNoNop(GTE_CMD_MVMVA | GTE_SF | GTE_V_V2 | GTE_MX_RT | GTE_CV_TR);
		sxy2 = (gte_getDataReg(GTE_IR1) & 0xFFFF) | gte_getDataReg(GTE_IR2) << 16;
		if(v3) {
			gte_commandNoNop(GTE_CMD_MVMVA | GTE_SF | GTE_V_V0 | GTE_MX_RT | GTE_CV_TR);
			sxy3 = (gte_getDataReg(GTE_IR1) & 0xFFFF) | gte_getDataReg(GTE_IR2) << 16;
		}
	} else {
		// RTPT transforms and projects all 3 vertices in a mere 23 cycles. based GTE :)
		gte_commandNoNop(GTE_CMD_RTPT | GTE_SF);

		debug_processed_poly_count++;
		is_2d_background = false;

		//// if there was any error in rtpt, cull it
		//if(gte_getControlReg(GTE_FLAG) & IMPORTANT_GTE_ERRORS) return;
		gte_errored = gte_getControlReg(GTE_FLAG) & IMPORTANT_GTE_ERRORS;

		// prepare to reject backfaces
		gte_commandNoNop(GTE_CMD_NCLIP);

		// sort z in the meantime
		z = gte_getDataReg(GTE_SZ1);
		if(z >= MAX_Z && !v3) {
			return; // the quickest rejection known to man
		}
		s32 v1sz = gte_getDataReg(GTE_SZ2);
		s32 v2sz = gte_getDataReg(GTE_SZ3);
		if(v1sz > z) {
			min_z = z;
			z = v1sz;
		} else {
			min_z = v1sz;
		}
		if(v2sz > z) {
			z = v2sz;
		} else if(v2sz < min_z) {
			min_z = v2sz;
		}

		// reject backfaced triangles asap (cannot reject quads early because they are not guaranteed to be flat)
		s32 nclip_result = gte_getDataReg(GTE_MAC0);
		if(nclip_result >= 0 && !v3) return;

		// fetch the rest of the results
		sxy0 = gte_getDataReg(GTE_SXY0);
		sxy1 = gte_getDataReg(GTE_SXY1);
		sxy2 = gte_getDataReg(GTE_SXY2);

		if(v3) {
			// if this is a quad, quickly transform the extra vertex with rtps
			gte_loadDataRegM(GTE_VXY0, (const u32*) &v3->xy);
			gte_loadDataRegM(GTE_VZ0, &v3->zuv);
			gte_commandAfterLoad(GTE_CMD_RTPS | GTE_SF);

			//// if there was any error in rtps, cull it
			//if(gte_getControlReg(GTE_FLAG) & IMPORTANT_GTE_ERRORS) return;
			gte_errored = gte_errored || (gte_getControlReg(GTE_FLAG) & IMPORTANT_GTE_ERRORS);

			// prepare to reject backfaces
			gte_commandNoNop(GTE_CMD_NCLIP);

			// get the result
			sxy3 = gte_getDataReg(GTE_SXY2);
			s32 v3sz = gte_getDataReg(GTE_SZ3);
			if(v3sz > z) {
				z = v3sz;
			} else if(v3sz < min_z) {
				min_z = v3sz;
			}

			// reject if both triangles are backfaced
			if(nclip_result >= 0 && (s32) gte_getDataReg(GTE_MAC0) >= 0) return;
		} else {
			sxy3 = sxy2;
		}
		if((u32) (z - 1) >= (u32) (MAX_Z - 1)) return;
		s16 sx0 = sxy0, sx1 = sxy1, sx2 = sxy2, sx3 = sxy3;
		if((sx0 <= 0 && sx1 <= 0 && sx2 <= 0 && sx3 <= 0) || (sx0 >= XRES && sx1 >= XRES && sx2 >= XRES && sx3 >= XRES)) {
			return;
		}
	}

	u32 rgb0, rgb1, rgb2, rgb3;
	if(flags & PRIM_FLAG_LIGHTED) {
		gte_setV0((s16) (s8) v0->color.r * (ONE / 128), (s16) (s8) v0->color.g * (ONE / 128), (s16) (s8) v0->color.b * (ONE / 128));
		gte_setV1((s16) (s8) v1->color.r * (ONE / 128), (s16) (s8) v1->color.g * (ONE / 128), (s16) (s8) v1->color.b * (ONE / 128));
		gte_setV2((s16) (s8) v2->color.r * (ONE / 128), (s16) (s8) v2->color.g * (ONE / 128), (s16) (s8) v2->color.b * (ONE / 128));
		gte_commandNoNop(GTE_CMD_NCT | GTE_SF | GTE_LM); // 30 cycles
	} else {
		if(flags & PRIM_FLAG_ENV_COLOR) {
			rgb0 = env_color.as_u32 << 8 >> 8;
			rgb1 = rgb0;
			rgb2 = rgb0;
			rgb3 = rgb0;
		} else {
			rgb0 = v0->color.as_u32;
			rgb1 = v1->color.as_u32;
			rgb2 = v2->color.as_u32;
			if(v3) rgb3 = v3->color.as_u32;
		}
	}

	// these things will hopefully be done while nct is cooking
	if(false && (gte_errored || ((flags & PRIM_FLAG_TESSELLATE) && min_z <= MAX_TESSELLATION_Z))) {
#if 1
		if(v3) {
			if(queue->count <= (u32) POLY_QUEUE_MAX - 4) {
				SubPoly* additions = &queue->sub_polys[queue->count + 1];
				queue->count += 4;

				// top right
				additions[0].v0 = between(v0, v2);
				additions[0].v1 = between(v0, v3);
				additions[0].v2 = *v2;
				additions[0].v3 = between(v2, v3);
				additions[0].is_quad = true;

				// bottom left
				additions[1].v0 = between(v0, v1);
				additions[1].v1 = *v1;
				additions[1].v2 = additions[0].v1;
				additions[1].v3 = between(v1, v3);
				additions[1].is_quad = true;

				// bottom right
				additions[2].v0 = additions[0].v1;
				additions[2].v1 = additions[1].v3;
				additions[2].v2 = additions[0].v3;
				additions[2].v3 = *v3;
				additions[2].is_quad = true;

				// top left
				*v1 = additions[1].v0;
				*v2 = additions[0].v0;
				*v3 = additions[0].v1;
			}
		} else {
			if(queue->count <= POLY_QUEUE_MAX - 3) {
				SubPoly* additions = &queue->sub_polys[queue->count + 1];
				queue->count += 3;

				// top right
				additions[0].v0 = between(v0, v2);
				additions[0].v1 = between(v1, v2);
				additions[0].v2 = *v2;
				additions[0].is_quad = false;

				// bottom left
				additions[1].v0 = between(v0, v1);
				additions[1].v1 = *v1;
				additions[1].v2 = additions[0].v1;
				additions[1].is_quad = false;

				// top left
				*v1 = additions[1].v0;
				*v2 = additions[0].v0;
				poly->v3 = additions[0].v1;
				poly->is_quad = true;
			}
		}
#else
		if(gte_errored) {
			flags |= PRIM_FLAG_TESSELLATE | PRIM_FLAG_TESSELLATE_HIGH;
		} else if(min_z > MAX_HIGH_TESSELLATION_Z) {
			flags &= ~PRIM_FLAG_TESSELLATE_HIGH;
		}
		gfx_begin_queueing_for_tessellation(v0, v1, v2, v3, flags);
		if(flags & PRIM_FLAG_LIGHTED) {
			rgb0 = gte_getDataReg(GTE_RGB0);
			rgb1 = gte_getDataReg(GTE_RGB1);
			rgb2 = gte_getDataReg(GTE_RGB2);
			if(v3) {
				gte_setV0((s16) (s8) v3->color.r * (ONE / 128), (s16) (s8) v3->color.g * (ONE / 128), (s16) (s8) v3->color.b * (ONE / 128));
				gte_commandNoNop(GTE_CMD_NCS | GTE_SF | GTE_LM);
				rgb3 = gte_getDataReg(GTE_RGB2) / 2 & 0x7F7F7F;
			}
		}
		rgb0 = rgb0 / 2 & 0x7F7F7F;
		rgb1 = rgb1 / 2 & 0x7F7F7F;
		rgb2 = rgb2 / 2 & 0x7F7F7F;
		gfx_finish_queueing_for_tessellation(rgb0, rgb1, rgb2, rgb3);
#endif
		return;
	}
	u32 ot_z = z / (MAX_Z / Z_BUCKETS) + FOREGROUND_BUCKETS;

	if(flags & PRIM_FLAG_LIGHTED) {
		rgb0 = gte_getDataReg(GTE_RGB0);
		rgb1 = gte_getDataReg(GTE_RGB1);
		rgb2 = gte_getDataReg(GTE_RGB2);
		if(v3) {
			gte_setV0((s16) (s8) v3->color.r * (ONE / 128), (s16) (s8) v3->color.g * (ONE / 128), (s16) (s8) v3->color.b * (ONE / 128));
			gte_commandNoNop(GTE_CMD_NCS | GTE_SF | GTE_LM);
			rgb3 = gte_getDataReg(GTE_RGB2);
		}
	}

	Packet packet = gfx_packet_begin();
	if(!(flags & PRIM_FLAG_TEXTURED) || (flags & PRIM_FLAG_DECAL)) {
		gfx_packet_append(&packet, rgb0 | _gp0_polygon(v3, false, true, false, flags & PRIM_FLAG_FORCE_BLEND));
		gfx_packet_append(&packet, sxy0);
		gfx_packet_append(&packet, rgb1);
		gfx_packet_append(&packet, sxy1);
		gfx_packet_append(&packet, rgb2);
		gfx_packet_append(&packet, sxy2);
		if(v3) {
			gfx_packet_append(&packet, rgb3);
			gfx_packet_append(&packet, sxy3);
		}
	}
	if(flags & PRIM_FLAG_TEXTURED) {
		TexHeader* tex = (TexHeader*) tex_ptr;
		gfx_packet_append(&packet, tex->window_cmd);
		gfx_packet_append(&packet, (rgb0 / 2 & 0x7F7F7F) | _gp0_polygon(v3, false, true, true, flags & PRIM_FLAG_FORCE_BLEND));
		gfx_packet_append(&packet, sxy0);
		gfx_packet_append(&packet, v0->uv | (u32) tex->clut_attr << 16);
		gfx_packet_append(&packet, (rgb1 / 2 & 0x7F7F7F));
		gfx_packet_append(&packet, sxy1);
		gfx_packet_append(&packet, v1->uv | (u32) tex->page_attr << 16);
		gfx_packet_append(&packet, (rgb2 / 2 & 0x7F7F7F));
		gfx_packet_append(&packet, sxy2);
		gfx_packet_append(&packet, v2->uv);
		if(v3) {
			gfx_packet_append(&packet, (rgb3 / 2 & 0x7F7F7F));
			gfx_packet_append(&packet, sxy3);
			gfx_packet_append(&packet, v3->uv);
		}
	}
	gfx_packet_end(packet, ot_z);
}

ALWAYS_INLINE static void set_light_from_cmd(dl_t cmd, u32 light_idx) {
	Light_t* n64light = DL_UNPACK_PTR(cmd);

	u32 normals = *(u32*) n64light->dir;
	gte_setDataReg(GTE_VXY0, (u32) ((s32) normals << 24 >> 8) >> 16 | (s32) (normals & 0xFF00) << 16 >> 8);
	gte_setDataReg(GTE_VZ0, (s32) normals << 8 >> 24);
	gte_commandNoNop(GTE_CMD_MVMVA | GTE_SF | GTE_V_V0 | GTE_MX_RT | GTE_CV_NONE);

	u32 rgb = *(u32*) n64light->col;
	u32 r = rgb << 4 & 0xFF0;
	u32 g = rgb >> 4 & 0xFF0;
	u32 b = rgb >> 12 & 0xFF0;

	s16 nx = gte_getDataReg(GTE_IR1);
	s16 ny = gte_getDataReg(GTE_IR2);
	s16 nz = gte_getDataReg(GTE_IR3);
	gte_commandNoNop(GTE_CMD_SQR);
	u32 normal_len_sq = gte_getDataReg(GTE_MAC1) + gte_getDataReg(GTE_MAC2) + gte_getDataReg(GTE_MAC3);
	if(normal_len_sq > 1) {
		u32 normal_len = sqrtu(normal_len_sq);
		s32 div = ONE * 128 / normal_len;
		nx = nx * div / 128;
		ny = ny * div / 128;
		nz = nz * div / 128;
	}

	u32 l13l21bak = gte_getControlReg(GTE_L13L21);
	if(light_idx == 0) {
		gte_setControlReg(GTE_LC11LC12, r);
		gte_setControlReg(GTE_LC13LC21, (gte_getControlReg(GTE_LC13LC21) & 0x0000FFFF) | g << 16);
		gte_setControlReg(GTE_LC31LC32, b);
		gte_setControlReg(GTE_L11L12, nx);
		gte_setControlReg(GTE_L13L21, (l13l21bak & 0x0000FFFF) | ny << 16);
		gte_setControlReg(GTE_L31L32, nz);
	} else {
		gte_setControlReg(GTE_LC13LC21, (gte_getControlReg(GTE_LC13LC21) & 0xFFFF0000) | r);
		gte_setControlReg(GTE_LC22LC23, g << 16);
		gte_setControlReg(GTE_LC33, b);
		gte_setControlReg(GTE_L13L21, (l13l21bak & 0xFFFF0000) | nx);
		gte_setControlReg(GTE_L22L23, ny << 16);
		gte_setControlReg(GTE_L33, nz);
	}
}

[[gnu::flatten]] static void draw_square_shadow(s32 radius, u8 opacity) {
	u32 sxy0, sxy1, sxy2, sxy3;
	gte_setV0(-radius, 0, -radius);
	gte_setV1(-radius, 0, radius);
	gte_setV2(radius, 0, -radius);
	gte_commandNoNop(GTE_CMD_RTPT | GTE_SF);

	if(gte_getControlReg(GTE_FLAG) & IMPORTANT_GTE_ERRORS) return;

	sxy0 = gte_getDataReg(GTE_SXY0);
	sxy1 = gte_getDataReg(GTE_SXY1);
	sxy2 = gte_getDataReg(GTE_SXY2);
	gte_setV0(radius, 0, radius);
	gte_commandNoNop(GTE_CMD_RTPS | GTE_SF);
	sxy3 = gte_getDataReg(GTE_SXY2);
	gte_commandNoNop(GTE_CMD_AVSZ4 | GTE_SF);
	s16 z = gte_getDataReg(GTE_OTZ);

	z -= DECAL_Z_BIAS;

	if((u32) (z - 1) >= (u32) (Z_BUCKETS - 1)) {
		return;
	}

	Packet packet = gfx_packet_begin();
	gfx_packet_append(&packet, gp0_texpage(gp0_page(0, 0, GP0_BLEND_SUBTRACT, GP0_COLOR_4BPP), true, false));

	u32 color = (u32) opacity | (u32) opacity << 8 | (u32) opacity << 16;
	gfx_packet_append(&packet, color | gp0_shadedQuad(false, false, true));
	gfx_packet_append(&packet, sxy0);
	gfx_packet_append(&packet, sxy1);
	gfx_packet_append(&packet, sxy2);
	gfx_packet_append(&packet, sxy3);

	gfx_packet_append(&packet, gp0_texpage(gp0_page(0, 0, GP0_BLEND_SEMITRANS, GP0_COLOR_4BPP), true, false));
	gfx_packet_end(packet, z);
}

#define DEGREE_SIN(deg) ((s16) (__builtin_sin(deg * (M_PI / 180.0)) * ONE))
#define DEGREE_COS(deg) ((s16) (__builtin_cos(deg * (M_PI / 180.0)) * ONE))

// the only vertex of the hexagon that cannot be easily derived
static const s16 shadow_vertex1[2] = {
	DEGREE_SIN(60), DEGREE_COS(60) // if you see an error here, it's because clang doesn't recognize gcc's __builtin_ math functions, ignore it
};

[[gnu::flatten]] static void draw_circle_shadow(s32 radius, u8 opacity) {
	u32 sxy0, sxy1, sxy2, sxy3, sxy4, sxy5;
	s16 v1x = shadow_vertex1[0] * radius / ONE;
	s16 v1z = shadow_vertex1[1] * radius / ONE;
	gte_setV0(0, 0, radius);
	gte_setV1(v1x, 0, v1z);
	gte_setV2(v1x, 0, -v1z);
	gte_commandNoNop(GTE_CMD_RTPT | GTE_SF);
	if(gte_getControlReg(GTE_FLAG) & IMPORTANT_GTE_ERRORS) return;
	sxy0 = gte_getDataReg(GTE_SXY0);
	sxy1 = gte_getDataReg(GTE_SXY1);
	sxy2 = gte_getDataReg(GTE_SXY2);
	s16 z = gte_getDataReg(GTE_SZ1); // depth of v0

	gte_setV0(0, 0, -radius);
	gte_setV1(-v1x, 0, -v1z);
	gte_setV2(-v1x, 0, v1z);
	gte_commandNoNop(GTE_CMD_RTPT | GTE_SF);
	if(gte_getControlReg(GTE_FLAG) & IMPORTANT_GTE_ERRORS) return;
	sxy3 = gte_getDataReg(GTE_SXY0);
	sxy4 = gte_getDataReg(GTE_SXY1);
	sxy5 = gte_getDataReg(GTE_SXY2);
	s16 sz3 = gte_getDataReg(GTE_SZ1); // depth of v3
	if(sz3 > z) {
		z = sz3;
	}

	z += 96; //ensure the shadow appears behind the object
	// z has been approximated as the max depth of the inner triangle that is rendered in the last part of the packet below
	if((u32) (z - 1) >= (u32) (MAX_Z - 1)) {
		return;
	}
	z /= (MAX_Z / Z_BUCKETS);

	Packet packet = gfx_packet_begin();
	gfx_packet_append(&packet, gp0_texpage(gp0_page(0, 0, GP0_BLEND_SUBTRACT, GP0_COLOR_4BPP), true, false));

	u32 color = (u32) opacity | (u32) opacity << 8 | (u32) opacity << 16;
	gfx_packet_append(&packet, color | gp0_shadedQuad(false, false, true));
	gfx_packet_append(&packet, sxy0);
	gfx_packet_append(&packet, sxy1);
	gfx_packet_append(&packet, sxy3);
	gfx_packet_append(&packet, sxy2);

	gfx_packet_append(&packet, color | gp0_shadedQuad(false, false, true));
	gfx_packet_append(&packet, sxy3);
	gfx_packet_append(&packet, sxy4);
	gfx_packet_append(&packet, sxy0);
	gfx_packet_append(&packet, sxy5);

	gfx_packet_append(&packet, gp0_texpage(gp0_page(0, 0, GP0_BLEND_SEMITRANS, GP0_COLOR_4BPP), true, false));
	gfx_packet_end(packet, z);
}

[[gnu::noinline]] static void handle_extra_cmd(u8 op, u32 cmd) {
	[[gnu::assume(op >= _DL_CMD_ENUM_FIRST_EXTRA && op <= _DL_CMD_ENUM_END)]];
	switch(op) {
		case DL_CMD_MTX_MUL: {
			const ShortMatrix* mtx = (const ShortMatrix*) cmd;
			gfx_modelview_mul(mtx);
			break;
		}
		case DL_CMD_MTX_N64_SET:
		case DL_CMD_MTX_N64_MUL: {
			const u32* addr = (const u32*) cmd;
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
		case DL_CMD_MTX_PUSH: {
			gfx_modelview_push();
			break;
		}
		case DL_CMD_MTX_POP: {
			gfx_modelview_pop();
			break;
		}
		case DL_CMD_SQUARE_SHADOW: {
			draw_square_shadow((s16) (cmd & 0xFFFF), (u8) (cmd >> 16 & 0xFF));
			break;
		}
	}
}

DL_EXEC_ICACHE_FUNC void gfx_run_compiled_dl(dl_t* dl) {
	dl_t* call_stack[16];
	u32 call_stack_idx = 0;
	while(true) {
		u32 cmd = *(dl++);
		u8 op = DL_UNPACK_OP(cmd);
		cmd &= 0xFFFFFF;
		[[gnu::assume(op >= _DL_CMD_ENUM_START && op <= _DL_CMD_ENUM_END)]];
		switch(op) {
			case DL_CMD_JUMP: {
				dl = (dl_t*) cmd;
				break;
			}
			case DL_CMD_CALL: {
				call_stack[call_stack_idx++] = dl;
				dl = (dl_t*) cmd;
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
				tex_ptr = (void*) cmd;
				break;
			}
			case DL_CMD_VTX: {
				vertices = (void*) cmd;
				break;
			}
			case DL_CMD_TRI: case DL_CMD_QUAD: {
				PolyQueue poly_queue;
				poly_queue.count = 1;
				poly_queue.sub_polys[0].v0 = vertices[cmd >> 20 /*& 0xF*/];
				poly_queue.sub_polys[0].v1 = vertices[cmd >> 16 & 0xF];
				poly_queue.sub_polys[0].v2 = vertices[cmd >> 12 & 0xF];
				if(op == DL_CMD_QUAD) {
					poly_queue.sub_polys[0].v3 = vertices[cmd >> 8 & 0xF];
					poly_queue.sub_polys[0].is_quad = true;
				} else {
					poly_queue.sub_polys[0].is_quad = false;
				}
				do {
					draw_poly(&poly_queue, cmd & 0xFF);
				} while(poly_queue.count);
				break;
			}
			case DL_CMD_ENV_COLOR_ALPHA_0:
			case DL_CMD_ENV_COLOR_ALPHA_HALF:
			case DL_CMD_ENV_COLOR_ALPHA_RESERVED:
			case DL_CMD_ENV_COLOR_ALPHA_FULL: {
				env_color.as_u32 = cmd /*& 0xFFFFFF)*/ | (s32) (op - DL_CMD_ENV_COLOR_ALPHA_0) << 30 >> 6;
				break;
			}
			case DL_CMD_LIGHT_AMBIENT: {
				gte_setControlReg(GTE_RBK, (cmd & 0xFF) * (ONE / 256));
				gte_setControlReg(GTE_GBK, (cmd >> 8 & 0xFF) * (ONE / 256));
				gte_setControlReg(GTE_BBK, (cmd >> 16 /*& 0xFF*/) * (ONE / 256));
				break;
			}
			case DL_CMD_LIGHT_DIRECTIONAL0:
			case DL_CMD_LIGHT_DIRECTIONAL1: {
				set_light_from_cmd(cmd, op - DL_CMD_LIGHT_DIRECTIONAL0);
				break;
			}
			case DL_CMD_MTX_SET: {
				const ShortMatrix* mtx = (const ShortMatrix*) cmd;
				gfx_modelview_set(mtx);
				break;
			}
			case DL_CMD_MULTIPLIER: {
				gte_setControlReg(GTE_H, cmd /*& 0xFFFFFF*/);
				break;
			}
			case DL_CMD_SET_BACKGROUND: {
				is_2d_background = cmd; //& 1;
				break;
			}
			case DL_CMD_SET_ORTHO: {
				is_ortho = cmd; //& 1;
				break;
			}
			case DL_CMD_SPRITE: {
				if(tex_ptr) {
					s32 x = ((u32) cmd << 20) >> 20;
					s32 y = ((u32) cmd << 8) >> 20;
					u16 z = is_2d_background? BACKGROUND_Z: (foreground_z? --foreground_z: 0);
					Packet packet = gfx_packet_begin();
					TexHeader* tex = tex_ptr;
					gfx_packet_append(&packet, tex->window_cmd);
					gfx_packet_append(&packet, gp0_texpage(tex->page_attr, true, false));
					gfx_packet_append(&packet, env_color.as_u32 << 8 >> 8 | gp0_rectangle(true, true, env_color._pad < ALPHA_OPAQUE));
					gfx_packet_append(&packet, gp0_xy(x, y));
					gfx_packet_append(&packet, gp0_uv(tex->offx, tex->offy, tex->clut_attr));
					gfx_packet_append(&packet, gp0_xy(tex->width, tex->height));
					gfx_packet_end(packet, z);
				}
				break;
			}
			case DL_CMD_CIRCLE_SHADOW: {
				draw_circle_shadow((s16) (cmd & 0xFFFF), (u8) (cmd >> 16 & 0xFF));
				break;
			}
			default: {
				handle_extra_cmd(op, cmd);
			}
		}
	}
}
