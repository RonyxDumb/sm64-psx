#include <assert.h>
#include <stdio.h>
#include "src/port/gfx/gfx_gpu_optimizations.h"

static unsigned int xy(int x, int y) {
    return (unsigned int)(unsigned short)x | ((unsigned int)(unsigned short)y << 16);
}

int main(void) {
    /* Flat faces save GP0 color words; even one different color
     * must keep Gouraud shading to preserve the visible gradient. */
    assert(!gfx_psx_colors_vary(0x00808080, 0x00808080, 0x00808080, 0, 0));
    assert(!gfx_psx_colors_vary(0x00123456, 0x00123456, 0x00123456, 0x00123456, 1));
    assert(gfx_psx_colors_vary(0x00123456, 0x00123456, 0x00123457, 0, 0));
    assert(gfx_psx_colors_vary(0x00123456, 0x00123456, 0x00123456, 0x00123457, 1));

    /* All-left, all-right, all-above, all-below are safe to discard. */
    assert(gfx_psx_outside_viewport(xy(-8, 30), xy(-20, 80), xy(-1, 60), 0, 0, 320, 240));
    assert(gfx_psx_outside_viewport(xy(320, 30), xy(400, 80), xy(321, 60), 0, 0, 320, 240));
    assert(gfx_psx_outside_viewport(xy(50, -10), xy(100, -4), xy(200, -1), 0, 0, 320, 240));
    assert(gfx_psx_outside_viewport(xy(50, 240), xy(100, 250), xy(200, 260), 0, 0, 320, 240));
    /* Crossing a viewport edge, near the screen or spanning it is kept. */
    assert(!gfx_psx_outside_viewport(xy(-50, 50), xy(10, 90), xy(-10, 120), 0, 0, 320, 240));
    assert(!gfx_psx_outside_viewport(xy(100, 30), xy(130, 90), xy(140, 100), 0, 0, 320, 240));
    assert(!gfx_psx_outside_viewport(xy(-20, 60), xy(340, 60), xy(160, 300), 0, 0, 320, 240));
    assert(!gfx_psx_outside_viewport(xy(-10, -10), xy(-20, -20), xy(-30, -30), xy(100, 100), 1, 320, 240));
    puts("PS1 screen-space culling and flat-shading selection: PASS");
    return 0;
}
