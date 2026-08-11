#include "gdf.h"
#include "goddard/gd_types.h"
#include "port/gfx/gfx.h"
#include "port/gfx/gfx_internal.h"
#include <engine/math_util.h>

static GdfObj objects[32];
static u32 object_count = 0;
u8* gdf_pool;

GdfObj* gdf_obj_create(struct GdVtxData vtx_data, struct GdFaceData face_data, Color* materials, GdfJointList joints) {
	GdfObj* obj = &objects[object_count++];

	obj->parent = NULL;
	obj->transform = mtx_identity();
	obj->mesh_visible = true;

	obj->vtx_data = vtx_data;
	obj->face_data = face_data;
	obj->materials = materials;
	obj->vertices = (GfxVtx*) gdf_pool;
	obj->face_count = face_data.count;
	gdf_pool += sizeof(GfxVtx) * 3 * face_data.count;

	obj->anim.bank = NULL;
	obj->joints = joints;

	return obj;
}

void gdf_obj_refresh_vertices(GdfObj* obj);

void gdf_joint_process(GdfJoint* joint) {
	if(!joint->anim.bank) {
		joint->latest_mtx = mtx_identity();
		return;
	}
	s16 nox = joint->net_attach_offset.vx;
	s16 noy = joint->net_attach_offset.vy;
	s16 noz = joint->net_attach_offset.vz;
	s16 nrx = joint->net_rotation.vx;
	s16 nry = joint->net_rotation.vy;
	s16 nrz = joint->net_rotation.vz;
	s16 ox = joint->attach_offset.vx;
	s16 oy = joint->attach_offset.vy;
	s16 oz = joint->attach_offset.vz;
	s16 rx = joint->rotation.vx;
	s16 ry = joint->rotation.vy;
	s16 rz = joint->rotation.vz;

	ShortMatrix inverse = mtx_translationi((s16[]) {-nox, -noy, -noz});
	ShortMatrix work = mtx_rotation_zxy((s16[]) {0, -nry, 0});
	inverse = mtx_mul(&inverse, &work);
	work = mtx_rotation_zxy((s16[]) {-nrx, 0, 0});
	inverse = mtx_mul(&inverse, &work);
	work = mtx_rotation_zxy((s16[]) {0, 0, -nrz});
	inverse = mtx_mul(&inverse, &work);
	work = mtx_translationi((s16[]) {-ox, -oy, -oz});
	inverse = mtx_mul(&inverse, &work);
	work = mtx_rotation_zxy((s16[]) {0, -ry, 0});
	inverse = mtx_mul(&inverse, &work);
	work = mtx_rotation_zxy((s16[]) {-rx, 0, 0});
	inverse = mtx_mul(&inverse, &work);
	work = mtx_rotation_zxy((s16[]) {0, 0, -rz});
	inverse = mtx_mul(&inverse, &work);

	joint->latest_mtx = gdf_process_anim(&joint->anim);
	ShortMatrix net_matrix = mtx_rotation_zxy_and_translation(joint->net_attach_offset.elems, joint->net_rotation.elems);
	joint->latest_mtx = mtx_mul(&joint->latest_mtx, &net_matrix);
	joint->latest_mtx = mtx_mul(&inverse, &joint->latest_mtx);
}

void gdf_obj_draw(GdfObj* obj) {
	if(obj->parent) {
		obj->latest_mtx = mtx_mul(&obj->transform, &obj->parent->latest_mtx);
	} else {
		obj->latest_mtx = obj->transform;
	}

	for(u32 i = 0; i < obj->joints.count; i++) {
		gdf_joint_process(&obj->joints.joints[i]);
	}

	if(obj->anim.bank) {
		ShortMatrix anim_mtx = gdf_process_anim(&obj->anim);
		obj->latest_mtx = mtx_mul(&anim_mtx, &obj->latest_mtx);
	}

	if(obj->mesh_visible) {
		gfx_emit_mtx_push();
		gfx_emit_mtx_set(&obj->latest_mtx);
		gdf_obj_refresh_vertices(obj);
		gfx_emit_mtx_pop();
	}

	//for(int i = 0; i < obj->face_count; i++) {
	//	gfx_emit_vertices_native(&obj->vertices[i * 3], 3);
	//	gfx_emit_tri(0, 2, 1, 0);
	//}
}

static Light_t lights[4];

void gdf_draw_all_objs(void) {
	//for(u32 i = 0; i < joint_count; i++) {
	//	gdf_joint_draw(&joints[i]);
	//}
	lights[0] = (Light_t) {
		.col = {255, 255, 255},
		.colc = {255, 255, 255},
		.dir = {127, 0, -127},
	};
	gfx_emit_light_directional0(lights);
	for(u32 i = 0; i < object_count; i++) {
		gdf_obj_draw(&objects[i]);
	}
}
