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

    /// The skybox's X position in world space.
    s32 scaledX;
    /// The skybox's Y position in world space.
    s32 scaledY;

    /// The index of the upper-left tile in the 3x3 grid that gets drawn
    s32 upperLeftTile;
};

struct Skybox sSkyBoxInfo[2];

#ifdef TARGET_PSX
/*
 * All ten skybox metadata records live in EXT.DAT. Only the currently active
 * skybox is kept in APP_RAM.
 *
 * Record layout must match tools/gen_psx_skybox_assets.py:
 *   u8 uniqueCount;
 *   u8 tileToHeader[80];
 *   u8 padding[3];
 *   TexHeader headers[64];
 */
#define PSX_SKYBOX_MAX_UNIQUE 64
#define PSX_SKYBOX_TILE_COUNT 80
#define PSX_SKYBOX_RECORD_STRIDE 2048

struct PsxSkyboxRecord {
    u8 uniqueCount;
    u8 tileToHeader[PSX_SKYBOX_TILE_COUNT];
    u8 padding[3];
    TexHeader headers[PSX_SKYBOX_MAX_UNIQUE];
};

STATIC_ASSERT(sizeof(struct PsxSkyboxRecord) == 1364,
              "PS1 skybox record layout changed");

extern u8 _skybox_meta_segment[];
extern u8 _skybox_meta_segment_end[];

ALIGNED4 static struct PsxSkyboxRecord sPsxSkyboxRecord;
static s8 sPsxSkyboxLoadedBackground = -1;

static bool psx_skybox_ensure_loaded(s8 background) {
    if ((u8) background >= 10) {
        return false;
    }

    if (sPsxSkyboxLoadedBackground == background) {
        return true;
    }

    /*
     * cd_read() on real PS1 addresses EXT.DAT by sector. Its `pos / 2048`
     * calculation does not preserve a sub-sector offset, so every record must
     * begin on a 2048-byte boundary.
     */
    const u8 *srcStart =
        _skybox_meta_segment + (u32) (u8) background * PSX_SKYBOX_RECORD_STRIDE;
    const u8 *srcEnd = srcStart + sizeof(struct PsxSkyboxRecord);

    if (srcStart + PSX_SKYBOX_RECORD_STRIDE > _skybox_meta_segment_end) {
        return false;
    }

    /*
     * Never allow the generic CD loader to call gfx_show_message_screen()
     * while a render graph is being built. That function discards the current
     * frame. gfx_load_texture() uses the same protection for texture DMA.
     */
    bool prevCanShowScreenMessage = can_show_screen_message;
    can_show_screen_message = false;
    dma_read((u8 *) &sPsxSkyboxRecord, srcStart, srcEnd);
    can_show_screen_message = prevCanShowScreenMessage;

    if (sPsxSkyboxRecord.uniqueCount == 0 ||
        sPsxSkyboxRecord.uniqueCount > PSX_SKYBOX_MAX_UNIQUE) {
        sPsxSkyboxLoadedBackground = -1;
        return false;
    }

    /*
     * TexHeader is mutated by gfx_load_texture() when the texture is uploaded
     * to VRAM. A newly DMA-loaded skybox record therefore gives us a fresh
     * cache for that level without keeping every game's skybox header in RAM.
     */
    sPsxSkyboxLoadedBackground = background;
    return true;
}

static TexHeader *psx_skybox_get_texture(s32 tileIndex) {
    u8 headerIndex =
        sPsxSkyboxRecord.tileToHeader[(u32) tileIndex % PSX_SKYBOX_TILE_COUNT];

    if (headerIndex >= sPsxSkyboxRecord.uniqueCount) {
        return NULL;
    }

    return &sPsxSkyboxRecord.headers[headerIndex];
}
#endif

void skybox_preload(s8 background) {
#ifdef TARGET_PSX
    (void) psx_skybox_ensure_loaded(background);
#else
    (void) background;
#endif
}

#ifndef TARGET_PSX
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
#endif

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
s32 calculate_skybox_scaled_xq(s8 player) {
    /*
     * Original formula:
     *
     * SCREEN_WIDTH * 360 * yaw / (90 * 65536)
     *
     * Since 360 / 90 == 4:
     *
     * SKYBOX_WIDTH * yaw / 65536
     *
     * Use 64-bit integer math so there is no fixed-point truncation.
     */
    u64 numerator = (u64) sSkyBoxInfo[player].yaw * SKYBOX_WIDTH;

    // Same rounding behaviour as the original non-BETTER path.
    s32 scaledX = (s32) ((numerator + 0x8000) >> 16);

    if (scaledX > SKYBOX_WIDTH) {
        scaledX = SKYBOX_WIDTH;
    }

    return SKYBOX_WIDTH - scaledX;
}
/**
 * Convert the camera's pitch into a y position in the scaled skybox image.
 *
 * fov may have been used in an earlier version, but the developers changed the function to always use
 * 90 degrees.
 */
