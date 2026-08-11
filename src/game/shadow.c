#include <PR/ultratypes.h>
#include <PR/gbi.h>
#include "libc/math.h"

#include "engine/math_util.h"
#include "engine/surface_collision.h"
#include "geo_misc.h"
#include "level_table.h"
#include "memory.h"
#include "object_list_processor.h"
#include "rendering_graph_node.h"
#include "segment2.h"
#include "shadow.h"
#include "sm64.h"

#ifndef TARGET_N64
// Avoid Z-fighting
#define find_floor_height_and_data 0.4 + find_floor_height_and_data
#endif

/**
 * @file shadow.c
 * This file implements a self-contained subsystem used to draw shadows.
 */

/**
 * Encapsulation of information about a shadow.
 */
struct Shadow {
    /* The (x, y, z) position of the object whose shadow this is. */
    q32 parentXq;
    q32 parentYq;
    q32 parentZq;
    /* The y-position of the floor (or water or lava) underneath the object. */
    q32 floorHeightq;
    /* Initial (unmodified) size of the shadow. */
    q32 shadowScaleq;
    /* (nx, ny, nz) normal vector of the floor underneath the object. */
    q32 floorNormalXq;
    q32 floorNormalYq;
    q32 floorNormalZq;
    /* "originOffset" of the floor underneath the object. */
    q32 floorOriginOffsetq;
    /* Angle describing "which way a marble would roll," in degrees. */
    q32 floorDownwardAngleq;
    /* Angle describing "how tilted the ground is" in degrees (-90 to 90). */
    q32 floorTiltq;
    /* Initial solidity of the shadow, from 0 to 255 (just an alpha value). */
    u8 solidity;
};

/**
 * Constant to indicate that a shadow should not be drawn.
 * This is used to disable shadows during specific frames of Mario's
 * animations.
 */
#define SHADOW_SOLIDITY_NO_SHADOW 0
/**
 * Constant to indicate that a shadow's solidity has been pre-set by a previous
 * function and should not be overwritten.
 */
#define SHADOW_SOILDITY_ALREADY_SET 1
/**
 * Constant to indicate that a shadow's solidity has not yet been set.
 */
#define SHADOW_SOLIDITY_NOT_YET_SET 2

/**
 * Constant to indicate any sort of circular shadow.
 */
#define SHADOW_SHAPE_CIRCLE 10
/**
 * Constant to indicate any sort of rectangular shadow.
 */
#define SHADOW_SHAPE_SQUARE 20

/**
 * Constant to indicate a shadow consists of 9 vertices.
 */
#define SHADOW_WITH_9_VERTS 0
/**
 * Constant to indicate a shadow consists of 4 vertices.
 */
#define SHADOW_WITH_4_VERTS 1

/**
 * A struct containing info about hardcoded rectangle shadows.
 */
typedef struct {
    /* Half the width of the rectangle. */
    q32 halfWidthq;
    /* Half the length of the rectangle. */
    q32 halfLengthq;
    /* Flag for if this shadow be smaller when its object is further away. */
    s8 scaleWithDistance;
} ShadowRectangle;

/**
 * An array consisting of all the hardcoded rectangle shadows in the game.
 */
ShadowRectangle rectangles[2] = {
    /* Shadow for Spindels. */
    { q(360), q(230), TRUE },
    /* Shadow for Whomps. */
    { q(200), q(180), TRUE }
};

// See shadow.h for documentation.
s8 gShadowAboveWaterOrLava;
s8 gMarioOnIceOrCarpet;
s8 sMarioOnFlyingCarpet;
s16 sSurfaceTypeBelowShadow;

/**
 * Let (oldZ, oldX) be the relative coordinates of a point on a rectangle,
 * assumed to be centered at the origin on the standard SM64 X-Z plane. This
 * function will update (newZ, newX) to equal the new coordinates of that point
 * after a rotation equal to the yaw of the current graph node object.
 */
void rotate_rectangleq(q32 *newZq, q32 *newXq, q32 oldZq, q32 oldXq) {
    struct Object *obj = (struct Object *) gCurGraphNodeObject;
    *newZq = qmul(oldZq, cosqs(obj->oFaceAngleYaw)) - qmul(oldXq, sinqs(obj->oFaceAngleYaw));
    *newXq = qmul(oldZq, sinqs(obj->oFaceAngleYaw)) + qmul(oldXq, cosqs(obj->oFaceAngleYaw));
}

/**
 * Return atan2(a, b) in degrees. Note that the argument order is swapped from
 * the standard atan2.
 */
f32 atan2_deg(f32 a, f32 b) {
    return ((f32) atan2s(a, b) / 65535.0 * 360.0);
}
q32 atan2_degq(q32 aq, q32 bq) {
#ifdef USE_FLOATS
	return q(atan2_deg(qtof(aq), qtof(bq)));
#else
    return (q(atan2sq(aq, bq)) * 360 / 65535);
#endif
}

