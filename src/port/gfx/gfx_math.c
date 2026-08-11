#include <port/gfx/gfx.h>
#include <engine/math_util.h>

ShortMatrix mtx_identity() {
	ShortMatrix mtx = {
		.m = {{ONE, 0, 0}, {0, ONE, 0}, {0, 0, ONE}},
		.t = {0, 0, 0}
	};
	return mtx;
}

void mtx_set_cell(ShortMatrix* dst, int i, int j, s32 val) {
	if(j < 3) {
		if(i == 3) {
			dst->t[j] = val / ONE;
		} else {
			dst->m[i][j] = val;
		}
	}
}

s32 mtx_get_cell(const ShortMatrix* dst, int i, int j) {
	if(j == 3) {
		if(i == 3) {
			return ONE;
		}
		return 0;
	} else if(i == 3) {
		return (s32) dst->t[j] * ONE;
	}
	return dst->m[i][j];
}

ShortMatrix mtx_mul(const ShortMatrix* a, const ShortMatrix* b) {
	scratch_mtx = gfx_modelview_get();
	gfx_modelview_set(b);
	gfx_modelview_mul(a);
	ShortMatrix result = gfx_modelview_get();
	gfx_modelview_set(&scratch_mtx);
	return result;
}

ShortVec mtx_apply_without_translation(const ShortVec* v, const ShortMatrix* mtx) {
	scratch_mtx = gfx_modelview_get();
	gfx_modelview_set(mtx);
	ShortVec result = gfx_modelview_apply_without_translation(v);
	gfx_modelview_set(&scratch_mtx);
	return result;
}

ShortVec mtx_apply(const ShortVec* v, const ShortMatrix* mtx) {
	scratch_mtx = gfx_modelview_get();
	gfx_modelview_set(mtx);
	ShortVec result = gfx_modelview_apply(v);
	gfx_modelview_set(&scratch_mtx);
	return result;
}

void mtx_transpose(ShortMatrix* mtx) {
	register s16 tmp10 = mtx->m[1][0];
	register s16 tmp20 = mtx->m[2][0];
	register s16 tmp21 = mtx->m[2][1];
	mtx->m[1][0] = mtx->m[0][1];
	mtx->m[2][0] = mtx->m[0][2];
	mtx->m[2][1] = mtx->m[1][2];
	mtx->m[0][1] = tmp10;
	mtx->m[0][2] = tmp20;
	mtx->m[1][2] = tmp21;
}

ShortMatrix mtx_translationi(const short off[3]) {
	return (ShortMatrix) {
		.m = {{ONE, 0, 0}, {0, ONE, 0}, {0, 0, ONE}},
		.t = {off[0], off[1], off[2]}
	};
}

ShortMatrix mtx_rotation_zxy(const short angles[3]) {
	register q32 sx = sinqs(angles[0]);
	register q32 cx = cosqs(angles[0]);
	register q32 sy = sinqs(angles[1]);
	register q32 cy = cosqs(angles[1]);
	register q32 sz = sinqs(angles[2]);
	register q32 cz = cosqs(angles[2]);
	return (ShortMatrix) {
		.m = {
			{qmul(cy, cz) + qmul3(sx, sy, sz), qmul(cx, sz), qmul(-sy, cz) + qmul3(sx, cy, sz)},
			{qmul(-cy, sz) + qmul3(sx, sy, cz), qmul(cx, cz), qmul(sy, sz) + qmul3(sx, cy, cz)},
			{qmul(cx, sy), -sx, qmul(cx, cy)}
		},
		.t = {0, 0, 0}
	};
}

ShortMatrix mtx_rotation_zxy_and_translation(const s16 translation[3], const short angles[3]) {
	ShortMatrix m = mtx_rotation_zxy(angles);
	m.t[0] = translation[0];
	m.t[1] = translation[1];
	m.t[2] = translation[2];
	return m;
}

ShortMatrix mtx_rotation_xyz(const short angles[3]) { // used for skeletal animations
	register q32 sx = sinqs(angles[0]);
	register q32 cx = cosqs(angles[0]);
	register q32 sy = sinqs(angles[1]);
	register q32 cy = cosqs(angles[1]);
	register q32 sz = sinqs(angles[2]);
	register q32 cz = cosqs(angles[2]);
	return (ShortMatrix) {
		.m = {
			{qmul(cy, cz), qmul(cy, sz), -sy},
			{qmul3(sx, sy, cz) - qmul(cx, sz), qmul3(sx, sy, sz) + qmul(cx, cz), qmul(sx, cy)},
			{qmul3(cx, sy, cz) + qmul(sx, sz), qmul3(cx, sy, sz) - qmul(sx, cz), qmul(cx, cy)}
		},
		.t = {0, 0, 0}
	};
}

ShortMatrix mtx_scalingq(const q32 scale[3]) {
	return (ShortMatrix) {
		.m = {{scale[0], 0, 0}, {0, scale[1], 0}, {0, 0, scale[2]}},
		.t = {0, 0, 0}
	};
}

void mtx_scaleq(ShortMatrix* mtx, const q32 scale[3]) {
	for(int i = 0; i < 3; i++) {
		for(int j = 0; j < 3; j++) {
			mtx->m[i][j] = qmul((q32) mtx->m[i][j], scale[i]);
		}
	}
}

scratchpad ShortMatrix scratch_mtx;
sbss ShortMatrix matrix_stack[16];
u8 matrix_stack_len = 0;

void gfx_modelview_push() {
	if(matrix_stack_len < 16) {
		matrix_stack[matrix_stack_len++] = gfx_modelview_get();
	}
}

void gfx_modelview_pop() {
	if(matrix_stack_len) {
		gfx_modelview_set(&matrix_stack[--matrix_stack_len]);
	}
}

void gfx_modelview_rotate_xyz(const short angles[3]) { // used for skeletal animations
	scratch_mtx = mtx_rotation_xyz(angles);
	gfx_modelview_mul(&scratch_mtx);
}

void gfx_modelview_rotate_zxy(const short angles[3]) {
	scratch_mtx = mtx_rotation_zxy(angles);
	gfx_modelview_mul(&scratch_mtx);
}

void gfx_modelview_scaleq(const q32 scale[3]) {
	scratch_mtx = mtx_scalingq(scale);
	gfx_modelview_mul(&scratch_mtx);
}

void gfx_modelview_scale_byq(q32 scale) {
	scratch_mtx = (ShortMatrix) {
		.m = {{scale, 0, 0}, {0, scale, 0}, {0, 0, scale}},
		.t = {0, 0, 0}
	};
	gfx_modelview_mul(&scratch_mtx);
}
