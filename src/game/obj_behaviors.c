#include <PR/ultratypes.h>

#include "sm64.h"
#include "area.h"
#include "audio/external.h"
#include "behavior_actions.h"
#include "behavior_data.h"
#include "camera.h"
#include "course_table.h"
#include "dialog_ids.h"
#include "engine/behavior_script.h"
#include "engine/math_util.h"
#include "engine/surface_collision.h"
#include "envfx_bubbles.h"
#include "game_init.h"
#include "ingame_menu.h"
#include "interaction.h"
#include "level_misc_macros.h"
#include "level_table.h"
#include "level_update.h"
#include "levels/bob/header.h"
#include "levels/ttm/header.h"
#include "mario.h"
#include "mario_actions_cutscene.h"
#include "mario_misc.h"
#include "memory.h"
#include "obj_behaviors.h"
#include "object_helpers.h"
#include "object_list_processor.h"
#include "rendering_graph_node.h"
#include "save_file.h"
#include "spawn_object.h"
#include "spawn_sound.h"
#include "rumble_init.h"

/**
 * @file obj_behaviors.c
 * This file contains a portion of the obj behaviors and many helper functions for those
 * specific behaviors. Few functions besides the bhv_ functions are used elsewhere in the repo.
 */

#define o gCurrentObject

#define OBJ_COL_FLAG_GROUNDED   (1 << 0)
#define OBJ_COL_FLAG_HIT_WALL   (1 << 1)
#define OBJ_COL_FLAG_UNDERWATER (1 << 2)
#define OBJ_COL_FLAG_NO_Y_VEL   (1 << 3)
#define OBJ_COL_FLAGS_LANDED    (OBJ_COL_FLAG_GROUNDED | OBJ_COL_FLAG_NO_Y_VEL)

/**
 * Current object floor as defined in object_step.
 */
static struct Surface *sObjFloor;

/**
 * Set to false when an object close to the floor should not be oriented in reference
 * to it. Happens with boulder, falling pillar, and the rolling snowman body.
 */
static s8 sOrientObjWithFloor = TRUE;

/**
 * Keeps track of Mario's previous non-zero room.
 * Helps keep track of room when Mario is over an object.
 */
s16 sPrevCheckMarioRoom = 0;

/**
 * Tracks whether or not Yoshi has walked/jumped off the roof.
 */
s8 sYoshiDead = FALSE;

extern void *ccm_seg7_trajectory_snowman;
extern void *inside_castle_seg7_trajectory_mips;

/**
 * Resets yoshi as spawned/despawned upon new file select.
 * Possibly a function with stubbed code.
 */
void set_yoshi_as_not_dead(void) {
    sYoshiDead = FALSE;
}

/**
 * An unused geo function. Bears strong similarity to geo_bits_bowser_coloring, and relates something
 * of the opacity of an object to something else. Perhaps like, giving a parent object the same
 * opacity?
 */
Gfx UNUSED *geo_obj_transparency_something(s32 callContext, struct GraphNode *node, UNUSED const ShortMatrix *mtxq) {
    Gfx *gfxHead;
    Gfx *gfx;
    struct Object *heldObject;
    struct Object *obj;
    UNUSED struct Object *unusedObject;
    UNUSED s32 pad;

    gfxHead = NULL;

    if (callContext == GEO_CONTEXT_RENDER) {
        heldObject = (struct Object *) gCurGraphNodeObject;
        obj = (struct Object *) node;
        unusedObject = (struct Object *) node;


        if (gCurGraphNodeHeldObject != NULL) {
            heldObject = gCurGraphNodeHeldObject->objNode;
        }

        gfxHead = alloc_display_list(3 * sizeof(Gfx));
        gfx = gfxHead;
        obj->header.gfx.node.flags =
            (obj->header.gfx.node.flags & 0xFF) | (GRAPH_NODE_TYPE_FUNCTIONAL | GRAPH_NODE_TYPE_400);

        gDPSetEnvColor(gfx++, 255, 255, 255, heldObject->oOpacity);

        gSPEndDisplayList(gfx);
    }

    return gfxHead;
}

/**
 * An absolute value function.
 */
f32 absf_2(f32 f) {
    if (f < 0) {
        f = -f;
    }
    return f;
}

/**
 * Turns an object away from floors/walls that it runs into.
 */
