// metal_box.c.inc

struct ObjectHitbox sMetalBoxHitbox = {
    /* interactType: */ 0,
    /* downOffset: */ 0,
    /* damageOrCoinValue: */ 0,
    /* health: */ 1,
    /* numLootCoins: */ 0,
    /* radius: */ 220,
    /* height: */ 300,
    /* hurtboxRadius: */ 220,
    /* hurtboxHeight: */ 300,
};

s32 check_if_moving_over_floorq(q32 a0q, q32 a1q) {
    struct Surface *sp24;
    q32 sp20q = QFIELD(o, oPosX) + qmul(sinqs(o->oMoveAngleYaw), a1q);
    q32 sp18q = QFIELD(o, oPosZ) + qmul(cosqs(o->oMoveAngleYaw), a1q);
    q32 floorHeightq = find_floorq(sp20q, QFIELD(o, oPosY), sp18q, &sp24);
    if (ABS(floorHeightq - QFIELD(o, oPosY)) < a0q) // abs
        return 1;
    else
        return 0;
}

void bhv_pushable_loop(void) {
    s16 sp1C;
    obj_set_hitbox(o, &sMetalBoxHitbox);
    QSETFIELD(o,  oForwardVel, q(0));
    if (obj_check_if_collided_with_object(o, gMarioObject) && gMarioStates[0].flags & MARIO_UNKNOWN_31) {
        sp1C = obj_angle_to_object(o, gMarioObject);
        if (abs_angle_diff(sp1C, gMarioObject->oMoveAngleYaw) > 0x4000) {
            o->oMoveAngleYaw = (s16)((gMarioObject->oMoveAngleYaw + 0x2000) & 0xc000);
            if (check_if_moving_over_floorq(q(8), q(150))) {
                QSETFIELD(o,  oForwardVel, q(4));
                cur_obj_play_sound_1(SOUND_ENV_METAL_BOX_PUSH);
            }
        }
    }
    cur_obj_move_using_fvel_and_gravity();
}
