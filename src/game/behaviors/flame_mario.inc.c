// flame_mario.c.inc

void bhv_black_smoke_upward_loop(void) {
    spawn_object_with_scale(o, MODEL_BURN_SMOKE, bhvBlackSmokeBowser, qtof(o->header.gfx.scaleq[0]));
}

void bhv_black_smoke_bowser_loop(void) {
    if (o->oTimer == 0) {
        QSETFIELD(o, oForwardVel, random_q32() * 2 + q(0.5));
        o->oMoveAngleYaw = random_u16();
        QSETFIELD(o, oVelY, q(8));
        QSETFIELD(o, oBlackSmokeBowserUnkF4, o->header.gfx.scaleq[0]);
    }
    o->oMoveAngleYaw += o->oAngleVelYaw;
    QMODFIELD(o, oPosY, += QFIELD(o, oVelY));
}

void bhv_black_smoke_mario_loop(void) {
    if (o->oTimer == 0) {
        cur_obj_set_pos_relative(gMarioObject, 0, 0, -30.0f);
        FSETFIELD(o, oForwardVel, random_float() * 2 + 0.5);
        o->oMoveAngleYaw = (gMarioObject->oMoveAngleYaw + 0x7000) + random_float() * 8192.0f;
        QSETFIELD(o,  oVelY, q(8));
    }
    o->oMoveAngleYaw += o->oAngleVelYaw;
    QMODFIELD(o, oPosY, += QFIELD(o, oVelY));
}

void bhv_flame_mario_loop(void) {
    cur_obj_scaleq(q(2.0f));
    if (o->oTimer != 0)
        if (o->oTimer & 1)
            spawn_object(o, MODEL_BURN_SMOKE, bhvBlackSmokeMario);
    gMarioObject->prevObj = o; // weird?
    obj_set_parent_relative_pos(o, 40, -120, 0);
    if (!(gMarioObject->oMarioParticleFlags & 0x800)) {
        o->parentObj->oActiveParticleFlags &= ~0x800;
        obj_mark_for_deletion(o);
        gMarioObject->prevObj = NULL;
    }
}
