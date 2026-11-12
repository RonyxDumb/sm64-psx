#include "src/port/gfx/gfx_spline_fixed.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void original_weights(int state, double t, double w[4]) {
    double inv = 1 - t, inv2 = inv*inv, inv3 = inv2*inv, t2=t*t, t3=t2*t;
    switch(state) {
        case 1: w[0]=inv3;w[1]=t3*1.75-t2*4.5+t*3;w[2]=-t3*11/12+t2*1.5;w[3]=t3/6;break;
        case 2: w[0]=inv3*.25;w[1]=t3*7/12-t2*1.25+t*.25+7.0/12;w[2]=-t3*.5+t2*.5+t*.5+1.0/6;w[3]=t3/6;break;
        case 3: w[0]=inv3/6;w[1]=t3*.5-t2+4.0/6;w[2]=-t3*.5+t2*.5+t*.5+1.0/6;w[3]=t3/6;break;
        case 4: w[0]=inv3/6;w[1]=-inv3*.5+inv2*.5+inv*.5+1.0/6;w[2]=inv3*7/12-inv2*1.25+inv*.25+7.0/12;w[3]=t3*.25;break;
        case 5: w[0]=inv3/6;w[1]=-inv3*11/12+inv2*1.5;w[2]=inv3*1.75-inv2*4.5+inv*3;w[3]=t3;break;
    }
}

int main(void) {
    double max_err = 0;
    for(int state = 1; state <= 5; state++) {
        for(int t = 0; t <= 1000; t++) {
            int32_t w[4]; double orig[4];
            psx_spline_weights_q16(state, t, w);
            original_weights(state, t/1000.0, orig);
            int32_t total = 0;
            for(int i = 0; i < 4; ++i) {
                assert(w[i] >= 0 && w[i] <= PSX_SPLINE_ONE);
                total += w[i];
                double err = fabs(w[i]/65536.0-orig[i]);
                if(err > max_err) max_err = err;
            }
            assert(total == PSX_SPLINE_ONE);
        }
    }
    printf("PS1 Q16 cinematic splines: max weight delta = %.6f\n", max_err);
    assert(max_err < 0.0002);
    return 0;
}