/**
 * Shrink a shadow when its parent object is further from the floor, given the
 * initial size of the shadow and the current distance.
 */
q32 scale_shadow_with_distanceq(q32 initialq, q32 distFromFloorq) {
    q32 newScaleq;

    if (distFromFloorq <= 0) {
        newScaleq = initialq;
    } else if (distFromFloorq >= q(600)) {
        newScaleq = initialq / 2;
    } else {
        newScaleq = qmul(initialq, QONE - distFromFloorq / (q32) 1200);
    }

    return newScaleq;
}

/**
 * Disable a shadow when its parent object is more than 600 units from the ground.
 */
q32 disable_shadow_with_distanceq(q32 shadowScaleq, q32 distFromFloorq) {
    if (distFromFloorq >= q(600)) {
        return 0;
    } else {
        return shadowScaleq;
    }
}

/**
 * Dim a shadow when its parent object is further from the ground.
 */
u8 dim_shadow_with_distanceq(u8 solidity, q32 distFromFloorq) {
    if (solidity < 121 || distFromFloorq <= 0) {
        return solidity;
    } else if (distFromFloorq >= q(600)) {
        return 120;
    } else {
        return qtrunc(distFromFloorq * (120 - solidity) / (q32) 600 + q(solidity));
    }
}

/**
 * Return the water level below a shadow, or 0 if the water level is below
 * -10,000.
 */
q32 get_water_level_below_shadowq(struct Shadow *s) {
    q32 waterLevelq = find_water_levelq(s->parentXq, s->parentZq);
    if (waterLevelq < q(FLOOR_LOWER_LIMIT_SHADOW)) {
        return 0;
    } else if (s->parentYq >= waterLevelq && s->floorHeightq <= waterLevelq) {
        gShadowAboveWaterOrLava = TRUE;
        return waterLevelq;
    }
    //! @bug Missing return statement. This compiles to return `waterLevel`
    //! incidentally.
#ifdef AVOID_UB
    return waterLevelq;
#endif
}

/**
 * Initialize a shadow. Return 0 on success, 1 on failure.
 *
 * @param xPos,yPos,zPos Position of the parent object (not the shadow)
 * @param shadowScale Diameter of the shadow
 * @param overwriteSolidity Flag for whether the existing shadow solidity should
 *                          be dimmed based on its distance to the floor
 */
s8 init_shadowq(struct Shadow *s, f32 xPosq, f32 yPosq, f32 zPosq, s16 shadowScale, u8 overwriteSolidity) {
    q32 waterLevelq = 0;
    q32 floorSteepnessq;
    struct FloorGeometry *floorGeometry;

    s->parentXq = xPosq;
    s->parentYq = yPosq;
    s->parentZq = zPosq;

    s->floorHeightq = find_floor_height_and_dataq(s->parentXq, s->parentYq, s->parentZq, &floorGeometry);

    if (gEnvironmentRegions != 0) {
        waterLevelq = get_water_level_below_shadowq(s);
    }
    if (gShadowAboveWaterOrLava) {
        //! @bug Use of potentially undefined variable `waterLevel`
        s->floorHeightq = waterLevelq;

        // Assume that the water is flat.
        s->floorNormalXq = 0;
        s->floorNormalYq = QONE;
        s->floorNormalZq = 0;
        s->floorOriginOffsetq = -waterLevelq;
    } else {
        // Don't draw a shadow if the floor is lower than expected possible,
        // or if the y-normal is negative (an unexpected result).
        if (s->floorHeightq < q(FLOOR_LOWER_LIMIT_SHADOW) || floorGeometry->normalYq <= 0) {
            return 1;
        }

        s->floorNormalXq = floorGeometry->normalXq;
        s->floorNormalYq = floorGeometry->normalYq;
        s->floorNormalZq = floorGeometry->normalZq;
        s->floorOriginOffsetq = floorGeometry->originOffsetq;
    }

    if (overwriteSolidity) {
        s->solidity = dim_shadow_with_distanceq(overwriteSolidity, yPosq - s->floorHeightq);
    }

    s->shadowScaleq = scale_shadow_with_distanceq(shadowScale, yPosq - s->floorHeightq);

    s->floorDownwardAngleq = atan2_degq(s->floorNormalZq, s->floorNormalXq);

    floorSteepnessq = sqrtq(qmul(s->floorNormalXq, s->floorNormalXq) + qmul(s->floorNormalZq, s->floorNormalZq));

    // This if-statement avoids dividing by 0.
    if (floorSteepnessq == 0) {
        s->floorTiltq = 0;
    } else {
        s->floorTiltq = q(90) - atan2_degq(floorSteepnessq, s->floorNormalYq);
    }
    return 0;
}

