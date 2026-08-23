#include <PR/ultratypes.h>

#include "area.h"
#include "engine/math_util.h"
#include "geo_misc.h"
#include "gfx_dimensions.h"
#include "level_update.h"
#include "memory.h"
#include "save_file.h"
#include "segment2.h"
#include "sm64.h"
#ifdef TARGET_PSX
#include <port/gfx/gfx.h>
#include <port/gfx/gfx_internal.h>
#endif

#ifndef TARGET_N64
//#define BETTER_SKYBOX_POSITION_PRECISION
#endif

/**
 * @file skybox.c
 *
 * Implements the skybox background.
 *
 * It's not exactly a sky"box": it's more of a sky tilemap. It renders a 3x3 grid of 32x32 pieces of the
 * whole skybox image, starting from the top left based on the camera's rotation. A skybox image has 64
 * unique 32x32 tiles, with the first two columns duplicated for a total of 80.
 *
 * The tiles are mapped to world space such that 2 full tiles fit on the screen, for a total of
 * 8 tiles around the full 360 degrees. Each tile takes up 45 degrees of the camera's field of view, and
 * the code draws 3 tiles or 135 degrees of the skybox in a frame. But only 2 tiles, or 90 degrees, can
 * fit on-screen at a time.
 *
 * @bug FOV is handled strangely by the code. It is used to scale and rotate the skybox, when really it
 * should probably only be used to calculate the distance drawn from the center of the looked-at tile.
 * But since the game always sets it to 90 degrees, the skybox always scales and rotates the same,
 * regardless of the camera's actual FOV. So even if the camera's FOV is 10 degrees the game draws a
 * full 90 degrees of the skybox, which makes the sky look really far away.
 *
 * @bug Skyboxes unnecessarily repeat the first 2 columns when they could just wrap the col index.
 * Although, the wasted space is only about 128 bytes for each image.
 */

/**
 * Describes the position, tiles, and orientation of the skybox image.
 *
 * Describes the scaled x and y offset into the tilemap, based on the yaw and pitch.  Computes the
 * upperLeftTile index into the skybox's tile list using scaledX and scaledY. See get_top_left_tile_idx.
 *
 * The skybox is always drawn behind everything, because in the level's geo script, the skybox is drawn
 * first, in a display list with the Z buffer disabled
 */
struct Skybox {
    /// The camera's yaw, from 0 to 65536, which maps to 0 to 360 degrees
    u16 yaw;
    /// The camera's pitch, which is bounded by +-16384, which maps to -90 to 90 degrees
    s16 pitch;
#ifdef BETTER_SKYBOX_POSITION_PRECISION
    /// The skybox's X position in world space
    f32 scaledX;
    /// The skybox's Y position in world space
    f32 scaledY;
#else
    /// The skybox's X position in world space
    s32 scaledX;
    /// The skybox's Y position in world space
    s32 scaledY;
#endif

    /// The index of the upper-left tile in the 3x3 grid that gets drawn
    s32 upperLeftTile;
};

struct Skybox sSkyBoxInfo[2];

typedef const u8 *const SkyboxTexture[80];

extern SkyboxTexture bbh_skybox_ptrlist;
extern SkyboxTexture bidw_skybox_ptrlist;
extern SkyboxTexture bitfs_skybox_ptrlist;
extern SkyboxTexture bits_skybox_ptrlist;
extern SkyboxTexture ccm_skybox_ptrlist;
extern SkyboxTexture cloud_floor_skybox_ptrlist;
extern SkyboxTexture clouds_skybox_ptrlist;
extern SkyboxTexture ssl_skybox_ptrlist;
extern SkyboxTexture water_skybox_ptrlist;
extern SkyboxTexture wdw_skybox_ptrlist;

SkyboxTexture *sSkyboxTextures[10] = {
    &water_skybox_ptrlist,
    &bitfs_skybox_ptrlist,
    &wdw_skybox_ptrlist,
    &cloud_floor_skybox_ptrlist,
    &ccm_skybox_ptrlist,
    &ssl_skybox_ptrlist,
    &bbh_skybox_ptrlist,
    &bidw_skybox_ptrlist,
    &clouds_skybox_ptrlist,
    &bits_skybox_ptrlist,
};

/**
 * The skybox color mask.
 * The final color of each pixel is computed from the bitwise AND of the color and the texture.
 */
u8 sSkyboxColors[][3] = {
    { 0x50, 0x64, 0x5A },
    { 0xFF, 0xFF, 0xFF },
};

/**
 * Constant used to scale the skybox horizontally to a multiple of the screen's width
 */
#define SKYBOX_WIDTH (4 * SCREEN_WIDTH)
/**
 * Constant used to scale the skybox vertically to a multiple of the screen's height
 */
#define SKYBOX_HEIGHT (4 * SCREEN_HEIGHT)