void turn_obj_away_from_surface(f32 velX, f32 velZ, f32 nX, UNUSED f32 nY, f32 nZ, f32 *objYawX,
                            f32 *objYawZ) {
    *objYawX = (nZ * nZ - nX * nX) * velX / (nX * nX + nZ * nZ)
               - 2 * velZ * (nX * nZ) / (nX * nX + nZ * nZ);

    *objYawZ = (nX * nX - nZ * nZ) * velZ / (nX * nX + nZ * nZ)
               - 2 * velX * (nX * nZ) / (nX * nX + nZ * nZ);
}

/**
 * Finds any wall collisions, applies them, and turns away from the surface.
 */
s8 obj_find_wall(f32 objNewX, f32 objY, f32 objNewZ, f32 objVelX, f32 objVelZ) {
    struct WallCollisionData hitbox;
    f32 wall_nX, wall_nY, wall_nZ, objYawX, objYawZ;

    hitbox.xq = q(objNewX);
    hitbox.yq = q(objY);
    hitbox.zq = q(objNewZ);
    hitbox.offsetYq = q(o->hitboxHeight_s16) / 2;
    hitbox.radiusq = q(o->hitboxRadius_s16);

    if (find_wall_collisions(&hitbox) != 0) {
        QSETFIELD(o, oPosX, hitbox.xq);
        QSETFIELD(o, oPosY, hitbox.yq);
        QSETFIELD(o, oPosZ, hitbox.zq);

        wall_nX = qtof((q32) hitbox.walls[0]->compressed_normal.x * QONE / COMPRESSED_NORMAL_ONE);
        wall_nY = qtof((q32) hitbox.walls[0]->compressed_normal.y * QONE / COMPRESSED_NORMAL_ONE);
        wall_nZ = qtof((q32) hitbox.walls[0]->compressed_normal.z * QONE / COMPRESSED_NORMAL_ONE);

        // Turns away from the first wall only.
        turn_obj_away_from_surface(objVelX, objVelZ, wall_nX, wall_nY, wall_nZ, &objYawX, &objYawZ);

        o->oMoveAngleYaw = atan2s(objYawZ, objYawX);
        return FALSE;
    }

    return TRUE;
}

/**
 * Turns an object away from steep floors, similarly to walls.
 */
s8 turn_obj_away_from_steep_floor(struct Surface *objFloor, f32 floorY, f32 objVelX, f32 objVelZ) {
    f32 objYawX, objYawZ;

    if (objFloor == NULL) {
        //! (OOB Object Crash) TRUNC overflow exception after 36 minutes
        o->oMoveAngleYaw += 32767.999200000002; /* ¯\_(ツ)_/¯ */
        return FALSE;
    }

    q32 floor_nXq = (q32) objFloor->compressed_normal.x * QONE / COMPRESSED_NORMAL_ONE;
    q32 floor_nYq = (q32) objFloor->compressed_normal.y * QONE / COMPRESSED_NORMAL_ONE;
    q32 floor_nZq = (q32) objFloor->compressed_normal.z * QONE / COMPRESSED_NORMAL_ONE;

    // If the floor is steep and we are below it (i.e. walking into it), turn away from the floor.
    if (floor_nYq < q(0.5) && floorY > FFIELD(o, oPosY)) {
        turn_obj_away_from_surface(objVelX, objVelZ, qtof(floor_nXq), qtof(floor_nYq), qtof(floor_nZq), &objYawX,
                               &objYawZ);
        o->oMoveAngleYaw = atan2s(objYawZ, objYawX);
        return FALSE;
    }

    return TRUE;
}

/**
 * Orients an object with the given normals, typically the surface under the object.
 */
void obj_orient_graph(struct Object *obj, f32 normalX, f32 normalY, f32 normalZ) {
    Vec3q objVisualPositionq, surfaceNormalsq;

    ShortMatrix *throwMatrixq;

    // Passes on orienting certain objects that shouldn't be oriented, like boulders.
    if (!sOrientObjWithFloor) {
        return;
    }

    // Passes on orienting billboard objects, i.e. coins, trees, etc.
    if (obj->header.gfx.node.flags & GRAPH_RENDER_BILLBOARD) {
        return;
    }

    throwMatrixq = alloc_display_list(sizeof(*throwMatrixq));
    // If out of memory, fail to try orienting the object.
    if (throwMatrixq == NULL) {
        return;
    }

    objVisualPositionq[0] = QFIELD(obj, oPosX);
    objVisualPositionq[1] = QFIELD(obj, oPosY) + QFIELD(obj, oGraphYOffset);
    objVisualPositionq[2] = QFIELD(obj, oPosZ);

    surfaceNormalsq[0] = q(normalX);
    surfaceNormalsq[1] = q(normalY);
    surfaceNormalsq[2] = q(normalZ);

    mtx_align_terrain_normal(throwMatrixq, surfaceNormalsq, objVisualPositionq, obj->oFaceAngleYaw);
	// TODO
    obj->header.gfx.throwMatrixq = throwMatrixq;
}

