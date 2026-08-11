#pragma once
#include <port/gfx/gfx_internal.h>
#include "gd_types.h"
#include "types.h"

// Goddard Fixed
// my simplified reimplementation of the goddard subsystem
// the original is just too horribly complex

#define GD_TO_GDF_ANIM_ANGLE(x) ((x) * 6553.6 / 360)

typedef struct {
	struct AnimDataInfo* bank;
	u16 current;
	u16 frame;
} GdfAnimationPlayback;

typedef struct {
	u16 index;
	u16 amount;
} GdfJointWeight;

typedef struct {
	ShortVec net_scale;
	ShortVec net_rotation;
	ShortVec net_attach_offset;
	ShortVec scale;
	ShortVec rotation;
	ShortVec attach_offset;
	ShortMatrix latest_mtx;

	const GdfJointWeight* weights;
	u32 weight_count;

	GdfAnimationPlayback anim;
} GdfJoint;

typedef struct {
	GdfJoint* joints;
	u32 count;
} GdfJointList;

typedef struct GdfObj {
	struct GdfObj* parent;
	ShortMatrix transform;
	ShortMatrix latest_mtx;
	bool mesh_visible;

	struct GdVtxData vtx_data;
	struct GdFaceData face_data;
	Color* materials;
	GfxVtx* vertices;
	u32 face_count;

	GdfAnimationPlayback anim;
	GdfJointList joints;
} GdfObj;

GdfObj* gdf_obj_create(struct GdVtxData vtx_data, struct GdFaceData face_data, Color* materials, GdfJointList joints);
void gdf_obj_draw(GdfObj* obj);
ShortMatrix gdf_process_anim(GdfAnimationPlayback* anim_info);
void gdf_draw_all_objs(void);
