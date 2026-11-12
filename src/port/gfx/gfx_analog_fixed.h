#pragma once

#include <stdint.h>

/* The PS1 controller is sampled every frame. Calculate deadzone, magnitude,
 * and circular clamping in signed Q20.12; only convert at the legacy float
 * Controller API boundary. The runtime already supplies integer sqrtu64(). */
uint32_t sqrtu64(uint64_t value);

static inline void psx_adjust_analog_q12(int raw_x, int raw_y,
                                         int32_t* xq, int32_t* yq, int32_t* magq) {
    int32_t x = raw_x <= -8 ? raw_x + 6 : (raw_x >= 8 ? raw_x - 6 : 0);
    int32_t y = raw_y <= -8 ? raw_y + 6 : (raw_y >= 8 ? raw_y - 6 : 0);
    *xq = x * 4096;
    *yq = y * 4096;
    uint64_t squared = (uint64_t)(x * x + y * y) << 24;
    *magq = (int32_t)sqrtu64(squared);

    if(*magq < 8 * 4096) {
        *magq = 0;
    } else if(*magq > 64 * 4096) {
        *xq = (int32_t)((int64_t)x * 64 * 4096 * 4096 / *magq);
        *yq = (int32_t)((int64_t)y * 64 * 4096 * 4096 / *magq);
        *magq = 64 * 4096;
    }
}