/**
 * Determines an object's forward speed multiplier.
 */
void calc_obj_friction(f32 *objFriction, f32 floor_nY) {
    if (floor_nY < 0.2 && QFIELD(o, oFriction) < q(0.9999)) {
        *objFriction = 0;
    } else {
        *objFriction = FFIELD(o, oFriction);
    }
}

/**
 * Updates an objects speed for gravity and updates Y position.
 */
void calc_new_obj_vel_and_pos_y(struct Surface *objFloor, f32 objFloorY, f32 objVelXinput, f32 objVelZinput) {
	q32 objVelXq = q(objVelXinput);
	q32 objVelZq = q(objVelZinput);
    q32 floor_nXq = (q32) objFloor->compressed_normal.x * QONE / COMPRESSED_NORMAL_ONE;
    q32 floor_nYq = (q32) objFloor->compressed_normal.y * QONE / COMPRESSED_NORMAL_ONE;
    q32 floor_nZq = (q32) objFloor->compressed_normal.z * QONE / COMPRESSED_NORMAL_ONE;
    f32 objFriction;

    // Caps vertical speed with a "terminal velocity".
    QMODFIELD(o, oVelY, -= QFIELD(o,oGravity));
    if (QFIELD(o, oVelY) > q(75)) {
        QSETFIELD(o, oVelY, q(75));
    }
    if (QFIELD(o, oVelY) < q(-75)) {
        QSETFIELD(o, oVelY, q(-75));
    }

    QMODFIELD(o, oPosY, += QFIELD(o, oVelY));

    //Snap the object up to the floor.
    if (FFIELD(o, oPosY) < objFloorY) {
        FSETFIELD(o, oPosY, objFloorY);

        // Bounces an object if the ground is hit fast enough.
        if (QFIELD(o, oVelY) < q(-17.5)) {
            QSETFIELD(o, oVelY, -(QFIELD(o, oVelY) / 2));
        } else {
            QSETFIELD(o, oVelY, 0);
        }
    }

    //! (Obj Position Crash) If you got an object with height past 2^31, the game would crash.
    if (IFIELD(o, oPosY) >= (s32) objFloorY && IFIELD(o, oPosY) < (s32) objFloorY + 37) {
        obj_orient_graph(o, qtof(floor_nXq), qtof(floor_nYq), qtof(floor_nZq));

        // Adds horizontal component of gravity for horizontal speed.
        objVelXq += qmul((q32) (qdiv(qmul(floor_nXq, (qmul(floor_nXq, floor_nXq) + qmul(floor_nZq, floor_nZq))),
                   (qmul(floor_nXq, floor_nXq) + qmul(floor_nYq, floor_nYq) + qmul(floor_nZq, floor_nZq))) * 2), QFIELD(o, oGravity));
        objVelZq += qmul((q32) (qdiv(qmul(floor_nZq, (qmul(floor_nXq, floor_nXq) + qmul(floor_nZq, floor_nZq))),
                   (qmul(floor_nXq, floor_nXq) + qmul(floor_nYq, floor_nYq) + qmul(floor_nZq, floor_nZq))) * 2), QFIELD(o, oGravity));

        //if (objVelX < 0.000001 && objVelX > -0.000001) {
        //    objVelX = 0;
        //}
        //if (objVelZ < 0.000001 && objVelZ > -0.000001) {
        //    objVelZ = 0;
        //}

        if (objVelXq != 0 || objVelZq != 0) {
            o->oMoveAngleYaw = atan2sq(objVelZq, objVelXq);
        }

        calc_obj_friction(&objFriction, qtof(floor_nYq));
        QSETFIELD(o, oForwardVel, qmul(sqrtq(qmul(objVelXq, objVelXq) + qmul(objVelZq, objVelZq)), q(objFriction)));
    }
}

