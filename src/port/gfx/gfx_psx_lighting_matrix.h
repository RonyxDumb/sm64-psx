#pragma once

#include <stdint.h>

/* PS1 GTE matrices are row-major, with two signed 16-bit coefficients
 * packed into each 32-bit control register. Normals are COLUMN vectors.
 * Put each light direction in a ROW of LLM, and its RGB in the matching
 * COLUMN of LCM. The previous port placed the directions in columns, so
 * upward-facing surfaces often received only ambient light. */
typedef struct {
    uint32_t light[5]; /* L11L12, L13L21, L22L23, L31L32, L33 */
    uint32_t color[5]; /* LC11LC12, LC13LC21, LC22LC23, LC31LC32, LC33 */
} GfxPsxLightingMatrices;

static inline uint32_t gfx_psx_pack_light_halves(int low, int high) {
    return (uint32_t)(uint16_t)low | ((uint32_t)(uint16_t)high << 16);
}

static inline uint32_t gfx_psx_light_low(uint32_t value, int low) {
    return (value & 0xffff0000u) | (uint16_t)low;
}

static inline uint32_t gfx_psx_light_high(uint32_t value, int high) {
    return (value & 0x0000ffffu) | ((uint32_t)(uint16_t)high << 16);
}

static inline void gfx_psx_set_directional_light_matrix(
    GfxPsxLightingMatrices* m, unsigned index, int x, int y, int z, uint32_t rgb
) {
    int r = (int)(rgb & 255u) << 4;
    int g = (int)((rgb >> 8) & 255u) << 4;
    int b = (int)((rgb >> 16) & 255u) << 4;
    if(index == 0) {
        /* Light 0: LLM row 1 / LCM column 1. */
        m->light[0] = gfx_psx_pack_light_halves(x, y);
        m->light[1] = gfx_psx_light_low(m->light[1], z);
        m->color[0] = gfx_psx_light_low(m->color[0], r);
        m->color[1] = gfx_psx_light_high(m->color[1], g);
        m->color[3] = gfx_psx_light_low(m->color[3], b);
    } else if(index == 1) {
        /* Light 1: LLM row 2 / LCM column 2. */
        m->light[1] = gfx_psx_light_high(m->light[1], x);
        m->light[2] = gfx_psx_pack_light_halves(y, z);
        m->color[0] = gfx_psx_light_high(m->color[0], r);
        m->color[2] = gfx_psx_light_low(m->color[2], g);
        m->color[3] = gfx_psx_light_high(m->color[3], b);
    }
}
