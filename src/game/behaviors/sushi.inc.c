// sushi.c.inc

void bhv_sushi_shark_collision_loop(void) {
}

void bhv_sushi_shark_loop(void) {
    q32 sp1Cq = find_water_levelq(QFIELD(o, oPosX), QFIELD(o, oPosZ));
    FSETFIELD(o, oPosX, FFIELD(o, oHomeX) + sins(o->oSushiSharkUnkF4) * 1700.0f);
    FSETFIELD(o, oPosZ, FFIELD(o, oHomeZ) + coss(o->oSushiSharkUnkF4) * 1700.0f);
    FSETFIELD(o, oPosY, qtof(sp1Cq + QFIELD(o, oHomeY) + sinqs(o->oSushiSharkUnkF4) * 200));
    o->oMoveAngleYaw = o->oSushiSharkUnkF4 + 0x4000;
    o->oSushiSharkUnkF4 += 0x80;
    if (QFIELD(gMarioObject, oPosY) - sp1Cq > q(-500))
        if (QFIELD(o, oPosY) - sp1Cq > q(-200))
            spawn_object_with_scale(o, MODEL_WAVE_TRAIL, bhvObjectWaveTrail, 4.0f);
    if ((o->oTimer & 0xF) == 0)
        cur_obj_play_sound_2(SOUND_OBJ_SUSHI_SHARK_WATER_SOUND);
    o->oInteractStatus = 0;
}
