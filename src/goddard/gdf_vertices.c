#include "gdf.h"
#include <port/gfx/gfx.h>

//static s32 int_lerp(s32 a, s32 b, q32 t) {
//	return a + (b - a) * t / ONE;
//}

#define PRECISION 8

static void affect_by_joints(ShortVec* vec, u32 idx, GdfJointList joints) {
	s32 offset_x = 0;
	s32 offset_y = 0;
	s32 offset_z = 0;
	q32 weights = 0;
	vec->vx *= PRECISION;
	vec->vy *= PRECISION;
	vec->vz *= PRECISION;
	for(u32 i = 0; i < joints.count; i++) {
		const GdfJoint* joint = &joints.joints[i];
		for(u32 j = 0; j < joint->weight_count; j++) {
			const GdfJointWeight* weight = &joint->weights[j];
			if(weight->index == idx) {
				gfx_modelview_set(&joint->latest_mtx);
				ShortVec offset = gfx_modelview_apply(vec);
				//vec->vx = int_lerp(vec->vx, offset.vx, weight->amount);
				//vec->vy = int_lerp(vec->vy, offset.vy, weight->amount);
				//vec->vz = int_lerp(vec->vz, offset.vz, weight->amount);
				offset_x += (s32) offset.vx * weight->amount / (ONE * PRECISION);
				offset_y += (s32) offset.vy * weight->amount / (ONE * PRECISION);
				offset_z += (s32) offset.vz * weight->amount / (ONE * PRECISION);
				weights += weight->amount;
				//*vec = target;
				break;
			}
		}
	}
	vec->vx /= PRECISION;
	vec->vy /= PRECISION;
	vec->vz /= PRECISION;
	if(weights != 0) {
		vec->vx = vec->vx * (ONE - weights) / ONE + offset_x;
		vec->vy = vec->vy * (ONE - weights) / ONE + offset_y;
		vec->vz = vec->vz * (ONE - weights) / ONE + offset_z;
	}
}

void gdf_obj_refresh_vertices(GdfObj* obj) {
	struct GdVtxData vtx_data = obj->vtx_data;
	struct GdFaceData face_data = obj->face_data;
	Color* materials = obj->materials;
	GfxVtx* conv_vtx = obj->vertices;
	u16 last_mtl_id = 0xFFFF;
	for(u32 i = 0; i < face_data.count; i++) {
		u32 i0 = face_data.data[i][1];
		u32 i1 = face_data.data[i][2];
		u32 i2 = face_data.data[i][3];
		ShortVec v0 = {.vx = vtx_data.data[i0][0], .vy = vtx_data.data[i0][1], .vz = vtx_data.data[i0][2]};
		ShortVec v1 = {.vx = vtx_data.data[i1][0], .vy = vtx_data.data[i1][1], .vz = vtx_data.data[i1][2]};
		ShortVec v2 = {.vx = vtx_data.data[i2][0], .vy = vtx_data.data[i2][1], .vz = vtx_data.data[i2][2]};
		affect_by_joints(&v0, i0, obj->joints);
		affect_by_joints(&v1, i1, obj->joints);
		affect_by_joints(&v2, i2, obj->joints);
		s32 edge0_x = v1.vx - v0.vx;
		s32 edge0_y = v1.vy - v0.vy;
		s32 edge0_z = v1.vz - v0.vz;
		s32 edge1_x = v2.vx - v0.vx;
		s32 edge1_y = v2.vy - v0.vy;
		s32 edge1_z = v2.vz - v0.vz;
		s32 nx = edge0_y * edge1_z - edge0_z * edge1_y;
		s32 ny = edge0_z * edge1_x - edge0_x * edge1_z;
		s32 nz = edge0_x * edge1_y - edge0_y * edge1_x;
		f32 normal_len_sq = nx * nx + ny * ny + nz * nz;
		if(normal_len_sq > 1) {
			f32 normal_len = sqrtf(normal_len_sq);
			//s32 div = ONE * ONE / normal_len;
			//nx = nx * div / ONE;
			//ny = ny * div / ONE;
			//nz = nz * div / ONE;
			nx = nx * 127.f / normal_len;
			ny = ny * 127.f / normal_len;
			nz = nz * 127.f / normal_len;
		}
		conv_vtx[0] = (GfxVtx) {
			.x = v0.vx, .y = v0.vy, .z = v0.vz, .color = {.r = nx, .g = ny, .b = nz}
		};
		conv_vtx[1] = (GfxVtx) {
			.x = v1.vx, .y = v1.vy, .z = v1.vz, .color = {.r = nx, .g = ny, .b = nz}
		};
		conv_vtx[2] = (GfxVtx) {
			.x = v2.vx, .y = v2.vy, .z = v2.vz, .color = {.r = nx, .g = ny, .b = nz}
		};
		u32 mtl_id = face_data.data[i][0];
		if(mtl_id != last_mtl_id) {
			gfx_emit_env_color_alpha_full(materials[mtl_id].as_u32);
			gfx_emit_light_ambient(materials[mtl_id].as_u32);
			last_mtl_id = mtl_id;
		}
		gfx_emit_vertices_native(conv_vtx);
		gfx_emit_tri(0, 1, 2, PRIM_FLAG_LIGHTED | PRIM_FLAG_ENV_COLOR);
		conv_vtx += 3;
	}
}
