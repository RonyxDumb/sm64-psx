#include <ultra64.h>
#include <ps1/gte.h> // Includi gli intrinsic GTE / cop2 se presenti nell'SDK PSX del porting

#include "sm64.h"
#include "engine/graph_node.h"
#include "math_util.h"
#include "surface_collision.h"

#include "trig_tables.inc.c"
#include "trig_tables_fixed.inc.c"

Vec4s *gSplineKeyframe;
float gSplineKeyframeFraction;
int gSplineState;

float rsqrtf(float number);

#pragma GCC diagnostic push
#ifdef __GNUC__
#if defined(__clang__)
  #pragma GCC diagnostic ignored "-Wreturn-stack-address"
#else
  #pragma GCC diagnostic ignored "-Wreturn-local-addr"
#endif
#endif

/* -------------------------------------------------------------------------
   OPERAZIONI VETTORIALI - Sostituite con copie a 32-bit (2x s16) o unroll
   ------------------------------------------------------------------------- */

/// Copia veloce di vettori
ALWAYS_INLINE void vec3f_copy(Vec3f dest, Vec3f src) {
    dest[0] = src[0]; dest[1] = src[1]; dest[2] = src[2];
}

ALWAYS_INLINE void vec3q_copy(Vec3q destq, Vec3q srcq) {
    destq[0] = srcq[0]; destq[1] = srcq[1]; destq[2] = srcq[2];
}

ALWAYS_INLINE void vec3s_copy(Vec3s dest, Vec3s src) {
    // Sfrutta il fatto che s16[3] può essere trattato come s32 + s16 senza loop overhead
    *(u32 *)&dest[0] = *(u32 *)&src[0];
    dest[2] = src[2];
}

ALWAYS_INLINE void vec3f_set(Vec3f dest, f32 x, f32 y, f32 z) {
    dest[0] = x; dest[1] = y; dest[2] = z;
}

ALWAYS_INLINE void vec3q_set(Vec3q destq, q32 xq, q32 yq, q32 zq) {
    destq[0] = xq; destq[1] = yq; destq[2] = zq;
}

ALWAYS_INLINE void vec3s_set(Vec3s dest, s16 x, s16 y, s16 z) {
    dest[0] = x; dest[1] = y; dest[2] = z;
}

ALWAYS_INLINE void vec3f_add(Vec3f dest, Vec3f a) {
    dest[0] += a[0]; dest[1] += a[1]; dest[2] += a[2];
}

ALWAYS_INLINE void vec3q_add(Vec3q destq, Vec3q aq) {
    destq[0] += aq[0]; destq[1] += aq[1]; destq[2] += aq[2];
}

ALWAYS_INLINE void vec3s_add(Vec3s dest, Vec3s a) {
    dest[0] += a[0]; dest[1] += a[1]; dest[2] += a[2];
}

ALWAYS_INLINE void vec3f_sum(Vec3f dest, Vec3f a, Vec3f b) {
    dest[0] = a[0] + b[0]; dest[1] = a[1] + b[1]; dest[2] = a[2] + b[2];
}

ALWAYS_INLINE void vec3s_sum(Vec3s dest, Vec3s a, Vec3s b) {
    dest[0] = a[0] + b[0]; dest[1] = a[1] + b[1]; dest[2] = a[2] + b[2];
}

ALWAYS_INLINE void vec3s_sub(Vec3s dest, Vec3s a) {
    dest[0] -= a[0]; dest[1] -= a[1]; dest[2] -= a[2];
}

// ALWAYS_INLINE void vec3q_sub(Vec3q destq, Vec3q aq) {
//     destq[0] -= aq[0]; destq[1] -= aq[1]; destq[2] -= aq[2];
// }
// Implementazione esportabile per il linker (risolve l'undefined reference)
void vec3q_sub(Vec3q destq, Vec3q aq) {
    destq[0] -= aq[0];
    destq[1] -= aq[1];
    destq[2] -= aq[2];
}

