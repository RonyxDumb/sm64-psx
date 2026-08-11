#include <PR/ultratypes.h>

#include "engine/math_util.h"
#include "engine/surface_collision.h"
#include "level_update.h"
#include "object_fields.h"
#include "object_helpers.h"
#include "object_list_processor.h"
#include "platform_displacement.h"
#include "port/gfx/gfx.h"
#include "types.h"

u16 D_8032FEC0 = 0;

u32 unused_8032FEC4[4] = { 0 };

struct Object *gMarioPlatform = NULL;

/**
 * Determine if Mario is standing on a platform object, meaning that he is
 * within 4 units of the floor. Set his referenced platform object accordingly.
 */
void update_mario_platform(void) {
    struct Surface *floor;
    UNUSED u32 unused;
    f32 marioX;
    f32 marioY;
    f32 marioZ;
    f32 floorHeight;
    u32 awayFromFloor;

    if (gMarioObject == NULL) {
        return;
    }

    //! If Mario moves onto a rotating platform in a PU, the find_floor call
    //  will detect the platform and he will end up receiving a large amount
    //  of displacement since he is considered to be far from the platform's
    //  axis of rotation.

    marioX = FFIELD(gMarioObject, oPosX);
    marioY = FFIELD(gMarioObject, oPosY);
    marioZ = FFIELD(gMarioObject, oPosZ);
    floorHeight = find_floor(marioX, marioY, marioZ, &floor);

    if (absf(marioY - floorHeight) < 4.0f) {
        awayFromFloor = 0;
    } else {
        awayFromFloor = 1;
    }

    switch (awayFromFloor) {
        case 1:
            gMarioPlatform = NULL;
            gMarioObject->platform = NULL;
            break;

        case 0:
            if (floor != NULL && floor->object != NULL) {
                gMarioPlatform = floor->object;
                gMarioObject->platform = floor->object;
            } else {
                gMarioPlatform = NULL;
                gMarioObject->platform = NULL;
            }
            break;
    }
}

/**
 * Get Mario's position and store it in x, y, and z.
 */
void get_mario_posi(s16 *xi, s16 *yi, s16 *zi) {
    *xi = gMarioStates[0].pos[0];
    *yi = gMarioStates[0].pos[1];
    *zi = gMarioStates[0].pos[2];
}

/**
 * Set Mario's position.
 */
void set_mario_posi(s16 xi, s16 yi, s16 zi) {
    gMarioStates[0].pos[0] = xi;
    gMarioStates[0].pos[1] = yi;
    gMarioStates[0].pos[2] = zi;
}

/**
 * Apply one frame of platform rotation to Mario or an object using the given
 * platform. If isMario is false, use gCurrentObject.
 */
void apply_platform_displacement(u32 isMario, struct Object *platform) {
    s16 xi;
    s16 yi;
    s16 zi;
    s16 platformPosXi;
    s16 platformPosYi;
    s16 platformPosZi;
    ShortVec currentObjectOffseti;
    ShortVec relativeOffseti;
    ShortVec newObjectOffseti;
    Vec3s rotation;

    rotation[0] = platform->oAngleVelPitch;
    rotation[1] = platform->oAngleVelYaw;
    rotation[2] = platform->oAngleVelRoll;

    if (isMario) {
        D_8032FEC0 = 0;
        get_mario_posi(&xi, &yi, &zi);
    } else {
        xi = IFIELD(gCurrentObject, oPosX);
        yi = IFIELD(gCurrentObject, oPosY);
        zi = IFIELD(gCurrentObject, oPosZ);
    }

    xi += IFIELD(platform, oVelX);
    zi += IFIELD(platform, oVelZ);

    if (rotation[0] != 0 || rotation[1] != 0 || rotation[2] != 0) {
        if (isMario) {
            gMarioStates[0].faceAngle[1] += rotation[1];
        }

        platformPosXi = IFIELD(platform, oPosX);
        platformPosYi = IFIELD(platform, oPosY);
        platformPosZi = IFIELD(platform, oPosZ);

        currentObjectOffseti.vx = xi - platformPosXi;
        currentObjectOffseti.vy = yi - platformPosYi;
        currentObjectOffseti.vz = zi - platformPosZi;

        rotation[0] = platform->oFaceAnglePitch - platform->oAngleVelPitch;
        rotation[1] = platform->oFaceAngleYaw - platform->oAngleVelYaw;
        rotation[2] = platform->oFaceAngleRoll - platform->oAngleVelRoll;

        scratch_mtx = mtx_rotation_zxy_and_translation(currentObjectOffseti.elems, rotation);
        mtx_transpose(&scratch_mtx);
        relativeOffseti = mtx_apply(&currentObjectOffseti, &scratch_mtx);

        rotation[0] = platform->oFaceAnglePitch;
        rotation[1] = platform->oFaceAngleYaw;
        rotation[2] = platform->oFaceAngleRoll;

        scratch_mtx = mtx_rotation_zxy_and_translation(currentObjectOffseti.elems, rotation);
        mtx_transpose(&scratch_mtx);
        newObjectOffseti = mtx_apply(&relativeOffseti, &scratch_mtx);

        xi = platformPosXi + newObjectOffseti.vx;
        yi = platformPosYi + newObjectOffseti.vy;
        zi = platformPosZi + newObjectOffseti.vz;
    }

    if (isMario) {
        set_mario_posi(xi, yi, zi);
    } else {
        ISETFIELD(gCurrentObject, oPosX, xi);
        ISETFIELD(gCurrentObject, oPosY, yi);
        ISETFIELD(gCurrentObject, oPosZ, zi);
    }
}

/**
 * If Mario's platform is not null, apply platform displacement.
 */
void apply_mario_platform_displacement(void) {
    struct Object *platform = gMarioPlatform;

    if (!(gTimeStopState & TIME_STOP_ACTIVE) && gMarioObject != NULL && platform != NULL) {
        apply_platform_displacement(TRUE, platform);
    }
}

#ifndef VERSION_JP
/**
 * Set Mario's platform to NULL.
 */
void clear_mario_platform(void) {
    gMarioPlatform = NULL;
}
#endif