void calc_new_obj_vel_and_pos_y_underwater(struct Surface *objFloor, f32 floorY, f32 objVelX, f32 objVelZ,
                                    f32 waterY) {
    q32 floor_nXq = (q32) objFloor->compressed_normal.x * QONE / COMPRESSED_NORMAL_ONE;
    q32 floor_nYq = (q32) objFloor->compressed_normal.y * QONE / COMPRESSED_NORMAL_ONE;
    q32 floor_nZq = (q32) objFloor->compressed_normal.z * QONE / COMPRESSED_NORMAL_ONE;

    q32 netYAccelq = qmul(q(1) - QFIELD(o, oBuoyancy), -QFIELD(o, oGravity));
    QMODFIELD(o, oVelY, -= netYAccelq);

    // Caps vertical speed with a "terminal velocity".
    if (QFIELD(o, oVelY) > q(75)) {
        QSETFIELD(o, oVelY, q(75));
    }
    if (QFIELD(o, oVelY) < q(-75)) {
        QSETFIELD(o, oVelY, q(-75));
    }

    QMODFIELD(o, oPosY, += QFIELD(o, oVelY));

    //Snap the object up to the floor.
    if (FFIELD(o, oPosY) < floorY) {
        FSETFIELD(o, oPosY, floorY);

        // Bounces an object if the ground is hit fast enough.
        if (QFIELD(o, oVelY) < q(-17.5)) {
            QSETFIELD(o, oVelY, -(QFIELD(o, oVelY) / 2));
        } else {
            QSETFIELD(o, oVelY, 0);
        }
    }

    // If moving fast near the surface of the water, flip vertical speed? To emulate skipping?
    if (QFIELD(o, oForwardVel) > q(12.5) && (waterY + 30.0f) > FFIELD(o, oPosY) && (waterY - 30.0f) < FFIELD(o, oPosY)) {
        QSETFIELD(o, oVelY, -QFIELD(o, oVelY));
    }

    if (IFIELD(o, oPosY) >= (s32) floorY && IFIELD(o, oPosY) < (s32) floorY + 37) {
        obj_orient_graph(o, qtof(floor_nXq), qtof(floor_nYq), qtof(floor_nZq));

        // Adds horizontal component of gravity for horizontal speed.
        objVelX += qmul(qdiv(qmul(floor_nXq, (qmul(floor_nXq, floor_nXq) + qmul(floor_nZq, floor_nZq))),
                   (qmul(floor_nXq, floor_nXq) + qmul(floor_nYq, floor_nYq) + qmul(floor_nZq, floor_nZq))), netYAccelq) * 2;
        objVelZ += qmul(qdiv(qmul(floor_nZq, (qmul(floor_nXq, floor_nXq) + qmul(floor_nZq, floor_nZq))),
                   (qmul(floor_nXq, floor_nXq) + qmul(floor_nYq, floor_nYq) + qmul(floor_nZq, floor_nZq))), netYAccelq) * 2;
    }

    if (objVelX < 0.000001 && objVelX > -0.000001) {
        objVelX = 0;
    }
    if (objVelZ < 0.000001 && objVelZ > -0.000001) {
        objVelZ = 0;
    }

    if (FFIELD(o, oVelY) < 0.000001 && FFIELD(o, oVelY) > -0.000001) {
        QSETFIELD(o,  oVelY, q(0));
    }

    if (objVelX != 0 || objVelZ != 0) {
        o->oMoveAngleYaw = atan2s(objVelZ, objVelX);
    }

    // Decreases both vertical velocity and forward velocity. Likely so that skips above
    // don't loop infinitely.
    FSETFIELD(o, oForwardVel, sqrtf(objVelX * objVelX + objVelZ * objVelZ) * 0.8);
    QSETFIELD(o, oVelY, QFIELD(o, oVelY) * 4 / 5);
}

/**
 * Updates an objects position from oForwardVel and oMoveAngleYaw.
 */
void obj_update_pos_vel_xz(void) {
    q32 xVel = qmul(QFIELD(o, oForwardVel), sinqs(o->oMoveAngleYaw));
    q32 zVel = qmul(QFIELD(o, oForwardVel), cosqs(o->oMoveAngleYaw));

    QMODFIELD(o, oPosX, += xVel);
    QMODFIELD(o, oPosZ, += zVel);
}

/**
 * Generates splashes if at surface of water, entering water, or bubbles
 * if underwater.
 */
void obj_splash(s32 waterY, s32 objY) {
    u32 globalTimer = gGlobalTimer;

    // Spawns waves if near surface of water and plays a noise if entering.
    if ((f32)(waterY + 30) > FFIELD(o, oPosY) && FFIELD(o, oPosY) > (f32)(waterY - 30)) {
        spawn_object(o, MODEL_IDLE_WATER_WAVE, bhvObjectWaterWave);

        if (FFIELD(o, oVelY) < -20.0f) {
            cur_obj_play_sound_2(SOUND_OBJ_DIVING_INTO_WATER);
        }
    }

    // Spawns bubbles if underwater.
    if ((objY + 50) < waterY && (globalTimer & 0x1F) == 0) {
        spawn_object(o, MODEL_WHITE_PARTICLE_SMALL, bhvObjectBubble);
    }
}

