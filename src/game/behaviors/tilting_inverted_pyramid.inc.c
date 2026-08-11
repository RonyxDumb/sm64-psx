/**
 * This is the behavior file for the tilting inverted pyramids in BitFS/LLL.
 * The object essentially just tilts and moves Mario with it.
 */

#include <port/gfx/gfx.h>

/**
 * Creates a transform matrix on a variable passed in from given normals
 * and the object's position.
 */
void create_transform_from_normalsq(ShortMatrix* transformq, q32 xNormq, q32 yNormq, q32 zNormq) {
	q32 normalq[] = {xNormq, yNormq, zNormq};
    q32 posq[] = {QFIELD(o, oPosX), QFIELD(o, oPosY), QFIELD(o, oPosZ)};
    mtx_align_terrain_normal(transformq, normalq, posq, 0);
}

/**
 * Initialize the object's transform matrix with Y being up.
 */
void bhv_platform_normals_init(void) {
    QSETFIELD(o, oTiltingPyramidNormalX, 0);
    QSETFIELD(o, oTiltingPyramidNormalY, QONE);
    QSETFIELD(o, oTiltingPyramidNormalZ, 0);

    create_transform_from_normalsq(&o->transformq, 0, ONE, 0);
}

/**
 * Returns a value that is src incremented/decremented by inc towards goal
 * until goal is reached. Does not overshoot.
 */
q32 approach_by_incrementq(q32 goal, q32 src, q32 inc) {
    q32 newVal;

    if (src <= goal) {
        if (goal - src < inc) {
            newVal = goal;
        } else {
            newVal = src + inc;
        }
    } else if (goal - src > -inc) {
        newVal = goal;
    } else {
        newVal = src - inc;
    }

    return newVal;
}

/**
 * Main behavior for the tilting pyramids in LLL/BitFS. These platforms calculate rough normals from Mario's position,
 * then gradually tilt back moving Mario with them.
 */
void bhv_tilting_inverted_pyramid_loop(void) {
    q32 dxq, dyq, dzq;

    ShortVec disti;
    ShortVec posBeforeRotationi;

    // Mario's position
    s16 mxi;
    s16 myi;
    s16 mzi;

    s32 marioOnPlatform = FALSE;

    if (gMarioObject->platform == o) {
        get_mario_posi(&mxi, &myi, &mzi);

        disti.vx = IFIELD(gMarioObject, oPosX) - IFIELD(o, oPosX);
        disti.vy = IFIELD(gMarioObject, oPosY) - IFIELD(o, oPosY);
        disti.vz = IFIELD(gMarioObject, oPosZ) - IFIELD(o, oPosZ);
        posBeforeRotationi = mtx_apply_without_translation(&disti, &o->transformq);

        s32 dxi = IFIELD(gMarioObject, oPosX) - IFIELD(o, oPosX);
        s32 dyi = 500;
        s32 dzi = IFIELD(gMarioObject, oPosZ) - IFIELD(o, oPosZ);
        s32 d = sqrtu(dxi * dxi + dyi + dzi * dzi);

        //! Always true since dy = 500, making d >= 500.
        //if (/d != 0.0f) {
            // Normalizing
        dxq = dxi * ONE / d;
        dyq = dyi * ONE / d;
        dzq = dzi * ONE / d;
        //} else {
        //    dx = 0.0f;
        //    dy = 1.0f;
        //    dz = 0.0f;
        //}

        if (o->oTiltingPyramidMarioOnPlatform == TRUE)
            marioOnPlatform++;

        o->oTiltingPyramidMarioOnPlatform = TRUE;
    } else {
        dxq = 0;
        dyq = ONE;
        dzq = 0;
        o->oTiltingPyramidMarioOnPlatform = FALSE;
    }

    // Approach the normals by 0.01f towards the new goal, then create a transform matrix and orient the object.
    // Outside of the other conditionals since it needs to tilt regardless of whether Mario is on.
    QSETFIELD(o, oTiltingPyramidNormalX, approach_by_incrementq(dxq, QFIELD(o, oTiltingPyramidNormalX), q(0.01)));
    QSETFIELD(o, oTiltingPyramidNormalY, approach_by_incrementq(dyq, QFIELD(o, oTiltingPyramidNormalY), q(0.01)));
    QSETFIELD(o, oTiltingPyramidNormalZ, approach_by_incrementq(dzq, QFIELD(o, oTiltingPyramidNormalZ), q(0.01)));
    create_transform_from_normalsq(&o->transformq, QFIELD(o, oTiltingPyramidNormalX), QFIELD(o, oTiltingPyramidNormalY), QFIELD(o, oTiltingPyramidNormalZ));

    // If Mario is on the platform, adjust his position for the platform tilt.
    if (marioOnPlatform != FALSE) {
        ShortVec posAfterRotationi = mtx_apply_without_translation(&disti, &o->transformq);
        mxi += posAfterRotationi.vx - posBeforeRotationi.vx;
        myi += posAfterRotationi.vy - posBeforeRotationi.vy;
        mzi += posAfterRotationi.vz - posBeforeRotationi.vz;
        set_mario_posi(mxi, myi, mzi);
    }

    o->header.gfx.throwMatrixq = &o->transformq;
}
