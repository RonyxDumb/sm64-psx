// heave_ho.c.inc

s16 D_8032F460[][2] = { { 30, 0 }, { 42, 1 }, { 52, 0 },  { 64, 1 },  { 74, 0 },
                        { 86, 1 }, { 96, 0 }, { 108, 1 }, { 118, 0 }, { -1, 0 }, };

void bhv_heave_ho_throw_mario_loop(void) {
    QSETFIELD(o, oParentRelativePosX, q(200));
    QSETFIELD(o, oParentRelativePosY, q(-50));
    QSETFIELD(o, oParentRelativePosZ, 0);
    o->oMoveAngleYaw = o->parentObj->oMoveAngleYaw;
    switch (o->parentObj->oHeaveHoUnk88) {
        case 0:
            break;
        case 1:
            break;
        case 2:
            cur_obj_play_sound_2(SOUND_OBJ_HEAVEHO_TOSSED);
            gMarioObject->oInteractStatus |= INT_STATUS_MARIO_UNK2;
            gMarioStates[0].forwardVel = -45.0f;
            gMarioStates[0].vel[1] = 95.0f;
            o->parentObj->oHeaveHoUnk88 = 0;
            break;
    }
}

void heave_ho_act_1(void) {
    s32 sp1C = 0;
    QSETFIELD(o,  oForwardVel, q(0));
    cur_obj_reverse_animation();
    while (TRUE) {
        if (D_8032F460[sp1C][0] == -1) {
            o->oAction = 2;
            break;
        }
        if (o->oTimer < D_8032F460[sp1C][0]) {
            cur_obj_init_animation_with_accel_and_sound(2, D_8032F460[sp1C][1]);
            break;
        }
        sp1C++;
    }
}

void heave_ho_act_2(void) {
    s16 angleVel;
    if (1000.0f < cur_obj_lateral_dist_from_mario_to_home())
        o->oAngleToMario = cur_obj_angle_to_home();
    if (o->oTimer > 150) {
        FSETFIELD(o, oHeaveHoUnkF4, (302 - o->oTimer) / 152);
        if (QFIELD(o, oHeaveHoUnkF4) < q(0.1)) {
            QSETFIELD(o, oHeaveHoUnkF4, q(0.1));
            o->oAction = 1;
        }
    } else
        QSETFIELD(o, oHeaveHoUnkF4, QONE);
    cur_obj_init_animation_with_accel_and_sound(0, FFIELD(o, oHeaveHoUnkF4));
    FSETFIELD(o, oForwardVel, FFIELD(o, oHeaveHoUnkF4) * 10.0f);
    angleVel = FFIELD(o, oHeaveHoUnkF4) * 0x400;
    o->oMoveAngleYaw = approach_s16_symmetric(o->oMoveAngleYaw, o->oAngleToMario, angleVel);
}

void heave_ho_act_3(void) {
    QSETFIELD(o,  oForwardVel, q(0));
    if (o->oTimer == 0)
        o->oHeaveHoUnk88 = 2;
    if (o->oTimer == 1) {
        cur_obj_init_animation_with_accel_and_sound(1, 1.0f);
        o->numCollidedObjs = 20;
    }
    if (cur_obj_check_if_near_animation_end())
        o->oAction = 1;
}

void heave_ho_act_0(void) {
    cur_obj_set_pos_to_home();
    if (find_water_levelq(QFIELD(o, oPosX), QFIELD(o, oPosZ)) < QFIELD(o, oPosY) && QFIELD(o, oDistanceToMario) < q(4000.0)) {
        cur_obj_become_tangible();
        cur_obj_unhide();
        o->oAction = 1;
    } else {
        cur_obj_become_intangible();
        cur_obj_hide();
    }
}

void (*sHeaveHoActions[])(void) = { heave_ho_act_0, heave_ho_act_1, heave_ho_act_2, heave_ho_act_3 };

void heave_ho_move(void) {
    cur_obj_update_floor_and_walls();
    cur_obj_call_action_function(sHeaveHoActions);
    cur_obj_move_standard(-78);
    if (o->oMoveFlags & OBJ_MOVE_MASK_IN_WATER)
        QSETFIELD(o, oGraphYOffset, q(-15));
    else
        QSETFIELD(o, oGraphYOffset, q(0));
    if (QFIELD(o, oForwardVel) > q(3))
        cur_obj_play_sound_1(SOUND_AIR_HEAVEHO_MOVE);
    if (o->oAction != 0 && o->oMoveFlags & OBJ_MOVE_MASK_IN_WATER)
        o->oAction = 0;
    if (o->oInteractStatus & INT_STATUS_GRABBED_MARIO) {
        o->oInteractStatus = 0;
        o->oHeaveHoUnk88 = 1;
        o->oAction = 3;
    }
}

void bhv_heave_ho_loop(void) {
    cur_obj_scaleq(q(2.0f));
    switch (o->oHeldState) {
        case HELD_FREE:
            heave_ho_move();
            break;
        case HELD_HELD:
            cur_obj_unrender_set_action_and_anim(0, 0);
            break;
        case HELD_THROWN:
            cur_obj_get_dropped();
            break;
        case HELD_DROPPED:
            cur_obj_get_dropped();
            break;
    }
    o->oInteractStatus = 0;
}
