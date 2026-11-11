#pragma once

/*
 * Optional RGB555 lighting on PS1 and the PC reference renderer.
 *
 * Keep N64 Light_t/Ambient_t assets in their original byte layout: changing
 * those structures breaks generated lights and display-list pointers.
 * The compiled display list carries ambient RGB555 only when enabled;
 * directional lights are quantized when read by the renderer.
 *
 * RGB555: bits 0..4 = red, 5..9 = green, 10..14 = blue.
 */
static inline u32 gfx_light_rgb888_to_rgb555(u32 rgb) {
    return ((rgb & 0x000000FFu) >> 3)
         | (((rgb >> 8) & 0xFFu) >> 3) << 5
         | (((rgb >> 16) & 0xFFu) >> 3) << 10;
}

static inline u32 gfx_light_rgb555_to_rgb888(u32 rgb555) {
    u32 r = rgb555 & 31u;
    u32 g = (rgb555 >> 5) & 31u;
    u32 b = (rgb555 >> 10) & 31u;
    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    return r | (g << 8) | (b << 16);
}

/* Constant expression for generated display lists and the runtime compiler. */
#if defined(PSX_LIGHTS_RGB555) && PSX_LIGHTS_RGB555
#define PSX_STATIC_AMBIENT(r, g, b) (((r) >> 3) | (((g) >> 3) << 5) | (((b) >> 3) << 10))
#else
#define PSX_STATIC_AMBIENT(r, g, b) ((r) | ((g) << 8) | ((b) << 16))
#endif

static inline u32 gfx_light_encode_ambient(u32 rgb888) {
    return PSX_STATIC_AMBIENT(rgb888 & 0xFFu, (rgb888 >> 8) & 0xFFu, (rgb888 >> 16) & 0xFFu);
}

static inline u32 gfx_light_decode_ambient(u32 encoded) {
#if defined(PSX_LIGHTS_RGB555) && PSX_LIGHTS_RGB555
    return gfx_light_rgb555_to_rgb888(encoded & 0x7FFFu);
#else
    return encoded & 0xFFFFFFu;
#endif
}

/* Directional lights still point to native N64 Light_t assets. */
static inline u32 gfx_light_quantize_directional(u32 rgb888) {
#if defined(PSX_LIGHTS_RGB555) && PSX_LIGHTS_RGB555
    return gfx_light_rgb555_to_rgb888(gfx_light_rgb888_to_rgb555(rgb888));
#else
    return rgb888 & 0xFFFFFFu;
#endif
}