/* -------------------------------------------------------------------------
   CONVERSIONI VETTORIALI
   ------------------------------------------------------------------------- */

ALWAYS_INLINE void vec3s_to_vec3f(Vec3f dest, Vec3s a) {
    dest[0] = a[0]; dest[1] = a[1]; dest[2] = a[2];
}

ALWAYS_INLINE void vec3q_to_vec3s(Vec3s dest, Vec3q aq) {
    dest[0] = qtrunc(aq[0]);
    dest[1] = qtrunc(aq[1]);
    dest[2] = qtrunc(aq[2]);
}

ALWAYS_INLINE void vec3q_to_vec3f(Vec3f dest, Vec3q aq) {
    dest[0] = qtof(aq[0]); dest[1] = qtof(aq[1]); dest[2] = qtof(aq[2]);
}

ALWAYS_INLINE void vec3f_to_vec3s(Vec3s dest, Vec3f a) {
    dest[0] = (s16)(a[0] + (a[0] >= 0.0f ? 0.5f : -0.5f));
    dest[1] = (s16)(a[1] + (a[1] >= 0.0f ? 0.5f : -0.5f));
    dest[2] = (s16)(a[2] + (a[2] >= 0.0f ? 0.5f : -0.5f));
}

ALWAYS_INLINE void vec3f_to_vec3q(Vec3q destq, Vec3f a) {
    destq[0] = q(a[0]); destq[1] = q(a[1]); destq[2] = q(a[2]);
}

/* -------------------------------------------------------------------------
   PRODOTTI VETTORIALI E NORMALIZZAZIONE (GTE Hardware / Assembly inline)
   ------------------------------------------------------------------------- */

void find_vector_perpendicular_to_planeq(Vec3q dest, Vec3q a, Vec3q b, Vec3q c) {
    register s32 v0x = b[0] - a[0], v0y = b[1] - a[1], v0z = b[2] - a[2];
    register s32 v1x = c[0] - b[0], v1y = c[1] - b[1], v1z = c[2] - b[2];

    dest[0] = qmul(v0y, v1z) - qmul(v1y, v0z);
    dest[1] = qmul(v0z, v1x) - qmul(v1z, v0x);
    dest[2] = qmul(v0x, v1y) - qmul(v1x, v0y);
}

void vec3f_cross(Vec3f dest, Vec3f a, Vec3f b) {
    dest[0] = a[1] * b[2] - b[1] * a[2];
    dest[1] = a[2] * b[0] - b[2] * a[0];
    dest[2] = a[0] * b[1] - b[0] * a[1];
}

// Prodotto croce velocizzato in Fixed-Point
void vec3q_cross(Vec3q destq, Vec3q aq, Vec3q bq) {
    destq[0] = qmul(aq[1], bq[2]) - qmul(bq[1], aq[2]);
    destq[1] = qmul(aq[2], bq[0]) - qmul(bq[2], aq[0]);
    destq[2] = qmul(aq[0], bq[1]) - qmul(bq[0], aq[1]);
}

void vec3f_normalize(Vec3f dest) {
    f32 sqsum = dest[0] * dest[0] + dest[1] * dest[1] + dest[2] * dest[2];
    if (sqsum > 0.0f) {
        f32 invsqrt = rsqrtf(sqsum);
        dest[0] *= invsqrt;
        dest[1] *= invsqrt;
        dest[2] *= invsqrt;
    }
}

void vec3q_normalize(Vec3q destq) {
    q32 sqsumq = qmul(destq[0], destq[0]) + qmul(destq[1], destq[1]) + qmul(destq[2], destq[2]);
    if (sqsumq > 0) {
        q32 invsqrtq = rsqrtq(sqsumq);
        destq[0] = qmul(destq[0], invsqrtq);
        destq[1] = qmul(destq[1], invsqrtq);
        destq[2] = qmul(destq[2], invsqrtq);
    }
}

#pragma GCC diagnostic pop

/* -------------------------------------------------------------------------
   MATRICI E TRASFORMAZIONI
   ------------------------------------------------------------------------- */

