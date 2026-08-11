#include "renderer.h"
#include "goddard/dynlists/animdata.h"
#include "port/gfx/gfx.h"
#include "port/gfx/gfx_internal.h"
#include "types.h"
#include "gd_types.h"
#include "dynlists/gdf_obj_data.h"
#include "gdf.h"

extern u8* gdf_pool;

#define EYE_Z_OFF 0

[[gnu::noinline]] void gdm_init(void* pool, UNUSED u32 size) {
	gdf_pool = pool;

	GdfObj* mario_face = gdf_obj_create(mario_Face_VtxInfo, mario_Face_FaceInfo, mario_face_materials, mario_face_joint_list);
	mario_face->transform = mtx_translationi((s16[]) {0, -200, -2000});
	mario_face->anim.bank = anim_mario_intro;
	//mario_face->mesh_visible = false;

	// despite the name, this is mario's right eye
	GdfObj* mario_eye_left = gdf_obj_create(vtx_mario_eye_left, faces_mario_eye_left, mario_eye_materials, (GdfJointList) {.count = 0});
	mario_eye_left->parent = mario_face;
	// fungle the numbers up a bit to mask the crooked rotation
	// they'll never notice :)
	//mario_eye_left->transform = mtx_rotation_zxy_and_translation((s16[]) {0, 0, 0}, (s16[]) {(s32) 0 * 65536 / 360, (s32) 0 * 65536 / 360, 0 * 65536 / 360});
	//ShortMatrix tmp = mtx_rotation_zxy_and_translation((s16[]) {-30, 192, -2 - EYE_Z_OFF}, (s16[]) {(s32) -90.0 * 65536 / 360, (s32) 0.0 * 65536 / 360, 0.0 * 65536 / 360});
	//mario_eye_left->transform = mtx_mul(&mario_eye_left->transform, &tmp);
	mario_eye_left->transform = mtx_rotation_zxy_and_translation((s16[]) {-30, 192, -2 - EYE_Z_OFF}, (s16[]) {(s32) -90.0 * 65536 / 360, (s32) 0.0 * 65536 / 360, 0.0 * 65536 / 360});
	mario_eye_left->anim.bank = anim_mario_eye_left;

	GdfObj* mario_eye_right = gdf_obj_create(vtx_mario_eye_right, faces_mario_eye_right, mario_eye_materials, (GdfJointList) {.count = 0});
	mario_eye_right->parent = mario_face;
	mario_eye_right->transform = mtx_rotation_zxy_and_translation((s16[]) {30, 192, -3 - EYE_Z_OFF}, (s16[]) {(s32) 90.0 * 65536 / 360, (s32) 180.0 * 65536 / 360, 0.0 * 65536 / 360});
	mario_eye_right->anim.bank = anim_mario_eye_right;

	GdfObj* mario_eyebrow_left = gdf_obj_create(vtx_mario_eyebrow_left, faces_mario_eyebrow_left, mario_eyebrow_mustache_materials, mario_eyebrow_left_joint_list);
	mario_eyebrow_left->parent = mario_face;

	GdfObj* mario_eyebrow_right = gdf_obj_create(vtx_mario_eyebrow_right, faces_mario_eyebrow_right, mario_eyebrow_mustache_materials, mario_eyebrow_right_joint_list);
	mario_eyebrow_right->parent = mario_face;

	GdfObj* mario_mustache = gdf_obj_create(vtx_mario_mustache, faces_mario_mustache, mario_eyebrow_mustache_materials, mario_mustache_joint_list);
	mario_mustache->parent = mario_face;
	mario_mustache->transform = mtx_rotation_zxy((s16[]) {0.02 * 65536 / 360, -0.002 * 65536 / 360, 0.04 * 65536 / 360});
	//gdf_obj_set_anim(mario_mustache, &anim_mario_mustache[0]);
}

void gdm_maketestdl(UNUSED int _id) {
}

u32* gdm_gettestdl(UNUSED int _id) {
	gdf_draw_all_objs();
	return NULL;
}

u32 gd_sfx_to_play(void) {
	return 0;
}