/**
 * Given a `vertexNum` from a shadow with nine vertices, update the
 * texture coordinates corresponding to that vertex. That is:
 *      0 = (-15, -15)         1 = (0, -15)         2 = (15, -15)
 *      3 = (-15,   0)         4 = (0,   0)         5 = (15,   0)
 *      6 = (-15,  15)         7 = (0,  15)         8 = (15,  15)
 */
void get_texture_coords_9_vertices(s8 vertexNum, s16 *textureX, s16 *textureY) {
    *textureX = vertexNum % 3 * 15 - 15;
    *textureY = vertexNum / 3 * 15 - 15;
}

/**
 * Given a `vertexNum` from a shadow with four vertices, update the
 * texture coordinates corresponding to that vertex. That is:
 *      0 = (-15, -15)         1 = (15, -15)
 *      2 = (-15,  15)         3 = (15,  15)
 */
void get_texture_coords_4_vertices(s8 vertexNum, s16 *textureX, s16 *textureY) {
    *textureX = (vertexNum % 2) * 2 * 15 - 15;
    *textureY = (vertexNum / 2) * 2 * 15 - 15;
}

/**
 * Make a shadow's vertex at a position relative to its parent.
 *
 * @param vertices A preallocated display list for vertices
 * @param index Index into `vertices` to insert the vertex
 * @param relX,relY,relZ Vertex position relative to its parent object
 * @param alpha Opacity of the vertex
 * @param shadowVertexType One of SHADOW_WITH_9_VERTS or SHADOW_WITH_4_VERTS
 */
void make_shadow_vertex_at_xyzq(Vtx *vertices, s8 index, q32 relXq, q32 relYq, q32 relZq, u8 alpha,
                               s8 shadowVertexType) {
    return;
    s16 vtxX = roundq(relXq);
    s16 vtxY = roundq(relYq);
    s16 vtxZ = roundq(relZq);
    s16 textureX, textureY;

    switch (shadowVertexType) {
        case SHADOW_WITH_9_VERTS:
            get_texture_coords_9_vertices(index, &textureX, &textureY);
            break;
        case SHADOW_WITH_4_VERTS:
            get_texture_coords_4_vertices(index, &textureX, &textureY);
            break;
    }

    // Move the shadow up and over slightly while standing on a flying carpet.
    if (sMarioOnFlyingCarpet) {
        vtxX += 5;
        vtxY += 5;
        vtxZ += 5;
    }
    make_vertex(vertices, index, vtxX, vtxY, vtxZ, textureX << 5, textureY << 5, 255, 255, 255,
                alpha // shadows are black
    );
}

/**
 * Given an (x, z)-position close to a shadow, extrapolate the y-position
 * according to the floor's normal vector.
 */
q32 extrapolate_vertex_y_positionq(struct Shadow s, q32 vtxXq, q32 vtxZq) {
    return qdiv(-(qmul(s.floorNormalXq, vtxXq) + qmul(s.floorNormalZq, vtxZq) + s.floorOriginOffsetq), s.floorNormalYq);
}

/**
 * Given a shadow vertex with the given `index`, return the corresponding texture
 * coordinates ranging in the square with corners at (-1, -1), (1, -1), (-1, 1),
 * and (1, 1) in the x-z plane. See `get_texture_coords_9_vertices()` and
 * `get_texture_coords_4_vertices()`, which have similar functionality, but
 * return 15 times these values.
 */
void get_vertex_coords(s8 index, s8 shadowVertexType, s8 *xCoord, s8 *zCoord) {
    *xCoord = index % (3 - shadowVertexType) - 1;
    *zCoord = index / (3 - shadowVertexType) - 1;

    // This just corrects the 4-vertex case to have consistent results with the
    // 9-vertex case.
    if (shadowVertexType == SHADOW_WITH_4_VERTS) {
        if (*xCoord == 0) {
            *xCoord = 1;
        }
        if (*zCoord == 0) {
            *zCoord = 1;
        }
    }
}

/**
 * Populate `xPosVtx`, `yPosVtx`, and `zPosVtx` with the (x, y, z) position of the
 * shadow vertex with the given index. If the shadow is to have 9 vertices,
 * then each of those vertices is clamped down to the floor below it. Otherwise,
 * in the 4 vertex case, the vertex positions are extrapolated from the center
 * of the shadow.
 *
 * In practice, due to the if-statement in `make_shadow_vertex()`, the 9
 * vertex and 4 vertex cases are identical, and the above-described clamping
 * behavior is overwritten.
 */