/**
 * Generic object move function. Handles walls, water, floors, and gravity.
 * Returns flags for certain interactions.
 */
s16 object_step(void) {
    f32 objX = FFIELD(o, oPosX);
    f32 objY = FFIELD(o, oPosY);
    f32 objZ = FFIELD(o, oPosZ);

    f32 floorY;
    f32 waterY = FLOOR_LOWER_LIMIT_MISC;

    f32 objVelX = FFIELD(o, oForwardVel) * sins(o->oMoveAngleYaw);
    f32 objVelZ = FFIELD(o, oForwardVel) * coss(o->oMoveAngleYaw);

    s16 collisionFlags = 0;

    // Find any wall collisions, receive the push, and set the flag.
    if (obj_find_wall(objX + objVelX, objY, objZ + objVelZ, objVelX, objVelZ) == 0) {
        collisionFlags += OBJ_COL_FLAG_HIT_WALL;
    }

    floorY = find_floor(objX + objVelX, objY, objZ + objVelZ, &sObjFloor);
    if (turn_obj_away_from_steep_floor(sObjFloor, floorY, objVelX, objVelZ) == 1) {
        waterY = find_water_level(objX + objVelX, objZ + objVelZ);
        if (waterY > objY) {
            calc_new_obj_vel_and_pos_y_underwater(sObjFloor, floorY, objVelX, objVelZ, waterY);
            collisionFlags += OBJ_COL_FLAG_UNDERWATER;
        } else {
            calc_new_obj_vel_and_pos_y(sObjFloor, floorY, objVelX, objVelZ);
        }
    } else {
        // Treat any awkward floors similar to a wall.
        collisionFlags +=
            ((collisionFlags & OBJ_COL_FLAG_HIT_WALL) ^ OBJ_COL_FLAG_HIT_WALL);
    }

    obj_update_pos_vel_xz();
    if ((s32) QFIELD(o, oPosY) == q((s32) floorY)) {
        collisionFlags += OBJ_COL_FLAG_GROUNDED;
    }

    if ((s32) QFIELD(o, oVelY) == 0) {
        collisionFlags += OBJ_COL_FLAG_NO_Y_VEL;
    }

    // Generate a splash if in water.
    obj_splash((s32) waterY, IFIELD(o, oPosY));
    return collisionFlags;
}

/**
 * Takes an object step but does not orient with the object's floor.
 * Used for boulders, falling pillars, and the rolling snowman body.
 */
s16 object_step_without_floor_orient(void) {
    s16 collisionFlags = 0;
    sOrientObjWithFloor = FALSE;
    collisionFlags = object_step();
    sOrientObjWithFloor = TRUE;

    return collisionFlags;
}

/**
 * Uses an object's forward velocity and yaw to move its X, Y, and Z positions.
 * This does accept an object as an argument, though it is always called with `o`.
 * If it wasn't called with `o`, it would modify `o`'s X and Z velocities based on
 * `obj`'s forward velocity and yaw instead of `o`'s, and wouldn't update `o`'s
 * position.
 */
void obj_move_xyz_using_fvel_and_yaw(struct Object *obj) {
    QSETFIELD(o, oVelX, qmul(QFIELD(obj, oForwardVel), sinqs(obj->oMoveAngleYaw)));
    QSETFIELD(o, oVelZ, qmul(QFIELD(obj, oForwardVel), cosqs(obj->oMoveAngleYaw)));

    QMODFIELD(obj, oPosX, += QFIELD(o, oVelX));
    QMODFIELD(obj, oPosY, += QFIELD(obj, oVelY));
    QMODFIELD(obj, oPosZ, += QFIELD(o, oVelZ));
}

/**
 * Checks if a point is within distance from Mario's graphical position. Test is exclusive.
 */
s32 is_point_within_radius_of_mario(s32 x, s32 y, s32 z, s32 dist) {
    s32 mGfxX = gMarioObject->header.gfx.posi[0];
    s32 mGfxY = gMarioObject->header.gfx.posi[1];
    s32 mGfxZ = gMarioObject->header.gfx.posi[2];

    return (x - mGfxX) * (x - mGfxX) + (y - mGfxY) * (y - mGfxY) + (z - mGfxZ) * (z - mGfxZ) < dist * dist;
}

