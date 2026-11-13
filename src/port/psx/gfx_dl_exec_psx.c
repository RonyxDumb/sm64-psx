#include "macros.h"
#include "psx_profile.h"
#include "types.h"
#include <assert.h>
#include <port/gfx/gfx.h>
#include <port/gfx/gfx_light_color.h>
#include <port/gfx/gfx_texture_modulation.h>
#include <port/gfx/gfx_psx_lighting_matrix.h>
#include <port/gfx/gfx_gpu_optimizations.h>
#include <ps1/registers.h>
#include <port/gfx/gfx_internal.h>
#include <ps1/gpucmd.h>
#include <ps1/gte.h>
#include <engine/math_util.h>
#include <game/game_init.h>
#include <game/memory.h>

//#define NO_GAPS // fills in T-junction gaps with more triangles (UNFINISHED - add some condition)
#define MAX_TESSELLATION_Z 800
#define VANITY_TESSELLATION 2 // applies to polygons near the camera
#define CLIP_GTE_ERRORS (IMPORTANT_GTE_ERRORS | GTE_FLAG_SX2_SATURATED | GTE_FLAG_SY2_SATURATED)

// check the .map file and ensure the section is smaller than 0x1000 so it actually fits in icache!!!
#define DL_EXEC_ICACHE_FUNC [[gnu::section(".dl_exec")]] [[gnu::noinline]] [[gnu::flatten]]

u32 debug_processed_poly_count;
scratchpad void* tex_ptr;
scratchpad static GfxVtx* vertices;
scratchpad static Color env_color;
scratchpad static bool is_ortho;
static bool is_2d_background;
static bool texture_gen_enabled;
static bool native_uv;

