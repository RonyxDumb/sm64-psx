// boulder.c.inc

void bhv_big_boulder_init(void) {
    QSETFIELD(o, oHomeX, QFIELD(o, oPosX));
    QSETFIELD(o, oHomeY, QFIELD(o, oPosY));
    QSETFIELD(o, oHomeZ, QFIELD(o, oPosZ));

    QSETFIELD(o, oGravity, q(8.0f));
    QSETFIELD(o, oFriction, q(0.999));
    QSETFIELD(o, oBuoyancy, q(2));
}

void boulder_act_1(void) {
    s16 sp1E;

    sp1E = object_step_without_floor_orient();
    if ((sp1E & 0x09) == 0x01 && FFIELD(o, oVelY) > 10.0f) {
        cur_obj_play_sound_2(SOUND_GENERAL_GRINDEL_ROLL);
        spawn_mist_particles();
    }

    if (QFIELD(o, oForwardVel) > q(70.0))
        QSETFIELD(o,  oForwardVel, q(70));

    if (FFIELD(o, oPosY) < -1000.0f)
        o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
}

void bhv_big_boulder_loop(void) {
    cur_obj_scaleq(q(1.5f));
    QSETFIELD(o,  oGraphYOffset, q(270));
    switch (o->oAction) {
        case 0:
            QSETFIELD(o,  oForwardVel, q(40));
            o->oAction = 1;
            break;

        case 1:
            boulder_act_1();
            adjust_rolling_face_pitchq(q(1.5f));
            cur_obj_play_sound_1(SOUND_ENV_UNKNOWN2);
            break;
    }

    set_rolling_sphere_hitbox();
}

void bhv_big_boulder_generator_loop(void) {
    struct Object *sp1C;
    if (o->oTimer >= 256) {
        o->oTimer = 0;
    }

    if (!current_mario_room_check(4) || is_point_within_radius_of_mario(IFIELD(o, oPosX), IFIELD(o, oPosY), IFIELD(o, oPosZ), 1500))
        return;

    u32 mask;
    if (is_point_within_radius_of_mario(IFIELD(o, oPosX), IFIELD(o, oPosY), IFIELD(o, oPosZ), 6000)) {
        mask = 0x3F;
    } else {
        mask = 0x7F;
    }
    if ((o->oTimer & mask) == 0) {
        sp1C = spawn_object(o, MODEL_HMC_ROLLING_ROCK, bhvBigBoulder);
        sp1C->oMoveAngleYaw = random_float() * 4096.0f;
    }
}