/**
 * Checks whether a point is within distance of a given point. Test is exclusive.
 */
s32 is_point_close_to_object(struct Object *obj, s32 x, s32 y, s32 z, s32 dist) {
    s32 objX = qtrunc(QFIELD(obj, oPosX));
    s32 objY = qtrunc(QFIELD(obj, oPosY));
    s32 objZ = qtrunc(QFIELD(obj, oPosZ));

    return (x - objX) * (x - objX) + (y - objY) * (y - objY) + (z - objZ) * (z - objZ) < dist * dist;
}

/**
 * Sets an object as visible if within a certain distance of Mario's graphical position.
 */
void set_object_visibility(struct Object *obj, s32 dist) {
    s32 objX = qtrunc(QFIELD(obj, oPosX));
    s32 objY = qtrunc(QFIELD(obj, oPosY));
    s32 objZ = qtrunc(QFIELD(obj, oPosZ));

    if (is_point_within_radius_of_mario(objX, objY, objZ, dist) == TRUE) {
        obj->header.gfx.node.flags &= ~GRAPH_RENDER_INVISIBLE;
    } else {
        obj->header.gfx.node.flags |= GRAPH_RENDER_INVISIBLE;
    }
}

/**
 * Turns an object towards home if Mario is not near to it.
 */
s32 obj_return_home_if_safe(struct Object *obj, s32 homeX, s32 y, s32 homeZ, s32 dist) {
    s32 homeDistX = homeX - IFIELD(obj, oPosX);
    s32 homeDistZ = homeZ - IFIELD(obj, oPosZ);
    s16 angleTowardsHome = atan2sq(homeDistZ, homeDistX);

    if(is_point_within_radius_of_mario(homeX, y, homeZ, dist)) {
        return TRUE;
    } else {
        obj->oMoveAngleYaw = approach_s16_symmetric(obj->oMoveAngleYaw, angleTowardsHome, 320);
        return FALSE;
    }
}

/**
 * Randomly displaces an objects home if RNG says to, and turns the object towards its home.
 */
void obj_return_and_displace_home(struct Object *obj, s32 homeX, s32 homeZ, s32 baseDisp) {
    s16 angleToNewHome;

    if(random_q32() * 50 / ONE == 0) {
        QSETFIELD(obj, oHomeX, baseDisp * 2 * random_q32() - q(baseDisp) + q(homeX));
        QSETFIELD(obj, oHomeZ, baseDisp * 2 * random_q32() - q(baseDisp) + q(homeZ));
    }

    q32 homeDistXq = QFIELD(obj, oHomeX) - QFIELD(obj, oPosX);
    q32 homeDistZq = QFIELD(obj, oHomeZ) - QFIELD(obj, oPosZ);
    angleToNewHome = atan2sq(homeDistZq, homeDistXq);
    obj->oMoveAngleYaw = approach_s16_symmetric(obj->oMoveAngleYaw, angleToNewHome, 320);
}

/**
 * A series of checks using sin and cos to see if a given angle is facing in the same direction
 * of a given angle, within a certain range.
 */
s32 obj_check_if_facing_toward_angle(u32 base, u32 goal, s16 range) {
    s16 dAngle = (u16) goal - (u16) base;

    return (sinqs(-range) < sinqs(dAngle)) && (sinqs(dAngle) < sinqs(range)) && (cosqs(dAngle) > 0);
}

/**
 * Finds any wall collisions and returns what the displacement vector would be.
 */
s32 obj_find_wall_displacement(Vec3f dist, f32 x, f32 y, f32 z, f32 radius) {
    struct WallCollisionData hitbox;
	q32 xq = q(x);
	q32 yq = q(y);
	q32 zq = q(z);
    hitbox.xq = xq;
    hitbox.yq = yq;
    hitbox.zq = zq;
    hitbox.offsetYq = q(10);
    hitbox.radiusq = q(radius);

    if (find_wall_collisions(&hitbox) != 0) {
        dist[0] = qtof(hitbox.xq - xq);
        dist[1] = qtof(hitbox.yq - yq);
        dist[2] = qtof(hitbox.zq - zq);
        return TRUE;
    }
    return FALSE;
}

/**
 * Spawns a number of coins at the location of an object
 * with a random forward velocity, y velocity, and direction.
 */
