// lll_hexagonal_ring.c.inc

void hexagonal_ring_spawn_flames(void) {
    struct Object *sp1C;
    sp1C = spawn_object(o, MODEL_RED_FLAME, bhvVolcanoFlames);
    QMODFIELD(sp1C, oPosY, += q(550));
    sp1C->oMoveAngleYaw = (u32) random_u16() << 16 >> 16;
    QSETFIELD(sp1C, oForwardVel, random_q32() * 40 + q(20));
    QSETFIELD(sp1C, oVelY, random_q32() * 50 + q(10));
    q32 sizeq = random_q32() * 6 + q(3);
    obj_scale_xyzq(sp1C, sizeq, sizeq, sizeq);
    if (random_q32() < q(0.1))
        cur_obj_play_sound_2(SOUND_GENERAL_VOLCANO_EXPLOSION);
}

void bhv_lll_rotating_hexagonal_ring_loop(void) {
    UNUSED s32 unused;
    QSETFIELD(o, oCollisionDistance, q(4000));
    QSETFIELD(o, oDrawingDistance, q(8000));
    switch (o->oAction) {
        case 0:
            if (gMarioObject->platform == o)
                o->oAction++;
            o->oAngleVelYaw = 0x100;
            break;
        case 1:
            o->oAngleVelYaw = 256.0f - sins(o->oTimer << 7) * 256.0f;
            if (o->oTimer > 128)
                o->oAction++;
            break;
        case 2:
            if (gMarioObject->platform != o)
                o->oAction++;
            if (o->oTimer > 128)
                o->oAction++;
            o->oAngleVelYaw = 0;
            hexagonal_ring_spawn_flames();
            break;
        case 3:
            o->oAngleVelYaw = sins(o->oTimer << 7) * 256.0f;
            if (o->oTimer > 128)
                o->oAction = 0;
            break;
        case 4:
            o->oAction = 0;
            break;
    }
    o->oAngleVelYaw = -o->oAngleVelYaw;
    o->oMoveAngleYaw += o->oAngleVelYaw;
}
