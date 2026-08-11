// white_puff_explode.c.inc

void bhv_white_puff_exploding_loop(void) {
    q32 sp24q;
    if (o->oTimer == 0) {
        cur_obj_compute_vel_xz();
        QSETFIELD(o, oWhitePuffUnkF4, o->header.gfx.scaleq[0]);
        switch (o->oBehParams2ndByte) {
            case 2:
                o->oOpacity = 254;
                o->oWhitePuffUnkF8 = -21;
                o->oWhitePuffUnkFC = 0;
                break;
            case 3:
                o->oOpacity = 254;
                o->oWhitePuffUnkF8 = -13;
                o->oWhitePuffUnkFC = 1;
                break;
        }
    }
    cur_obj_move_using_vel_and_gravity();
    cur_obj_apply_drag_xzq(QFIELD(o, oDragStrength));
    if (QFIELD(o, oVelY) > q(100))
        QSETFIELD(o, oVelY, q(100));
    if (o->oTimer > 20)
        obj_mark_for_deletion(o);
    if (o->oOpacity) {
        o->oOpacity += o->oWhitePuffUnkF8;
        if (o->oOpacity < 2)
            obj_mark_for_deletion(o);
        if (o->oWhitePuffUnkFC)
            sp24q = QFIELD(o, oWhitePuffUnkF4) * (254 - o->oOpacity) / 254;
        else
            sp24q = QFIELD(o, oWhitePuffUnkF4) * o->oOpacity / 254;
        cur_obj_scaleq(sp24q);
    }
}