/**
 * The tile's width in world space.
 * By default, two full tiles can fit in the screen.
 */
#define SKYBOX_TILE_WIDTH (SCREEN_WIDTH / 2)
/**
 * The tile's height in world space.
 * By default, two full tiles can fit in the screen.
 */
#define SKYBOX_TILE_HEIGHT (SCREEN_HEIGHT / 2)

/**
 * The horizontal length of the skybox tilemap in tiles.
 */
#define SKYBOX_COLS (10)
/**
 * The vertical length of the skybox tilemap in tiles.
 */
#define SKYBOX_ROWS (8)


/**
 * Convert the camera's yaw into an x position into the scaled skybox image.
 *
 * fov is always 90 degrees, set in draw_skybox_facing_camera.
 *
 * The calculation performed is equivalent to (360 / fov) * (yaw / 65536) * SCREEN_WIDTH
 * in other words: (the number of fov-sized parts of the circle there are) *
 *                 (how far is the camera rotated from 0, scaled 0 to 1)   *
 *                 (the screen width)
 */
#ifdef BETTER_SKYBOX_POSITION_PRECISION
f32
#else
s32
#endif
calculate_skybox_scaled_xq(s8 player) {
	s32 fov = 90;
    q32 yawq = q(sSkyBoxInfo[player].yaw);
    q32 yawScaledq = yawq * (SCREEN_WIDTH * 360 / (fov * 65536));

#ifdef BETTER_SKYBOX_POSITION_PRECISION
    f32 scaledX = yawScaled;

    if (scaledX > SKYBOX_WIDTH) {
        scaledX -= (s32) scaledX / SKYBOX_WIDTH * SKYBOX_WIDTH;
    }
#else
    // Round the scaled yaw. Since yaw is a u16, it doesn't need to check for < 0
    s32 scaledX = yawScaledq + 0.5;

    if (scaledX > SKYBOX_WIDTH) {
        scaledX -= scaledX / SKYBOX_WIDTH * SKYBOX_WIDTH;
    }
#endif

    return SKYBOX_WIDTH - scaledX;
}

/**
 * Convert the camera's pitch into a y position in the scaled skybox image.
 *
 * fov may have been used in an earlier version, but the developers changed the function to always use
 * 90 degrees.
 */
#ifdef BETTER_SKYBOX_POSITION_PRECISION
f32
#else
s32
#endif
calculate_skybox_scaled_yq(s8 player) {
    // Convert pitch to degrees. Pitch is bounded between -90 (looking down) and 90 (looking up).
    q32 pitchInDegreesq = q(sSkyBoxInfo[player].pitch) / (65535 / 360);

    // Scale by 360 / fov
    q32 degreesToScaleq = pitchInDegreesq * (360 / 90);

#ifdef BETTER_SKYBOX_POSITION_PRECISION
    q32 scaledYq = degreesToScaleq + 5 * SKYBOX_TILE_HEIGHT;
#else
    s32 roundedY = roundq(degreesToScaleq);

    // Since pitch can be negative, and the tile grid starts 1 octant above the camera's focus, add
    // 5 octants to the y position
    s32 scaledY = roundedY + 5 * SKYBOX_TILE_HEIGHT;
#endif

    if (scaledY > SKYBOX_HEIGHT) {
        scaledY = SKYBOX_HEIGHT;
    }
    if (scaledY < SCREEN_HEIGHT) {
        scaledY = SCREEN_HEIGHT;
    }
    return scaledY;
}

/**
 * Converts the upper left xPos and yPos to the index of the upper left tile in the skybox.
 */
static s32 get_top_left_tile_idx(s8 player) {
    s32 tileCol = sSkyBoxInfo[player].scaledX / SKYBOX_TILE_WIDTH;
    s32 tileRow = (SKYBOX_HEIGHT - sSkyBoxInfo[player].scaledY) / SKYBOX_TILE_HEIGHT;

    return tileRow * SKYBOX_COLS + tileCol;
}

/**
 * Generates vertices for the skybox tile.
 *
 * @param tileIndex The index into the 32x32 sections of the whole skybox image. The index is converted
 *                  into an x and y by modulus and division by SKYBOX_COLS. x and y are then scaled by
 *                  SKYBOX_TILE_WIDTH to get a point in world space.
 */
