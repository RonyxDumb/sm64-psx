#include <port/gfx/gfx_internal.h>
#include <engine/math_util.h>

static ShortMatrix modelview;

void gfx_modelview_identity() {
	modelview = (ShortMatrix) {
		.m = {{ONE, 0, 0}, {0, ONE, 0}, {0, 0, ONE}},
		.t = {0, 0, 0}
	};
}

void gfx_modelview_set(const ShortMatrix* new_modelview) {
	modelview = *new_modelview;
	//for(int i = 0; i < 3; i++) {
	//modelview.m[i][2] *= -1;
	//}
	//modelview.t[2] *= -1;
	//mtx_transpose(&modelview);
}

ShortMatrix gfx_modelview_get(void) {
	ShortMatrix mv = modelview;
	//mtx_transpose(&mv);
	//for(int i = 0; i < 3; i++) {
	//mv.m[i][2] *= -1;
	//}
	//mv.t[2] *= -1;
	return mv;
}

void gfx_modelview_mul(const ShortMatrix* o) {
	ShortMatrix m = gfx_modelview_get();
	ShortMatrix ot = *o;
	ShortMatrix result;
	for(int i = 0; i < 4; i++) {
		for(int j = 0; j < 4; j++) {
			mtx_set_cell(&result, i, j, (
				(s64) mtx_get_cell(&ot, i, 0) * mtx_get_cell(&m, 0, j) +
				(s64) mtx_get_cell(&ot, i, 1) * mtx_get_cell(&m, 1, j) +
				(s64) mtx_get_cell(&ot, i, 2) * mtx_get_cell(&m, 2, j) +
				(s64) mtx_get_cell(&ot, i, 3) * mtx_get_cell(&m, 3, j)
			) / ONE);
		}
	}
	gfx_modelview_set(&result);
}

ShortVec gfx_modelview_apply_without_translation(const ShortVec* v) {
	ShortMatrix m = gfx_modelview_get();
	return (ShortVec) {
		.vx = ((s32) v->vx * m.m[0][0] + (s32) v->vy * m.m[1][0] + (s32) v->vz * m.m[2][0]) / ONE,
		.vy = ((s32) v->vx * m.m[0][1] + (s32) v->vy * m.m[1][1] + (s32) v->vz * m.m[2][1]) / ONE,
		.vz = ((s32) v->vx * m.m[0][2] + (s32) v->vy * m.m[1][2] + (s32) v->vz * m.m[2][2]) / ONE
	};
}

ShortVec gfx_modelview_apply(const ShortVec* v) {
	ShortMatrix m = gfx_modelview_get();
	return (ShortVec) {
		.vx = ((s32) v->vx * m.m[0][0] + (s32) v->vy * m.m[1][0] + (s32) v->vz * m.m[2][0]) / ONE + m.t[0],
		.vy = ((s32) v->vx * m.m[0][1] + (s32) v->vy * m.m[1][1] + (s32) v->vz * m.m[2][1]) / ONE + m.t[1],
		.vz = ((s32) v->vx * m.m[0][2] + (s32) v->vy * m.m[1][2] + (s32) v->vz * m.m[2][2]) / ONE + m.t[2]
	};
}

void gfx_modelview_translatei(const short off[3]) {
	ShortMatrix tmp = mtx_translationi(off);
	gfx_modelview_mul(&tmp);
}

void gfx_modelview_get_rotation_into(ShortMatrix* mtx) {
	ShortMatrix modelview = gfx_modelview_get();
	memcpy(mtx->m, modelview.m, 2 * 3 * 3);
}

void gfx_modelview_get_translation_into(ShortMatrix* mtx) {
	ShortMatrix modelview = gfx_modelview_get();
	memcpy(mtx->t, modelview.t, 2 * 3);
}

void gfx_modelview_set_rotation_from(const ShortMatrix* mtx) {
	ShortMatrix modelview = gfx_modelview_get();
	memcpy(modelview.m, mtx->m, 2 * 3 * 3);
	gfx_modelview_set(&modelview);
}

void gfx_modelview_set_translation_from(const ShortMatrix* mtx) {
	ShortMatrix modelview = gfx_modelview_get();
	memcpy(modelview.t, mtx->t, 2 * 3);
	gfx_modelview_set(&modelview);
}
