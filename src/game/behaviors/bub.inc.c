// bub.c.inc

// NOTE: These first set of functions spawn a school of bub depending on objF4's
// value. The later action functions seem to check Y distance to Mario and proceed
// to do nothing, which indicates this behavior set is incomplete.

// TODO: Rename these. These have nothing to do with birds.
void bub_spawner_act_0(void) {
    s32 i;
    s32 sp18 = o->oBirdChirpChirpUnkF4;
    if (QFIELD(o, oDistanceToMario) < q(1500.0)) {
        for (i = 0; i < sp18; i++)
            spawn_object(o, MODEL_BUB, bhvBub);
        o->oAction = 1;
    }
}

void bub_spawner_act_1(void) {
    if (QFIELD(gMarioObject, oPosY) - QFIELD(o, oPosY) > q(2000))
        o->oAction = 2;
}

void bub_spawner_act_2(void) {
    o->oAction = 3;
}

void bub_spawner_act_3(void) {
    o->oAction = 0;
}

void (*sBirdChirpChirpActions[])(void) = { bub_spawner_act_0, bub_spawner_act_1,
                                           bub_spawner_act_2, bub_spawner_act_3 };

void bhv_bub_spawner_loop(void) {
    cur_obj_call_action_function(sBirdChirpChirpActions);
}

void bub_move_vertically(s32 a0) {
    f32 sp1C = FFIELD(o->parentObj, oPosY);
    if (sp1C - 100.0f - FFIELD(o, oCheepCheepUnk104) < FFIELD(o, oPosY)
        && FFIELD(o, oPosY) < sp1C + 1000.0f + FFIELD(o, oCheepCheepUnk104)) {
        FSETFIELD(o, oPosY, approach_f32_symmetric(FFIELD(o, oPosY), FFIELD(o, oCheepCheepUnkF8), a0));
	}
}

void bub_act_0(void) {
    QSETFIELD(o, oCheepCheepUnkFC, random_float() * 100.0f);
    QSETFIELD(o, oCheepCheepUnk104, random_float() * 300.0f);
    o->oAction = 1;
}

void bub_act_1(void) {
    f32 dy;
    if (o->oTimer == 0) {
        FSETFIELD(o, oForwardVel, random_float() * 2 + 2);
        FSETFIELD(o, oCheepCheepUnk108, random_float());
    }
    dy = FFIELD(o, oPosY) - FFIELD(gMarioObject, oPosY);
    if (QFIELD(o, oPosY) < QFIELD(o, oCheepCheepUnkF4) - q(50)) {
        if (dy < 0.0f)
            dy = 0.0f - dy;
        if (dy < 500.0f)
            bub_move_vertically(1);
        else
            bub_move_vertically(4);
    } else {
        QSETFIELD(o, oPosY, QFIELD(o, oCheepCheepUnkF4) - q(50));
        if (dy > 300.0f)
            QSETFIELD(o, oPosY, QFIELD(o, oPosY) - QONE);
    }
    if (800.0f < cur_obj_lateral_dist_from_mario_to_home())
        o->oAngleToMario = cur_obj_angle_to_home();
    cur_obj_rotate_yaw_toward(o->oAngleToMario, 0x100);
    if (QFIELD(o, oDistanceToMario) < q(200))
        if (QFIELD(o, oCheepCheepUnk108) < q(0.5))
            o->oAction = 2;
    if (o->oInteractStatus & INT_STATUS_INTERACTED)
        o->oAction = 2;
}

void bub_act_2(void) {
    f32 dy;
    if (o->oTimer < 20) {
        if (o->oInteractStatus & INT_STATUS_INTERACTED)
            spawn_object(o, MODEL_WHITE_PARTICLE_SMALL, bhvSmallParticleSnow);
    } else
        o->oInteractStatus = 0;
    if (o->oTimer == 0)
        cur_obj_play_sound_2(SOUND_GENERAL_MOVING_WATER);
    if (QFIELD(o, oForwardVel) == 0)
        QSETFIELD(o,  oForwardVel, q(6));
    dy = FFIELD(o, oPosY) - FFIELD(gMarioObject, oPosY);
    if (QFIELD(o, oPosY) < QFIELD(o, oCheepCheepUnkF4) - q(50)) {
        if (dy < 0.0f)
            dy = 0.0f - dy;
        if (dy < 500.0f)
            bub_move_vertically(2);
        else
            bub_move_vertically(4);
    } else {
        QSETFIELD(o, oPosY, QFIELD(o, oCheepCheepUnkF4) - q(50));
        if (dy > 300.0f)
            QMODFIELD(o, oPosY, -= QONE);
    }
    if (cur_obj_lateral_dist_from_mario_to_home() > 800.0f)
        o->oAngleToMario = cur_obj_angle_to_home();
    cur_obj_rotate_yaw_toward(o->oAngleToMario + 0x8000, 0x400);
    if (o->oTimer > 200 && QFIELD(o, oDistanceToMario) > q(600.0))
        o->oAction = 1;
}

void (*sCheepCheepActions[])(void) = { bub_act_0, bub_act_1, bub_act_2 };

void bhv_bub_loop(void) {
    QSETFIELD(o, oCheepCheepUnkF4, find_water_levelq(QFIELD(o, oPosX), QFIELD(o, oPosZ)));
    QSETFIELD(o, oCheepCheepUnkF8, QFIELD(gMarioObject, oPosY) + QFIELD(o, oCheepCheepUnkFC));
    QSETFIELD(o, oWallHitboxRadius, q(30));
    cur_obj_update_floor_and_walls();
    cur_obj_call_action_function(sCheepCheepActions);
    cur_obj_move_using_fvel_and_gravity();
    if (o->parentObj->oAction == 2)
        obj_mark_for_deletion(o);
}