void calculate_vertex_xyzq(s8 index, struct Shadow s, q32 *xPosVtxq, q32 *yPosVtxq, q32 *zPosVtxq,
                          s8 shadowVertexType) {
    q32 tiltedScaleq = qmul(cosq(qmul(s.floorTiltq, M_PIq / (q32) 180)), s.shadowScaleq);
    q32 downwardAngleq = qmul(s.floorDownwardAngleq, M_PIq / (q32) 180);
    q32 halfScaleq;
    q32 halfTiltedScaleq;
    s8 xCoordUnit;
    s8 zCoordUnit;
    struct FloorGeometry *dummy;

    // This makes xCoordUnit and yCoordUnit each one of -1, 0, or 1.
    get_vertex_coords(index, shadowVertexType, &xCoordUnit, &zCoordUnit);

    halfScaleq = (xCoordUnit * s.shadowScaleq) / 2;
    halfTiltedScaleq = (zCoordUnit * tiltedScaleq) / 2;

    *xPosVtxq = qmul(halfTiltedScaleq, sinq(downwardAngleq)) + qmul(halfScaleq, cosq(downwardAngleq)) + s.parentXq;
    *zPosVtxq = qmul(halfTiltedScaleq, cosq(downwardAngleq)) - qmul(halfScaleq, sinq(downwardAngleq)) + s.parentZq;

    if (gShadowAboveWaterOrLava) {
        *yPosVtxq = s.floorHeightq;
    } else {
        switch (shadowVertexType) {
            /**
             * Note that this dichotomy is later overwritten in
             * make_shadow_vertex().
             */
            case SHADOW_WITH_9_VERTS:
                // Clamp this vertex's y-position to that of the floor directly
                // below it, which may differ from the floor below the center
                // vertex.
                *yPosVtxq = find_floor_height_and_dataq(*xPosVtxq, s.parentYq, *zPosVtxq, &dummy);
                break;
            case SHADOW_WITH_4_VERTS:
                // Do not clamp. Instead, extrapolate the y-position of this
                // vertex based on the directly floor below the parent object.
                *yPosVtxq = extrapolate_vertex_y_positionq(s, *xPosVtxq, *zPosVtxq);
                break;
        }
    }
}

/**
 * Given a vertex's location, return the dot product of the
 * position of that vertex (relative to the shadow's center) with the floor
 * normal (at the shadow's center).
 *
 * Since it is a dot product, this returns 0 if these two vectors are
 * perpendicular, meaning the ground is locally flat. It returns nonzero
 * in most cases where `vtxY` is on a different floor triangle from the
 * center vertex, as in the case with SHADOW_WITH_9_VERTS, which sets
 * the y-value from `find_floor_height_and_data`. (See the bottom of
 * `calculate_vertex_xyz`.)
 */
s16 floor_local_tiltq(struct Shadow s, q32 vtxXq, q32 vtxYq, q32 vtxZq) {
    q32 relXq = vtxXq - s.parentXq;
    q32 relYq = vtxYq - s.floorHeightq;
    q32 relZq = vtxZq - s.parentZq;

    q32 retq = qmul(relXq, s.floorNormalXq) + qmul(relYq, s.floorNormalYq) + qmul(relZq, s.floorNormalZq);
    return retq;
}

/**
 * Make a particular vertex from a shadow, calculating its position and solidity.
 */
void make_shadow_vertex(Vtx *vertices, s8 index, struct Shadow s, s8 shadowVertexType) {
    q32 xPosVtxq, yPosVtxq, zPosVtxq;
    q32 relXq, relYq, relZq;

    u8 solidity = s.solidity;
    if (gShadowAboveWaterOrLava) {
        solidity = 200;
    }

    calculate_vertex_xyzq(index, s, &xPosVtxq, &yPosVtxq, &zPosVtxq, shadowVertexType);

    /**
     * This is the hack that makes "SHADOW_WITH_9_VERTS" act identically to
     * "SHADOW_WITH_4_VERTS" in the game; this same hack is disabled by the
     * GameShark code in this video: https://youtu.be/MSIh4rtNGF0. The code in
     * the video makes `extrapolate_vertex_y_position` return the same value as
     * the last-called function that returns a float; in this case, that's
     * `find_floor_height_and_data`, which this if-statement was designed to
     * overwrite in the first place. Thus, this if-statement is disabled by that
     * code.
     *
     * The last condition here means the y-position calculated previously
     * was probably on a different floor triangle from the center vertex.
     * The gShadowAboveWaterOrLava check is redundant, since `floor_local_tilt`
     * will always be 0 over water or lava (since they are always flat).
     */
    if (shadowVertexType == SHADOW_WITH_9_VERTS && !gShadowAboveWaterOrLava
        && floor_local_tiltq(s, xPosVtxq, yPosVtxq, zPosVtxq) != 0) {
        yPosVtxq = extrapolate_vertex_y_positionq(s, xPosVtxq, zPosVtxq);
        solidity = 0;
    }
    relXq = xPosVtxq - s.parentXq;
    relYq = yPosVtxq - s.parentYq;
    relZq = zPosVtxq - s.parentZq;

    make_shadow_vertex_at_xyzq(vertices, index, relXq, relYq, relZq, solidity, shadowVertexType);
}