s32 calculate_skybox_scaled_yq(s8 player) {
    /*
     * Original:
     *
     * pitchDegrees = pitch * 360 / 65535
     * scaled       = pitchDegrees * 4
     *
     * therefore:
     *
     * scaled = pitch * 1440 / 65535
     */
    s64 numerator = (s64) sSkyBoxInfo[player].pitch * 1440;
    s32 pitchOffset;

    if (numerator >= 0) {
        pitchOffset = (s32) ((numerator + 32767) / 65535);
    } else {
        pitchOffset = (s32) ((numerator - 32767) / 65535);
    }

    s32 scaledY = pitchOffset + 5 * SKYBOX_TILE_HEIGHT;

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

#ifndef TARGET_PSX
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

#endif

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
    s32 row;
    s32 col;

    if (!psx_skybox_ensure_loaded(background)) {
        return NULL;
    }
    u32 color =
        (u32) sSkyboxColors[colorIndex][0]
        | ((u32) sSkyboxColors[colorIndex][1] << 8)
        | ((u32) sSkyboxColors[colorIndex][2] << 16);

    if ((u8) background >= 10) {
        return NULL;
    }

    /*
     * The PS1 graph renderer already processes the background under the
     * orthographic branch. Emit native background quads directly.
     */
    gfx_emit_set_background(true);
    gfx_emit_env_color_alpha_full(color);

    for (row = 0; row < 3; row++) {
        for (col = 0; col < 3; col++) {
            s32 tileIndex = sSkyBoxInfo[player].upperLeftTile
                          + row * SKYBOX_COLS
                          + col;
            s32 tileX = (tileIndex % SKYBOX_COLS) * SKYBOX_TILE_WIDTH;
            s32 tileY = SKYBOX_HEIGHT
                      - (tileIndex / SKYBOX_COLS) * SKYBOX_TILE_HEIGHT;
            s16 x0 = tileX - sSkyBoxInfo[player].scaledX;
            s16 y0 = sSkyBoxInfo[player].scaledY - tileY;
            s16 x1 = x0 + SKYBOX_TILE_WIDTH;
            s16 y1 = y0 + SKYBOX_TILE_HEIGHT;

            /*
             * Native main-executable TexHeader pointer. Do not call
             * segmented_to_virtual() on this address.
             */
            TexHeader *texture = psx_skybox_get_texture(tileIndex);
            if (texture == NULL) {
                continue;
            }

            gfx_emit_tex(texture);
            gfx_emit_screen_quad(x0, y0, x1, y1);
        }
    }

    /*
     * Do not leak the last skybox texture into subsequent native screen-quad
     * emission or compiled display-list state.
     */
    gfx_emit_tex(NULL);
    gfx_emit_set_background(false);
    gfx_emit_env_color_alpha_full(0xFFFFFF);
    return NULL;
#else
    UNUSED(player);
    UNUSED(background);
    UNUSED(colorIndex);
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
    return NULL; // disattiva la creazione delle skybox (rompono il rendering dei poligoni)

    // If the first star is collected in JRB, make the sky darker and slightly green
    if (background == 8 && !(save_file_get_star_flags(gCurrSaveFileNum - 1, COURSE_JRB - 1) & 1)) {
        colorIndex = 0;
    }

    //! fov is always set to 90.0f. If this line is removed, then the game crashes because fov is 0 on
    //! the first frame, which causes a floating point divide by 0
    fovq = q(90);
    sSkyBoxInfo[player].yaw = atan2sq(cameraFaceZq, cameraFaceXq);
    sSkyBoxInfo[player].pitch = atan2sq(sqrtq64((q64) qmul(cameraFaceXq, cameraFaceXq) + (q64) qmul(cameraFaceZq, cameraFaceZq)), cameraFaceYq);
    sSkyBoxInfo[player].scaledX = calculate_skybox_scaled_xq(player);
    sSkyBoxInfo[player].scaledY = calculate_skybox_scaled_yq(player);
    sSkyBoxInfo[player].upperLeftTile = get_top_left_tile_idx(player);

    return init_skybox_display_list(player, background, colorIndex);
}