Vtx *make_skybox_rect(s32 tileIndex, s8 colorIndex) {
    Vtx *verts = alloc_display_list(4 * sizeof(*verts));
    s16 x = tileIndex % SKYBOX_COLS * SKYBOX_TILE_WIDTH;
    s16 y = SKYBOX_HEIGHT - tileIndex / SKYBOX_COLS * SKYBOX_TILE_HEIGHT;

    if (verts != NULL) {
        make_vertex(verts, 0, x, y, -1, 0, 0, sSkyboxColors[colorIndex][0], sSkyboxColors[colorIndex][1],
                    sSkyboxColors[colorIndex][2], 255);
        make_vertex(verts, 1, x, y - SKYBOX_TILE_HEIGHT, -1, 0, 31 << 5, sSkyboxColors[colorIndex][0], sSkyboxColors[colorIndex][1],
                    sSkyboxColors[colorIndex][2], 255);
        make_vertex(verts, 2, x + SKYBOX_TILE_WIDTH, y - SKYBOX_TILE_HEIGHT, -1, 31 << 5, 31 << 5, sSkyboxColors[colorIndex][0],
                    sSkyboxColors[colorIndex][1], sSkyboxColors[colorIndex][2], 255);
        make_vertex(verts, 3, x + SKYBOX_TILE_WIDTH, y, -1, 31 << 5, 0, sSkyboxColors[colorIndex][0], sSkyboxColors[colorIndex][1],
                    sSkyboxColors[colorIndex][2], 255);
    } else {
    }
    return verts;
}

/**
 * Draws a 3x3 grid of 32x32 sections of the original skybox image.
 * The row and column are converted into an index into the skybox's tile list, which is then drawn in
 * world space so that the tiles will rotate with the camera.
 */
#include <stdio.h>
void draw_skybox_tile_grid(Gfx **dlist, s8 background, s8 player, s8 colorIndex) {
    s32 row;
    s32 col;

    for (row = 0; row < 3; row++) {
        for (col = 0; col < 3; col++) {
            s32 tileIndex = sSkyBoxInfo[player].upperLeftTile + row * SKYBOX_COLS + col;
            const u8* texture =
                (*(SkyboxTexture*) segmented_to_virtual(sSkyboxTextures[background]))[tileIndex % 80];
            Vtx *vertices = make_skybox_rect(tileIndex, colorIndex);

            gLoadBlockTexture((*dlist)++, 32, 32, G_IM_FMT_RGBA, texture);
            gSPVertex((*dlist)++, VIRTUAL_TO_PHYSICAL(vertices), 4, 0);
            gSPDisplayList((*dlist)++, dl_draw_quad_verts_0123);
        }
    }
}

//void *create_skybox_ortho_matrix(s8 player) {
//    f32 left = sSkyBoxInfo[player].scaledX;
//    f32 right = sSkyBoxInfo[player].scaledX + SCREEN_WIDTH;
//    f32 bottom = sSkyBoxInfo[player].scaledY - SCREEN_HEIGHT;
//    f32 top = sSkyBoxInfo[player].scaledY;
//    Mtx *mtx = alloc_display_list(sizeof(*mtx));
//
//#ifdef WIDESCREEN
//    f32 half_width = (4.0f / 3.0f) / GFX_DIMENSIONS_ASPECT_RATIO * SCREEN_WIDTH / 2;
//    f32 center = (sSkyBoxInfo[player].scaledX + SCREEN_WIDTH / 2);
//    if (half_width < SCREEN_WIDTH / 2) {
//        // A wider screen than 4:3
//        left = center - half_width;
//        right = center + half_width;
//    }
//#endif
//
//    if (mtx != NULL) {
//        guOrtho(mtx, left, right, bottom, top, 0.0f, 3.0f, 1.0f);
//    } else {
//    }
//
//    return mtx;
//}

/**
 * Creates the skybox's display list, then draws the 3x3 grid of tiles.
 */
