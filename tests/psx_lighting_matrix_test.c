#include "src/port/gfx/gfx_psx_lighting_matrix.h"
#include <assert.h>
#include <stdio.h>

static int16_t lo(uint32_t v) { return (int16_t)(uint16_t)v; }
static int16_t hi(uint32_t v) { return (int16_t)(uint16_t)(v >> 16); }

/* The GTE's 3x3 light and color transforms, without fixed-point rounding. */
static int light_channel(const GfxPsxLightingMatrices* m, int nx, int ny, int nz,
                         int channel) {
    int rows[3] = {
        (lo(m->light[0]) * nx + hi(m->light[0]) * ny + lo(m->light[1]) * nz) / 4096,
        (hi(m->light[1]) * nx + lo(m->light[2]) * ny + hi(m->light[2]) * nz) / 4096,
        (lo(m->light[3]) * nx + hi(m->light[3]) * ny + lo(m->light[4]) * nz) / 4096,
    };
    int cr[3][3] = {
        {lo(m->color[0]), hi(m->color[0]), lo(m->color[1])},
        {hi(m->color[1]), lo(m->color[2]), hi(m->color[2])},
        {lo(m->color[3]), hi(m->color[3]), lo(m->color[4])},
    };
    return (cr[channel][0] * rows[0] + cr[channel][1] * rows[1]
            + cr[channel][2] * rows[2]) / 4096 / 16;
}

int main(void) {
    GfxPsxLightingMatrices m = {0};
    /* Bob-omb Battlefield's 0x66 ambient + 0xff white angled sun. */
    gfx_psx_set_directional_light_matrix(&m, 0, 2365, 2365, 2365, 0xffffffu);
    assert(lo(m.light[0]) == 2365 && hi(m.light[0]) == 2365);
    assert(lo(m.light[1]) == 2365);
    /* Top-facing grass must have substantial directional lighting. */
    assert(light_channel(&m, 0, 4032, 0, 0) > 130);
    assert(light_channel(&m, 4032, 0, 0, 1) > 130);
    assert(light_channel(&m, 0, 0, 4032, 2) > 130);
    assert(light_channel(&m, 0, -4032, 0, 0) < 0);

    /* A second directional light must not overwrite the first. */
    gfx_psx_set_directional_light_matrix(&m, 1, -2896, 0, 2896, 0x4080ffu);
    assert(lo(m.light[0]) == 2365 && hi(m.light[0]) == 2365);
    assert(lo(m.light[1]) == 2365 && hi(m.light[1]) == -2896);
    assert(lo(m.light[2]) == 0 && hi(m.light[2]) == 2896);
    assert(lo(m.color[0]) == 4080 && hi(m.color[0]) == 4080);
    assert(hi(m.color[1]) == 4080 && lo(m.color[2]) == 2048);
    assert(lo(m.color[3]) == 4080 && hi(m.color[3]) == 1024);
    assert(light_channel(&m, 0, 4032, 0, 0) > 130);
    assert(light_channel(&m, 0, 0, 4032, 0) > 200);
    puts("PS1 directional GTE lighting matrices: PASS");
    return 0;
}
