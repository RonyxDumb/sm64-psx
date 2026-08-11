// pole_base.inc.c

void bhv_pole_base_loop(void) {
    if (QFIELD(o, oPosY) - q(10) < QFIELD(gMarioObject, oPosY)
        && QFIELD(gMarioObject, oPosY) < QFIELD(o, oPosY) + q(o->hitboxHeight_s16) + q(30))
        if (o->oTimer > 10)
            if (!(gMarioStates[0].action & MARIO_PUNCHING))
                cur_obj_push_mario_away(70);
}
