// floating_box.c.inc

void bhv_jrb_floating_box_loop(void) {
    FSETFIELD(o, oPosY, FFIELD(o, oHomeY) + sins(o->oTimer * 0x400) * 10.0f);
}
