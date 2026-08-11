#if 0
#include "port/gfx/gfx.h"
#include <port/gfx/gfx_internal.h>
#include <macros.h>
#include <ps1/gpucmd.h>
#include <ps1/gte.h>
#include <stdio.h>
#include <engine/math_util.h>

// check the .map file and ensure the section is smaller than 0x1000 so it actually fits in icache!!!
//#define TESSELLATION_ICACHE_FUNC [[gnu::section(".tessellation")]] [[gnu::noinline]]

#define NO_GAPS
//#define NO_GAPS_AT_ALL
//#define DEBUG_DEPTH

typedef union {
	struct {
		u32 flags: 8;
		u32 is_quad: 1;
		u32 tex_ptr: 23;
	};
	u32 as_u32;
} PackedFlagsAndTexPtr;

STATIC_ASSERT(sizeof(PackedFlagsAndTexPtr) == 4, "optimization");

typedef ALIGNED4 struct {
	GfxVtx v0;
	GfxVtx v1;
	GfxVtx v2;
	GfxVtx v3;
	PackedFlagsAndTexPtr packed_flags_and_tex_ptr;
} TessellationQueueEntry;

#define TESSELLATION_QUEUE_COUNT (TESSELLATION_QUEUE_SIZE_BYTES / sizeof(TessellationQueueEntry))
#ifndef NO_KERNEL_RAM
#define TESSELLATION_QUEUE_START ((TessellationQueueEntry*) (65536 - TESSELLATION_QUEUE_SIZE_BYTES))
#else
#warning kernel ram tessellation queue disabled
static TessellationQueueEntry tessellation_queue_buf[TESSELLATION_QUEUE_COUNT];
#define TESSELLATION_QUEUE_START (tessellation_queue_buf)
#endif
#define TESSELLATION_QUEUE_END (TESSELLATION_QUEUE_START + TESSELLATION_QUEUE_COUNT)

scratchpad static TessellationQueueEntry* tessellation_queue_head;

void gfx_init_tessellation_queue() {
	tessellation_queue_head = TESSELLATION_QUEUE_START;
}

typedef struct {
	u8 flags;
	u8 page_attr;
	u16 clut_attr;
	u32 window_cmd;
} PreloadedFlags;