void obj_spawn_yellow_coins(struct Object *obj, s8 nCoins) {
    struct Object *coin;
    s8 count;

    for (count = 0; count < nCoins; count++) {
        coin = spawn_object(obj, MODEL_YELLOW_COIN, bhvMovingYellowCoin);
        QSETFIELD(coin, oForwardVel, random_q32() * 20);
        QSETFIELD(coin, oVelY, random_q32() * 40 + q(20));
        coin->oMoveAngleYaw = random_u16();
    }
}

/**
 * Controls whether certain objects should flicker/when to despawn.
 */
s8 obj_flicker_and_disappear(struct Object *obj, s16 lifeSpan) {
    if (obj->oTimer < lifeSpan) {
        return FALSE;
    }

    if (obj->oTimer < lifeSpan + 40) {
        if (obj->oTimer % 2 != 0) {
            obj->header.gfx.node.flags |= GRAPH_RENDER_INVISIBLE;
        } else {
            obj->header.gfx.node.flags &= ~GRAPH_RENDER_INVISIBLE;
        }
    } else {
        obj->activeFlags = ACTIVE_FLAG_DEACTIVATED;
        return TRUE;
    }

    return FALSE;
}

/**
 * Checks if a given room is Mario's current room, even if on an object.
 */
s8 current_mario_room_check(s16 room) {
    s16 result;

    // Since object surfaces have room 0, this tests if the surface is an
    // object first and uses the last room if so.
    if (gMarioCurrentRoom == 0) {
        if (room == sPrevCheckMarioRoom) {
            return TRUE;
        } else {
            return FALSE;
        }
    } else {
        if (room == gMarioCurrentRoom) {
            result = TRUE;
        } else {
            result = FALSE;
        }

        sPrevCheckMarioRoom = gMarioCurrentRoom;
    }

    return result;
}

/**
 * Triggers dialog when Mario is facing an object and controls it while in the dialog.
 */
s16 trigger_obj_dialog_when_facing(s32 *inDialog, s16 dialogID, f32 dist, s32 actionArg) {
    s16 dialogueResponse;

    if ((is_point_within_radius_of_mario(IFIELD(o, oPosX), IFIELD(o, oPosY), IFIELD(o, oPosZ), (s32) dist) == TRUE
         && obj_check_if_facing_toward_angle(o->oFaceAngleYaw, gMarioObject->header.gfx.angle[1] + 0x8000, 0x1000) == TRUE
         && obj_check_if_facing_toward_angle(o->oMoveAngleYaw, o->oAngleToMario, 0x1000) == TRUE)
        || (*inDialog == TRUE)) {
        *inDialog = TRUE;

        if (set_mario_npc_dialog(actionArg) == MARIO_DIALOG_STATUS_SPEAK) { //If Mario is speaking.
            dialogueResponse = cutscene_object_with_dialog(CUTSCENE_DIALOG, o, dialogID);
            if (dialogueResponse) {
                set_mario_npc_dialog(MARIO_DIALOG_STOP);
                *inDialog = FALSE;
                return dialogueResponse;
            }
            return 0;
        }
    }

    return 0;
}

/**
 *Checks if a floor is one that should cause an object to "die".
 */
void obj_check_floor_death(s16 collisionFlags, struct Surface *floor) {
    if (floor == NULL) {
        return;
    }

    if ((collisionFlags & OBJ_COL_FLAG_GROUNDED) == OBJ_COL_FLAG_GROUNDED) {
        switch (floor->type) {
            case SURFACE_BURNING:
                o->oAction = OBJ_ACT_LAVA_DEATH;
                break;
            //! @BUG Doesn't check for the vertical wind death floor.
            case SURFACE_DEATH_PLANE:
                o->oAction = OBJ_ACT_DEATH_PLANE_DEATH;
                break;
            default:
                break;
        }
    }
}

/**
 * Controls an object dying in lava by creating smoke, sinking the object, playing
 * audio, and eventually despawning it. Returns TRUE when the obj is dead.
 */
s8 obj_lava_death(void) {
    struct Object *deathSmoke;

    if (o->oTimer >= 31) {
        o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
        return TRUE;
    } else {
        // Sinking effect
        QMODFIELD(o, oPosY, -= q(10.0f));
    }

    if ((o->oTimer % 8) == 0) {
        cur_obj_play_sound_2(SOUND_OBJ_BULLY_EXPLODE_2);
        deathSmoke = spawn_object(o, MODEL_SMOKE, bhvBobombBullyDeathSmoke);
        QMODFIELD(deathSmoke, oPosX, += random_float() * 20.0f);
        QMODFIELD(deathSmoke, oPosY, += random_float() * 20.0f);
        QMODFIELD(deathSmoke, oPosZ, += random_float() * 20.0f);
        QMODFIELD(deathSmoke, oForwardVel, = random_float() * 10.0f);
    }

    return FALSE;
}

