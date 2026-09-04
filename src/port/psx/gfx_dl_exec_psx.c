#include "macros.h"
#include "types.h"
#include <assert.h>
#include <port/gfx/gfx.h>
#include <ps1/registers.h>
#include <port/gfx/gfx_internal.h>
#include <ps1/gpucmd.h>
#include <ps1/gte.h>
#include <engine/math_util.h>
#include <game/game_init.h>

//#define NO_GAPS // fills in T-junction gaps with more triangles (UNFINISHED - add some condition)
#define MAX_TESSELLATION_Z 800
#define VANITY_TESSELLATION 2 // applies to polygons near the camera
#define SPARING_TESSELLATION 3 // applies when saving polygons from clipping

// check the .map file and ensure the section is smaller than 0x1000 so it actually fits in icache!!!
#define DL_EXEC_ICACHE_FUNC [[gnu::section(".dl_exec")]] [[gnu::noinline]] [[gnu::flatten]]

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
	gte_setDataReg(GTE_RGBC, 0);
}

// Interpolate an 8-bit PS1 texture coordinate through the shortest 0..255
// branch. A plain average turns e.g. 250 -> 6 into 128, which creates a
// long diagonal smear after tessellation.
DL_EXEC_ICACHE_FUNC static u8 between_uv(u8 a, u8 b) {
	s32 aa = a;
	s32 bb = b;
	s32 delta = bb - aa;
	if(delta > 127) {
		aa += 256;
	} else if(delta < -128) {
		bb += 256;
	}
	return (u8) ((aa + bb) / 2);
}

DL_EXEC_ICACHE_FUNC static void between(GfxVtx* dst, const GfxVtx* a, const GfxVtx* b) {
	// note: since both vertices are definitely on the scratchpad now, loads are fast here!
	// note: the GTE has instructions for interpolation, but setting them up isn't fast enough
	dst->x = ((s32) a->x + b->x) / 2;
	dst->y = ((s32) a->y + b->y) / 2;
	dst->z = ((s32) a->z + b->z) / 2;
	dst->u = between_uv(a->u, b->u);
	dst->v = between_uv(a->v, b->v);
	dst->color.as_u32 = ((a->color.as_u32 & 0xFEFEFE) + (b->color.as_u32 & 0xFEFEFE)) / 2;
}

DL_EXEC_ICACHE_FUNC static void append_poly(Packet* packet, bool is_quad, s32 sxy3, u32 flags, const GfxVtx* vtx) {
	TexHeader* tex = (TexHeader*) tex_ptr;
	bool textured = tex && (flags & PRIM_FLAG_TEXTURED);
	if(textured) {
		gfx_packet_append(packet, tex->window_cmd);
	}
	gfx_packet_append(packet, vtx[0].color.as_u32 | _gp0_polygon(is_quad, false, true, textured, flags & PRIM_FLAG_FORCE_BLEND));
	gte_storeDataRegM(GTE_SXY0, gfx_packet_skip(packet));
	if(textured) {
		gfx_packet_append(packet, vtx[0].uv | (u32) tex->clut_attr << 16);
	}
	gfx_packet_append(packet, vtx[1].color.as_u32);
	gte_storeDataRegM(GTE_SXY1, gfx_packet_skip(packet));
	if(textured) {
		gfx_packet_append(packet, vtx[1].uv | (u32) tex->page_attr << 16);
	}
	gfx_packet_append(packet, vtx[2].color.as_u32);
	gte_storeDataRegM(GTE_SXY2, gfx_packet_skip(packet));
	if(textured) {
		gfx_packet_append(packet, vtx[2].uv);
	}
	if(is_quad) {
		gfx_packet_append(packet, vtx[3].color.as_u32);
		gfx_packet_append(packet, sxy3);
		if(textured) {
			gfx_packet_append(packet, vtx[3].uv);
		}
	}
}

enum {
	V0 = 0,
	V1,
	V2,
	V3,
	V01,
	V02,
	V13,
	V23,
	VC,
	V_COUNT,
};