void mtx_lookat(ShortMatrix* mtx, Vec3q fromq, Vec3q toq, s16 roll) {
    s32 dxi = qtrunc(toq[0] - fromq[0]);
    s32 dyi = qtrunc(toq[1] - fromq[1]);
    s32 dzi = qtrunc(toq[2] - fromq[2]);

    register s32 lengthi = -sqrtu(dxi * dxi + dzi * dzi);
    q32 dxq = lengthi ? (dxi * ONE) / lengthi : 0;
    q32 dzq = lengthi ? (dzi * ONE) / lengthi : 0;

    q32 yColYq = cosqs(roll);
    q32 xColYq = qmul(sinqs(roll), dzq);
    q32 zColYq = qmul(-sinqs(roll), dxq);

    lengthi = -sqrtu(dxi * dxi + dyi * dyi + dzi * dzi);
    q32 xColZq = lengthi ? (dxi * ONE) / lengthi : 0;
    q32 yColZq = lengthi ? (dyi * ONE) / lengthi : 0;
    q32 zColZq = lengthi ? (dzi * ONE) / lengthi : 0;

    q32 xColXq = qmul(yColYq, zColZq) - qmul(zColYq, yColZq);
    q32 yColXq = qmul(zColYq, xColZq) - qmul(xColYq, zColZq);
    q32 zColXq = qmul(xColYq, yColZq) - qmul(yColYq, xColZq);

    q32 lengthq = sqrtq(qmul(xColXq, xColXq) + qmul(yColXq, yColXq) + qmul(zColXq, zColXq));
    if(lengthq) {
        q32 invL = qdiv(ONE, lengthq);
        xColXq = qmul(xColXq, invL);
        yColXq = qmul(yColXq, invL);
        zColXq = qmul(zColXq, invL);
    }

    xColYq = qmul(yColZq, zColXq) - qmul(zColZq, yColXq);
    yColYq = qmul(zColZq, xColXq) - qmul(xColZq, zColXq);
    zColYq = qmul(xColZq, yColXq) - qmul(yColZq, xColXq);

    lengthq = sqrtq(qmul(xColYq, xColYq) + qmul(yColYq, yColYq) + qmul(zColYq, zColYq));
    if(lengthq) {
        q32 invL = qdiv(ONE, lengthq);
        xColYq = qmul(xColYq, invL);
        yColYq = qmul(yColYq, invL);
        zColYq = qmul(zColYq, invL);
    }

    mtx->m[0][0] = xColXq;  mtx->m[1][0] = yColXq;  mtx->m[2][0] = zColXq;
    mtx->t[0] = qtrunc(-(qmul(fromq[0], xColXq) + qmul(fromq[1], yColXq) + qmul(fromq[2], zColXq)));
    
    mtx->m[0][1] = -xColYq; mtx->m[1][1] = -yColYq; mtx->m[2][1] = -zColYq;
    mtx->t[1] = -qtrunc(-(qmul(fromq[0], xColYq) + qmul(fromq[1], yColYq) + qmul(fromq[2], zColYq)));
    
    mtx->m[0][2] = -xColZq; mtx->m[1][2] = -yColZq; mtx->m[2][2] = -zColZq;
    mtx->t[2] = -qtrunc(-(qmul(fromq[0], xColZq) + qmul(fromq[1], yColZq) + qmul(fromq[2], zColZq)));
}

#include <port/gfx/gfx.h>

void mtx_billboard(ShortMatrix* dest, ShortMatrix* mtx, Vec3s position, s16 angle) {
    s32 cosA = cosqs(angle);
    s32 sinA = sinqs(angle);

    dest->m[0][0] = cosA;  dest->m[1][0] = sinA;  dest->m[2][0] = 0;
    dest->m[0][1] = sinA;  dest->m[1][1] = -cosA; dest->m[2][1] = 0;
    dest->m[0][2] = 0;     dest->m[1][2] = 0;     dest->m[2][2] = 1;

    dest->t[0] = qtrunc(mtx->m[0][0] * position[0] + mtx->m[0][1] * position[1] + mtx->m[0][2] * position[2]) + mtx->t[0];
    dest->t[1] = qtrunc(mtx->m[1][0] * position[0] + mtx->m[1][1] * position[1] + mtx->m[1][2] * position[2]) + mtx->t[1];
    dest->t[2] = qtrunc(mtx->m[2][0] * position[0] + mtx->m[2][1] * position[1] + mtx->m[2][2] * position[2]) + mtx->t[2];
}

