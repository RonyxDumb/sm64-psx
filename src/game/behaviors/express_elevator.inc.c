// express_elevator.c.inc

void bhv_wdw_express_elevator_loop(void) {
    QSETFIELD(o,  oVelY, q(0));
    if (o->oAction == 0) {
        if (cur_obj_is_mario_on_platform())
            o->oAction++;
    } else if (o->oAction == 1) {
        QSETFIELD(o,  oVelY, q(-20));
        QMODFIELD(o, oPosY, += QFIELD(o, oVelY));
        cur_obj_play_sound_1(SOUND_ENV_ELEVATOR4);
        if (o->oTimer > 132)
            o->oAction++;
    } else if (o->oAction == 2) {
        if (o->oTimer > 110)
            o->oAction++;
    } else if (o->oAction == 3) {
        QSETFIELD(o,  oVelY, q(10));
        QMODFIELD(o, oPosY, += QFIELD(o, oVelY));
        cur_obj_play_sound_1(SOUND_ENV_ELEVATOR4);
        if (QFIELD(o, oPosY) >= QFIELD(o, oHomeY)) {
            QSETFIELD(o,  oPosY, QFIELD(o,  oHomeY));
            o->oAction++;
        }
    } else if (!cur_obj_is_mario_on_platform())
        o->oAction = 0;
}