DL_EXEC_ICACHE_FUNC static void draw_poly(const GfxVtx* v0, const GfxVtx* v1, const GfxVtx* v2, const GfxVtx* v3, u32 flags, GfxVtx* storage, u32 level) {
	u32 zuv0 = v0->zuv;
	u32 zuv1 = v1->zuv;
	u32 zuv2 = v2->zuv;
	gte_loadDataRegM(GTE_VXY0, (const u32*) &v0->xy);
	gte_setDataReg(GTE_VZ0, zuv0);
	storage[0].zuv = zuv0;
	gte_loadDataRegM(GTE_VXY1, (const u32*) &v1->xy);
	gte_setDataReg(GTE_VZ1, zuv1);
	storage[1].zuv = zuv1;
	gte_loadDataRegM(GTE_VXY2, (const u32*) &v2->xy);
	gte_setDataReg(GTE_VZ2, zuv2);
	storage[2].zuv = zuv2;

#ifdef PRIM_FLAG_ENV_ALPHA
	if((flags & PRIM_FLAG_ENV_ALPHA) && env_alpha.cmd < ALPHA_OPAQUE) {
#else
	if((flags & PRIM_FLAG_ENV_COLOR) && env_color.a < ALPHA_OPAQUE) {
#endif
		if(env_color.a < ALPHA_TRANSLUCENT) {
			return;
		}
		flags |= PRIM_FLAG_FORCE_BLEND;
	} else if((flags & PRIM_FLAG_TEXTURED) && ((TexHeader*) tex_ptr)->has_translucency) {
		flags |= PRIM_FLAG_FORCE_BLEND;
	}

	s32 sxy3;
	s32 z, min_z;
	u32 gte_flags;
	if(is_ortho) {
		z = is_2d_background? BACKGROUND_Z: (foreground_z? --foreground_z: 0);
		min_z = z;
		gte_flags = 0;
		gte_setControlReg(GTE_RT31RT32, 0);
		gte_setControlReg(GTE_RT33, 0);
		gte_commandNoNop(GTE_CMD_MVMVA | GTE_SF | GTE_V_V0 | GTE_MX_RT | GTE_CV_TR);
		debug_processed_poly_count++;
		gte_setDataReg(GTE_SXY0, (gte_getDataReg(GTE_IR1) & 0xFFFF) | gte_getDataReg(GTE_IR2) << 16);
		gte_commandNoNop(GTE_CMD_MVMVA | GTE_SF | GTE_V_V1 | GTE_MX_RT | GTE_CV_TR);
		u32 zuv3;
		if(v3) {
			zuv3 = v3->zuv;
			gte_loadDataRegM(GTE_VXY0, (const u32*) &v3->xy);
			gte_setDataReg(GTE_VZ0, zuv3);
		}
		gte_setDataReg(GTE_SXY1, (gte_getDataReg(GTE_IR1) & 0xFFFF) | gte_getDataReg(GTE_IR2) << 16);
		gte_commandNoNop(GTE_CMD_MVMVA | GTE_SF | GTE_V_V2 | GTE_MX_RT | GTE_CV_TR);
		gte_setDataReg(GTE_SXY2, (gte_getDataReg(GTE_IR1) & 0xFFFF) | gte_getDataReg(GTE_IR2) << 16);
		if(v3) {
			gte_commandNoNop(GTE_CMD_MVMVA | GTE_SF | GTE_V_V0 | GTE_MX_RT | GTE_CV_TR);
			storage[3].zuv = zuv3;
			sxy3 = (gte_getDataReg(GTE_IR1) & 0xFFFF) | gte_getDataReg(GTE_IR2) << 16;
		}
	} else {
		// RTPT transforms and projects all 3 vertices in a mere 23 cycles. based GTE :)
		gte_commandNoNop(GTE_CMD_RTPT | GTE_SF);

		gte_storeDataRegM(GTE_VXY0, (u32*) &storage[0].xy);
		gte_storeDataRegM(GTE_VXY1, (u32*) &storage[1].xy);
		gte_storeDataRegM(GTE_VXY2, (u32*) &storage[2].xy);

		debug_processed_poly_count++;
		is_2d_background = false;

		// if there was any error in rtpt, cull it
		gte_flags = gte_getControlReg(GTE_FLAG) & IMPORTANT_GTE_ERRORS;
		// Keep going: subdivision below is also the clip-recovery path.
		if(gte_flags && level >= SPARING_TESSELLATION) return;

		// prepare to reject backfaces
		gte_commandNoNop(GTE_CMD_NCLIP);

		// sort z in the meantime
		z = gte_getDataReg(GTE_SZ1);
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

		// fetch the rest of the results
		s32 sxy0 = gte_getDataReg(GTE_SXY0);

		// reject backfaced triangles asap (cannot reject quads early because they are not guaranteed to be flat)
		bool triangle_backfaced = (s32) gte_getDataReg(GTE_MAC0) >= 0;
		s32 near_z_for_cull = (s16) gte_getControlReg(GTE_H) / 2;
		bool depth_is_safe_for_cull = min_z > near_z_for_cull && z < MAX_Z;
		if(triangle_backfaced && !v3 && !gte_flags && depth_is_safe_for_cull) return;

		if(v3) {
			// if this is a quad, quickly transform the extra vertex with rtps
			u32 zuv3 = v3->zuv;
			gte_loadDataRegM(GTE_VXY0, (const u32*) &v3->xy);
			gte_setDataReg(GTE_VZ0, zuv3);
			gte_commandNoNop(GTE_CMD_RTPS | GTE_SF);

			storage[3].zuv = zuv3;
			gte_storeDataRegM(GTE_VXY0, (u32*) &storage[3].xy);

			// if there was any error in rtps, cull it
			gte_flags |= gte_getControlReg(GTE_FLAG) & IMPORTANT_GTE_ERRORS;
			if(gte_flags && level >= SPARING_TESSELLATION) return;

			// prepare to reject backfaces
			gte_commandNoNop(GTE_CMD_NCLIP);

			// get the result
			sxy3 = gte_getDataReg(GTE_SXY2);

			bool quad_second_triangle_backfaced = (s32) gte_getDataReg(GTE_MAC0) <= 0;

			gte_setDataReg(GTE_SXY2, gte_getDataReg(GTE_SXY1));
			gte_setDataReg(GTE_SXY1, gte_getDataReg(GTE_SXY0));
			gte_setDataReg(GTE_SXY0, sxy0);

			s32 v3sz = gte_getDataReg(GTE_SZ3);
			if(v3sz > z) {
				z = v3sz;
			} else if(v3sz < min_z) {
				min_z = v3sz;
			}

			// Reject only when all four depths are safely inside the clip range.
			// Otherwise the recursive clip-recovery path below gets first chance.
			if(triangle_backfaced && quad_second_triangle_backfaced && !gte_flags) {
				s32 near_z_for_quad_cull = (s16) gte_getControlReg(GTE_H) / 2;
				if(min_z > near_z_for_quad_cull && z < MAX_Z) return;
			}
		}
	}

	bool depth_straddles_clip = false;
	if(!is_ortho) {
		s32 near_z = (s16) gte_getControlReg(GTE_H) / 2;
		// Reject only when the entire primitive is outside. If it crosses the
		// near/far plane, subdivide it so the inside children can survive.
		if(z <= near_z || min_z >= MAX_Z) return;
		depth_straddles_clip = min_z <= near_z || z >= MAX_Z;
	}

	if(flags & PRIM_FLAG_LIGHTED) {
		gte_setV0((s16) (s8) v0->color.r * (ONE / 128), (s16) (s8) v0->color.g * (ONE / 128), (s16) (s8) v0->color.b * (ONE / 128));
		gte_setV1((s16) (s8) v1->color.r * (ONE / 128), (s16) (s8) v1->color.g * (ONE / 128), (s16) (s8) v1->color.b * (ONE / 128));
		gte_setV2((s16) (s8) v2->color.r * (ONE / 128), (s16) (s8) v2->color.g * (ONE / 128), (s16) (s8) v2->color.b * (ONE / 128));
		gte_commandNoNop(GTE_CMD_NCT | GTE_SF | GTE_LM); // 30 cycles

		gte_storeDataRegM(GTE_RGB0, &storage[0].color.as_u32);
		gte_storeDataRegM(GTE_RGB1, &storage[1].color.as_u32);
		gte_storeDataRegM(GTE_RGB2, &storage[2].color.as_u32);
		if(v3) {
			gte_setV0((s16) (s8) v3->color.r * (ONE / 128), (s16) (s8) v3->color.g * (ONE / 128), (s16) (s8) v3->color.b * (ONE / 128));
			gte_commandNoNop(GTE_CMD_NCS | GTE_SF | GTE_LM);
			gte_storeDataRegM(GTE_RGB2, &storage[3].color.as_u32);
		}
	} else if(flags & PRIM_FLAG_ENV_COLOR) {
		u32 color = env_color.as_u32 & 0xFFFFFF;
		storage[0].color.as_u32 = color;
		storage[1].color.as_u32 = color;
		storage[2].color.as_u32 = color;
		if(v3) {
			storage[3].color.as_u32 = color;
		}
	} else {
		storage[0].color = v0->color;
		storage[1].color = v1->color;
		storage[2].color = v2->color;
		if(v3) {
			storage[3].color = v3->color;
		}
	}

	bool needs_clip_tessellation = !is_ortho
		&& (gte_flags || depth_straddles_clip)
		&& level < SPARING_TESSELLATION;
	bool needs_vanity_tessellation = !is_ortho
		&& (flags & PRIM_FLAG_TESSELLATE)
		&& min_z <= (MAX_TESSELLATION_Z >> level)
		&& level < VANITY_TESSELLATION;

	if(needs_clip_tessellation || needs_vanity_tessellation) {
		flags &= ~PRIM_FLAG_LIGHTED;
		GfxVtx* const v0 = &storage[0];
		GfxVtx* const v1 = &storage[1];
		GfxVtx* const v2 = &storage[2];
		GfxVtx* const v01 = &storage[V01];
		GfxVtx* const v02 = &storage[V02];
		GfxVtx* const v13 = &storage[V13];
		GfxVtx* const v23 = &storage[V23];
		GfxVtx* const vc = &storage[VC];
		GfxVtx* const next_storage = storage + V_COUNT;
		level++;
		between(v01, v0, v1);
		between(v02, v0, v2);
		if(v3) {
			GfxVtx* const v3 = &storage[3];
			// remember that quads on ps1 are zigzagged!! it's kind of confusing
			between(v13, v1, v3);
			between(v23, v2, v3);
			between(vc, v01, v23);
			/*
				.v0━━━v01━━v1
				.┃    │    ┃
				.v02──vc───v13
				.┃    │    ┃
				.v2━━━v23━━v3
			*/
			draw_poly(v0, v01, v02, vc, flags, next_storage, level);
			draw_poly(v01, v1, vc, v13, flags, next_storage, level);
			draw_poly(v02, vc, v2, v23, flags, next_storage, level);
			draw_poly(vc, v13, v23, v3, flags, next_storage, level);

			#ifdef NO_GAPS
			// prevent T junction gaps
			flags &= ~PRIM_FLAG_TESSELLATE;
			draw_poly(v0, v01, v1, NULL, flags, next_storage, depth);
			draw_poly(v0, v02, v2, NULL, flags, next_storage, depth);
			draw_poly(v2, v23, v3, NULL, flags, next_storage, depth);
			draw_poly(v3, v13, v1, NULL, flags, next_storage, depth);
			#endif
		} else {
			// it is important that the diagonal direction be kept consistent, so no cleverness to store 2 triangles as quads sadly
			between(vc, v1, v2);
			/*
				.v0━v01━v1
				. ┃  ╎ ╱
				.v02╌vc
				. ┃ ╱
				.v2
			*/
			draw_poly(v0, v01, v02, vc, flags, next_storage, level);
			draw_poly(v01, v1, vc, NULL, flags, next_storage, level);
			draw_poly(v02, vc, v2, NULL, flags, next_storage, level);

			#ifdef NO_GAPS
			// prevent T junction gaps
			flags &= ~PRIM_FLAG_TESSELLATE;
			draw_poly(v0, v02, v2, NULL, flags, next_storage, depth);
			draw_poly(v0, v1, v01, NULL, flags, next_storage, depth);
			draw_poly(v1, vc, v2, NULL, flags, next_storage, depth);
			#endif
		}
		return;
	} else if(gte_flags || depth_straddles_clip) {
		// Subdivision budget exhausted and this residual child is still unsafe.
		return;
	}

	u32 ot_z = z / (MAX_Z / Z_BUCKETS) + FOREGROUND_BUCKETS;

	Packet packet = gfx_packet_begin();
	if((flags & PRIM_FLAG_TEXTURED) && (flags & PRIM_FLAG_DECAL)) {
		append_poly(&packet, v3, sxy3, flags & ~PRIM_FLAG_TEXTURED, storage);
	}
	append_poly(&packet, v3, sxy3, flags, storage);
	gfx_packet_end(packet, ot_z);
}

DL_EXEC_ICACHE_FUNC static void set_light_n64(Light_t* n64light, u32 light_idx) {
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

[[gnu::flatten]] static void draw_circle_shadow(s32 radius, u8 opacity) {
	u32 sxy0, sxy1, sxy2, sxy3, sxy4, sxy5;
	s16 v1x = 3547 /* sin(60 degrees) * ONE */ * radius / ONE;
	s16 v1z = 2048 /* cos(60 degrees) * ONE */ * radius / ONE;
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

DL_EXEC_ICACHE_FUNC static void handle_sprite(u32 cmd) {
	s32 x = ((u32) cmd << 20) >> 20;
	s32 y = ((u32) cmd << 8) >> 20;
	u16 z = is_2d_background? BACKGROUND_Z: (foreground_z? --foreground_z: 0);
	Packet packet = gfx_packet_begin();
	TexHeader* tex = tex_ptr;
	gfx_packet_append(&packet, tex->window_cmd);
	gfx_packet_append(&packet, gp0_texpage(tex->page_attr, true, false));
	gfx_packet_append(&packet, env_color.as_u32 << 8 >> 8 | gp0_rectangle(true, true, env_color.a < ALPHA_OPAQUE));
	gfx_packet_append(&packet, gp0_xy(x, y));
	gfx_packet_append(&packet, gp0_uv(tex->offx, tex->offy, tex->clut_attr));
	gfx_packet_append(&packet, gp0_xy(tex->width, tex->height));
	gfx_packet_end(packet, z);
}

[[gnu::noinline]] static void handle_extra_cmd(u8 op, u32 cmd) {
	assert(op >= _DL_CMD_ENUM_FIRST_EXTRA && op <= _DL_CMD_ENUM_END);
	switch(op) {
		case DL_CMD_MTX_MUL: {
			const ShortMatrix* mtx = (const ShortMatrix*) cmd;
			gfx_modelview_mul(mtx);
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
		case DL_CMD_CIRCLE_SHADOW: {
			draw_circle_shadow((s16) (cmd & 0xFFFF), (u8) (cmd >> 16 & 0xFF));
			break;
		}
		case DL_CMD_SQUARE_SHADOW: {
			draw_square_shadow((s16) (cmd & 0xFFFF), (u8) (cmd >> 16 & 0xFF));
			break;
		}
	}
}

static dl_t* call_stack[16];
scratchpad static GfxVtx storage[V_COUNT * SPARING_TESSELLATION + 4];

DL_EXEC_ICACHE_FUNC void gfx_run_compiled_dl(dl_t* dl) {
	u32 call_stack_idx = 0;
	while(true) {
		u32 cmd = *(dl++);
		u8 op = DL_UNPACK_OP(cmd);
		assert(op >= _DL_CMD_ENUM_START && op <= _DL_CMD_ENUM_END);
		cmd &= 0xFFFFFF;
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
				tex_ptr = (dl_t*) cmd;
				break;
			}
			case DL_CMD_VTX: {
				vertices = (GfxVtx*) cmd;
				break;
			}
			case DL_CMD_TRI: case DL_CMD_QUAD: {
				draw_poly(
					&vertices[cmd >> 20],
					&vertices[cmd >> 16 & 0xF],
					&vertices[cmd >> 12 & 0xF],
					op == DL_CMD_QUAD? &vertices[cmd >> 8 & 0xF]: NULL,
					cmd & 0xFF,
					storage,
					0
				);
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
				set_light_n64((Light_t*) cmd, op - DL_CMD_LIGHT_DIRECTIONAL0);
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
				handle_sprite(cmd);
				break;
			}
			default: {
				handle_extra_cmd(op, cmd);
			}
		}
	}
}