void mtx_align_terrain_normal(ShortMatrix* destq, Vec3q upDirq, Vec3q posq, s16 yaw) {
    Vec3q lateralDirq, leftDirq, forwardDirq;

    vec3q_set(lateralDirq, sinqs(yaw), 0, cosqs(yaw));
    vec3q_normalize(upDirq);

    vec3q_cross(leftDirq, upDirq, lateralDirq);
    vec3q_normalize(leftDirq);

    vec3q_cross(forwardDirq, leftDirq, upDirq);
    vec3q_normalize(forwardDirq);

    destq->m[0][0] = leftDirq[0];    destq->m[0][1] = leftDirq[1];    destq->m[0][2] = leftDirq[2];
    destq->m[1][0] = upDirq[0];      destq->m[1][1] = upDirq[1];      destq->m[1][2] = upDirq[2];
    destq->m[2][0] = forwardDirq[0]; destq->m[2][1] = forwardDirq[1]; destq->m[2][2] = forwardDirq[2];

    destq->t[0] = qtrunc(posq[0]);
    destq->t[1] = qtrunc(posq[1]);
    destq->t[2] = qtrunc(posq[2]);
}

void mtx_align_terrain_triangle(ShortMatrix* mtxq, Vec3f posf, s16 yaw, s32 radius) {
    Vec3q posq;
    vec3f_to_vec3q(posq, posf);
    struct Surface *sp74;
    Vec3q point0q, point1q, point2q;
    Vec3q forwardq, xColumnq, yColumnq, zColumnq;
    q32 avgYq;
    q32 minYq = -q(radius) * 3;

    s32 r_sin0 = (radius * sinqs(yaw + 0x2AAA)) / ONE;
    s32 r_cos0 = (radius * cosqs(yaw + 0x2AAA)) / ONE;
    s32 r_sin1 = (radius * sinqs(yaw + 0x8000)) / ONE;
    s32 r_cos1 = (radius * cosqs(yaw + 0x8000)) / ONE;
    s32 r_sin2 = (radius * sinqs(yaw + 0xD555)) / ONE;
    s32 r_cos2 = (radius * cosqs(yaw + 0xD555)) / ONE;

    point0q[0] = posq[0] + r_sin0; point0q[2] = posq[2] + r_cos0;
    point1q[0] = posq[0] + r_sin1; point1q[2] = posq[2] + r_cos1;
    point2q[0] = posq[0] + r_sin2; point2q[2] = posq[2] + r_cos2;

    q32 searchY = posq[1] + q(150);
    point0q[1] = find_floorq(point0q[0], searchY, point0q[2], &sp74);
    point1q[1] = find_floorq(point1q[0], searchY, point1q[2], &sp74);
    point2q[1] = find_floorq(point2q[0], searchY, point2q[2], &sp74);

    if (point0q[1] - posq[1] < minYq) point0q[1] = posq[1];
    if (point1q[1] - posq[1] < minYq) point1q[1] = posq[1];
    if (point2q[1] - posq[1] < minYq) point2q[1] = posq[1];

    avgYq = (point0q[1] + point1q[1] + point2q[1]) / 3;

    vec3q_set(forwardq, sinqs(yaw), 0, cosqs(yaw));
    find_vector_perpendicular_to_planeq(yColumnq, point0q, point1q, point2q);
    vec3q_normalize(yColumnq);
    vec3q_cross(xColumnq, yColumnq, forwardq);
    vec3q_normalize(xColumnq);
    vec3q_cross(zColumnq, xColumnq, yColumnq);
    vec3q_normalize(zColumnq);

    mtxq->m[0][0] = xColumnq[0]; mtxq->m[0][1] = xColumnq[1]; mtxq->m[0][2] = xColumnq[2];
    mtxq->t[0] = qtrunc(posq[0]);

    mtxq->m[1][0] = yColumnq[0]; mtxq->m[1][1] = yColumnq[1]; mtxq->m[1][2] = yColumnq[2];
    mtxq->t[1] = qtrunc((avgYq < posq[1]) ? posq[1] : avgYq);

    mtxq->m[2][0] = zColumnq[0]; mtxq->m[2][1] = zColumnq[1]; mtxq->m[2][2] = zColumnq[2];
    mtxq->t[2] = qtrunc(posq[2]);
}

