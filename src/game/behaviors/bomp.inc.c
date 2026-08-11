// bomp.c.inc

void bhv_small_bomp_init(void) {
    o->oFaceAngleYaw -= 0x4000;
    QSETFIELD(o,  oSmallBompInitX, QFIELD(o,  oPosX));
    o->oTimer = random_float() * 100.0f;
}

void bhv_small_bomp_loop(void) {
    switch (o->oAction) {
        case BOMP_ACT_WAIT:
            if (o->oTimer >= 101) {
                o->oAction = BOMP_ACT_POKE_OUT;
                QSETFIELD(o,  oForwardVel, q(30));
            }
            break;

        case BOMP_ACT_POKE_OUT:
            if (QFIELD(o, oPosX) > q(3450.0f)) {
                QSETFIELD(o,  oPosX, q(3450));
                QSETFIELD(o,  oForwardVel, q(0));
            }

            if (o->oTimer == 15.0) {
                o->oAction = BOMP_ACT_EXTEND;
                QSETFIELD(o,  oForwardVel, q(40));
                cur_obj_play_sound_2(SOUND_OBJ_UNKNOWN2);
            }
            break;

        case BOMP_ACT_EXTEND:
            if (QFIELD(o, oPosX) > q(3830.0f)) {
                QSETFIELD(o,  oPosX, q(3830));
                QSETFIELD(o,  oForwardVel, q(0));
            }

            if (o->oTimer == 60) {
                o->oAction = BOMP_ACT_RETRACT;
                QSETFIELD(o,  oForwardVel, q(10));
                o->oMoveAngleYaw -= 0x8000;
                cur_obj_play_sound_2(SOUND_OBJ_UNKNOWN2);
            }
            break;

        case BOMP_ACT_RETRACT:
            if (QFIELD(o, oPosX) < q(3330.0f)) {
                QSETFIELD(o,  oPosX, q(3330));
                QSETFIELD(o,  oForwardVel, q(0));
            }

            if (o->oTimer == 90) {
                o->oAction = BOMP_ACT_POKE_OUT;
                QSETFIELD(o,  oForwardVel, q(25));
                o->oMoveAngleYaw -= 0x8000;
            }
            break;
    }
}

void bhv_large_bomp_init(void) {
    o->oMoveAngleYaw += 0x4000;
    o->oTimer = random_float() * 100.0f;
}

void bhv_large_bomp_loop(void) {
    switch (o->oAction) {
        case BOMP_ACT_WAIT:
            if (o->oTimer >= 101) {
                o->oAction = BOMP_ACT_POKE_OUT;
                QSETFIELD(o,  oForwardVel, q(30));
            }
            break;

        case BOMP_ACT_POKE_OUT:
            if (QFIELD(o, oPosX) > q(3450.0f)) {
                QSETFIELD(o,  oPosX, q(3450));
                QSETFIELD(o,  oForwardVel, q(0));
            }

            if (o->oTimer == 15.0) {
                o->oAction = BOMP_ACT_EXTEND;
                QSETFIELD(o,  oForwardVel, q(10));
                cur_obj_play_sound_2(SOUND_OBJ_UNKNOWN2);
            }
            break;

        case BOMP_ACT_EXTEND:
            if (QFIELD(o, oPosX) > q(3830.0f)) {
                QSETFIELD(o,  oPosX, q(3830));
                QSETFIELD(o,  oForwardVel, q(0));
            }

            if (o->oTimer == 60) {
                o->oAction = BOMP_ACT_RETRACT;
                QSETFIELD(o,  oForwardVel, q(10));
                o->oMoveAngleYaw -= 0x8000;
                cur_obj_play_sound_2(SOUND_OBJ_UNKNOWN2);
            }
            break;

        case BOMP_ACT_RETRACT:
            if (QFIELD(o, oPosX) < q(3330.0f)) {
                QSETFIELD(o,  oPosX, q(3330));
                QSETFIELD(o,  oForwardVel, q(0));
            }

            if (o->oTimer == 90) {
                o->oAction = BOMP_ACT_POKE_OUT;
                QSETFIELD(o,  oForwardVel, q(25));
                o->oMoveAngleYaw -= 0x8000;
            }
            break;
    }
}