/**
 * Spawns an orange number object relatively, such as those that count up for secrets.
 */
void spawn_orange_number(s8 behParam, s16 relX, s16 relY, s16 relZ) {
    struct Object *orangeNumber;

    if (behParam >= 10) {
        return;
    }

    orangeNumber = spawn_object_relative(behParam, relX, relY, relZ, o, MODEL_NUMBER, bhvOrangeNumber);
    QMODFIELD(orangeNumber, oPosY, += q(25));
}

/**
 * Unused variables for debug_sequence_tracker.
 */
s8 sDebugSequenceTracker = 0;
s8 sDebugTimer = 0;

/**
 * Unused presumably debug function that tracks for a sequence of inputs.
 */
s8 UNUSED debug_sequence_tracker(s16 debugInputSequence[]) {
    // If end of sequence reached, return true.
    if (debugInputSequence[sDebugSequenceTracker] == 0) {
        sDebugSequenceTracker = 0;
        return TRUE;
    }

    // If the third controller button pressed is next in sequence, reset timer and progress to next value.
    if (debugInputSequence[sDebugSequenceTracker] & gPlayer3Controller->buttonPressed) {
        sDebugSequenceTracker++;
        sDebugTimer = 0;
    // If wrong input or timer reaches 10, reset sequence progress.
    } else if (sDebugTimer == 10 || gPlayer3Controller->buttonPressed != 0) {
        sDebugSequenceTracker = 0;
        sDebugTimer = 0;
        return FALSE;
    }
    sDebugTimer++;

    return FALSE;
}

#include "behaviors/moving_coin.inc.c"
#include "behaviors/seaweed.inc.c"
#include "behaviors/bobomb.inc.c"
#include "behaviors/cannon_door.inc.c"
#include "behaviors/whirlpool.inc.c"
#include "behaviors/amp.inc.c"
#include "behaviors/butterfly.inc.c"
#include "behaviors/hoot.inc.c"
#include "behaviors/beta_holdable_object.inc.c"
#include "behaviors/bubble.inc.c"
#include "behaviors/water_wave.inc.c"
#include "behaviors/explosion.inc.c"
#include "behaviors/corkbox.inc.c"
#include "behaviors/bully.inc.c"
#include "behaviors/water_ring.inc.c"
#include "behaviors/bowser_bomb.inc.c"
#include "behaviors/celebration_star.inc.c"
#include "behaviors/drawbridge.inc.c"
#include "behaviors/bomp.inc.c"
#include "behaviors/sliding_platform.inc.c"
#include "behaviors/moneybag.inc.c"
#include "behaviors/bowling_ball.inc.c"
#include "behaviors/cruiser.inc.c"
#include "behaviors/spindel.inc.c"
#include "behaviors/pyramid_wall.inc.c"
#include "behaviors/pyramid_elevator.inc.c"
#include "behaviors/pyramid_top.inc.c"
#include "behaviors/sound_waterfall.inc.c"
#include "behaviors/sound_volcano.inc.c"
#include "behaviors/castle_flag.inc.c"
#include "behaviors/sound_birds.inc.c"
#include "behaviors/sound_ambient.inc.c"
#include "behaviors/sound_sand.inc.c"
#include "behaviors/castle_cannon_grate.inc.c"
#include "behaviors/snowman.inc.c"
#include "behaviors/boulder.inc.c"
#include "behaviors/cap.inc.c"
#include "behaviors/spawn_star.inc.c"
#include "behaviors/red_coin.inc.c"
#include "behaviors/hidden_star.inc.c"
#include "behaviors/rolling_log.inc.c"
#include "behaviors/mushroom_1up.inc.c"
#include "behaviors/controllable_platform.inc.c"
#include "behaviors/breakable_box_small.inc.c"
#include "behaviors/snow_mound.inc.c"
#include "behaviors/floating_platform.inc.c"
#include "behaviors/arrow_lift.inc.c"
#include "behaviors/orange_number.inc.c"
#include "behaviors/manta_ray.inc.c"
#include "behaviors/falling_pillar.inc.c"
#include "behaviors/floating_box.inc.c"
#include "behaviors/decorative_pendulum.inc.c"
#include "behaviors/treasure_chest.inc.c"
#include "behaviors/mips.inc.c"
#include "behaviors/yoshi.inc.c"