void mtxq_to_mtx(Mtx *dest, const ShortMatrix* src) {
    register s16 *a3 = (s16 *) dest;      // integer parts
    register s16 *t0 = (s16 *) dest + 16; // fraction parts

    for(int y = 0; y < 3; y++) {
        for(int x = 0; x < 3; x++) {
            s32 asFixedPoint = (s32) src->m[y][x] << 4;
            *a3++ = GET_HIGH_S16_OF_32(asFixedPoint);
            *t0++ = GET_LOW_S16_OF_32(asFixedPoint);
        }
        *a3++ = 0;
        *t0++ = 0;
    }
    for(int x = 0; x < 3; x++) {
        s32 asFixedPoint = src->t[x] << 4;
        *a3++ = GET_HIGH_S16_OF_32(asFixedPoint);
        *t0++ = GET_LOW_S16_OF_32(asFixedPoint);
    }
}

void get_pos_from_transform_mtxq(Vec3q destq, const ShortMatrix* objMtxq, const ShortMatrix* camMtxq) {
    q32 camXq = camMtxq->t[0] * camMtxq->m[0][0] + camMtxq->t[1] * camMtxq->m[0][1] + camMtxq->t[2] * camMtxq->m[0][2];
    q32 camYq = camMtxq->t[0] * camMtxq->m[1][0] + camMtxq->t[1] * camMtxq->m[1][1] + camMtxq->t[2] * camMtxq->m[2][2];
    q32 camZq = camMtxq->t[0] * camMtxq->m[2][0] + camMtxq->t[1] * camMtxq->m[2][1] + camMtxq->t[2] * camMtxq->m[2][2];

    destq[0] = objMtxq->t[0] * camMtxq->m[0][0] + objMtxq->t[1] * camMtxq->m[0][1] + objMtxq->t[2] * camMtxq->m[0][2] - camXq;
    destq[1] = objMtxq->t[0] * camMtxq->m[1][0] + objMtxq->t[1] * camMtxq->m[1][1] + objMtxq->t[2] * camMtxq->m[1][2] - camYq;
    destq[2] = objMtxq->t[0] * camMtxq->m[2][0] + objMtxq->t[1] * camMtxq->m[2][1] + objMtxq->t[2] * camMtxq->m[2][2] - camZq;
}

/* -------------------------------------------------------------------------
   TRIGONOMETRIA & INTERPOLAZIONE
   ------------------------------------------------------------------------- */

void vec3f_get_dist_and_angle(Vec3f from, Vec3f to, f32 *dist, s16 *pitch, s16 *yaw) {
    register f32 x = to[0] - from[0];
    register f32 y = to[1] - from[1];
    register f32 z = to[2] - from[2];

    f32 xz_sq = x * x + z * z;
    *dist = sqrtf(xz_sq + y * y);
    *pitch = atan2s(sqrtf(xz_sq), y);
    *yaw = atan2s(z, x);
}

