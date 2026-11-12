#pragma once

#include <stdint.h>

/* PS1 cinematic path: Q16 cubic interpolation. No software floating point
 * in weight evaluation; 1000-tick progress is converted to Q16 only once. */
#define PSX_SPLINE_ONE 65536

static inline int32_t psx_spline_mul_q16(int32_t a, int32_t b) {
    return (int32_t) (((int64_t) a * b)  >> 16);
}

static inline void psx_spline_weights_q16(int state, int progress_milli, int32_t result[4]) {
    if(progress_milli < 0) progress_milli = 0;
    if(progress_milli > 1000) progress_milli = 1000;

    int32_t t = progress_milli * PSX_SPLINE_ONE / 1000;
    int32_t inv = PSX_SPLINE_ONE - t;
    int32_t t2 = psx_spline_mul_q16(t, t);
    int32_t t3 = psx_spline_mul_q16(t2, t);
    int32_t inv2 = psx_spline_mul_q16(inv, inv);
    int32_t inv3 = psx_spline_mul_q16(inv2, inv);

    switch(state) {
        case 1: /* CURVE_BEGIN_1 */
            result[0] = inv3;
            result[1] = (21*t3 - 54*t2 + 36*t) / 12;
            result[2] = (-11*t3 + 18*t2) / 12;
            result[3] = t3 / 6;
            break;
        case 2: /* CURVE_BEGIN_2 */
            result[0] = inv3 / 4;
            result[1] = (7*t3 - 15*t2 + 3*t + 7*PSX_SPLINE_ONE) / 12;
            result[2] = (-6*t3 + 6*t2 + 6*t + 2*PSX_SPLINE_ONE) / 12;
            result[3] = t3 / 6;
            break;
        case 3: /* CURVE_MIDDLE */
            result[0] = inv3 / 6;
            result[1] = (6*t3 - 12*t2 + 8*PSX_SPLINE_ONE) / 12;
            result[2] = (-6*t3 + 6*t2 + 6*t + 2*PSX_SPLINE_ONE) / 12;
            result[3] = t3 / 6;
            break;
        case 4: /* CURVE_END_1 */
            result[0] = inv3 / 6;
            result[1] = (-6*inv3 + 6*inv2 + 6*inv + 2*PSX_SPLINE_ONE) / 12;
            result[2] = (7*inv3 - 15*inv2 + 3*inv + 7*PSX_SPLINE_ONE) / 12;
            result[3] = t3 / 4;
            break;
        case 5: /* CURVE_END_2 */
            result[0] = inv3 / 6;
            result[1] = (-11*inv3 + 18*inv2) / 12;
            result[2] = (21*inv3 - 54*inv2 + 36*inv) / 12;
            result[3] = t3;
            break;
        default:
            result[0] = PSX_SPLINE_ONE;
            result[1] = result[2] = result[3] = 0;
            return;
    }

    /* Compensate for independent Q16 truncation so the interpolation retains
     * constant trajectories exactly, including across state transitions. */
    int32_t sum = 0;
    int largest = 0;
    for(int i = 0; i < 4; ++i) {
        if(result[i] < 0) result[i] = 0;
        if(result[i] > PSX_SPLINE_ONE) result[i] = PSX_SPLINE_ONE;
        if(result[i] > result[largest]) largest = i;
        sum += result[i];
    }
    result[largest] += PSX_SPLINE_ONE - sum;
}