TESSELLATION_ICACHE_FUNC [[gnu::flatten]] static void draw_sub_poly(const GfxVtx* v0, const GfxVtx* v1, const GfxVtx* v2, const GfxVtx* v3, PreloadedFlags* preloaded_flags) {
	gte_loadDataRegM(GTE_VXY0, (u32*) &v0->xy);
	gte_loadDataRegM(GTE_VZ0, (u32*) &v0->z);
	gte_loadDataRegM(GTE_VXY1, (u32*) &v1->xy);
	gte_loadDataRegM(GTE_VZ1, (u32*) &v1->z);
	gte_loadDataRegM(GTE_VXY2, (u32*) &v2->xy);
	gte_loadDataRegM(GTE_VZ2, (u32*) &v2->z);

	// RTPT transforms and projects all 3 vertices in a mere 23 cycles. based GTE :)
	gte_commandAfterLoad(GTE_CMD_RTPT | GTE_SF);

	bool blend = preloaded_flags->flags & PRIM_FLAG_FORCE_BLEND;

	// if there was any error in rtpt, cull it // this may not actually be good!
	//if(gte_getControlReg(GTE_FLAG) & FATAL_GTE_ERRORS) return;

	// fetch the results
	s32 sxy0 = gte_getDataReg(GTE_SXY0);
	s32 sxy1 = gte_getDataReg(GTE_SXY1);
	s32 sxy2 = gte_getDataReg(GTE_SXY2);
	s32 sxy3;

	s32 z = gte_getDataReg(GTE_SZ1);
	s32 v1sz = gte_getDataReg(GTE_SZ2);
	if(v1sz > z) {
		z = v1sz;
	}
	s32 v2sz = gte_getDataReg(GTE_SZ3);
	if(v2sz > z) {
		z = v2sz;
	}

	if(v3) {
		// if this is a quad, quickly transform the extra vertex with rtps
		gte_loadDataRegM(GTE_VXY0, (u32*) &v3->xy);
		gte_loadDataRegM(GTE_VZ0, (u32*) &v3->z);
		gte_commandAfterLoad(GTE_CMD_RTPS | GTE_SF);

		// if there was any error in rtps, cull it
		//if(gte_getControlReg(GTE_FLAG) & FATAL_GTE_ERRORS) return;

		// get the result
		sxy3 = gte_getDataReg(GTE_SXY2);
		s32 v3sz = gte_getDataReg(GTE_SZ3);
		if(v3sz > z) {
			z = v3sz;
		}
	}
	if((u32) (z - 1) >= (u32) (MAX_Z - 1)) {
		return;
	}
	s16 sx0 = sxy0, sx1 = sxy1, sx2 = sxy2, sx3 = sxy3;
	if((sx0 <= 0 && sx1 <= 0 && sx2 <= 0 && sx3 <= 0) || (sx0 >= XRES && sx1 >= XRES && sx2 >= XRES && sx3 >= XRES)) {
		return;
	}

	u32 ot_z = z / (MAX_Z / Z_BUCKETS) + FOREGROUND_BUCKETS;

	Packet packet = gfx_packet_begin();
#ifdef DEBUG_DEPTH
	gfx_packet_append(&packet, z * 256 / MAX_Z | _gp0_polygon(v3, false, false, false, blend));
	gfx_packet_append(&packet, sxy0);
	gfx_packet_append(&packet, sxy1);
	gfx_packet_append(&packet, sxy2);
	if(v3) {
		gfx_packet_append(&packet, sxy3);
	}
#else
	gfx_packet_append(&packet, preloaded_flags->window_cmd);
	gfx_packet_append(&packet, v0->color.as_u32 | _gp0_polygon(v3, false, true, true, blend));
	gfx_packet_append(&packet, sxy0);
	gfx_packet_append(&packet, v0->uv | (u32) preloaded_flags->clut_attr << 16);
	gfx_packet_append(&packet, v1->color.as_u32);
	gfx_packet_append(&packet, sxy1);
	gfx_packet_append(&packet, v1->uv | (u32) preloaded_flags->page_attr << 16);
	gfx_packet_append(&packet, v2->color.as_u32);
	gfx_packet_append(&packet, sxy2);
	gfx_packet_append(&packet, v2->uv);
	if(v3) {
		gfx_packet_append(&packet, v3->color.as_u32);
		gfx_packet_append(&packet, sxy3);
		gfx_packet_append(&packet, v3->uv);
	}
#endif
	gfx_packet_end(packet, ot_z);
}

TESSELLATION_ICACHE_FUNC static GfxVtx between(const GfxVtx* v0, const GfxVtx* v1) {
	return (GfxVtx) {
		.x = ((s32) v0->x + v1->x) / 2, .y = ((s32) v0->y + v1->y) / 2, .z = ((s32) v0->z + v1->z) / 2,
		.u = (v0->u + v1->u) / 2, .v = (v0->v + v1->v) / 2,
		.color.as_u32 = ((v0->color.as_u32 & 0xFEFEFE) + (v1->color.as_u32 & 0xFEFEFE)) / 2,
	};
}

