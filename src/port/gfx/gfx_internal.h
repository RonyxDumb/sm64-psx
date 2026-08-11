#pragma once
#include "gfx.h"

typedef struct {
	u16 width;
	u16 height;
	bool rotated;
	bool has_translucency;
	u8 offx;
	u8 offy;
#ifdef TARGET_PC
	[[gnu::packed]] u64 sdl_tex_ptr;
#else
	u16 page_attr;
	u16 clut_attr;
	u32 window_cmd;
#endif
	u16 pixel_data_sector;
	u16 pixel_data_sector_count;
} TexHeader;

STATIC_ASSERT(sizeof(TexHeader) == 20, "TexHeader must match the layout output by convertImage.py and pack_textures.py");

#define Z_BUCKETS 2000 // for performance, should be equal to MAX_Z divided by a power of two
#define FOREGROUND_BUCKETS 32
#define BACKGROUND_Z (Z_BUCKETS + FOREGROUND_BUCKETS)
#define OT_LEN 2048
#define PACKET_POOL_LEN 16384
#define TESSELLATION_QUEUE_SIZE_BYTES 12288

typedef struct {
	u32 ot[OT_LEN];
	u32 packet_pool[PACKET_POOL_LEN];
	u32 frame_start_packet[8];
} Framebuffer;

void gfx_init_buffers();
void gfx_swap_buffers(bool vsync_30fps);
void gfx_discard_frame();

typedef struct {
	u32* header;
	u32* start;
	u32* end;
} Packet;

Packet gfx_packet_begin();
void gfx_packet_append(Packet* packet, u32 cmd);
void gfx_packet_end(Packet packet, u32 ot_z);

typedef ALIGNED4 struct {
	union {
		struct {
			s16 x;
			s16 y;
		};
		s32 xy;
	};
	union {
		u32 zuv;
		struct {
			s16 z;
			union {
				struct {
					u8 u;
					u8 v;
				};
				u16 uv;
				u8 uv_arr[2];
			};
		};
	};
	Color color; // also used for normals
} GfxVtx;

STATIC_ASSERT(sizeof(GfxVtx) + 4 <= sizeof(Vtx), "GfxVtx must be at least 4 bytes smaller than Vtx");

typedef union {
	struct {
		u32 tag;
		GfxVtx psx[];
	};
	struct {
		Vtx n64[];
	};
} VtxList;

void ensure_vertices_converted(VtxList* vtx_list, u32 count);

#define COMPILED_TAG 0x777A1210 // MARIO :)

void gfx_begin_queueing_for_tessellation(const GfxVtx* v0, const GfxVtx* v1, const GfxVtx* v2, const GfxVtx* v3, u8 flags);
void gfx_finish_queueing_for_tessellation(u32 rgb0, u32 rgb1, u32 rgb2, u32 rgb3);

#define FATAL_GTE_ERRORS (\
	GTE_FLAG_MAC0_UNDERFLOW |\
	GTE_FLAG_MAC0_OVERFLOW |\
	GTE_FLAG_IR2_SATURATED |\
	GTE_FLAG_IR1_SATURATED |\
	GTE_FLAG_MAC3_UNDERFLOW |\
	GTE_FLAG_MAC2_UNDERFLOW |\
	GTE_FLAG_MAC1_UNDERFLOW |\
	GTE_FLAG_MAC3_OVERFLOW |\
	GTE_FLAG_MAC2_OVERFLOW |\
	GTE_FLAG_MAC1_OVERFLOW\
)

#define IMPORTANT_GTE_ERRORS 0xFFFFFFFF
