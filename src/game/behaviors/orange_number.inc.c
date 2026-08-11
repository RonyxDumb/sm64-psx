// orange_number.c.inc

void bhv_orange_number_init(void) {
    o->oAnimState = o->oBehParams2ndByte;
    QSETFIELD(o,  oVelY, q(26));
}

void bhv_orange_number_loop(void) {
    struct Object *sp1C;
    QMODFIELD(o, oPosY, += QFIELD(o, oVelY));
    QMODFIELD(o, oVelY, -= q(2.0f));
        QSETFIELD(o,  oVelY, q(14));

    if (o->oTimer == 35) {
        sp1C = spawn_object(o, MODEL_SPARKLES, bhvGoldenCoinSparkles);
        QMODFIELD(sp1C, oPosY, -= q(30.f));
        o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
    }
}
