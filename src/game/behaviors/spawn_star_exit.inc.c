// spawn_star_exit.c.inc

void bhv_ccm_touched_star_spawn_loop(void) {
    if (gCCMEnteredSlide & 1) {
        QMODFIELD(o, oPosY, += q(100.0f));
        QSETFIELD(o,  oPosX, q(2780));
        QSETFIELD(o,  oPosZ, q(4666));
        spawn_default_star(2500.0f, -4350.0f, 5750.0f);
        obj_mark_for_deletion(o);
    }
}
