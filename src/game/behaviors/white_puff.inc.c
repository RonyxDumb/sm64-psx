// white_puff.c.inc

void bhv_white_puff_1_loop(void) {
    q32 sp1Cq = 0.1f;
    q32 sp18q = 0.5f;
    if (o->oTimer == 0) {
        obj_translate_xz_random(o, 40.0f);
        QMODFIELD(o, oPosY, += q(30.0f));
    }
    cur_obj_scaleq(o->oTimer * sp18q + sp1Cq);
    o->oOpacity = 50;
    cur_obj_move_using_fvel_and_gravity();
    if (o->oTimer > 4)
        obj_mark_for_deletion(o);
}

void bhv_white_puff_2_loop(void) {
    if (o->oTimer == 0)
        obj_translate_xz_random(o, 40.0f);
}
