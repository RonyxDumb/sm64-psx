#include "src/port/gfx/gfx_texture_uv.h"
#include <assert.h>
#include <stdio.h>

static void test_period(unsigned size) {
    const int samples[] = {-1025, -513, -300, -129, -65, -33, -1, 0, 1, 31, 32, 63, 64, 128, 255, 256, 513, 1025};
    for (unsigned i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
        int v = samples[i];
        unsigned char coord = gfx_wrap_texel_psx(v, size);
        assert((coord % size) == ((v % (int) size + (int) size) % (int) size));
        assert(coord == gfx_wrap_texel_psx(v + (int) size, size));
        assert(coord == gfx_wrap_texel_psx(v + 256, size));
    }
}

int main(void) {
    test_period(8);
    test_period(16);
    test_period(32);
    test_period(64);
    test_period(128);
    assert(gfx_wrap_texel_psx(-129, 32) == 255);
    assert(gfx_wrap_texel_psx(129, 32) == 1);
    puts("PS1 texture wrapping: PASS");
    return 0;
}
