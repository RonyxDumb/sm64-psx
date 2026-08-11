// snow_mound.c.inc

void bhv_sliding_snow_mound_loop(void) {
    switch (o->oAction) {
        case 0:
            QSETFIELD(o, oVelX, q(-40));
            QMODFIELD(o, oPosX, += QFIELD(o, oVelX));
            if (o->oTimer >= 118)
                o->oAction = 1;

            cur_obj_play_sound_1(SOUND_ENV_SINK_QUICKSAND);
            break;

        case 1:
            QSETFIELD(o, oVelX, q(-5));
            QMODFIELD(o, oPosX, += QFIELD(o, oVelX));
            QSETFIELD(o, oVelY, q(-10));
            QMODFIELD(o, oPosY, += QFIELD(o, oVelY));
            QSETFIELD(o, oPosZ, QFIELD(o, oHomeZ) - q(2));
            if (o->oTimer > 50)
                o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
            break;
    }
}

void bhv_snow_mound_spawn_loop(void) {
    struct Object *sp1C;

    if (!is_point_within_radius_of_mario(IFIELD(o, oPosX), IFIELD(o, oPosY), IFIELD(o, oPosZ), 6000)
        || qtrunc(QFIELD(o, oPosY)) + 1000 < gMarioObject->header.gfx.posi[1])
        return;

    if (o->oTimer == 64 || o->oTimer == 128 || o->oTimer == 192 || o->oTimer == 224 || o->oTimer == 256)
        sp1C = spawn_object(o, MODEL_SL_SNOW_TRIANGLE, bhvSlidingSnowMound);

    if (o->oTimer == 256) {
        sp1C->header.gfx.scaleq[0] = q(2);
        sp1C->header.gfx.scaleq[1] = q(2);
    }

    if (o->oTimer >= 256)
        o->oTimer = 0;
}