TESSELLATION_ICACHE_FUNC void gfx_flush_tessellation_queue_inner() {
	while(tessellation_queue_head != TESSELLATION_QUEUE_START) {
		tessellation_queue_head--;
		const TessellationQueueEntry* entry = tessellation_queue_head;

		// preload some of the flags
		PreloadedFlags preloaded_flags;
		bool is_quad = entry->packed_flags_and_tex_ptr.is_quad;
		preloaded_flags.flags = entry->packed_flags_and_tex_ptr.flags;
		TexHeader* tex = (void*) (uintptr_t) entry->packed_flags_and_tex_ptr.tex_ptr;
		preloaded_flags.window_cmd = tex->window_cmd;
		preloaded_flags.page_attr = tex->page_attr;
		preloaded_flags.clut_attr = tex->clut_attr;

		bool high = preloaded_flags.flags & PRIM_FLAG_TESSELLATE_HIGH;
		// all of the "bottom/top left/right" terminology below is just imaginary nicknames for v0/v1/v2/v3 to keep my brain from breaking
		const GfxVtx* vtl = &entry->v0;
		const GfxVtx* vbl = &entry->v1;
		const GfxVtx* vtr = &entry->v2;
		sdata static GfxVtx vt, vl, vctl;
		vt = between(vtl, vtr);
		vl = between(vtl, vbl);
		vctl = between(&vl, &vt);
		if(is_quad) {
			// remember that quads on ps1 are zigzagged!! it's kind of confusing
			const GfxVtx* vbr = &entry->v3;
			sdata static GfxVtx vb, vr, vc_vertical, vc_horizontal, vc;
			vb = between(vbl, vbr);
			vr = between(vtr, vbr);
			vc_vertical = between(&vt, &vb);
			vc_horizontal = between(&vl, &vr);
			vc = between(&vc_horizontal, &vc_vertical);
			if(high) {
				/*
					vtl━━━vtlt━━vt━━━━vtrt━━vtr
					 ┃     │     ┃     │     ┃
					vtll──vctl──vct───vctr──vtrr
					 ┃     │     ┃     │     ┃
					vl━━━━vcl━━━vc━━━━vcr━━━vr
					 ┃     │     ┃     │     ┃
					vbll──vcbl──vcb───vcbr──vbrr
					 ┃     │     ┃     │     ┃
					vbl━━━vblb━━vb━━━━vbrb━━vbr
				*/
				sdata static GfxVtx vtll, vbll, vtrr, vbrr, vtrt, vtlt, vbrb, vblb, vct, vctr, vcl, vcr, vcbl, vcb, vcbr;
				vtll = between(vtl, &vl);
				vbll = between(vbl, &vl);
				vtrr = between(vtr, &vr);
				vbrr = between(vbr, &vr);
				vtrt = between(vtr, &vt);
				vtlt = between(vtl, &vt);
				vbrb = between(vbr, &vb);
				vblb = between(vbl, &vb);
				vct = between(&vc, &vt);
				vctr = between(&vc, vtr);
				vcl = between(&vc, &vl);
				vcr = between(&vc, &vr);
				vcbl = between(&vc, vbl);
				vcb = between(&vc, &vb);
				vcbr = between(&vc, vbr);
				draw_sub_poly(vtl, &vtll, &vtlt, &vctl, &preloaded_flags);
				draw_sub_poly(&vtlt, &vctl, &vt, &vct, &preloaded_flags);
				draw_sub_poly(&vt, &vct, &vtrt, &vctr, &preloaded_flags);
				draw_sub_poly(&vtrt, &vctr, vtr, &vtrr, &preloaded_flags);

				draw_sub_poly(&vtll, &vl, &vctl, &vcl, &preloaded_flags);
				draw_sub_poly(&vctl, &vcl, &vct, &vc, &preloaded_flags);
				draw_sub_poly(&vct, &vc, &vctr, &vcr, &preloaded_flags);
				draw_sub_poly(&vctr, &vcr, &vtrr, &vr, &preloaded_flags);

				draw_sub_poly(&vl, &vbll, &vcl, &vcbl, &preloaded_flags);
				draw_sub_poly(&vcl, &vcbl, &vc, &vcb, &preloaded_flags);
				draw_sub_poly(&vc, &vcb, &vcr, &vcbr, &preloaded_flags);
				draw_sub_poly(&vcr, &vcbr, &vr, &vbrr, &preloaded_flags);

				draw_sub_poly(&vbll, vbl, &vcbl, &vblb, &preloaded_flags);
				draw_sub_poly(&vcbl, &vblb, &vcb, &vb, &preloaded_flags);
				draw_sub_poly(&vcb, &vb, &vcbr, &vbrb, &preloaded_flags);
				draw_sub_poly(&vcbr, &vbrb, &vbrr, vbr, &preloaded_flags);

#ifdef NO_GAPS_AT_ALL
				// prevent T junction gaps
				draw_sub_poly(vtl, &vl, &vtll, NULL, &preloaded_flags);
				draw_sub_poly(&vl, vbl, &vbll, NULL, &preloaded_flags);
				draw_sub_poly(vtl, &vtlt, &vt, NULL, &preloaded_flags);
				draw_sub_poly(&vt, &vtrt, vtr, NULL, &preloaded_flags);
				draw_sub_poly(vbl, &vb, &vblb, NULL, &preloaded_flags);
				draw_sub_poly(&vb, vbr, &vbrb, NULL, &preloaded_flags);
				draw_sub_poly(vbr, &vr, &vbrr, NULL, &preloaded_flags);
				draw_sub_poly(&vr, vtr, &vtrr, NULL, &preloaded_flags);
#elifdef NO_GAPS
				// prevent T junction gaps
				draw_sub_poly(vtl, vbl, &vl, NULL, &preloaded_flags);
				draw_sub_poly(vtr, vtl, &vt, NULL, &preloaded_flags);
				draw_sub_poly(vbl, vbr, &vb, NULL, &preloaded_flags);
				draw_sub_poly(vbr, vtr, &vr, NULL, &preloaded_flags);
#endif
			} else {
				/*
					vtl━━vt━━━vtr
					 ┃    │    ┃
					vl───vc───vr
					 ┃    │    ┃
					vbl━━vb━━━vbr
				*/
				draw_sub_poly(vtl, &vl, &vt, &vc, &preloaded_flags);
				draw_sub_poly(&vt, &vc, vtr, &vr, &preloaded_flags);
				draw_sub_poly(&vl, vbl, &vc, &vb, &preloaded_flags);
				draw_sub_poly(&vc, &vb, &vr, vbr, &preloaded_flags);

#ifdef NO_GAPS
				// prevent T junction gaps
				draw_sub_poly(vtl, vbl, &vl, NULL, &preloaded_flags);
				draw_sub_poly(vtr, vtl, &vt, NULL, &preloaded_flags);
				draw_sub_poly(vbl, vbr, &vb, NULL, &preloaded_flags);
				draw_sub_poly(vbr, vtr, &vr, NULL, &preloaded_flags);
#endif
			}
		} else {
			const GfxVtx vc = between(vbl, vtr);
			// it is important that the diagonal direction be kept consistent, so no cleverness to store 2 triangles as quads sadly
			if(high) {
				/*
					vtl━━━vtlt━━vt━━━━vtrt━━vtr
					 ┃     │     ┃  ╱  ╎  ╱
					vtll──vctl──vct╌╌╌vctr
					 ┃     │     ┃  ╱
					vl━━━━vcl━━━vc
					 ┃  ╱  ╎  ╱
					vbll╌╌vcbl
					 ┃  ╱
					vbl
				*/
				sdata static GfxVtx vtlt, vtll, vct, vcl;
				vtlt = between(vtl, &vt);
				vtll = between(vtl, &vl);
				vct = between(&vt, &vc);
				vcl = between(&vl, &vc);
				// top left part
				draw_sub_poly(vtl, &vtll, &vtlt, &vctl, &preloaded_flags);
				draw_sub_poly(&vtlt, &vctl, &vt, &vct, &preloaded_flags);
				draw_sub_poly(&vtll, &vl, &vctl, &vcl, &preloaded_flags);
				draw_sub_poly(&vctl, &vcl, &vct, &vc, &preloaded_flags);

				sdata static GfxVtx vtrt, vctr;
				vtrt = between(vtr, &vt);
				vctr = between(vtr, &vc);
				// right part
				draw_sub_poly(&vt, &vct, &vtrt, &vctr, &preloaded_flags);
				draw_sub_poly(&vct, &vc, &vctr, NULL, &preloaded_flags);
				draw_sub_poly(&vtrt, &vctr, vtr, NULL, &preloaded_flags);

				sdata static GfxVtx vbll, vcbl;
				vbll = between(vbl, &vl);
				vcbl = between(vbl, &vc);
				// bottom part
				draw_sub_poly(&vl, &vbll, &vcl, &vcbl, &preloaded_flags);
				draw_sub_poly(&vcl, &vcbl, &vc, NULL, &preloaded_flags);
				draw_sub_poly(&vbll, vbl, &vcbl, NULL, &preloaded_flags);

#ifdef NO_GAPS_AT_ALL
				// prevent T junction gaps
				draw_sub_poly(vtl, &vl, &vtll, NULL, &preloaded_flags);
				draw_sub_poly(&vl, vbl, &vbll, NULL, &preloaded_flags);
				draw_sub_poly(vbl, &vc, &vcbl, NULL, &preloaded_flags);
				draw_sub_poly(&vc, vtr, &vctr, NULL, &preloaded_flags);
				draw_sub_poly(vtl, &vt, &vtlt, NULL, &preloaded_flags);
				draw_sub_poly(&vt, vtr, &vtrt, NULL, &preloaded_flags);
#elifdef NO_GAPS
				// prevent T junction gaps
				draw_sub_poly(vtl, vbl, &vl, NULL, &preloaded_flags);
				draw_sub_poly(vbl, vtr, &vc, NULL, &preloaded_flags);
				draw_sub_poly(vtr, vtl, &vt, NULL, &preloaded_flags);
#endif
			} else {
				/*
					vtl━━vt━━vtr
					 ┃    ╎ ╱
					vl╌╌╌vc
					 ┃ ╱
					vbl
				*/
				draw_sub_poly(vtl, &vl, &vt, &vc, &preloaded_flags);
				draw_sub_poly(&vt, &vc, vtr, NULL, &preloaded_flags);
				draw_sub_poly(&vl, vbl, &vc, NULL, &preloaded_flags);

#ifdef NO_GAPS
				// prevent T junction gaps
				draw_sub_poly(vtl, vbl, &vl, NULL, &preloaded_flags);
				draw_sub_poly(vbl, vtr, &vc, NULL, &preloaded_flags);
				draw_sub_poly(vtr, vtl, &vt, NULL, &preloaded_flags);
#endif
			}
		}
	}
}

