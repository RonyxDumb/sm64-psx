#include <assert.h>
#include <stdint.h>
#include <stdio.h>

typedef uint32_t u32;
#include "src/port/gfx/gfx_light_color.h"

static u32 channel(u32 rgb, unsigned shift) {
    return (rgb >> shift) & 0xFFu;
}

int main(void) {
    assert(gfx_light_rgb888_to_rgb555(0x000000u) == 0x0000u);
    assert(gfx_light_rgb888_to_rgb555(0xFFFFFFu) == 0x7FFFu);
    assert(gfx_light_rgb888_to_rgb555(0x0000FFu) == 0x001Fu);
    assert(gfx_light_rgb888_to_rgb555(0x00FF00u) == 0x03E0u);
    assert(gfx_light_rgb888_to_rgb555(0xFF0000u) == 0x7C00u);

    for (u32 value = 0; value < 256; ++value) {
        for (unsigned shift = 0; shift <= 16; shift += 8) {
            u32 rgb = value << shift;
            u32 restored = gfx_light_rgb555_to_rgb888(
                gfx_light_rgb888_to_rgb555(rgb));
            assert(channel(restored, shift) <= value);
            assert(value - channel(restored, shift) <= 7u);
            for (unsigned other = 0; other <= 16; other += 8) {
                if (other != shift) assert(channel(restored, other) == 0u);
            }
        }
    }

    /* Every 5-bit code round trips without changing its encoded value. */
    for (u32 value = 0; value <= 0x7FFFu; ++value) {
        assert(gfx_light_rgb888_to_rgb555(
            gfx_light_rgb555_to_rgb888(value)) == value);
    }

    for (u32 color = 0; color < 0xFFFFFFu; color += 7919u) {
        u32 encoded = gfx_light_encode_ambient(color);
        u32 restored = gfx_light_decode_ambient(encoded);
        assert(restored == gfx_light_quantize_directional(color));
#if defined(PSX_LIGHTS_RGB555) && PSX_LIGHTS_RGB555
        assert(encoded <= 0x7FFFu);
#else
        assert(restored == color);
#endif
    }
    puts("RGB555 and RGB888 lighting tests passed");
    return 0;
}
