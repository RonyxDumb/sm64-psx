// thwomp.c.inc

void grindel_thwomp_act_4(void) {
    if (o->oTimer == 0)
        o->oThwompRandomTimer = random_float() * 10.0f + 20.0f;
    if (o->oTimer > o->oThwompRandomTimer)
        o->oAction = 0;
}

void grindel_thwomp_act_2(void) {
    QMODFIELD(o, oVelY, += q(-4));
    QMODFIELD(o, oPosY, += QFIELD(o, oVelY));
    if (QFIELD(o, oPosY) < QFIELD(o, oHomeY)) {
        QSETFIELD(o,  oPosY, QFIELD(o,  oHomeY));
        QSETFIELD(o,  oVelY, q(0));
        o->oAction = 3;
    }
}

void grindel_thwomp_act_3(void) {
    if (o->oTimer == 0)
        if (QFIELD(o, oDistanceToMario) < q(1500.0)) {
            cur_obj_shake_screen(SHAKE_POS_SMALL);
            cur_obj_play_sound_2(SOUND_OBJ_THWOMP);
        }
    if (o->oTimer > 9)
        o->oAction = 4;
}

void grindel_thwomp_act_1(void) {
    if (o->oTimer == 0)
        o->oThwompRandomTimer = random_float() * 30.0f + 10.0f;
    if (o->oTimer > o->oThwompRandomTimer)
        o->oAction = 2;
}

void grindel_thwomp_act_0(void) {
    if (o->oBehParams2ndByte + 40 < o->oTimer) {
        o->oAction = 1;
        QMODFIELD(o, oPosY, += q(5));
    } else
        QMODFIELD(o, oPosY, += q(10));
}

void (*sGrindelThwompActions[])(void) = { grindel_thwomp_act_0, grindel_thwomp_act_1,
                                          grindel_thwomp_act_2, grindel_thwomp_act_3,
                                          grindel_thwomp_act_4 };

void bhv_grindel_thwomp_loop(void) {
    cur_obj_call_action_function(sGrindelThwompActions);
}
