// cap.c.inc

static struct ObjectHitbox sCapHitbox = {
    /* interactType:      */ INTERACT_CAP,
    /* downOffset:        */ 0,
    /* damageOrCoinValue: */ 0,
    /* health:            */ 0,
    /* numLootCoins:      */ 0,
    /* radius:            */ 80,
    /* height:            */ 80,
    /* hurtboxRadius:     */ 90,
    /* hurtboxHeight:     */ 90,
};

s32 cap_set_hitbox(void) {
    obj_set_hitbox(o, &sCapHitbox);
    if (o->oInteractStatus & INT_STATUS_INTERACTED) {
        o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
        o->oInteractStatus = 0;
        return 1;
    }

    return 0;
}

void cap_despawn(void) {
    if (o->oTimer > 300) {
        obj_flicker_and_disappear(o, 300);
    }
}

void cap_check_quicksand(void) {
    if (sObjFloor == NULL)
        return;

    switch (sObjFloor->type) {
        case SURFACE_DEATH_PLANE:
            o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
            break;

        case SURFACE_SHALLOW_QUICKSAND:
        case SURFACE_DEEP_QUICKSAND:
        case SURFACE_QUICKSAND:
            o->oAction = 10;
            QSETFIELD(o,  oForwardVel, q(0));
            break;

        case SURFACE_DEEP_MOVING_QUICKSAND:
        case SURFACE_SHALLOW_MOVING_QUICKSAND:
        case SURFACE_MOVING_QUICKSAND:
            o->oAction = 11;
            o->oMoveAngleYaw = (sObjFloor->force & 0xFF) << 8;
            FSETFIELD(o, oForwardVel, 8 + 2 * (0 - ((sObjFloor->force & 0xFF00) >> 8)));
            break;

        case SURFACE_INSTANT_QUICKSAND:
            o->oAction = 12;
            QSETFIELD(o,  oForwardVel, q(0));
            break;

        case SURFACE_INSTANT_MOVING_QUICKSAND:
            o->oAction = 13;
            o->oMoveAngleYaw = (sObjFloor->force & 0xFF) << 8;
            FSETFIELD(o, oForwardVel, 8 + 2 * (0 - ((sObjFloor->force & 0xFF00) >> 8)));
            break;
    }
}

void cap_sink_quicksand(void) {
    switch (o->oAction) {
        case 10:
            if (o->oTimer < 10) {
                FMODFIELD(o, oGraphYOffset, += -1.0f);
                o->oFaceAnglePitch = 0x2000;
            }
            break;

        case 11:
            if (o->oTimer < 10)
                FMODFIELD(o, oGraphYOffset, += -3.0f);

            o->oFaceAnglePitch = 0x2000;
            break;

        case 12:
            FMODFIELD(o, oGraphYOffset, += -1.0f);
            if (o->oTimer >= 21)
                o->activeFlags = ACTIVE_FLAG_DEACTIVATED;

            break;

        case 13:
            FMODFIELD(o, oGraphYOffset, += -6.0f);
            if (o->oTimer >= 21)
                o->activeFlags = ACTIVE_FLAG_DEACTIVATED;

            o->oFaceAnglePitch = 0x2000;
            break;
    }

    cap_check_quicksand();
}

void bhv_wing_cap_init(void) {
    QSETFIELD(o, oGravity, q(1.2f));
    QSETFIELD(o,  oFriction, q(0.999));
    QSETFIELD(o,  oBuoyancy, q(0.9));
    o->oOpacity = 255;
}

void cap_scale_vertically(void) {
    o->oCapUnkF8 += 0x2000;
    o->header.gfx.scaleq[1] = cosqs(o->oCapUnkF8) * 3 / 10 + q(0.7);
    if (o->oCapUnkF8 == 0x10000) {
        o->oCapUnkF8 = 0;
        o->oCapUnkF4 = 2;
    }
}

void wing_vanish_cap_act_0(void) {
    s16 sp1E;

    o->oFaceAngleYaw += FFIELD(o, oForwardVel) * 128.0f;
    sp1E = object_step();
    if (sp1E & 0x01) {
        cap_check_quicksand();
        if (QFIELD(o, oVelY) != 0) {
            o->oCapUnkF4 = 1;
            QSETFIELD(o,  oVelY, q(0));
        }
    }

    if (o->oCapUnkF4 == 1)
        cap_scale_vertically();
}