/**
 * Add a shadow to the given display list.
 */
void add_shadow_to_display_list(Gfx *displayListHead, Vtx *verts, s8 shadowVertexType, s8 shadowShape) {
    switch (shadowShape) {
        case SHADOW_SHAPE_CIRCLE:
            gSPDisplayList(displayListHead++, dl_shadow_circle);
            break;
        case SHADOW_SHAPE_SQUARE:
            gSPDisplayList(displayListHead++, dl_shadow_square) break;
    }
    switch (shadowVertexType) {
        case SHADOW_WITH_9_VERTS:
            gSPVertex(displayListHead++, verts, 9, 0);
            gSPDisplayList(displayListHead++, dl_shadow_9_verts);
            break;
        case SHADOW_WITH_4_VERTS:
            gSPVertex(displayListHead++, verts, 4, 0);
            gSPDisplayList(displayListHead++, dl_shadow_4_verts);
            break;
    }
    gSPDisplayList(displayListHead++, dl_shadow_end);
    gSPEndDisplayList(displayListHead);
}

/**
 * Linearly interpolate a shadow's solidity between zero and finalSolidity
 * depending on curr's relation to start and end.
 */
void linearly_interpolate_solidity_positive(struct Shadow *s, u8 finalSolidity, s16 curr, s16 start,
                                            s16 end) {
    if (curr >= 0 && curr < start) {
        s->solidity = 0;
    } else if (end < curr) {
        s->solidity = finalSolidity;
    } else {
        s->solidity = (f32) finalSolidity * (curr - start) / (end - start);
    }
}

/**
 * Linearly interpolate a shadow's solidity between initialSolidity and zero
 * depending on curr's relation to start and end. Note that if curr < start,
 * the solidity will be zero.
 */
void linearly_interpolate_solidity_negative(struct Shadow *s, u8 initialSolidity, s16 curr, s16 start,
                                            s16 end) {
    // The curr < start case is not handled. Thus, if start != 0, this function
    // will have the surprising behavior of hiding the shadow until start.
    // This is not necessarily a bug, since this function is only used once,
    // with start == 0.
    if (curr >= start && end >= curr) {
        s->solidity = ((f32) initialSolidity * (1.0 - (f32)(curr - start) / (end - start)));
    } else {
        s->solidity = 0;
    }
}

/**
 * Change a shadow's solidity based on the player's current animation frame.
 */
s8 correct_shadow_solidity_for_animations(UNUSED s32 isLuigi, u8 initialSolidity, struct Shadow *shadow) {
    struct Object *player;
    s8 ret;
    s16 animFrame;

    //switch (isLuigi) {
    //    case 0:
            player = gMarioObject;
    //        break;
    //    case 1:
    //        /**
    //         * This is evidence of a removed second player, likely Luigi.
    //         * This variable lies in memory just after the gMarioObject and
    //         * has the same type of shadow that Mario does. The `isLuigi`
    //         * variable is never 1 in the game. Note that since this was a
    //         * switch-case, not an if-statement, the programmers possibly
    //         * intended there to be even more than 2 characters.
    //         */
    //        player = gLuigiObject;
    //        break;
    //}

    animFrame = player->header.gfx.animInfo.animFrame;
    switch (player->header.gfx.animInfo.animID) {
        case MARIO_ANIM_IDLE_ON_LEDGE:
            ret = SHADOW_SOLIDITY_NO_SHADOW;
            break;
        case MARIO_ANIM_FAST_LEDGE_GRAB:
            linearly_interpolate_solidity_positive(shadow, initialSolidity, animFrame, 5, 14);
            ret = SHADOW_SOILDITY_ALREADY_SET;
            break;
        case MARIO_ANIM_SLOW_LEDGE_GRAB:
            linearly_interpolate_solidity_positive(shadow, initialSolidity, animFrame, 21, 33);
            ret = SHADOW_SOILDITY_ALREADY_SET;
            break;
        case MARIO_ANIM_CLIMB_DOWN_LEDGE:
            linearly_interpolate_solidity_negative(shadow, initialSolidity, animFrame, 0, 5);
            ret = SHADOW_SOILDITY_ALREADY_SET;
            break;
        default:
            ret = SHADOW_SOLIDITY_NOT_YET_SET;
            break;
    }
    return ret;
}

/**
 * Slightly change the height of a shadow in levels with lava.
 */
