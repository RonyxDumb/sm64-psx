#pragma once

/* PS1 GPU modulates a texture by vertex RGB / 128 (0x80 is neutral).
 * N64 G_CC_MODULATERGB uses vertex RGB / 255 (0xFF is neutral).
 * Halve RGB only for textured *modulated* polygons, including world
 * geometry, characters, and the intro logo. Leave solid-color polygons
 * and untextured decal base passes unchanged.
 *
 * G_CC_BLENDRGBFADEA decals are a special case: they overlay the original
 * texture colors on top of a separately rendered shaded base. Keep their
 * texture pass neutral so Mario's cap emblem and buttons retain colors.
 *
 * The high alpha byte is excluded because GP0 command opcode flags occupy
 * that byte. The main renderer uses the returned low 24-bit RGB payload.
 */
static inline unsigned int gfx_psx_material_color(
    unsigned int n64_rgb, int textured, int decal_overlay
) {
    if(decal_overlay) return 0x00808080u;
    if(textured) return (n64_rgb & 0x00FEFEFEu) >> 1;
    return n64_rgb & 0x00FFFFFFu;
}