Gfx *init_skybox_display_list(s8 player, s8 background, s8 colorIndex) {
#ifdef TARGET_PSX
    enum {
        SKYBOX_VISIBLE_TILES = 9,
        SKYBOX_COMMANDS_PER_TILE = 3,
        SKYBOX_FIXED_COMMANDS = 8
    };

    dl_t *dl = alloc_display_list(
        (SKYBOX_FIXED_COMMANDS + SKYBOX_VISIBLE_TILES * SKYBOX_COMMANDS_PER_TILE)
        * sizeof(dl_t));
    GfxVtx *verts = alloc_display_list(SKYBOX_VISIBLE_TILES * 4 * sizeof(GfxVtx));
    ShortMatrix *identity = alloc_display_list(sizeof(ShortMatrix));

    if (dl == NULL || verts == NULL || identity == NULL) {
        return NULL;
    }

    *identity = mtx_identity();

    dl_t *out = dl;
    *(out++) = DL_PACK_OP(DL_CMD_MTX_PUSH);
    *(out++) = DL_PACK_OP(DL_CMD_MTX_SET) | DL_PACK_PTR(identity);
    *(out++) = DL_PACK_OP(DL_CMD_SET_BACKGROUND) | 1;
    *(out++) = DL_PACK_OP(DL_CMD_SET_ORTHO) | 1;

    s32 tileNo = 0;
    for (s32 row = 0; row < 3; row++) {
        for (s32 col = 0; col < 3; col++, tileNo++) {
            s32 tileIndex = sSkyBoxInfo[player].upperLeftTile + row * SKYBOX_COLS + col;

            const u8 *textureSeg =
                (*(SkyboxTexture *) segmented_to_virtual(sSkyboxTextures[background]))[tileIndex % 80];
            TexHeader *tex = segmented_to_virtual((void *) textureSeg);
            if (tex == NULL) {
                continue;
            }

            gfx_load_texture(tex);

            s32 worldX = (tileIndex % SKYBOX_COLS) * SKYBOX_TILE_WIDTH;
            s32 worldY = SKYBOX_HEIGHT - (tileIndex / SKYBOX_COLS) * SKYBOX_TILE_HEIGHT;

            s16 x0 = (s16) (worldX - sSkyBoxInfo[player].scaledX);
            s16 y0 = (s16) (sSkyBoxInfo[player].scaledY - worldY);
            s16 x1 = (s16) (x0 + SKYBOX_TILE_WIDTH);
            s16 y1 = (s16) (y0 + SKYBOX_TILE_HEIGHT);

            GfxVtx *v = &verts[tileNo * 4];
            Color c = {
                .r = sSkyboxColors[colorIndex][0],
                .g = sSkyboxColors[colorIndex][1],
                .b = sSkyboxColors[colorIndex][2],
                ._pad = 0xFF
            };

            v[0] = (GfxVtx) { .x = x0, .y = y0, .z = -1, .u = 0,  .v = 0,  .color = c };
            v[1] = (GfxVtx) { .x = x0, .y = y1, .z = -1, .u = 0,  .v = 31, .color = c };
            v[2] = (GfxVtx) { .x = x1, .y = y0, .z = -1, .u = 31, .v = 0,  .color = c };
            v[3] = (GfxVtx) { .x = x1, .y = y1, .z = -1, .u = 31, .v = 31, .color = c };

            *(out++) = DL_PACK_OP(DL_CMD_TEX) | DL_PACK_PTR(tex);
            *(out++) = DL_PACK_OP(DL_CMD_VTX) | DL_PACK_PTR(v);
            *(out++) = DL_PACK_OP(DL_CMD_QUAD)
                       | (0u << 20) | (1u << 16) | (2u << 12) | (3u << 8)
                       | PRIM_FLAG_TEXTURED;
        }
    }

    *(out++) = DL_PACK_OP(DL_CMD_SET_ORTHO);
    *(out++) = DL_PACK_OP(DL_CMD_SET_BACKGROUND);
    *(out++) = DL_PACK_OP(DL_CMD_MTX_POP);
    *(out++) = DL_PACK_OP(DL_CMD_END);

    return (Gfx *) dl;
#else
    return NULL;
#endif
}

/**
 * Draw a skybox facing the direction from pos to foc.
 *
 * @param player Unused, determines which orientation info struct to update
 * @param background The skybox image to use
 * @param fov Unused. It SHOULD control how much the skybox is scaled, but the way it's coded it just
 *            controls how fast the skybox rotates. The given value is replaced with 90 right before the
 *            dl is created
 * @param posX,posY,posZ The camera's position
 * @param focX,focY,focZ The camera's focus.
 */
Gfx *create_skybox_facing_cameraq(s8 player, s8 background, UNUSED q32 fovq,
                                    q32 posXq, q32 posYq, q32 posZq,
                                    q32 focXq, q32 focYq, q32 focZq) {
    volatile q32 cameraFaceXq = focXq - posXq;
    q32 cameraFaceYq = focYq - posYq;
    q32 cameraFaceZq = focZq - posZq;
    s8 colorIndex = 1;

    // If the first star is collected in JRB, make the sky darker and slightly green
    if (background == 8 && !(save_file_get_star_flags(gCurrSaveFileNum - 1, COURSE_JRB - 1) & 1)) {
        colorIndex = 0;
    }

    //! fov is always set to 90.0f. If this line is removed, then the game crashes because fov is 0 on
    //! the first frame, which causes a floating point divide by 0
    fovq = q(90);
    sSkyBoxInfo[player].yaw = atan2sq(cameraFaceZq, cameraFaceXq);
    sSkyBoxInfo[player].pitch = atan2sq(sqrtq64((q64) qmul(cameraFaceXq, cameraFaceXq) + (q64) qmul(cameraFaceZq, cameraFaceZq)), cameraFaceYq);
    sSkyBoxInfo[player].scaledX = qtof(calculate_skybox_scaled_xq(player));
    sSkyBoxInfo[player].scaledY = qtof(calculate_skybox_scaled_yq(player));
    sSkyBoxInfo[player].upperLeftTile = get_top_left_tile_idx(player);

    return init_skybox_display_list(player, background, colorIndex);
}
