#include "src/port/gfx/gfx_analog_fixed.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

/* Test-only oracle; the PS1 links its existing integer sqrtu64. */
uint32_t sqrtu64(uint64_t value) {
    uint64_t lo = 0, hi = 1u << 24;
    while(lo + 1 < hi) {
        uint64_t mid = (lo + hi) / 2;
        if(mid * mid <= value) lo = mid;
        else hi = mid;
    }
    return (uint32_t)lo;
}

int main(void) {
    double max_err = 0;
    for(int x = -128; x <= 127; ++x) {
        for(int y = -128; y <= 127; ++y) {
            int32_t xq, yq, mq;
            psx_adjust_analog_q12(x, y, &xq, &yq, &mq);
            double fx = x <= -8 ? x+6 : (x >= 8 ? x-6 : 0);
            double fy = y <= -8 ? y+6 : (y >= 8 ? y-6 : 0);
            double mag = sqrt(fx*fx + fy*fy);
            if(mag < 8) mag = 0;
            else if(mag > 64) {fx *= 64/mag; fy *= 64/mag; mag = 64;}
            double diffs[3] = {fabs(xq/4096.0 - fx), fabs(yq/4096.0 - fy), fabs(mq/4096.0 - mag)};
            for(int i=0;i<3;++i) if(diffs[i] > max_err) max_err = diffs[i];
            assert(diffs[0] < 0.001 && diffs[1] < 0.001 && diffs[2] < 0.001);
        }
    }
    printf("PS1 Q12 analog stick: all 65536 input pairs, max delta %.6f\n",max_err);
    return 0;
}