void gfx_reset_dl_exec() {
	tex_ptr = NULL;
	is_2d_background = true;
	texture_gen_enabled = false;
	native_uv = false;
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
[[gnu::noinline]] static u8 between_uv(u8 a, u8 b) {
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

[[gnu::noinline]] static void between(GfxVtx* dst, const GfxVtx* a, const GfxVtx* b) {
	// note: since both vertices are definitely on the scratchpad now, loads are fast here!
	// note: the GTE has instructions for interpolation, but setting them up isn't fast enough
	dst->x = ((s32) a->x + b->x) / 2;
	dst->y = ((s32) a->y + b->y) / 2;
	dst->z = ((s32) a->z + b->z) / 2;
	// Native batches already share a continuous UV interval. Wrapping
	// their midpoint through 256 would undo the build-time UV correction.
	dst->u = native_uv ? ((u32) a->u + b->u) / 2 : between_uv(a->u, b->u);
	dst->v = native_uv ? ((u32) a->v + b->v) / 2 : between_uv(a->v, b->v);
	dst->color.as_u32 = ((a->color.as_u32 & 0xFEFEFE) + (b->color.as_u32 & 0xFEFEFE)) / 2;
}

/* The N64->PS1 RGB modulation conversion is shared by every textured
 * polygon; the title-only geometry bit is still used for depth ordering. */
[[gnu::noinline]] [[gnu::flatten]] static void append_poly(Packet* packet, bool is_quad, s32 sxy3, u32 flags, const GfxVtx* vtx) {
	TexHeader* tex = (TexHeader*) tex_ptr;
	bool textured = tex && (flags & PRIM_FLAG_TEXTURED);
	/* N64 G_CC_BLENDRGBFADEA decals use a neutral PS1 overlay.
	 * Compare the *final* GP0 colors after the N64->PS1 conversion:
	 * flat triangles/quads need only one RGB word and have lower setup
	 * cost on the PS1 GPU. This preserves every per-vertex gradient. */
	bool decal_overlay = textured && (flags & PRIM_FLAG_DECAL);
	u32 c0 = gfx_psx_material_color(vtx[0].color.as_u32, textured, decal_overlay);
	u32 c1 = gfx_psx_material_color(vtx[1].color.as_u32, textured, decal_overlay);
	u32 c2 = gfx_psx_material_color(vtx[2].color.as_u32, textured, decal_overlay);
	u32 c3 = is_quad ? gfx_psx_material_color(vtx[3].color.as_u32, textured, decal_overlay) : c0;
	bool gouraud = gfx_psx_colors_vary(c0, c1, c2, c3, is_quad);
	if(textured) {
		gfx_packet_append(packet, tex->window_cmd);
	} else if(flags & PRIM_FLAG_FORCE_BLEND) {
		gfx_packet_append(packet, gp0_texpage(gp0_page(0, 0, GP0_BLEND_SEMITRANS, 0), true, false));
	}
	gfx_packet_append(packet, c0 | _gp0_polygon(is_quad, false, gouraud, textured, flags & PRIM_FLAG_FORCE_BLEND));
	gte_storeDataRegM(GTE_SXY0, gfx_packet_skip(packet));
	if(textured) {
		gfx_packet_append(packet, vtx[0].uv | (u32) tex->clut_attr << 16);
	}
	if(gouraud) gfx_packet_append(packet, c1);
	gte_storeDataRegM(GTE_SXY1, gfx_packet_skip(packet));
	if(textured) {
		gfx_packet_append(packet, vtx[1].uv | (u32) tex->page_attr << 16);
	}
	if(gouraud) gfx_packet_append(packet, c2);
	gte_storeDataRegM(GTE_SXY2, gfx_packet_skip(packet));
	if(textured) {
		gfx_packet_append(packet, vtx[2].uv);
	}
	if(is_quad) {
		if(gouraud) gfx_packet_append(packet, c3);
		gfx_packet_append(packet, sxy3);
		if(textured) {
			gfx_packet_append(packet, vtx[3].uv);
		}
	}
}

static u32 texture_window_period(u32 size) {
	u32 period = 8;
	while(period < size) period *= 2;
	return period;
}

/* Legacy (non-native) vertices store each UV wrapped to 8 bits on its own.
 * A polygon whose texels cross a multiple of 256 then interpolates backwards
 * across the whole range (e.g. 250 -> 4), squeezing several texture repeats
 * into it: the "streaky" castle walls and hills. Rebuild the polygon's real
 * UV interval along the shortest wrap (as between_uv()/clip_lerp_uv() do),
 * then shift it by a multiple of the texture-window period so it starts in
 * [0, period). The texture window keeps the pattern phase. Valid while a
 * polygon spans under 128 texels, which offline tessellation ensures. */
[[gnu::noinline]] static const GfxVtx* normalize_legacy_uvs(GfxVtx* v, const GfxVtx* src, u32 count, const TexHeader* tex) {
	for(u32 i = 0; i < count; i++) v[i] = src[i];
	for(u32 axis = 0; axis < 2; axis++) {
		s32 raw_lo = 255, raw_hi = 0;
		for(u32 i = 0; i < count; i++) {
			s32 value = v[i].uv_arr[axis];
			if(value < raw_lo) raw_lo = value;
			if(value > raw_hi) raw_hi = value;
		}
		// A raw span up to 128 is already correct (this also keeps clamped
		// 0..128 coordinates of 128-texel textures untouched).
		if(raw_hi - raw_lo <= 128) continue;
		s32 period = texture_window_period(axis ? tex->height : tex->width);
		s32 base = v[0].uv_arr[axis];
		s32 unwrapped[4];
		s32 lo = base;
		for(u32 i = 0; i < count; i++) {
			s32 value = base + (s8) (u8) (v[i].uv_arr[axis] - base);
			unwrapped[i] = value;
			if(value < lo) lo = value;
		}
		s32 shift = lo & ~(period - 1); // floor to a whole period, also below zero
		for(u32 i = 0; i < count; i++) {
			v[i].uv_arr[axis] = (u8) (unwrapped[i] - shift);
		}
	}
	return v;
}

/* A decal is an untextured base plus a textured overlay (up to 21 GP0
 * words). Linked-list DMA only checks GPU readiness per node, and the GPU
 * command FIFO holds 16 words: one combined node overflowed it, dropped
 * words and left the GPU waiting forever in the middle of a quad (hang when
 * Mario's face appears). Emit one node per pass. Each OT bucket is drawn
 * newest-first, except the UI bucket which keeps submission order, so link
 * the base accordingly. Kept out of the icache-resident section. */
[[gnu::noinline]] static void emit_decal_nodes(bool quad, s32 sxy3, u32 flags, const GfxVtx* vertices, u32 ot_z, bool ui) {
	u32 base = flags & ~PRIM_FLAG_TEXTURED;
	bool in_order = ot_z == UI_Z;
	for(u32 pass = 0; pass < 2; pass++) {
		Packet packet = gfx_packet_begin_for(ui);
		append_poly(&packet, quad, sxy3, (pass == 0) == in_order ? base : flags, vertices);
		gfx_packet_end(packet, ot_z);
	}
}

DL_EXEC_ICACHE_FUNC static void emit_poly_packet(bool quad, s32 sxy3, u32 flags, const GfxVtx* vertices, u32 depth) {
	PROF_COUNT(PROF_N_PACKETS);
	PROF_BEGIN(emit);
	GfxVtx normalized[4];
	if(!native_uv && tex_ptr && (flags & PRIM_FLAG_TEXTURED)) {
		vertices = normalize_legacy_uvs(normalized, vertices, quad ? 4 : 3, (const TexHeader*) tex_ptr);
	}
	u32 ot_z = is_ortho ? depth : depth / (MAX_Z / Z_BUCKETS) + FOREGROUND_BUCKETS;
	if((flags & PRIM_FLAG_DECAL) && ot_z > FOREGROUND_BUCKETS + DECAL_Z_BIAS) {
		ot_z -= DECAL_Z_BIAS;
	}
	bool ui = is_ortho && !is_2d_background;
	if((flags & PRIM_FLAG_TEXTURED) && (flags & PRIM_FLAG_DECAL)) {
		emit_decal_nodes(quad, sxy3, flags, vertices, ot_z, ui);
		PROF_END(emit, PROF_EMIT);
		return;
	}
	Packet packet = gfx_packet_begin_for(ui);
	append_poly(&packet, quad, sxy3, flags, vertices);
	gfx_packet_end(packet, ot_z);
	PROF_END(emit, PROF_EMIT);
}

typedef struct {
	s32 x, y, z;
	Color color;
	u8 u, v;
	/* GTE screen position of an original (unclipped) vertex. Shared edges
	 * must project bit-identically in neighbouring polygons, otherwise the
	 * rasterizer leaves single-pixel cracks between a clipped polygon and
	 * an unclipped one. Intersection vertices have gte_sxy_valid clear. */
	bool gte_sxy_valid;
	s32 gte_sxy;
} ClipVertex;

enum { CLIP_NEAR, CLIP_FAR, CLIP_LEFT, CLIP_RIGHT, CLIP_TOP, CLIP_BOTTOM, CLIP_PLANE_COUNT };

// A convex triangle gains at most one vertex per clipping plane.
// Keep this workspace off the small scratchpad stack.
static ClipVertex clip_vertices[2][3 + CLIP_PLANE_COUNT];

/* Side planes form a guard band instead of the screen edges: the GPU already
 * clips rasterization to the drawing area, so cutting at the exact edges only
 * cost CPU time. The band keeps clipped polygons inside the GPU primitive
 * limits (width <= 1023, height <= 511 pixels). */
#define CLIP_GUARD_X 340 // x in [-340, 660)
#define CLIP_GUARD_Y 130 // y in [-130, 370)

static s64 clip_distance(const ClipVertex* vertex, u32 plane, s32 focal, s32 near_z) {
	switch(plane) {
		case CLIP_NEAR: return vertex->z - near_z;
		case CLIP_FAR: return (MAX_Z - 1) - vertex->z;
		case CLIP_LEFT: return (s64) vertex->x * focal + (s64) vertex->z * (XRES / 2 + CLIP_GUARD_X);
		case CLIP_RIGHT: return -(s64) vertex->x * focal + (s64) vertex->z * (XRES / 2 + CLIP_GUARD_X);
		case CLIP_TOP: return (s64) vertex->y * focal + (s64) vertex->z * (YRES / 2 + CLIP_GUARD_Y);
		default: return -(s64) vertex->y * focal + (s64) vertex->z * (YRES / 2 + CLIP_GUARD_Y);
	}
}

static s32 clip_lerp(s32 a, s32 b, s32 fraction) {
	return a + (((s64) b - a) * fraction >> 16);
}

static u8 clip_lerp_uv(u8 a, u8 b, s32 fraction) {
	s32 end = b;
	if(!native_uv) {
		if(end - a > 127) end -= 256;
		if(end - a < -128) end += 256;
	}
	return (u8) clip_lerp(a, end, fraction);
}

static ClipVertex clip_intersection(const ClipVertex* outside, const ClipVertex* inside,
                                   s64 outside_distance, s64 inside_distance) {
	// Interpolate from the same end of a shared edge in both triangles.
	// 0.16 fraction = -out / (in - out). The R3000 has no 64-bit divide and
	// __divdi3 cost hundreds of cycles per call (the clipper averaged ~6000
	// cycles per polygon). Scale both terms down to 15 bits, then use the
	// 32-bit DIVU: deterministic, so neighbours still get identical points.
	u64 num = (u64) -outside_distance;
	u64 den = (u64) (inside_distance - outside_distance);
	while(den > 0x7FFF) {
		num >>= 1;
		den >>= 1;
	}
	s32 fraction = den ? (s32) (((u32) num << 16) / (u32) den) : 0;
	return (ClipVertex) {
		.x = clip_lerp(outside->x, inside->x, fraction),
		.y = clip_lerp(outside->y, inside->y, fraction),
		.z = clip_lerp(outside->z, inside->z, fraction),
		.color = {
			.r = clip_lerp(outside->color.r, inside->color.r, fraction),
			.g = clip_lerp(outside->color.g, inside->color.g, fraction),
			.b = clip_lerp(outside->color.b, inside->color.b, fraction),
		},
		.u = clip_lerp_uv(outside->u, inside->u, fraction),
		.v = clip_lerp_uv(outside->v, inside->v, fraction),
		.gte_sxy_valid = false,
	};
}

static ClipVertex clip_load_vertex(const GfxVtx* position, const GfxVtx* material) {
	gte_setV0(position->x, position->y, position->z);
	gte_commandNoNop(GTE_CMD_MVMVA | GTE_SF | GTE_V_V0 | GTE_MX_RT | GTE_CV_TR);
	s32 x = (s32) gte_getDataReg(GTE_MAC1);
	s32 y = (s32) gte_getDataReg(GTE_MAC2);
	s32 z = (s32) gte_getDataReg(GTE_MAC3);
	// Same projection as RTPT in draw_poly(); only used if the vertex
	// survives clipping, i.e. it lies inside every plane.
	gte_commandNoNop(GTE_CMD_RTPS | GTE_SF);
	return (ClipVertex) {
		.x = x,
		.y = y,
		.z = z,
		.color = material->color,
		.u = material->u,
		.v = material->v,
		.gte_sxy_valid = true,
		.gte_sxy = (s32) gte_getDataReg(GTE_SXY2),
	};
}

static s32 clip_project(s32 coord, s32 z, s32 focal, s32 half_size, s32 guard) {
	s32 screen;
	if(coord > -(1 << 21) && coord < (1 << 21)) {
		// |coord * focal| < 2^31 for focal < 1024: use the 32-bit divider.
		screen = half_size + coord * focal / z;
	} else {
		screen = half_size + (s64) coord * focal / z;
	}
	// Rounding can push a point on a guard plane one pixel past it.
	if(screen < -guard) return -guard;
	if(screen > half_size * 2 + guard) return half_size * 2 + guard;
	return screen;
}

[[gnu::noinline]] static void clip_and_draw_triangle(const GfxVtx* a, const GfxVtx* b, const GfxVtx* c,
                                                    const GfxVtx* ma, const GfxVtx* mb, const GfxVtx* mc,
                                                    u32 flags, s32 focal) {
	ClipVertex* input = clip_vertices[0];
	ClipVertex* output = clip_vertices[1];
	input[0] = clip_load_vertex(a, ma);
	input[1] = clip_load_vertex(b, mb);
	input[2] = clip_load_vertex(c, mc);
	u32 count = 3;
	s32 near_z = focal / 2 + 1;

	// Only planes that some corner lies outside of can cut the triangle:
	// intersection points of other planes' edges stay inside the rest.
	u32 crossed = 0;
	for(u32 plane = 0; plane < CLIP_PLANE_COUNT; plane++) {
		for(u32 i = 0; i < 3; i++) {
			if(clip_distance(&input[i], plane, focal, near_z) < 0) {
				crossed |= 1 << plane;
				break;
			}
		}
	}

	for(u32 plane = 0; plane < CLIP_PLANE_COUNT; plane++) {
		if(!(crossed & (1 << plane))) continue;
		u32 written = 0;
		const ClipVertex* prev = &input[count - 1];
		s64 prev_distance = clip_distance(prev, plane, focal, near_z);
		for(u32 i = 0; i < count; i++) {
			const ClipVertex* current = &input[i];
			s64 distance = clip_distance(current, plane, focal, near_z);
			if(prev_distance < 0 && distance > 0) {
				output[written++] = clip_intersection(prev, current, prev_distance, distance);
			} else if(prev_distance > 0 && distance < 0) {
				output[written++] = clip_intersection(current, prev, distance, prev_distance);
			}
			if(distance >= 0) output[written++] = *current;
			prev = current;
			prev_distance = distance;
		}
		if(written < 3) return;
		count = written;
		ClipVertex* swap = input;
		input = output;
		output = swap;
	}

	s32 xy[3 + CLIP_PLANE_COUNT];
	for(u32 i = 0; i < count; i++) {
		// Rounding the fixed-point intersection can move Z by one unit.
		if(input[i].gte_sxy_valid) {
			xy[i] = input[i].gte_sxy;
			continue;
		}
		s32 z = input[i].z < near_z ? near_z : input[i].z;
		xy[i] = gp0_xy(clip_project(input[i].x, z, focal, XRES / 2, CLIP_GUARD_X),
		               clip_project(input[i].y, z, focal, YRES / 2, CLIP_GUARD_Y));
	}
	for(u32 i = 1; i + 1 < count; i++) {
		gte_setDataReg(GTE_SXY0, xy[0]);
		gte_setDataReg(GTE_SXY1, xy[i]);
		gte_setDataReg(GTE_SXY2, xy[i + 1]);
		gte_commandNoNop(GTE_CMD_NCLIP);
		s32 area = (s32) gte_getDataReg(GTE_MAC0);
		if(area == 0 || (area > 0 && !(flags & PRIM_FLAG_TWO_SIDED))) continue;

		const ClipVertex* corners[3] = {&input[0], &input[i], &input[i + 1]};
		GfxVtx material[3];
		u32 depth = 0;
		u32 depth_sum = 0;
		for(u32 corner = 0; corner < 3; corner++) {
			const ClipVertex* vertex = corners[corner];
			material[corner].u = vertex->u;
			material[corner].v = vertex->v;
			material[corner].color = vertex->color;
			u32 z = vertex->z < MAX_Z ? vertex->z : MAX_Z - 1;
			if(z > depth) depth = z;
			depth_sum += z;
		}
		if(flags & PRIM_FLAG_TITLE_LOGO) depth = depth_sum / 3;
		emit_poly_packet(false, 0, flags, material, depth);
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

/* PS1 has no N64 reflection mapper. Derive a stable, per-vertex metal
 * lookup from the original model normals; generated coordinates must be
 * calculated at draw time because normal and metal Mario share vertex lists. */
[[gnu::noinline]] static void set_reflection_uv(GfxVtx* dst, const GfxVtx* src, const TexHeader* tex) {
	int nx = (s8) src->color.r + 128;
	int ny = (s8) src->color.g + 128;
	int u = nx * ((tex->rotated ? tex->height : tex->width) - 1) / 255;
	int v = ny * ((tex->rotated ? tex->width : tex->height) - 1) / 255;
	dst->u = tex->rotated ? v : u;
	dst->v = tex->rotated ? u : v;
}

static void draw_poly(const GfxVtx* v0, const GfxVtx* v1, const GfxVtx* v2, const GfxVtx* v3, u32 flags, GfxVtx* storage, u32 level);

[[gnu::noinline]] static void tessellate_poly(bool quad, u32 flags, GfxVtx* storage, u32 level) {
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
	if(quad) {
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
	}
}

[[gnu::noinline]] static s32 project_ortho(const GfxVtx* v3, GfxVtx* storage) {
	gte_setControlReg(GTE_RT31RT32, 0);
	gte_setControlReg(GTE_RT33, 0);
	gte_commandNoNop(GTE_CMD_MVMVA | GTE_SF | GTE_V_V0 | GTE_MX_RT | GTE_CV_TR);
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
		return (gte_getDataReg(GTE_IR1) & 0xFFFF) | gte_getDataReg(GTE_IR2) << 16;
	}
	return 0;
}

/* Crack-free near-camera tessellation for triangles. The old rule split a
 * whole polygon when *it* was large and near, so a split edge often met an
 * unsplit neighbour: the projected, rounded midpoint is not exactly on the
 * neighbour's edge and the sky showed through as a dotted line (castle
 * grounds sand). Here every edge decides on its own from its two vertices
 * only (model-space length and GTE depth), so both triangles sharing an
 * edge always agree and create the same midpoint; each triangle is then
 * cut into 1-4 pieces according to its split edges. */
#define TESSELLATION_EDGE 384

static const u8 edge_split_tris[8][4][3] = {
	/* corners: 0=v0 1=m01 2=v1 3=m12 4=v2 5=m20; listed in winding order */
	[1] = {{0, 1, 4}, {1, 2, 4}},
	[2] = {{0, 2, 3}, {0, 3, 4}},
	[3] = {{1, 2, 3}, {0, 1, 3}, {0, 3, 4}},
	[4] = {{0, 2, 5}, {5, 2, 4}},
	[5] = {{0, 1, 5}, {1, 2, 4}, {1, 4, 5}},
	[6] = {{5, 3, 4}, {0, 2, 3}, {0, 3, 5}},
	[7] = {{0, 1, 5}, {1, 2, 3}, {5, 3, 4}, {1, 3, 5}},
};
static const u8 edge_split_tri_count[8] = {0, 2, 2, 3, 2, 3, 3, 4};

[[gnu::noinline]] static bool tessellate_triangle_edges(u32 flags, GfxVtx* storage, u32 level) {
	// SZ1..SZ3 still hold this triangle's RTPT depths (lighting does not touch them).
	s32 sz[3] = {
		(s32) gte_getDataReg(GTE_SZ1), (s32) gte_getDataReg(GTE_SZ2), (s32) gte_getDataReg(GTE_SZ3)
	};
	s32 max_z = MAX_TESSELLATION_Z >> level;
	u32 min_len = TESSELLATION_EDGE >> level;
	u32 split = 0;
	GfxVtx* corner[6] = {&storage[0], &storage[V01], &storage[1], &storage[V02], &storage[2], &storage[V13]};
	for(u32 e = 0; e < 3; e++) {
		GfxVtx* a = corner[e * 2];
		GfxVtx* b = corner[(e * 2 + 2) % 6];
		if((sz[e] < sz[(e + 1) % 3] ? sz[e] : sz[(e + 1) % 3]) > max_z) continue;
		s32 dx = a->x - b->x, dy = a->y - b->y, dz = a->z - b->z;
		if(dx < 0) dx = -dx;
		if(dy < 0) dy = -dy;
		if(dz < 0) dz = -dz;
		bool long_edge = dx > 16384 || dy > 16384 || dz > 16384
			|| (u32) (dx * dx) + (u32) (dy * dy) + (u32) (dz * dz) > min_len * min_len;
		if(!long_edge) continue;
		// Same operand order on both sides of a shared edge.
		if(a->xy > b->xy || (a->xy == b->xy && a->z > b->z)) {
			GfxVtx* t = a; a = b; b = t;
		}
		between(corner[e * 2 + 1], a, b);
		split |= 1 << e;
	}
	if(!split) return false;
	flags &= ~PRIM_FLAG_LIGHTED; // colours are already lit
	for(u32 i = 0; i < edge_split_tri_count[split]; i++) {
		const u8* t = edge_split_tris[split][i];
		draw_poly(corner[t[0]], corner[t[1]], corner[t[2]], NULL, flags, storage + V_COUNT, level + 1);
	}
	return true;
}

// Quads (rare in level meshes) keep whole-polygon subdivision.
[[gnu::noinline]] static bool tessellate_near(bool quad, u32 flags, GfxVtx* storage, u32 level) {
	PROF_COUNT(PROF_N_TESSELLATED); // counts attempts; split ones re-enter draw_poly
	if(!quad) return tessellate_triangle_edges(flags, storage, level);
	if(!(flags & PRIM_FLAG_TESSELLATE)) return false;
	tessellate_poly(true, flags, storage, level);
	return true;
}

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

	/* Apply inherited metal texturing even to shared geometry that was
	 * originally compiled in the normal/untextured Mario material. */
	if(level == 0 && texture_gen_enabled && tex_ptr) {
		const TexHeader* tex = (const TexHeader*) tex_ptr;
		flags |= PRIM_FLAG_TEXTURED;
		set_reflection_uv(&storage[0], v0, tex);
		set_reflection_uv(&storage[1], v1, tex);
		set_reflection_uv(&storage[2], v2, tex);
		if(v3) {
			/* The fourth vertex is copied below after projection. */
			set_reflection_uv(&storage[3], v3, tex);
		}
	}

#ifdef PRIM_FLAG_ENV_ALPHA
	if((flags & PRIM_FLAG_ENV_ALPHA) && env_alpha.cmd < ALPHA_OPAQUE) {
#else
	if((flags & PRIM_FLAG_ENV_COLOR) && env_color.a < ALPHA_OPAQUE) {
#endif
		if(env_color.a < ALPHA_TRANSLUCENT) {
			return;
		}
		flags |= PRIM_FLAG_FORCE_BLEND;
	} else if((flags & PRIM_FLAG_TEXTURED) && tex_ptr && ((TexHeader*) tex_ptr)->has_translucency) {
		flags |= PRIM_FLAG_FORCE_BLEND;
	}

	s32 sxy3 = 0;
	s32 z, min_z;
	u32 gte_flags;
	u32 depth_sum = 0;
	if(is_ortho) {
		z = is_2d_background ? BACKGROUND_Z : UI_Z;
		min_z = z;
		gte_flags = 0;
		sxy3 = project_ortho(v3, storage);
		debug_processed_poly_count++;
		PROF_COUNT(PROF_N_POLYS);
	} else {
		// RTPT transforms and projects all 3 vertices in a mere 23 cycles. based GTE :)
		gte_commandNoNop(GTE_CMD_RTPT | GTE_SF);

		gte_storeDataRegM(GTE_VXY0, (u32*) &storage[0].xy);
		gte_storeDataRegM(GTE_VXY1, (u32*) &storage[1].xy);
		gte_storeDataRegM(GTE_VXY2, (u32*) &storage[2].xy);

		debug_processed_poly_count++;
		PROF_COUNT(PROF_N_POLYS);
		is_2d_background = false;

		// if there was any error in rtpt, cull it
		gte_flags = gte_getControlReg(GTE_FLAG) & CLIP_GTE_ERRORS;

		// prepare to reject backfaces
		gte_commandNoNop(GTE_CMD_NCLIP);

		// sort z in the meantime
		z = gte_getDataReg(GTE_SZ1);
		s32 v1sz = gte_getDataReg(GTE_SZ2);
		s32 v2sz = gte_getDataReg(GTE_SZ3);
		depth_sum = (u32) (z + v1sz + v2sz);
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
		if(triangle_backfaced && !v3 && !gte_flags && depth_is_safe_for_cull
			&& !(flags & PRIM_FLAG_TWO_SIDED)) return;

		if(v3) {
			// if this is a quad, quickly transform the extra vertex with rtps
			u32 zuv3 = v3->zuv;
			gte_loadDataRegM(GTE_VXY0, (const u32*) &v3->xy);
			gte_setDataReg(GTE_VZ0, zuv3);
			gte_commandNoNop(GTE_CMD_RTPS | GTE_SF);

			/* Preserve the metal lookup UV computed above instead of
			 * replacing it with the original vertex's untextured UV. */
			if(!(level == 0 && texture_gen_enabled && tex_ptr)) {
				storage[3].zuv = zuv3;
			} else {
				storage[3].z = v3->z;
			}
			gte_storeDataRegM(GTE_VXY0, (u32*) &storage[3].xy);

			// if there was any error in rtps, cull it
			gte_flags |= gte_getControlReg(GTE_FLAG) & CLIP_GTE_ERRORS;

			// prepare to reject backfaces
			gte_commandNoNop(GTE_CMD_NCLIP);

			// get the result
			sxy3 = gte_getDataReg(GTE_SXY2);

			bool quad_second_triangle_backfaced = (s32) gte_getDataReg(GTE_MAC0) <= 0;

			gte_setDataReg(GTE_SXY2, gte_getDataReg(GTE_SXY1));
			gte_setDataReg(GTE_SXY1, gte_getDataReg(GTE_SXY0));
			gte_setDataReg(GTE_SXY0, sxy0);

			s32 v3sz = gte_getDataReg(GTE_SZ3);
			depth_sum += v3sz;
			if(v3sz > z) {
				z = v3sz;
			} else if(v3sz < min_z) {
				min_z = v3sz;
			}

			// Reject only when all four depths are safely inside the clip range.
			// The clipper handles polygons crossing the view frustum.
			if(triangle_backfaced && quad_second_triangle_backfaced && !gte_flags
				&& !(flags & PRIM_FLAG_TWO_SIDED)) {
				s32 near_z_for_quad_cull = (s16) gte_getControlReg(GTE_H) / 2;
				if(min_z > near_z_for_quad_cull && z < MAX_Z) return;
			}
		}
	}

	bool depth_straddles_clip = false;
	if(!is_ortho) {
		s32 near_z = (s16) gte_getControlReg(GTE_H) / 2;
		// Reject only when the entire primitive is outside. If it crosses the
		// near/far plane, clip it against the view frustum.
		if(z <= near_z || min_z >= MAX_Z) return;
		depth_straddles_clip = min_z <= near_z || z >= MAX_Z;
	}

	/* Screen-space clipping is conservative: reject only primitives whose
	 * entire projected hull lies beyond one viewport edge. Never use it
	 * when the GTE reports saturation or a near/far-plane crossing;
	 * those must continue through the clipper. */
	if(!is_ortho && !gte_flags && !depth_straddles_clip
		&& gfx_psx_outside_viewport(
			gte_getDataReg(GTE_SXY0), gte_getDataReg(GTE_SXY1),
			gte_getDataReg(GTE_SXY2), (u32) sxy3,
			v3 != NULL, XRES, YRES)) {
		return;
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

	if(!is_ortho && (gte_flags || depth_straddles_clip)) {
		s32 focal = gte_getControlReg(GTE_H) & 0xFFFF;
		PROF_COUNT(PROF_N_CLIPPED);
		PROF_BEGIN(clip);
		clip_and_draw_triangle(v0, v1, v2, &storage[0], &storage[1], &storage[2], flags, focal);
		if(v3) {
			clip_and_draw_triangle(v1, v3, v2, &storage[1], &storage[3], &storage[2], flags, focal);
		}
		PROF_END(clip, PROF_CLIP);
		return;
	}

	if(!is_ortho && level < VANITY_TESSELLATION && min_z <= (MAX_TESSELLATION_Z >> level)
		&& tessellate_near(v3 != NULL, flags, storage, level)) {
		return;
	}

	/* The PS1 has no depth buffer. For the interlocking title letters
	 * (notably the M crossing the A), ordering by the farthest vertex
	 * paints long side faces in front of nearer faces. Sort title polygons
	 * by their mean camera-space depth instead; keep the world renderer's
	 * established conservative farthest-depth ordering unchanged. */
	u32 sort_z = z;
	if(!is_ortho && (flags & PRIM_FLAG_TITLE_LOGO)) {
		sort_z = depth_sum / (v3 ? 4 : 3);
	}
	emit_poly_packet(v3 != NULL, sxy3, flags, storage, sort_z);
}

[[gnu::noinline]] static void set_light(u32 normals, u32 rgb, u32 light_idx) {
    if(light_idx > 1) return;

    /* Decode every signed normal component separately: the former left
     * shifts of signed packed bytes could also corrupt negative directions. */
    gte_setV0((s8) (normals & 0xff), (s8) ((normals >> 8) & 0xff),
              (s8) ((normals >> 16) & 0xff));
    gte_commandNoNop(GTE_CMD_MVMVA | GTE_SF | GTE_V_V0 | GTE_MX_RT | GTE_CV_NONE);

    s32 nx = (s16) gte_getDataReg(GTE_IR1);
    s32 ny = (s16) gte_getDataReg(GTE_IR2);
    s32 nz = (s16) gte_getDataReg(GTE_IR3);
    gte_commandNoNop(GTE_CMD_SQR);
    u32 normal_len_sq = gte_getDataReg(GTE_MAC1) + gte_getDataReg(GTE_MAC2) + gte_getDataReg(GTE_MAC3);
    if(normal_len_sq > 1) {
        u32 normal_len = sqrtu(normal_len_sq);
        if(normal_len != 0) {
            s32 div = ONE * 128 / normal_len;
            nx = nx * div / 128;
            ny = ny * div / 128;
            nz = nz * div / 128;
        }
    }

    /* NCT multiplies LLM * vertex_normal, then LCM * light_intensity.
     * Direction vectors therefore belong in LLM rows, whereas their
     * RGB contributions belong in LCM columns (not matching columns). */
    GfxPsxLightingMatrices m = {0};
    m.light[1] = gte_getControlReg(GTE_L13L21);
    m.color[0] = gte_getControlReg(GTE_LC11LC12);
    m.color[3] = gte_getControlReg(GTE_LC31LC32);
    if(light_idx == 0) {
        m.color[1] = gte_getControlReg(GTE_LC13LC21);
    } else {
        m.color[2] = gte_getControlReg(GTE_LC22LC23);
    }
    gfx_psx_set_directional_light_matrix(&m, light_idx, nx, ny, nz, rgb);

    if(light_idx == 0) {
        gte_setControlReg(GTE_L11L12, m.light[0]);
        gte_setControlReg(GTE_L13L21, m.light[1]);
        gte_setControlReg(GTE_LC11LC12, m.color[0]);
        gte_setControlReg(GTE_LC13LC21, m.color[1]);
        gte_setControlReg(GTE_LC31LC32, m.color[3]);
    } else {
        gte_setControlReg(GTE_L13L21, m.light[1]);
        gte_setControlReg(GTE_L22L23, m.light[2]);
        gte_setControlReg(GTE_LC11LC12, m.color[0]);
        gte_setControlReg(GTE_LC22LC23, m.color[2]);
        gte_setControlReg(GTE_LC31LC32, m.color[3]);
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

[[gnu::noinline]] static void handle_sprite(u32 cmd) {
	s32 x = ((u32) cmd << 20) >> 20;
	s32 y = ((u32) cmd << 8) >> 20;
	u16 z = is_2d_background ? BACKGROUND_Z : UI_Z;
	Packet packet = !is_2d_background ? gfx_packet_begin_ui() : gfx_packet_begin();
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
scratchpad static GfxVtx storage[V_COUNT * VANITY_TESSELLATION + 4];

DL_EXEC_ICACHE_FUNC void gfx_run_compiled_dl(dl_t* dl) {
	u32 call_stack_idx = 0;
	u8 native_flags = 0;
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
				// A malformed or cyclic display list must not overwrite the call stack.
				if(call_stack_idx >= sizeof(call_stack) / sizeof(call_stack[0])) {
					return;
				}
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
			case DL_CMD_STATIC_MESH: {
				dl += cmd & DL_STATIC_MESH_TEXTURE_COUNT;
				break;
			}
			case DL_CMD_SEG_TEX: {
				tex_ptr = segmented_to_virtual((void*) *dl++);
				break;
			}
			case DL_CMD_VTX: {
				vertices = (GfxVtx*) cmd;
				native_uv = false;
				break;
			}
			case DL_CMD_SEG_VTX: {
				vertices = segmented_to_virtual((void*) *dl++);
				native_uv = true;
				native_flags = cmd;
				break;
			}
			case DL_CMD_STATIC_TRI: {
				draw_poly(&vertices[cmd >> 16], &vertices[cmd >> 8 & 0xFF],
				          &vertices[cmd & 0xFF], NULL, native_flags, storage, 0);
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
				static const u8 alpha[] = {0, 128, 192, 255};
				env_color.as_u32 = cmd | (u32) alpha[op - DL_CMD_ENV_COLOR_ALPHA_0] << 24;
				break;
			}
			case DL_CMD_LIGHT_AMBIENT: {
				u32 rgb = gfx_light_decode_ambient(cmd);
				gte_setControlReg(GTE_RBK, (rgb & 0xFF) * (ONE / 256));
				gte_setControlReg(GTE_GBK, (rgb >> 8 & 0xFF) * (ONE / 256));
				gte_setControlReg(GTE_BBK, (rgb >> 16 & 0xFF) * (ONE / 256));
				break;
			}
			case DL_CMD_LIGHT_DIRECTIONAL0:
			case DL_CMD_LIGHT_DIRECTIONAL1: {
				const Light_t* light = (const Light_t*) cmd;
				u32 rgb = gfx_light_quantize_directional(light->col[0] | (u32) light->col[1] << 8 | (u32) light->col[2] << 16);
				set_light(*(const u32*) light->dir, rgb, op - DL_CMD_LIGHT_DIRECTIONAL0);
				break;
			}
			case DL_CMD_STATIC_LIGHT0:
			case DL_CMD_STATIC_LIGHT1: {
				set_light(*dl++, gfx_light_decode_ambient(cmd), op - DL_CMD_STATIC_LIGHT0);
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
			case DL_CMD_TEXTURE_GEN_ON: {
				texture_gen_enabled = true;
				break;
			}
			case DL_CMD_TEXTURE_GEN_OFF: {
				texture_gen_enabled = false;
				break;
			}
			default: {
				handle_extra_cmd(op, cmd);
			}
		}
	}
}
