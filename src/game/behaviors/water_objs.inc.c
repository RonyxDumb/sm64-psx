// water_objs.c.inc
// TODO: Better name, please

void bhv_water_air_bubble_init(void) {
    cur_obj_scaleq(q(4.0f));
}

// Fields 0xF4 & 0xF8 seem to be angles for bubble and cannon

void bhv_water_air_bubble_loop(void) {
    s32 i;
    o->header.gfx.scaleq[0] = sinqs(o->oWaterObjUnkF4) / 2 + q(4);
    o->header.gfx.scaleq[1] = -sinqs(o->oWaterObjUnkF4) / 2 + q(4);
    o->oWaterObjUnkF4 += 0x400;
    if (o->oTimer < 30) {
        cur_obj_become_intangible();
        QMODFIELD(o, oPosY, += q(3));
    } else {
        cur_obj_become_tangible();
        cur_obj_forward_vel_approach_upward(2.0f, 10.0f);
        o->oMoveAngleYaw = obj_angle_to_object(o, gMarioObject);
        cur_obj_move_using_fvel_and_gravity();
    }
    QMODFIELD(o, oPosX, += random_q32() * 4 - q(2));
    QMODFIELD(o, oPosZ, += random_q32() * 4 - q(2));
    if (o->oInteractStatus & INT_STATUS_INTERACTED || o->oTimer > 200) {
        cur_obj_play_sound_2(SOUND_GENERAL_QUIET_BUBBLE);
        obj_mark_for_deletion(o);
        for (i = 0; i < 30; i++)
            spawn_object(o, MODEL_BUBBLE, bhvBubbleMaybe);
    }
    if (find_water_levelq(QFIELD(o, oPosX), QFIELD(o, oPosZ)) < QFIELD(o, oPosY))
        obj_mark_for_deletion(o);
    o->oInteractStatus = 0;
}

void bhv_bubble_wave_init(void) {
    o->oWaterObjUnkFC  = 0x800 + (s32)(random_float() * 2048.0f);
    o->oWaterObjUnk100 = 0x800 + (s32)(random_float() * 2048.0f);
    cur_obj_play_sound_2(SOUND_GENERAL_QUIET_BUBBLE);
}

void scale_bubble_random(void) {
    cur_obj_scaleq(random_q32() + q(1));
}

void bhv_bubble_maybe_loop(void) {
    QMODFIELD(o, oPosY, += random_q32() * 3 + q(6));
    QMODFIELD(o, oPosX, += random_q32() * 10 - q(5));
    QMODFIELD(o, oPosZ, += random_q32() * 10 - q(5));
    o->header.gfx.scaleq[0] = sinqs(o->oWaterObjUnkF4) / 5 + QONE;
    o->oWaterObjUnkF4 += o->oWaterObjUnkFC;
    o->header.gfx.scaleq[1] = sinqs(o->oWaterObjUnkF8) / 5 + QONE;
    o->oWaterObjUnkF8 += o->oWaterObjUnk100;
}

void bhv_small_water_wave_loop(void) {
    q32 sp1Cq = find_water_levelq(QFIELD(o, oPosX), QFIELD(o, oPosZ));
    o->header.gfx.scaleq[0] = sinqs(o->oWaterObjUnkF4) / 5 + QONE;
    o->oWaterObjUnkF4 += o->oWaterObjUnkFC;
    o->header.gfx.scaleq[1] = sinqs(o->oWaterObjUnkF8) / 5 + QONE;
    o->oWaterObjUnkF8 += o->oWaterObjUnk100;
    if (QFIELD(o, oPosY) > sp1Cq) {
        o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
        QMODFIELD(o, oPosY, += q(5));
        if (gFreeObjectList.next) {
            spawn_object(o, MODEL_SMALL_WATER_SPLASH, bhvObjectWaterSplash);
        }
    }
    if (o->oInteractStatus & INT_STATUS_INTERACTED)
        obj_mark_for_deletion(o);
}

void scale_bubble_sin(void) {
    o->header.gfx.scaleq[0] = sinqs(o->oWaterObjUnkF4) / 2 + q(2);
    o->oWaterObjUnkF4 += o->oWaterObjUnkFC;
    o->header.gfx.scaleq[1] = sinqs(o->oWaterObjUnkF8) / 2 + q(2);
    o->oWaterObjUnkF8 += o->oWaterObjUnk100;
}

void bhv_particle_init(void) {
    obj_scale_xyzq(o, q(2), q(2), q(1));
    o->oWaterObjUnkFC = 0x800 + (s32)(random_u16() % 2048);
    o->oWaterObjUnk100 = 0x800 + (s32)(random_u16() % 2048);
    obj_translate_xyz_random(o, 100.0f);
}

void bhv_particle_loop() {
    q32 sp24q = find_water_levelq(QFIELD(o, oPosX), QFIELD(o, oPosZ));
    QMODFIELD(o, oPosY, += q(5));
    obj_translate_xz_random(o, 4.0f);
    scale_bubble_sin();
    if (QFIELD(o, oPosY) > sp24q && o->oTimer) {
        obj_mark_for_deletion(o);
        try_to_spawn_object(5, 0, o, MODEL_SMALL_WATER_SPLASH, bhvObjectWaterSplash);
    }
}

void bhv_small_bubbles_loop(void) {
    QMODFIELD(o, oPosY, += q(5.0f));
    obj_translate_xz_random(o, 4.0f);
    scale_bubble_sin();
}

void bhv_fish_group_loop(void) {
    if (gMarioCurrentRoom == 15 || gMarioCurrentRoom == 7)
        if (gGlobalTimer & 1)
            spawn_object(o, MODEL_WHITE_PARTICLE_SMALL, bhvSmallParticleBubbles);
}

void bhv_water_waves_init(void) {
    s32 sp1C;
    for (sp1C = 0; sp1C < 3; sp1C++)
        spawn_object(o, MODEL_WHITE_PARTICLE_SMALL, bhvSmallParticle);
}
