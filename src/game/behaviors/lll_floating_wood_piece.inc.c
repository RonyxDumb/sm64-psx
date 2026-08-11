// lll_floating_wood_piece.c.inc

void bhv_lll_wood_piece_loop(void) {
    if (o->oTimer == 0)
        QMODFIELD(o, oPosY, -= q(100));
    QMODFIELD(o, oPosY, += sinqs(o->oLllWoodPieceOscillationTimer) * 3);
    o->oLllWoodPieceOscillationTimer += 0x400;
    if (o->parentObj->oAction == 2)
        obj_mark_for_deletion(o);
}

void bhv_lll_floating_wood_bridge_loop(void) {
    struct Object *sp3C;
    s32 i;
    switch (o->oAction) {
        case 0:
            if (QFIELD(o, oDistanceToMario) < q(2500.0)) {
                for (i = 1; i < 4; i++) {
                    sp3C = spawn_object_relative(0, (i - 2) * 300, 0, 0, o, MODEL_LLL_WOOD_BRIDGE,
                                                 bhvLllWoodPiece);
                    sp3C->oLllWoodPieceOscillationTimer = i * 4096;
                }
                o->oAction = 1;
            }
            break;
        case 1:
            if (QFIELD(o, oDistanceToMario) > q(2600.0))
                o->oAction = 2;
            break;
        case 2:
            o->oAction = 0;
            break;
    }
}