void correct_lava_shadow_height(struct Shadow *s) {
    if (gCurrLevelNum == LEVEL_BITFS && sSurfaceTypeBelowShadow == SURFACE_BURNING) {
        if (s->floorHeightq < q(-3000)) {
            s->floorHeightq = q(-3062);
            gShadowAboveWaterOrLava = TRUE;
        } else if (s->floorHeightq > q(3400)) {
            s->floorHeightq = q(3492);
            gShadowAboveWaterOrLava = TRUE;
        }
    } else if (gCurrLevelNum == LEVEL_LLL && gCurrAreaIndex == 1
               && sSurfaceTypeBelowShadow == SURFACE_BURNING) {
        s->floorHeightq = q(5);
        gShadowAboveWaterOrLava = TRUE;
    }
}

/**
 * Create a shadow under a player, correcting that shadow's opacity during
 * appropriate animations and other states.
 */
Gfx *create_shadow_playerq(q32 xPosq, q32 yPosq, q32 zPosq, s16 shadowScale, u8 solidity, s32 isLuigi) {
    Vtx *verts;
    Gfx *displayList;
    struct Shadow shadow;
    s8 ret = 0;
    s32 i;

    // Update global variables about whether Mario is on a flying carpet.
    if (gCurrLevelNum == LEVEL_RR && sSurfaceTypeBelowShadow != SURFACE_DEATH_PLANE) {
        switch (gFlyingCarpetState) {
            case FLYING_CARPET_MOVING_WITHOUT_MARIO:
                gMarioOnIceOrCarpet = 1;
                sMarioOnFlyingCarpet = 1;
                break;
            case FLYING_CARPET_MOVING_WITH_MARIO:
                gMarioOnIceOrCarpet = 1;
                break;
        }
    }

    switch (correct_shadow_solidity_for_animations(isLuigi, solidity, &shadow)) {
        case SHADOW_SOLIDITY_NO_SHADOW:
            return NULL;
            break;
        case SHADOW_SOILDITY_ALREADY_SET:
            ret = init_shadowq(&shadow, xPosq, yPosq, zPosq, shadowScale, /* overwriteSolidity */ 0);
            break;
        case SHADOW_SOLIDITY_NOT_YET_SET:
            ret = init_shadowq(&shadow, xPosq, yPosq, zPosq, shadowScale, solidity);
            break;
    }
    if (ret != 0) {
        return NULL;
    }

    verts = alloc_display_list(9 * sizeof(Vtx));
    displayList = alloc_display_list(5 * sizeof(Gfx));
    if (verts == NULL || displayList == NULL) {
        return NULL;
    }

    correct_lava_shadow_height(&shadow);

    for (i = 0; i < 9; i++) {
        make_shadow_vertex(verts, i, shadow, SHADOW_WITH_9_VERTS);
    }
    add_shadow_to_display_list(displayList, verts, SHADOW_WITH_9_VERTS, SHADOW_SHAPE_CIRCLE);
    return displayList;
}

/**
 * Create a circular shadow composed of 9 vertices.
 */
Gfx *create_shadow_circle_9_vertsq(q32 xPosq, q32 yPosq, q32 zPosq, s16 shadowScale, u8 solidity) {
    Vtx *verts;
    Gfx *displayList;
    struct Shadow shadow;
    s32 i;

    if (init_shadowq(&shadow, xPosq, yPosq, zPosq, shadowScale, solidity) != 0) {
        return NULL;
    }

    verts = alloc_display_list(9 * sizeof(Vtx));
    displayList = alloc_display_list(5 * sizeof(Gfx));

    if (verts == NULL || displayList == NULL) {
        return 0;
    }
    for (i = 0; i < 9; i++) {
        make_shadow_vertex(verts, i, shadow, SHADOW_WITH_9_VERTS);
    }
    add_shadow_to_display_list(displayList, verts, SHADOW_WITH_9_VERTS, SHADOW_SHAPE_CIRCLE);
    return displayList;
}

/**
 * Create a circular shadow composed of 4 vertices.
 */
Gfx *create_shadow_circle_4_vertsq(q32 xPosq, q32 yPosq, q32 zPosq, s16 shadowScale, u8 solidity) {
    Vtx *verts;
    Gfx *displayList;
    struct Shadow shadow;
    s32 i;

    if (init_shadowq(&shadow, xPosq, yPosq, zPosq, shadowScale, solidity) != 0) {
        return NULL;
    }

    verts = alloc_display_list(4 * sizeof(Vtx));
    displayList = alloc_display_list(5 * sizeof(Gfx));

    if (verts == NULL || displayList == NULL) {
        return 0;
    }

    for (i = 0; i < 4; i++) {
        make_shadow_vertex(verts, i, shadow, SHADOW_WITH_4_VERTS);
    }
    add_shadow_to_display_list(displayList, verts, SHADOW_WITH_4_VERTS, SHADOW_SHAPE_CIRCLE);
    return displayList;
}

