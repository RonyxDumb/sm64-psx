#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "endian.h"
#include <png.h>
#define WUFFS_IMPLEMENTATION
#define WUFFS_CONFIG__STATIC_FUNCTIONS
#define WUFFS_CONFIG__MODULES
#define WUFFS_CONFIG__MODULE__AUX__BASE
#define WUFFS_CONFIG__MODULE__AUX__IMAGE
#define WUFFS_CONFIG__MODULE__BASE
#define WUFFS_CONFIG__MODULE__ADLER32
#define WUFFS_CONFIG__MODULE__CRC32
#define WUFFS_CONFIG__MODULE__DEFLATE
#define WUFFS_CONFIG__MODULE__ZLIB
#define WUFFS_CONFIG__MODULE__PNG
#define WUFFS_CONFIG__DST_PIXEL_FORMAT__ENABLE_ALLOWLIST
#define WUFFS_CONFIG__DST_PIXEL_FORMAT__ALLOW_RGBA_NONPREMUL
#define WUFFS_CONFIG__ENABLE_DROP_IN_REPLACEMENT__STB
#include "wuffs-v0.4.c"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef struct {
	u8 r;
	u8 g;
	u8 b;
	u8 a;
} Rgba;

Rgba rgba_avg(Rgba c0, Rgba c1) {
	u32 r = (((u32) c0.r + c1.r) + 1) / 2;
	u32 g = (((u32) c0.g + c1.g) + 1) / 2;
	u32 b = (((u32) c0.b + c1.b) + 1) / 2;
	u32 a = (((u32) c0.a + c1.a) + 1) / 2;
	return (Rgba) {r, g, b, a};
}

#define MIN_VISIBLE_ALPHA 32
#define MIN_OPAQUE_ALPHA 192

int color_dist_sq(u16 c0, u16 c1) {
	int r0 = c0 & 0x1F;
	int g0 = c0 >> 5 & 0x1F;
	int b0 = c0 >> 10 & 0x1F;
	int r1 = c1 & 0x1F;
	int g1 = c1 >> 5 & 0x1F;
	int b1 = c1 >> 10 & 0x1F;
	return (r0 - r1) * (r0 - r1) + (g0 - g1) * (g0 - g1) + (b0 - b1) * (b0 - b1);
}

void swap_colors(u16* colors, u32* frequencies, int i, int j) {
	u16 tmp_color = colors[i];
	int tmp_freq = frequencies[i];
	colors[i] = colors[j];
	frequencies[i] = frequencies[j];
	colors[j] = tmp_color;
	frequencies[j] = tmp_freq;
}

void sort_colors(u16* colors, u32* frequencies, int count) {
	for(int i = 0; i + 1 < count; i++) {
		int winner = i;
		for(int j = i + 1; j < count; j++) {
			if(frequencies[j] > frequencies[winner]) {
				winner = j;
			}
		}
		if(winner != i) {
			swap_colors(colors, frequencies, i, winner);
		}
	}
}

u32 get_palette_mapping_of_color(u16 color, u16* unique_colors, u32* unique_colors_to_palette, u32 unique_color_count) {
	if(color == 0) {
		return 0;
	}
	for(u32 i = 0; i < unique_color_count; i++) {
		if(color == unique_colors[i]) {
			return unique_colors_to_palette[i];
		}
	}
	abort();
}