ALWAYS_INLINE void gfx_flush_tessellation_queue_if_necessary() {
	if(tessellation_queue_head != TESSELLATION_QUEUE_START) {
		gfx_flush_tessellation_queue_inner();
	}
}

extern void* tex_ptr;

ALWAYS_INLINE void gfx_begin_queueing_for_tessellation(const GfxVtx* v0, const GfxVtx* v1, const GfxVtx* v2, const GfxVtx* v3, u8 flags) {
	tessellation_queue_head->v0.xy = v0->xy;
	tessellation_queue_head->v0.zuv = v0->zuv;
	tessellation_queue_head->v1.xy = v1->xy;
	tessellation_queue_head->v1.zuv = v1->zuv;
	tessellation_queue_head->v2.xy = v2->xy;
	tessellation_queue_head->v2.zuv = v2->zuv;
	PackedFlagsAndTexPtr packed_flags_and_tex_ptr = {.flags = flags, .is_quad = 0, .tex_ptr = (u32) tex_ptr};
	if(v3) {
		tessellation_queue_head->v3.xy = v3->xy;
		tessellation_queue_head->v3.zuv = v3->zuv;
		packed_flags_and_tex_ptr.is_quad = true;
	}
	tessellation_queue_head->packed_flags_and_tex_ptr = packed_flags_and_tex_ptr;
}

ALWAYS_INLINE void gfx_finish_queueing_for_tessellation(u32 rgb0, u32 rgb1, u32 rgb2, u32 rgb3) {
	tessellation_queue_head->v0.color.as_u32 = rgb0;
	tessellation_queue_head->v1.color.as_u32 = rgb1;
	tessellation_queue_head->v2.color.as_u32 = rgb2;
	tessellation_queue_head->v3.color.as_u32 = rgb3;
	tessellation_queue_head++;
	if(tessellation_queue_head >= TESSELLATION_QUEUE_END) {
		gfx_flush_tessellation_queue_inner();
	}
}
#endif
