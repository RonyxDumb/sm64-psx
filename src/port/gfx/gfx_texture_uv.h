#pragma once

/* PS1 has 8-bit polygon UVs. For a wrapping texture the GPU texture-window
 * command supplies the 8/16/32/64/128 texel repeat period. Keep the coordinate
 * modulo 256 instead of clamping it: clamping turns tiled floor textures into
 * large stretched bands when SM64 vertices exceed the first tile.
 *
 * The origin bias prevents a negative coordinate from being treated as an
 * out-of-range unsigned value before packet emission. Each bias is divisible
 * by the corresponding texture size, so it cannot shift the pattern.
 */
static inline unsigned char gfx_wrap_texel_psx(int texel, unsigned int size) {
	const int bias = size <= 32 ? 128 : (size <= 64 ? 64 : 0);
	return (unsigned char)(texel + bias);
}
