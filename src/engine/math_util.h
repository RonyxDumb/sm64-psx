#ifndef MATH_UTIL_H
#define MATH_UTIL_H

#include <PR/ultratypes.h>

#include "types.h"

/*
 * The sine and cosine tables overlap, but "#define gCosineTable (gSineTable +
 * 0x400)" doesn't give expected codegen; gSineTable and gCosineTable need to
 * be different symbols for code to match. Most likely the tables were placed
 * adjacent to each other, and gSineTable cut short, such that reads overflow
 * into gCosineTable.
 *
 * These kinds of out of bounds reads are undefined behavior, and break on
 * e.g. GCC (which doesn't place the tables next to each other, and probably
 * exploits array sizes for range analysis-based optimizations as well).
 * Thus, for non-IDO compilers we use the standard-compliant version.
 */
#ifdef USE_FLOATS
extern f32 gSineTable[];
#endif
extern q32 gSineTableq[];
#ifdef USE_FLOATS
#ifdef AVOID_UB
#define gCosineTable (gSineTable + 0x400)
#else
extern f32 gCosineTable[];
#endif
#endif
#define gCosineTableq (gSineTableq + 0x400)

#ifdef USE_FLOATS
#define sins(x) gSineTable[(u16) (x) >> 4]
#define coss(x) gCosineTable[(u16) (x) >> 4]
#define sinqs(x) q(sins(x))
#define cosqs(x) q(coss(x))
#else
#define sins(x) qtof(gSineTableq[(u16) (x) >> 4])
#define coss(x) qtof(gCosineTableq[(u16) (x) >> 4])
#define sinqs(x) gSineTableq[(u16) (x) >> 4]
#define cosqs(x) gCosineTableq[(u16) (x) >> 4]
#endif

#define min(a, b) ((a) <= (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

#define sqr(x) ((x) * (x))

void vec3f_copy(Vec3f dest, Vec3f src);
void vec3q_copy(Vec3q destq, Vec3q srcq);
void vec3f_set(Vec3f dest, f32 x, f32 y, f32 z);
void vec3q_set(Vec3q destq, q32 xq, q32 yq, q32 zq);
void vec3f_add(Vec3f dest, Vec3f a);
void vec3q_add(Vec3q destq, Vec3q aq);
void vec3f_sum(Vec3f dest, Vec3f a, Vec3f b);
void vec3s_copy(Vec3s dest, Vec3s src);
void vec3s_set(Vec3s dest, s16 x, s16 y, s16 z);
void vec3s_add(Vec3s dest, Vec3s a);
void vec3s_sum(Vec3s dest, Vec3s a, Vec3s b);
void vec3s_sub(Vec3s dest, Vec3s a);
void vec3s_to_vec3f(Vec3f dest, Vec3s a);
void vec3q_to_vec3s(Vec3s dest, Vec3q aq);
void vec3f_to_vec3s(Vec3s dest, Vec3f a);
void vec3f_to_vec3q(Vec3q destq, Vec3f a);
void vec3q_to_vec3f(Vec3f dest, Vec3q aq);
void find_vector_perpendicular_to_planeq(Vec3q dest, Vec3q a, Vec3q b, Vec3q c);
void vec3f_cross(Vec3f dest, Vec3f a, Vec3f b);
void vec3q_cross(Vec3q destq, Vec3q aq, Vec3q bq);
void vec3f_normalize(Vec3f dest);
void vec3q_normalize(Vec3q destq);
void mtx_lookat(ShortMatrix* mtx, Vec3q fromq, Vec3q toq, s16 roll);
void mtx_billboard(ShortMatrix* dest, ShortMatrix* mtx, Vec3s position, s16 angle);
void mtx_align_terrain_normal(ShortMatrix* destq, Vec3q upDirq, Vec3q posq, s16 yaw);
void mtx_align_terrain_triangle(ShortMatrix* mtx, Vec3f pos, s16 yaw, s32 radius);
void mtxq_to_mtx(Mtx *dest, const ShortMatrix* src);
void get_pos_from_transform_mtxq(Vec3q destq, const ShortMatrix* objMtxq, const ShortMatrix* camMtxq);
void vec3f_get_dist_and_angle(Vec3f from, Vec3f to, f32 *dist, s16 *pitch, s16 *yaw);
void vec3q_get_dist_and_angle(Vec3q fromq, Vec3q toq, q32 *distq, s16 *pitch, s16 *yaw);
void vec3f_set_dist_and_angle(Vec3f from, Vec3f to, f32  dist, s16  pitch, s16  yaw);
void vec3q_set_dist_and_angle(Vec3q fromq, Vec3q toq, q32 distq, s16  pitch, s16  yaw);
s32 approach_s32(s32 current, s32 target, s32 inc, s32 dec);
f32 approach_f32(f32 current, f32 target, f32 inc, f32 dec);
#define approach_q32(currentq, targetq, incq, decq) ((q32) approach_s32((s32) (currentq), (s32) (targetq), (s32) (incq), (s32) (decq)))
s16 atan2s(f32 y, f32 x);
f32 atan2f(f32 a, f32 b);
q32 atan2q(q32 aq, q32 bq);
void spline_get_weights(Vec4f result, f32 t, UNUSED s32 c);
void anim_spline_init(Vec4s *keyFrames);
s32 anim_spline_poll(Vec3f result);

u16 sqrtu(u32 x);
u32 sqrtu64(u64 x);
float rsqrtf(float x);
s16 atan2sq(q32 xq, q32 yq);
q32 sqrtq(q32 xq);
q32 sqrtq64(q64 xq);
q32 rsqrtq(q32 xq);
q32 sinq(q32);
q32 cosq(q32);
#ifdef M_PI
#undef M_PI
#endif
#define M_PI 3.14159265359f
#define M_PIq (q(3.14159265359))

#endif // MATH_UTIL_H
