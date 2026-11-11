#include "src/port/gfx/gfx_texture_modulation.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    /* Opaque textured white and primary channels must use PS1 0x80. */
    assert(gfx_psx_material_color(0x00FFFFFFu, 1, 0) == 0x007F7F7Fu);
    assert(gfx_psx_material_color(0x00FEFEFEu, 1, 0) == 0x007F7F7Fu);
    assert(gfx_psx_material_color(0x00FF0000u, 1, 0) == 0x007F0000u);
    assert(gfx_psx_material_color(0x0000FF00u, 1, 0) == 0x00007F00u);
    assert(gfx_psx_material_color(0x000000FFu, 1, 0) == 0x0000007Fu);
    assert(gfx_psx_material_color(0x00123456u, 1, 0) == 0x00091A2Bu);
    /* Untextured geometry and shadow colors must not be darkened. */
    assert(gfx_psx_material_color(0x00FFFFFFu, 0, 0) == 0x00FFFFFFu);
    assert(gfx_psx_material_color(0x00123456u, 0, 0) == 0x00123456u);
    /* Preserve decals' neutral modulation regardless of lighting color. */
    assert(gfx_psx_material_color(0x00000000u, 1, 1) == 0x00808080u);
    assert(gfx_psx_material_color(0x00FFFFFFu, 1, 1) == 0x00808080u);
    /* Byte 24..31 is never allowed to change GP0 command flags. */
    assert(gfx_psx_material_color(0xFFFFFFFFu, 1, 0) == 0x007F7F7Fu);
    puts("PS1 game-wide texture modulation: PASS");
    return 0;
}