/**
 * Create a circular shadow composed of 4 vertices and assume that the ground
 * underneath it is totally flat.
 */
Gfx *create_shadow_circle_assuming_flat_groundq(q32 xPosq, q32 yPosq, q32 zPosq, s16 shadowScale,
                                               u8 solidity) {
    Vtx *verts;
    Gfx *displayList;
    struct FloorGeometry *dummy; // only for calling find_floor_height_and_data
    q32 distBelowFloorq;
    q32 floorHeightq = find_floor_height_and_dataq(xPosq, yPosq, zPosq, &dummy);
    q32 radiusq = q(shadowScale) / 2;

    if (floorHeightq < q(FLOOR_LOWER_LIMIT_SHADOW)) {
        return NULL;
    } else {
        distBelowFloorq = floorHeightq - yPosq;
    }

    verts = alloc_display_list(4 * sizeof(Vtx));
    displayList = alloc_display_list(5 * sizeof(Gfx));

    if (verts == NULL || displayList == NULL) {
        return 0;
    }

    make_shadow_vertex_at_xyzq(verts, 0, -radiusq, distBelowFloorq, -radiusq, solidity, 1);
    make_shadow_vertex_at_xyzq(verts, 1, radiusq, distBelowFloorq, -radiusq, solidity, 1);
    make_shadow_vertex_at_xyzq(verts, 2, -radiusq, distBelowFloorq, radiusq, solidity, 1);
    make_shadow_vertex_at_xyzq(verts, 3, radiusq, distBelowFloorq, radiusq, solidity, 1);

    add_shadow_to_display_list(displayList, verts, SHADOW_WITH_4_VERTS, SHADOW_SHAPE_CIRCLE);
    return displayList;
}

/**
 * Create a rectangular shadow composed of 4 vertices. This assumes the ground
 * underneath the shadow is totally flat.
 */
Gfx *create_shadow_rectangleq(q32 halfWidthq, q32 halfLengthq, q32 relYq, u8 solidity) {
    Vtx *verts = alloc_display_list(4 * sizeof(Vtx));
    Gfx *displayList = alloc_display_list(5 * sizeof(Gfx));
    q32 frontLeftXq, frontLeftZq, frontRightXq, frontRightZq, backLeftXq, backLeftZq, backRightXq, backRightZq;

    if (verts == NULL || displayList == NULL) {
        return NULL;
    }

    // Rotate the shadow based on the parent object's face angle.
    rotate_rectangleq(&frontLeftZq, &frontLeftXq, -halfLengthq, -halfWidthq);
    rotate_rectangleq(&frontRightZq, &frontRightXq, -halfLengthq, halfWidthq);
    rotate_rectangleq(&backLeftZq, &backLeftXq, halfLengthq, -halfWidthq);
    rotate_rectangleq(&backRightZq, &backRightXq, halfLengthq, halfWidthq);

    make_shadow_vertex_at_xyzq(verts, 0, frontLeftXq, relYq, frontLeftZq, solidity, 1);
    make_shadow_vertex_at_xyzq(verts, 1, frontRightXq, relYq, frontRightZq, solidity, 1);
    make_shadow_vertex_at_xyzq(verts, 2, backLeftXq, relYq, backLeftZq, solidity, 1);
    make_shadow_vertex_at_xyzq(verts, 3, backRightXq, relYq, backRightZq, solidity, 1);

    add_shadow_to_display_list(displayList, verts, SHADOW_WITH_4_VERTS, SHADOW_SHAPE_SQUARE);
    return displayList;
}

/**
 * Populate `shadowHeight` and `solidity` appropriately; the default solidity
 * value is 200. Return 0 if a shadow should be drawn, 1 if not.
 */
s32 get_shadow_height_solidityq(q32 xPosq, q32 yPosq, q32 zPosq, q32 *shadowHeightq, u8 *solidity) {
    struct FloorGeometry *dummy;
    q32 waterLevelq;
    *shadowHeightq = find_floor_height_and_dataq(xPosq, yPosq, zPosq, &dummy);

    if (*shadowHeightq < q(FLOOR_LOWER_LIMIT_SHADOW)) {
        return 1;
    } else {
        waterLevelq = find_water_levelq(xPosq, zPosq);

        if (waterLevelq < q(FLOOR_LOWER_LIMIT_SHADOW)) {
            // Dead if-statement. There may have been an assert here.
        } else if (yPosq >= waterLevelq && waterLevelq >= *shadowHeightq) {
            gShadowAboveWaterOrLava = TRUE;
            *shadowHeightq = waterLevelq;
            *solidity = 200;
        }
    }
    return 0;
}

/**
 * Create a square shadow composed of 4 vertices.
 */
