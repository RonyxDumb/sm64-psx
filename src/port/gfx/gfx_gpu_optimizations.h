#pragma once

/* A PS1 flat-shaded GP0 polygon omits every vertex RGB word except the
 * first. Only choose it when the final GPU modulation colors are equal.
 * This reduces GPU setup work and packet pressure with pixel-identical
 * results; gradients retain the original Gouraud path.
 */
static inline int gfx_psx_colors_vary(
    unsigned int c0, unsigned int c1, unsigned int c2, unsigned int c3, int quad
) {
    return c0 != c1 || c0 != c2 || (quad && c0 != c3);
}

/* The GPU clips in screen space, but sending fully offscreen polygons
 * wastes packet RAM and CPU/GPU setup. An AND of the four outcodes rejects
 * only a primitive fully beyond one viewport edge. Call only after a
 * successful GTE projection wholly inside the near/far range.
 */
static inline unsigned int gfx_psx_viewport_outcode(
    unsigned int packed_xy, int width, int height
) {
    const int x = (short) (packed_xy & 0xffffu);
    const int y = (short) (packed_xy >> 16);
    return (x < 0 ? 1u : 0u)
         | (x >= width ? 2u : 0u)
         | (y < 0 ? 4u : 0u)
         | (y >= height ? 8u : 0u);
}

static inline int gfx_psx_outside_viewport(
    unsigned int p0, unsigned int p1, unsigned int p2, unsigned int p3,
    int quad, int width, int height
) {
    unsigned int reject =
        gfx_psx_viewport_outcode(p0, width, height)
        & gfx_psx_viewport_outcode(p1, width, height)
        & gfx_psx_viewport_outcode(p2, width, height);
    if(quad) reject &= gfx_psx_viewport_outcode(p3, width, height);
    return reject != 0;
}