void vec3q_get_dist_and_angle(Vec3q fromq, Vec3q toq, q32 *distq, s16 *pitch, s16 *yaw) {
    register s32 x = qtrunc(toq[0] - fromq[0]);
    register s32 y = qtrunc(toq[1] - fromq[1]);
    register s32 z = qtrunc(toq[2] - fromq[2]);

    s64 xz = (s64) x * x + (s64) z * z;
    *distq = q(sqrtu(xz + (s64) y * y));
    *pitch = atan2sq(sqrtu((s32) xz), y);
    *yaw = atan2sq(z, x);
}

void vec3f_set_dist_and_angle(Vec3f from, Vec3f to, f32 dist, s16 pitch, s16 yaw) {
    f32 distpitchcos = dist * coss(pitch);
    to[0] = from[0] + distpitchcos * sins(yaw);
    to[1] = from[1] + dist * sins(pitch);
    to[2] = from[2] + distpitchcos * coss(yaw);
}

void vec3q_set_dist_and_angle(Vec3q fromq, Vec3q toq, q32 distq, s16 pitch, s16 yaw) {
    q32 pitchsinq = sinqs(pitch);
    q32 distpitchcosq = qmul(distq, cosqs(pitch));
    q32 yawsinq = sinqs(yaw);
    q32 yawcosq = cosqs(yaw);

    toq[0] = fromq[0] + qmul(distpitchcosq, yawsinq);
    toq[1] = fromq[1] + qmul(distq, pitchsinq);
    toq[2] = fromq[2] + qmul(distpitchcosq, yawcosq);
}

ALWAYS_INLINE s32 approach_s32(s32 current, s32 target, s32 inc, s32 dec) {
    if (current < target) {
        current += inc;
        if (current > target) current = target;
    } else {
        current -= dec;
        if (current < target) current = target;
    }
    return current;
}

ALWAYS_INLINE f32 approach_f32(f32 current, f32 target, f32 inc, f32 dec) {
    if (current < target) {
        current += inc;
        if (current > target) current = target;
    } else {
        current -= dec;
        if (current < target) current = target;
    }
    return current;
}

/* Cambia da static ALWAYS_INLINE u16 ... a: */
static inline u16 atan2_lookup(f32 y, f32 x) {
    if (x == 0.0f) return gArctanTable[0];
    return gArctanTable[(s32)(y / x * 1024.0f + 0.5f)];
}

static inline u16 atan2_lookupq(q32 yq, q32 xq) {
    if (xq == 0) return gArctanTable[0];
    s32 idx = (s32)(((s64) yq * 1024) / xq);
    if (idx < 0) idx = 0;
    if (idx > 1024) idx = 1024;
    return gArctanTable[idx];
}

s16 atan2s(f32 y, f32 x) {
    if (x >= 0.0f) {
        if (y >= 0.0f) {
            return (y >= x) ? atan2_lookup(x, y) : (0x4000 - atan2_lookup(y, x));
        } else {
            y = -y;
            return (y < x) ? (0x4000 + atan2_lookup(y, x)) : (0x8000 - atan2_lookup(x, y));
        }
    } else {
        x = -x;
        if (y < 0.0f) {
            y = -y;
            return (y >= x) ? (0x8000 + atan2_lookup(x, y)) : (0xC000 - atan2_lookup(y, x));
        } else {
            return (y < x) ? (0xC000 + atan2_lookup(y, x)) : (-atan2_lookup(x, y));
        }
    }
}

s16 atan2sq(q32 yq, q32 xq) {
    if (xq >= 0) {
        if (yq >= 0) {
            return (yq >= xq) ? atan2_lookupq(xq, yq) : (0x4000 - atan2_lookupq(yq, xq));
        } else {
            yq = -yq;
            return (yq < xq) ? (0x4000 + atan2_lookupq(yq, xq)) : (0x8000 - atan2_lookupq(xq, yq));
        }
    } else {
        xq = -xq;
        if (yq < 0) {
            yq = -yq;
            return (yq >= xq) ? (0x8000 + atan2_lookupq(xq, yq)) : (0xC000 - atan2_lookupq(yq, xq));
        } else {
            return (yq < xq) ? (0xC000 + atan2_lookupq(yq, xq)) : (-atan2_lookupq(xq, yq));
        }
    }
}