Gfx *create_shadow_squareq(q32 xPosq, q32 yPosq, q32 zPosq, s16 shadowScale, u8 solidity, s8 shadowType) {
    q32 shadowHeightq;
    q32 distFromShadowq;
    q32 shadowRadiusq;

    if (get_shadow_height_solidityq(xPosq, yPosq, zPosq, &shadowHeightq, &solidity) != 0) {
        return NULL;
    }

    distFromShadowq = yPosq - shadowHeightq;
    switch (shadowType) {
        case SHADOW_SQUARE_PERMANENT:
            shadowRadiusq = q(shadowScale) / 2;
            break;
        case SHADOW_SQUARE_SCALABLE:
            shadowRadiusq = scale_shadow_with_distanceq(q(shadowScale), distFromShadowq) / 2;
            break;
        case SHADOW_SQUARE_TOGGLABLE:
            shadowRadiusq = disable_shadow_with_distanceq(q(shadowScale), distFromShadowq) / 2;
            break;
        default:
            return NULL;
    }

    return create_shadow_rectangleq(shadowRadiusq, shadowRadiusq, -distFromShadowq, solidity);
}

/**
 * Create a rectangular shadow whose parameters have been hardcoded in the
 * `rectangles` array.
 */
Gfx *create_shadow_hardcoded_rectangleq(q32 xPosq, q32 yPosq, q32 zPosq, UNUSED s16 shadowScale,
                                       u8 solidity, s8 shadowType) {
    q32 shadowHeightq;
    q32 distFromShadowq;
    q32 halfWidthq;
    q32 halfLengthq;
    s8 idx = shadowType - SHADOW_RECTANGLE_HARDCODED_OFFSET;

    if (get_shadow_height_solidityq(xPosq, yPosq, zPosq, &shadowHeightq, &solidity) != 0) {
        return NULL;
    }

    distFromShadowq = yPosq - shadowHeightq;
    /**
     * Note that idx could be negative or otherwise out of the bounds of
     * the `rectangles` array. In practice, it never is, because this was
     * only used twice.
     */
    if (rectangles[idx].scaleWithDistance == TRUE) {
        halfWidthq = scale_shadow_with_distanceq(rectangles[idx].halfWidthq, distFromShadowq);
        halfLengthq = scale_shadow_with_distanceq(rectangles[idx].halfLengthq, distFromShadowq);
    } else {
        // This code is never used because the third element of the rectangle
        // struct is always TRUE.
        halfWidthq = rectangles[idx].halfWidthq;
        halfLengthq = rectangles[idx].halfLengthq;
    }
    return create_shadow_rectangleq(halfWidthq, halfLengthq, -distFromShadowq, solidity);
}

/**
 * Create a shadow at the absolute position given, with the given parameters.
 * Return a pointer to the display list representing the shadow.
 */
Gfx *create_shadow_below_xyzq(q32 xPosq, q32 yPosq, q32 zPosq, s16 shadowScale, u8 shadowSolidity, s8 shadowType) {
    Gfx *displayList = NULL;
    struct Surface *pfloor;
    find_floor(xPosq, yPosq, zPosq, &pfloor);

    gShadowAboveWaterOrLava = FALSE;
    gMarioOnIceOrCarpet = 0;
    sMarioOnFlyingCarpet = 0;
    if (pfloor != NULL) {
        if (pfloor->type == SURFACE_ICE) {
            gMarioOnIceOrCarpet = 1;
        }
        sSurfaceTypeBelowShadow = pfloor->type;
    }
    switch (shadowType) {
        case SHADOW_CIRCLE_9_VERTS:
            displayList = create_shadow_circle_9_vertsq(xPosq, yPosq, zPosq, shadowScale, shadowSolidity);
            break;
        case SHADOW_CIRCLE_4_VERTS:
            displayList = create_shadow_circle_4_vertsq(xPosq, yPosq, zPosq, shadowScale, shadowSolidity);
            break;
        case SHADOW_CIRCLE_4_VERTS_FLAT_UNUSED: // unused shadow type
            displayList = create_shadow_circle_assuming_flat_groundq(xPosq, yPosq, zPosq, shadowScale,
                                                                    shadowSolidity);
            break;
        case SHADOW_SQUARE_PERMANENT:
        case SHADOW_SQUARE_SCALABLE:
        case SHADOW_SQUARE_TOGGLABLE:
            displayList = create_shadow_squareq(xPosq, yPosq, zPosq, shadowScale, shadowSolidity, shadowType);
            break;
        case SHADOW_CIRCLE_PLAYER:
            displayList = create_shadow_playerq(xPosq, yPosq, zPosq, shadowScale, shadowSolidity,
                                               /* isLuigi */ FALSE);
            break;
        default:
            displayList = create_shadow_hardcoded_rectangleq(xPosq, yPosq, zPosq, shadowScale,
                                                            shadowSolidity, shadowType);
            break;
    }
    return displayList;
}