void bhv_wing_vanish_cap_loop(void) {
    switch (o->oAction) {
        case 0:
            wing_vanish_cap_act_0();
            break;

        default:
            object_step();
            cap_sink_quicksand();
            break;
    }

    if (o->oTimer > 20)
        cur_obj_become_tangible();

    cap_despawn();
    cap_set_hitbox();
}

void bhv_metal_cap_init(void) {
    QSETFIELD(o, oGravity, q(2.4f));
    QSETFIELD(o,  oFriction, q(0.999));
    QSETFIELD(o,  oBuoyancy, q(1.5));
    o->oOpacity = 0xFF;
}

void metal_cap_act_0(void) {
    s16 sp1E;

    o->oFaceAngleYaw += FFIELD(o, oForwardVel) * 128.0f;
    sp1E = object_step();
    if (sp1E & 0x01)
        cap_check_quicksand();
}

void bhv_metal_cap_loop(void) {
    switch (o->oAction) {
        case 0:
            metal_cap_act_0();
            break;

        default:
            object_step();
            cap_sink_quicksand();
            break;
    }

    if (o->oTimer > 20)
        cur_obj_become_tangible();

    cap_set_hitbox();
    cap_despawn();
}

void bhv_normal_cap_init(void) {
    QSETFIELD(o, oGravity, q(0.7f));
    QSETFIELD(o,  oFriction, q(0.89));
    QSETFIELD(o,  oBuoyancy, q(0.9));
    o->oOpacity = 0xFF;

    save_file_set_cap_pos(FFIELD(o, oPosX), FFIELD(o, oPosY), FFIELD(o, oPosZ));
}

void normal_cap_set_save_flags(void) {
    save_file_clear_flags(SAVE_FLAG_CAP_ON_GROUND);

    switch (gCurrCourseNum) {
        case COURSE_SSL:
            save_file_set_flags(SAVE_FLAG_CAP_ON_KLEPTO);
            break;

        case COURSE_SL:
            save_file_set_flags(SAVE_FLAG_CAP_ON_MR_BLIZZARD);
            break;

        case COURSE_TTM:
            save_file_set_flags(SAVE_FLAG_CAP_ON_UKIKI);
            break;

        default:
            save_file_set_flags(SAVE_FLAG_CAP_ON_KLEPTO);
            break;
    }
}

void normal_cap_act_0(void) {
    s16 sp1E;

    o->oFaceAngleYaw += FFIELD(o, oForwardVel) * 128.0f;
    o->oFaceAnglePitch += FFIELD(o, oForwardVel) * 80.0f;
    sp1E = object_step();
    if (sp1E & 0x01) {
        cap_check_quicksand();

        if (QFIELD(o, oVelY) != 0) {
            o->oCapUnkF4 = 1;
            QSETFIELD(o,  oVelY, q(0));
            o->oFaceAnglePitch = 0;
        }
    }

    if (o->oCapUnkF4 == 1)
        cap_scale_vertically();
}

void bhv_normal_cap_loop(void) {
    switch (o->oAction) {
        case 0:
            normal_cap_act_0();
            break;

        default:
            object_step();
            cap_sink_quicksand();
            break;
    }

    if (IFIELD(o, oForwardVel) != 0)
        save_file_set_cap_pos(FFIELD(o, oPosX), FFIELD(o, oPosY), FFIELD(o, oPosZ));

    if (o->activeFlags == ACTIVE_FLAG_DEACTIVATED)
        normal_cap_set_save_flags();

    if (cap_set_hitbox() == 1)
        save_file_clear_flags(SAVE_FLAG_CAP_ON_GROUND);
}

void bhv_vanish_cap_init(void) {
    QSETFIELD(o, oGravity, q(1.2f));
    QSETFIELD(o,  oFriction, q(0.999));
    QSETFIELD(o,  oBuoyancy, q(0.9));
    o->oOpacity = 150;
}
