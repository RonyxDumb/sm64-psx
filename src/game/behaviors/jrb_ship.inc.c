// jrb_ship.c.inc

#include <port/gfx/gfx.h>

struct ObjectHitbox sSkullSlidingBoxHitbox = {
    /* interactType: */ INTERACT_DAMAGE,
    /* downOffset: */ 0,
    /* damageOrCoinValue: */ 1,
    /* health: */ 1,
    /* numLootCoins: */ 0,
    /* radius: */ 130,
    /* height: */ 100,
    /* hurtboxRadius: */ 0,
    /* hurtboxHeight: */ 0,
};

void bhv_sunken_ship_part_loop(void) {
    if (QFIELD(o, oDistanceToMario) > q(10000))
        o->oOpacity = 140;
    else
        o->oOpacity = qtrunc(QFIELD(o, oDistanceToMario) * 7 / 500);
    cur_obj_disable_rendering();
}

void bhv_ship_part_3_loop(void) {
    s16 sp1E = o->oFaceAnglePitch;
    s16 sp1C = o->oFaceAngleRoll;
    cur_obj_set_pos_to_home_with_debug();
    o->oShipPart3UnkF4 += 0x100;
    o->oFaceAnglePitch = sinqs(o->oShipPart3UnkF4) / 4;
    o->oFaceAngleRoll = sinqs(o->oShipPart3UnkF8) / 4;
    o->oAngleVelPitch = o->oFaceAnglePitch - sp1E;
    o->oAngleVelRoll = o->oFaceAngleRoll - sp1C;
    if (QFIELD(gMarioObject, oPosY) > q(1000))
        cur_obj_play_sound_1(SOUND_ENV_BOAT_ROCKING1);
}

void bhv_jrb_sliding_box_loop(void) {
    ShortMatrix sp60q;
    ShortVec sp54q;
    ShortVec sp48q;
    Vec3s sp40;
    struct Object *sp3C;
    struct Surface *sp38;
    Vec3q sp20q;
    s16 sp1E = 0;
    if (o->oJrbSlidingBoxUnkF4 == NULL) {
        sp3C = cur_obj_nearest_object_with_behavior(bhvInSunkenShip3);
        if (sp3C != NULL) // NULL check only for assignment, not for dereference?
            o->oJrbSlidingBoxUnkF4 = sp3C;
        QSETFIELD(o, oParentRelativePosX, QFIELD(o, oPosX) - QFIELD(sp3C, oPosX));
        QSETFIELD(o, oParentRelativePosY, QFIELD(o, oPosY) - QFIELD(sp3C, oPosY));
        QSETFIELD(o, oParentRelativePosZ, QFIELD(o, oPosZ) - QFIELD(sp3C, oPosZ));
    } else {
        sp3C = o->oJrbSlidingBoxUnkF4;
        sp40[0] = sp3C->oFaceAnglePitch;
        sp40[1] = sp3C->oFaceAngleYaw;
        sp40[2] = sp3C->oFaceAngleRoll;
        sp54q.vx = qtrunc(QFIELD(o, oParentRelativePosX));
        sp54q.vy = qtrunc(QFIELD(o, oParentRelativePosY));
        sp54q.vz = qtrunc(QFIELD(o, oParentRelativePosZ));
        sp60q = mtx_rotation_zxy_and_translation(sp54q.elems, sp40);
        sp48q = mtx_apply_without_translation(&sp54q, &sp60q); // TODO: is this valid?
        QSETFIELD(o, oPosX, QFIELD(sp3C, oPosX) + sp48q.vx);
        QSETFIELD(o, oPosY, QFIELD(sp3C, oPosY) + sp48q.vy);
        QSETFIELD(o, oPosZ, QFIELD(sp3C, oPosZ) + sp48q.vz);
        sp1E = sp3C->oFaceAnglePitch;
    }
    sp20q[0] = QFIELD(o, oPosX);
    sp20q[1] = QFIELD(o, oPosY);
    sp20q[2] = QFIELD(o, oPosZ);
    find_floorq(sp20q[0], sp20q[1], sp20q[2], &sp38);
    if (sp38 != NULL) {
        o->oFaceAnglePitch = sp1E;
    }
    QSETFIELD(o, oJrbSlidingBoxUnkFC, sinqs(o->oJrbSlidingBoxUnkF8) * 20);
    o->oJrbSlidingBoxUnkF8 += 0x100;
    QMODFIELD(o, oParentRelativePosZ, += QFIELD(o, oJrbSlidingBoxUnkFC));
    if (QFIELD(gMarioObject, oPosY) > q(1000))
        if (ABS(QFIELD(o, oJrbSlidingBoxUnkFC)) > q(3))
            cur_obj_play_sound_1(SOUND_AIR_ROUGH_SLIDE);
    obj_set_hitbox(o, &sSkullSlidingBoxHitbox);
    if (!(o->oJrbSlidingBoxUnkF8 & 0x7FFF))
        cur_obj_become_tangible();
    if (obj_check_if_collided_with_object(o, gMarioObject)) {
        o->oInteractStatus = 0;
        cur_obj_become_intangible();
    }
}