int main(int argc, const char** argv) {
	if(argc != 4) {
		fprintf(stderr, "usage: %s <bit depth (4/8/16)> <input> <output>\n", argv[0]);
		return 1;
	}
	int bit_depth = atoi(argv[1]);
	if(bit_depth != 4 && bit_depth != 8 && bit_depth != 16) {
		fprintf(stderr, "bit depth must be 4, 8, or 16");
		return 1;
	}
	int in_w, h, channels;
	u8* bytes = stbi_load(argv[2], &in_w, &h, &channels, 4);
	Rgba* rgba_pixels;
	if(channels == 2) {
		rgba_pixels = malloc(in_w * h * sizeof(Rgba));
		for(u32 i = 0; i < in_w * h; i++) {
			u8 intensity = bytes[i * 2];
			u8 alpha = bytes[i * 2 + 1];
			rgba_pixels[i] = (Rgba) {.r = intensity, .g = intensity, .b = intensity, .a = alpha};
		}
		channels = 4;
		free(bytes);
	} else {
		rgba_pixels = (Rgba*) bytes;
	}
	if(channels != 4) {
		printf("requested 4 channels, had %d\n", channels);
		return 1;
	}

	bool has_translucency = false;
	for(int i = 0; i < in_w * h; i++) {
		Rgba color = rgba_pixels[i];
		if(color.a >= MIN_VISIBLE_ALPHA && color.a < MIN_OPAQUE_ALPHA) {
			has_translucency = true;
			break;
		}
	}

	//if(in_w == 64 && h == 64) {
	//	for(u32 y = 0; y < 32; y++) {
	//		for(u32 x = 0; x < 32; x++) {
	//			Rgba tl = rgba_pixels[y * 2 * 64 + x * 2];
	//			Rgba tr = rgba_pixels[y * 2 * 64 + x * 2 + 1];
	//			Rgba bl = rgba_pixels[(y * 2 + 1) * 64 + x * 2];
	//			Rgba br = rgba_pixels[(y * 2 + 1) * 64 + x * 2 + 1];
	//			rgba_pixels[y * 32 + x] = rgba_avg(rgba_avg(tl, tr), rgba_avg(bl, br));
	//		}
	//	}
	//	in_w = 32;
	//	h = 32;
	//}

	int w = in_w % 2 != 0? in_w + 1: in_w;

	u16 psx_pixels[w * h];
	if(has_translucency) {
		for(int y = 0; y < h; y++) {
			for(int x = 0; x < w; x++) {
				if(x == in_w) {
					psx_pixels[y * w + x] = psx_pixels[y * w + x - 1];
				} else {
					Rgba color = rgba_pixels[y * in_w + x];
					u16 psx;
					if(color.a < MIN_VISIBLE_ALPHA) {
						psx = 0;
					} else {
						u16 r = (color.r + 4) >> 3;
						if(r > 0x1F) r = 0x1F;
						u16 g = (color.g + 4) >> 3;
						if(g > 0x1F) g = 0x1F;
						u16 b = (color.b + 4) >> 3;
						if(b > 0x1F) b = 0x1F;
						psx = r | g << 5 | b << 10;
						if(color.a < MIN_OPAQUE_ALPHA) {
							psx |= 0x8000;
						} else if(psx == 0) {
							psx = 1 | 1 << 5 | 1 << 10;
						}
					}
					psx_pixels[y * w + x] = psx;
				}
			}
		}
	} else {
		for(int y = 0; y < h; y++) {
			for(int x = 0; x < w; x++) {
				if(x == in_w) {
					psx_pixels[y * w + x] = psx_pixels[y * w + x - 1];
				} else {
					Rgba color = rgba_pixels[y * in_w + x];
					u16 psx;
					if(color.a < MIN_VISIBLE_ALPHA) {
						psx = 0;
					} else {
						u16 r = (color.r + 4) >> 3;
						if(r > 0x1F) r = 0x1F;
						u16 g = (color.g + 4) >> 3;
						if(g > 0x1F) g = 0x1F;
						u16 b = (color.b + 4) >> 3;
						if(b > 0x1F) b = 0x1F;
						psx = r | g << 5 | b << 10 | 0x8000;
					}
					psx_pixels[y * w + x] = psx;
				}
			}
		}
	}

	bool img_rotated = false;
	if(w == 32 && h == 64) {
		u16 rotated_psx_pixels[w * h];
		int i = 0;
		for(int x = 0; x < w; x++) {
			for(int y = 0; y < h; y++) {
				rotated_psx_pixels[i++] = psx_pixels[y * w + x];
			}
		}
		memcpy(psx_pixels, rotated_psx_pixels, w * h * 2);
		img_rotated = true;
		w = 64;
		h = 32;
	}

	free(rgba_pixels);

	u8 header[16] = {
		w & 0xFF, w >> 8,
		h & 0xFF, h >> 8,
		img_rotated? 1: 0,
		has_translucency? 1: 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	};

	if(bit_depth == 16) {
		FILE* out = fopen(argv[3], "wb");
		fwrite(header, 16, 1, out);
		fwrite(psx_pixels, 2 * w * h, 1, out);
		fclose(out);
	} else {
		u32 unique_color_count = 0;
		u16 unique_colors[w * h];
		u32 unique_color_frequencies[w * h];
		bool has_empty_pixel = false;
		for(u32 i = 0; i < w * h; i++) {
			u16 color = psx_pixels[i];
			if(color == 0) {
				has_empty_pixel = true;
			} else {
				for(u32 j = 0; j < unique_color_count; j++) {
					if(color == unique_colors[j]) {
						unique_color_frequencies[j]++;
						goto found_color;
					}
				}
				unique_colors[unique_color_count] = color;
				unique_color_frequencies[unique_color_count] = 1;
				unique_color_count++;
			found_color:;
			}
		}

		u32 max_palette_colors = 1 << bit_depth;
		u32 max_unique_colors = max_palette_colors - has_empty_pixel;  // empty pixel will always be included in the palette if present
		u32 eaten_color_count = 0;

		u32 unique_color_remaps[unique_color_count];
		u32 color_r_accum[unique_color_count];
		u32 color_g_accum[unique_color_count];
		u32 color_b_accum[unique_color_count];
		for(int i = 0; i < unique_color_count; i++) {
			u16 psx_color = unique_colors[i];
			u32 freq = unique_color_frequencies[i];
			color_r_accum[i] = (u32) (psx_color & 0x1F) * freq;
			color_g_accum[i] = (u32) ((psx_color >> 5) & 0x1F) * freq;
			color_b_accum[i] = (u32) ((psx_color >> 10) & 0x1F) * freq;
			unique_color_remaps[i] = i;
		}
		while(unique_color_count - eaten_color_count > max_unique_colors) {
			u32 winning_pair_dist_sq = INT32_MAX;
			u32 winning_pair_idx0 = 0;
			u32 winning_pair_idx1 = 1;
			u64 winning_pair_importance = INT64_MAX;
			for(u32 i = 0; i + 1 < unique_color_count; i++) {
				u16 c0 = unique_colors[i];
				if(unique_color_frequencies[i] != 0) {
					for(u32 j = i + 1; j < unique_color_count; j++) {
						u16 c1 = unique_colors[j];
						if(unique_color_frequencies[j] != 0) {
							u32 dist_sq = color_dist_sq(c0, c1);
							u32 freq = unique_color_frequencies[i] + unique_color_frequencies[j];
							u64 importance = dist_sq * freq;
							if(importance < winning_pair_importance) {
								winning_pair_dist_sq = dist_sq;
								winning_pair_idx0 = i;
								winning_pair_idx1 = j;
								winning_pair_importance = importance;
							}
						}
					}
				}
			}
			assert(winning_pair_dist_sq != INT32_MAX);
			eaten_color_count++;
			unique_color_frequencies[winning_pair_idx0] += unique_color_frequencies[winning_pair_idx1];
			unique_color_frequencies[winning_pair_idx1] = 0;
			color_r_accum[winning_pair_idx0] += color_r_accum[winning_pair_idx1];
			color_g_accum[winning_pair_idx0] += color_g_accum[winning_pair_idx1];
			color_b_accum[winning_pair_idx0] += color_b_accum[winning_pair_idx1];
			unique_color_remaps[winning_pair_idx1] = winning_pair_idx0;
		}

		u16 palette_colors[256];
		u32 palette_color_count = 0;
		if(has_empty_pixel) {
			palette_colors[0] = 0;
			palette_color_count++;
		}
		u32 unique_colors_to_palette[unique_color_count];
		// change each included color to the average of all colors consumed by it and place it into the palette
		for(u32 i = 0; palette_color_count < max_palette_colors && i < unique_color_count; i++) {
			if(unique_color_frequencies[i] != 0) {
				u32 freq = unique_color_frequencies[i];
				u32 r = color_r_accum[i] / freq;
				if(r > 0x1F) r = 0x1F;
				u32 g = color_g_accum[i] / freq;
				if(g > 0x1F) g = 0x1F;
				u32 b = color_b_accum[i] / freq;
				if(b > 0x1F) b = 0x1F;
				u32 psx_color = r | (g << 5) | (b << 10) | (unique_colors[i] & 0x8000);
				if(psx_color == 0) {
					if(has_translucency) {
						psx_color |= 0x8000;
					} else {
						psx_color = 1 | 1 << 5 | 1 << 10;
					}
				}
				palette_colors[palette_color_count] = psx_color;
				unique_colors_to_palette[i] = palette_color_count;
				palette_color_count++;
			}
		}
		// set palette index of excluded colors based on what included color they remap to
		for(u32 i = 0; i < unique_color_count; i++) {
			if(unique_color_frequencies[i] == 0) {
				u32 remap = unique_color_remaps[i];
				while(unique_color_remaps[remap] != remap) {
					remap = unique_color_remaps[remap];
				}
				unique_colors_to_palette[i] = unique_colors_to_palette[remap];
			}
		}

		memset(palette_colors + palette_color_count, 0, (256 - palette_color_count) * 2);

		if(bit_depth == 4) {
			u8 double_indices[w * h / 2];
			for(int i = 0; i < w * h; i += 2) {
				u8 index0 = get_palette_mapping_of_color(psx_pixels[i], unique_colors, unique_colors_to_palette, unique_color_count);
				u8 index1 = get_palette_mapping_of_color(psx_pixels[i + 1], unique_colors, unique_colors_to_palette, unique_color_count);
				double_indices[i / 2] = index0 | index1 << 4;
			}
			FILE* out = fopen(argv[3], "wb");
			fwrite(header, 16, 1, out);
			fwrite(palette_colors, 16 * 2, 1, out);
			fwrite(double_indices, w * h / 2, 1, out);
			fclose(out);
		} else if(bit_depth == 8) {
			u8 indices[w * h];
			for(int i = 0; i < w * h; i++) {
				indices[i] = get_palette_mapping_of_color(psx_pixels[i], unique_colors, unique_colors_to_palette, unique_color_count);
			}
			FILE* out = fopen(argv[3], "wb");
			fwrite(header, 16, 1, out);
			fwrite(palette_colors, 256 * 2, 1, out);
			fwrite(indices, w * h, 1, out);
			fclose(out);
		}
	}
}