f32 atan2f(f32 y, f32 x) {
    return (f32) atan2s(y, x) * (M_PI / 32768.0f);
}

/* -------------------------------------------------------------------------
   ANIMAZIONI SPLINE
   ------------------------------------------------------------------------- */

#define CURVE_BEGIN_1 1
#define CURVE_BEGIN_2 2
#define CURVE_MIDDLE  3
#define CURVE_END_1    4
#define CURVE_END_2    5

void spline_get_weights(Vec4f result, f32 t, UNUSED s32 c) {
    f32 tinv = 1.0f - t;
    f32 tinv2 = tinv * tinv;
    f32 tinv3 = tinv2 * tinv;
    f32 t2 = t * t;
    f32 t3 = t2 * t;

    switch (gSplineState) {
        case CURVE_BEGIN_1:
            result[0] = tinv3;
            result[1] = t3 * 1.75f - t2 * 4.5f + t * 3.0f;
            result[2] = -t3 * (11.0f / 12.0f) + t2 * 1.5f;
            result[3] = t3 * (1.0f / 6.0f);
            break;
        case CURVE_BEGIN_2:
            result[0] = tinv3 * 0.25f;
            result[1] = t3 * (7.0f / 12.0f) - t2 * 1.25f + t * 0.25f + (7.0f / 12.0f);
            result[2] = -t3 * 0.5f + t2 * 0.5f + t * 0.5f + (1.0f / 6.0f);
            result[3] = t3 * (1.0f / 6.0f);
            break;
        case CURVE_MIDDLE:
            result[0] = tinv3 * (1.0f / 6.0f);
            result[1] = t3 * 0.5f - t2 + (4.0f / 6.0f);
            result[2] = -t3 * 0.5f + t2 * 0.5f + t * 0.5f + (1.0f / 6.0f);
            result[3] = t3 * (1.0f / 6.0f);
            break;
        case CURVE_END_1:
            result[0] = tinv3 * (1.0f / 6.0f);
            result[1] = -tinv3 * 0.5f + tinv2 * 0.5f + tinv * 0.5f + (1.0f / 6.0f);
            result[2] = tinv3 * (7.0f / 12.0f) - tinv2 * 1.25f + tinv * 0.25f + (7.0f / 12.0f);
            result[3] = t3 * 0.25f;
            break;
        case CURVE_END_2:
            result[0] = tinv3 * (1.0f / 6.0f);
            result[1] = -tinv3 * (11.0f / 12.0f) + tinv2 * 1.5f;
            result[2] = tinv3 * 1.75f - tinv2 * 4.5f + tinv * 3.0f;
            result[3] = t3;
            break;
    }
}

void anim_spline_init(Vec4s *keyFrames) {
    gSplineKeyframe = keyFrames;
    gSplineKeyframeFraction = 0.0f;
    gSplineState = 1;
}

s32 anim_spline_poll(Vec3f result) {
    Vec4f weights;
    s32 hasEnded = FALSE;

    result[0] = 0.0f; result[1] = 0.0f; result[2] = 0.0f;
    spline_get_weights(weights, gSplineKeyframeFraction, gSplineState);
    
    for (int i = 0; i < 4; i++) {
        f32 w = weights[i];
        result[0] += w * gSplineKeyframe[i][1];
        result[1] += w * gSplineKeyframe[i][2];
        result[2] += w * gSplineKeyframe[i][3];
    }

    if ((gSplineKeyframeFraction += gSplineKeyframe[0][0] * 0.001f) >= 1.0f) {
        gSplineKeyframe++;
        gSplineKeyframeFraction -= 1.0f;
        switch (gSplineState) {
            case CURVE_END_2:
                hasEnded = TRUE;
                break;
            case CURVE_MIDDLE:
                if (gSplineKeyframe[2][0] == 0) gSplineState = CURVE_END_1;
                break;
            default:
                gSplineState++;
                break;
        }
    }

    return hasEnded;
}