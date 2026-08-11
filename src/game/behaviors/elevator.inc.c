// elevator.c.inc

static s16 sElevatorHeights[] = {   -51,    0, 0,
                                    -461,    0, 0,
                                    -512,    0, 0,
                                   -2611,    0, 0,
                                   -2360,    0, 0,
                                     214,    0, 0,
                                     -50, 1945, 1 };

void elevator_starting_shake(void) {
    cur_obj_play_sound_2(SOUND_GENERAL_QUIET_POUND1);
    cur_obj_shake_screen(SHAKE_POS_SMALL);
}

void elevator_act_0(void) {
    QSETFIELD(o, oVelY, 0);
    if (o->oElevatorUnk100 == 2) {
        if (gMarioObject->platform == o) {
            if (QFIELD(o, oPosY) > QFIELD(o, oElevatorUnkFC))
                o->oAction = 2;
            else
                o->oAction = 1;
        }
    } else if (QFIELD(gMarioObject, oPosY) > QFIELD(o, oElevatorUnkFC) || o->oElevatorUnk100 == 1) {
        QSETFIELD(o, oPosY, QFIELD(o, oElevatorUnkF8));
        if (gMarioObject->platform == o)
            o->oAction = 2;
    } else {
        QSETFIELD(o, oPosY, QFIELD(o, oElevatorUnkF4));
        if (gMarioObject->platform == o)
            o->oAction = 1;
    }
}

void elevator_act_1(void) {
    cur_obj_play_sound_1(SOUND_ENV_ELEVATOR1);
    if (o->oTimer == 0 && cur_obj_is_mario_on_platform())
        elevator_starting_shake();
    q32 oVelYq = QFIELD(o, oVelY);
    approach_q32_signed(&oVelYq, q(10), q(2));
	QSETFIELD(o, oVelY, oVelYq);
    QMODFIELD(o, oPosY, += oVelYq);
    if (QFIELD(o, oPosY) > QFIELD(o, oElevatorUnkF8)) {
        QSETFIELD(o, oPosY, QFIELD(o, oElevatorUnkF8));
        if (o->oElevatorUnk100 == 2 || o->oElevatorUnk100 == 1)
            o->oAction = 3;
        else if (QFIELD(gMarioObject, oPosY) < QFIELD(o, oElevatorUnkFC))
            o->oAction = 2;
        else
            o->oAction = 3;
    }
}

void elevator_act_2(void) // Pretty similar code to action 1
{
    cur_obj_play_sound_1(SOUND_ENV_ELEVATOR1);
    if (o->oTimer == 0 && cur_obj_is_mario_on_platform())
        elevator_starting_shake();
    f32 velY = FFIELD(o, oVelY);
    approach_f32_signed(&velY, -10.0f, -2.0f);
    FSETFIELD(o, oVelY, velY);
    QMODFIELD(o, oPosY, += QFIELD(o, oVelY));
    if (QFIELD(o, oPosY) < QFIELD(o, oElevatorUnkF4)) {
        QSETFIELD(o, oPosY, QFIELD(o, oElevatorUnkF4));
        if (o->oElevatorUnk100 == 1)
            o->oAction = 4;
        else if (o->oElevatorUnk100 == 2)
            o->oAction = 3;
        else if (QFIELD(gMarioObject, oPosY) > QFIELD(o, oElevatorUnkFC))
            o->oAction = 1;
        else
            o->oAction = 3;
    }
}

void elevator_act_4(void) {
    QSETFIELD(o,  oVelY, q(0));
    if (o->oTimer == 0) {
        cur_obj_shake_screen(SHAKE_POS_SMALL);
        cur_obj_play_sound_2(SOUND_GENERAL_METAL_POUND);
    }
    if (!mario_is_in_air_action() && !cur_obj_is_mario_on_platform())
        o->oAction = 1;
}

void elevator_act_3(void) // nearly identical to action 2
{
    QSETFIELD(o,  oVelY, q(0));
    if (o->oTimer == 0) {
        cur_obj_shake_screen(SHAKE_POS_SMALL);
        cur_obj_play_sound_2(SOUND_GENERAL_METAL_POUND);
    }
    if (!mario_is_in_air_action() && !cur_obj_is_mario_on_platform())
        o->oAction = 0;
}

void bhv_elevator_init(void) {
    s32 sp1C = sElevatorHeights[o->oBehParams2ndByte * 3 + 2];
    if (sp1C == 0) {
        QSETFIELD(o, oElevatorUnkF4, q(sElevatorHeights[o->oBehParams2ndByte * 3]));
        QSETFIELD(o, oElevatorUnkF8, QFIELD(o, oHomeY));
        QSETFIELD(o, oElevatorUnkFC, (QFIELD(o, oElevatorUnkF4) + QFIELD(o, oElevatorUnkF8)) / 2);
        o->oElevatorUnk100 = cur_obj_has_behavior(bhvRrElevatorPlatform);
    } else {
        QSETFIELD(o, oElevatorUnkF4, q(sElevatorHeights[o->oBehParams2ndByte * 3]));
        QSETFIELD(o, oElevatorUnkF8, q(sElevatorHeights[o->oBehParams2ndByte * 3 + 1]));
        QSETFIELD(o, oElevatorUnkFC, (QFIELD(o, oElevatorUnkF4) + QFIELD(o, oElevatorUnkF8)) / 2);
        o->oElevatorUnk100 = 2;
    }
}

void (*sElevatorActions[])(void) = { elevator_act_0, elevator_act_1, elevator_act_2, elevator_act_3,
                                     elevator_act_4 };

void bhv_elevator_loop(void) {
    cur_obj_call_action_function(sElevatorActions);
}
