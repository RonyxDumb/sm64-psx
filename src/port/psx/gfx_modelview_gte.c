#include <port/gfx/gfx_internal.h>
#include <engine/math_util.h>
#include <ps1/cop0.h>
#include <ps1/gte.h>

q32 vec_distance_sq(const ShortVec* v0, const ShortVec* v1) {
	gte_setDataReg(GTE_IR0, 1);
	gte_setDataReg(GTE_IR1, v0->vx_vy);
	gte_setDataReg(GTE_IR2, v0->vx_vy >> 16);
	gte_loadDataRegM(GTE_IR3, (const u32*) &v0->vz);
	gte_setDataReg(GTE_MAC1, -(s32) v1->vx);
	gte_setDataReg(GTE_MAC2, -(s32) v1->vy);
	gte_setDataReg(GTE_MAC3, -(s32) v1->vz);
	gte_commandNoNop(GTE_CMD_GPL);
	gte_commandNoNop(GTE_CMD_SQR | GTE_SF);
	return gte_getDataReg(GTE_MAC1) + gte_getDataReg(GTE_MAC2) + gte_getDataReg(GTE_MAC3);
}

void gfx_modelview_identity() {
	gte_setRotationMatrix(
		ONE, 0, 0,
		0, ONE, 0,
		0, 0, ONE
	);
	gte_setControlReg(GTE_TRX, 0);
	gte_setControlReg(GTE_TRY, 0);
	gte_setControlReg(GTE_TRZ, 0);
}

void gfx_modelview_set(const ShortMatrix* mtx) {
	gte_loadRotationMatrix((GTEMatrix*) mtx->m);
	gte_setControlReg(GTE_TRX, mtx->t[0]);
	gte_setControlReg(GTE_TRY, mtx->t[1]);
	gte_setControlReg(GTE_TRZ, mtx->t[2]);
}

ShortMatrix gfx_modelview_get(void) {
	ShortMatrix mtx;
	gte_storeRotationMatrix((GTEMatrix*) mtx.m);
	mtx.t[0] = gte_getControlReg(GTE_TRX);
	mtx.t[1] = gte_getControlReg(GTE_TRY);
	mtx.t[2] = gte_getControlReg(GTE_TRZ);
	return mtx;
}

void gfx_modelview_mul(const ShortMatrix* o) {
	gte_loadV0((GTEVector16*) o->m[0]);
	gte_commandAfterLoad(GTE_CMD_MVMVA | GTE_SF | GTE_V_V0 | GTE_MX_RT | GTE_CV_NONE);
	gte_setV1(o->m[1][0], o->m[1][1], o->m[1][2]);
	register u32 m00m01 = gte_getDataReg(GTE_IR1) & 0xFFFF;
	register u32 m02m10 = gte_getDataReg(GTE_IR2) << 16;
	register u32 m20m21 = gte_getDataReg(GTE_IR3) & 0xFFFF;
	gte_commandNoNop(GTE_CMD_MVMVA | GTE_SF | GTE_V_V1 | GTE_MX_RT | GTE_CV_NONE);
	gte_loadV2((GTEVector16*) o->m[2]);
	m00m01 |= gte_getDataReg(GTE_IR1) << 16;
	register u32 m11m12 = gte_getDataReg(GTE_IR2) & 0xFFFF;
	m20m21 |= gte_getDataReg(GTE_IR3) << 16;
	gte_commandNoNop(GTE_CMD_MVMVA | GTE_SF | GTE_V_V2 | GTE_MX_RT | GTE_CV_NONE);
	gte_loadV0((GTEVector16*) o->t);
	m02m10 |= gte_getDataReg(GTE_IR1) & 0xFFFF;
	m11m12 |= gte_getDataReg(GTE_IR2) << 16;
	register u32 m22 = gte_getDataReg(GTE_IR3);
	gte_commandNoNop(GTE_CMD_MVMVA | GTE_SF | GTE_V_V0 | GTE_MX_RT | GTE_CV_TR);
	gte_setControlReg(GTE_RT11RT12, m00m01);
	gte_setControlReg(GTE_RT13RT21, m02m10);
	gte_setControlReg(GTE_RT22RT23, m11m12);
	gte_setControlReg(GTE_RT31RT32, m20m21);
	gte_setControlReg(GTE_RT33, m22);
	gte_setControlReg(GTE_TRX, gte_getDataReg(GTE_IR1));
	gte_setControlReg(GTE_TRY, gte_getDataReg(GTE_IR2));
	gte_setControlReg(GTE_TRZ, gte_getDataReg(GTE_IR3));
}

ShortVec gfx_modelview_apply_without_translation(const ShortVec* v) {
	gte_loadV0((const GTEVector16*) v);
	gte_commandAfterLoad(GTE_CMD_MVMVA | GTE_SF | GTE_V_V0 | GTE_MX_RT);
	return (ShortVec) {
		.vx = gte_getDataReg(GTE_IR1),
		.vy = gte_getDataReg(GTE_IR2),
		.vz = gte_getDataReg(GTE_IR3)
	};
}

ShortVec gfx_modelview_apply(const ShortVec* v) {
	gte_loadV0((const GTEVector16*) v);
	gte_commandAfterLoad(GTE_CMD_MVMVA | GTE_SF | GTE_V_V0 | GTE_MX_RT | GTE_CV_TR);
	return (ShortVec) {
		.vx = gte_getDataReg(GTE_IR1),
		.vy = gte_getDataReg(GTE_IR2),
		.vz = gte_getDataReg(GTE_IR3)
	};
}

void gfx_modelview_translatei(const short off[3]) {
	gte_setV0(off[0], off[1], off[2]);
	gte_commandNoNop(GTE_CMD_MVMVA | GTE_SF | GTE_V_V0 | GTE_MX_RT | GTE_CV_TR);
	gte_setControlReg(GTE_TRX, gte_getDataReg(GTE_IR1));
	gte_setControlReg(GTE_TRY, gte_getDataReg(GTE_IR2));
	gte_setControlReg(GTE_TRZ, gte_getDataReg(GTE_IR3));
}

void gfx_modelview_get_rotation_into(ShortMatrix* mtx) {
	mtx->m32[0] = gte_getControlReg(GTE_RT11RT12);
	mtx->m32[1] = gte_getControlReg(GTE_RT13RT21);
	mtx->m32[2] = gte_getControlReg(GTE_RT22RT23);
	mtx->m32[3] = gte_getControlReg(GTE_RT31RT32);
	mtx->m32[4] = gte_getControlReg(GTE_RT33);
}

void gfx_modelview_get_translation_into(ShortMatrix* mtx) {
	mtx->t[0] = gte_getControlReg(GTE_TRX);
	mtx->t[1] = gte_getControlReg(GTE_TRY);
	mtx->t[2] = gte_getControlReg(GTE_TRZ);
}

void gfx_modelview_set_rotation_from(const ShortMatrix* mtx) {
	gte_setControlReg(GTE_RT11RT12, mtx->m32[0]);
	gte_setControlReg(GTE_RT13RT21, mtx->m32[1]);
	gte_setControlReg(GTE_RT22RT23, mtx->m32[2]);
	gte_setControlReg(GTE_RT31RT32, mtx->m32[3]);
	gte_setControlReg(GTE_RT33, mtx->m32[4]);
}

void gfx_modelview_set_translation_from(const ShortMatrix* mtx) {
	gte_setControlReg(GTE_TRX, mtx->t[0]);
	gte_setControlReg(GTE_TRY, mtx->t[1]);
	gte_setControlReg(GTE_TRZ, mtx->t[2]);
}
