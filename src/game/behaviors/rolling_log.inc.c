// rolling_log.c.inc

// why are the falling platforms and rolling logs grouped
// together? seems strange, but it also cooresponds to the
// behavior script grouping if the same file is assumed.
// hypothesis is that the object in the middle here used to be
// a rolling log of another variation.

void bhv_ttm_rolling_log_init(void) {
    QSETFIELD(o, oPitouneUnkF8, 3970);
    QSETFIELD(o, oPitouneUnkFC, 3654);
    QSETFIELD(o, oPitouneUnkF4, 271037);
    o->oMoveAngleYaw = 8810;
    QSETFIELD(o,  oForwardVel, q(0));
    QSETFIELD(o,  oVelX, q(0));
    QSETFIELD(o,  oVelZ, q(0));
    o->oFaceAnglePitch = 0;
    o->oAngleVelPitch = 0;
}

void rolling_log_roll_log(void) {
    if (gMarioObject->platform == o) {
        q32 sp24 =
            (gMarioObject->header.gfx.posi[2] - qtrunc(QFIELD(o, oPosZ))) * cosqs(-o->oMoveAngleYaw) -
            (gMarioObject->header.gfx.posi[0] - qtrunc(QFIELD(o, oPosX))) * sinqs(-o->oMoveAngleYaw);
        if (sp24 > 0)
            o->oAngleVelPitch += 0x10;
        else
            o->oAngleVelPitch -= 0x10;

        if (o->oAngleVelPitch > 0x200)
            o->oAngleVelPitch = 0x200;

        if (o->oAngleVelPitch < -0x200)
            o->oAngleVelPitch = -0x200;
    } else {
        if (is_point_close_to_object(o, FFIELD(o, oHomeX), FFIELD(o, oHomeY), FFIELD(o, oHomeZ), 100)) {
            if (o->oAngleVelPitch != 0) {
                if (o->oAngleVelPitch > 0)
                    o->oAngleVelPitch -= 0x10;
                else
                    o->oAngleVelPitch += 0x10;

                if (o->oAngleVelPitch < 0x10 && o->oAngleVelPitch > -0x10)
                    o->oAngleVelPitch = 0;
            }
        } else {
            if (o->oAngleVelPitch != 0x100) {
                if (o->oAngleVelPitch > 0x100)
                    o->oAngleVelPitch -= 0x10;
                else
                    o->oAngleVelPitch += 0x10;

                if (o->oAngleVelPitch < 0x110 && o->oAngleVelPitch > 0xF0)
                    o->oAngleVelPitch = 0x100;
            }
        }
    }
}

void bhv_rolling_log_loop(void) {
    f32 prevX = FFIELD(o, oPosX);
    f32 prevZ = FFIELD(o, oPosZ);

    rolling_log_roll_log();

    FSETFIELD(o, oForwardVel, o->oAngleVelPitch / 0x40);
    FSETFIELD(o, oVelX, FFIELD(o, oForwardVel) * sins(o->oMoveAngleYaw));
    FSETFIELD(o, oVelZ, FFIELD(o, oForwardVel) * coss(o->oMoveAngleYaw));

    QMODFIELD(o, oPosX, += QFIELD(o, oVelX));
    QMODFIELD(o, oPosZ, += QFIELD(o, oVelZ));

    if (QFIELD(o, oPitouneUnkF4) < sqr(IFIELD(o, oPosX) - QFIELD(o, oPitouneUnkF8)) + sqr(IFIELD(o, oPosZ) - QFIELD(o, oPitouneUnkFC))) {
        QSETFIELD(o,  oForwardVel, q(0));
        FSETFIELD(o, oPosX, prevX);
        FSETFIELD(o, oPosZ, prevZ);
        QSETFIELD(o,  oVelX, q(0));
        QSETFIELD(o,  oVelZ, q(0));
    }

    o->oFaceAnglePitch += o->oAngleVelPitch;
    if (absf_2(o->oFaceAnglePitch & 0x1FFF) < 528.0f && o->oAngleVelPitch != 0) {
        cur_obj_play_sound_2(SOUND_GENERAL_UNKNOWN1_2);
    }
}

void volcano_act_1(void) {
    QMODFIELD(o, oRollingLogUnkF4, += q(4.0f));
    o->oAngleVelPitch += FFIELD(o, oRollingLogUnkF4);
    o->oFaceAnglePitch -= o->oAngleVelPitch;

    if (o->oFaceAnglePitch < -0x4000) {
        o->oFaceAnglePitch = -0x4000;
        o->oAngleVelPitch = 0;
        QSETFIELD(o,  oRollingLogUnkF4, q(0));
        o->oAction = 2;
        cur_obj_play_sound_2(SOUND_GENERAL_BIG_POUND);
        set_camera_shake_from_pointq(SHAKE_POS_LARGE, QFIELD(o, oPosX), QFIELD(o, oPosY), QFIELD(o, oPosZ));
    }
}

void volcano_act_3(void) {
    o->oAngleVelPitch = 0x90;
    o->oFaceAnglePitch += o->oAngleVelPitch;
    if (o->oFaceAnglePitch > 0)
        o->oFaceAnglePitch = 0;

    if (o->oTimer == 200)
        o->oAction = 0;
}

void bhv_volcano_trap_loop(void) {
    switch (o->oAction) {
        case 0:
            if (is_point_within_radius_of_mario(IFIELD(o, oPosX), IFIELD(o, oPosY), IFIELD(o, oPosZ), 1000)) {
                o->oAction = 1;
                cur_obj_play_sound_2(SOUND_GENERAL_QUIET_POUND2);
            }
            break;

        case 1:
            volcano_act_1();
            break;

        case 2:
            if (o->oTimer < 8) {
                FSETFIELD(o, oPosY, FFIELD(o, oHomeY) + sins(o->oTimer * 0x1000) * 10.0f);
            }
            if (o->oTimer == 50) {
                cur_obj_play_sound_2(SOUND_GENERAL_UNK45);
                o->oAction = 3;
            }
            break;

        case 3:
            volcano_act_3();
            break;
    }
}

void bhv_lll_rolling_log_init(void) {
    QSETFIELD(o, oPitouneUnkF8, 5120);
    QSETFIELD(o, oPitouneUnkFC, 6016);
    QSETFIELD(o, oPitouneUnkF4, 1048576);

    o->oMoveAngleYaw = 0x3FFF;
    QSETFIELD(o,  oForwardVel, q(0));
    QSETFIELD(o,  oVelX, q(0));
    QSETFIELD(o,  oVelZ, q(0));
    o->oFaceAnglePitch = 0;
    o->oAngleVelPitch = 0;
}
