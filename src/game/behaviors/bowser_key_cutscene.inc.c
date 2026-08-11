// bowser_key_cutscene.inc.c

Gfx *geo_scale_bowser_key(s32 run, struct GraphNode *node, UNUSED const ShortMatrix* mtxq) {
    struct Object *sp4;
    if (run == TRUE) {
        sp4 = (struct Object *) gCurGraphNodeObject;
        ((struct GraphNodeScale *) node->next)->scale = FFIELD(sp4, oBowserKeyScale);
    }
    return 0;
}

void bhv_bowser_key_unlock_door_loop(void) {
    s32 animTimer;
    animTimer = o->header.gfx.animInfo.animFrame;
    cur_obj_init_animation_with_sound(0);
    if (animTimer < 38)
        QSETFIELD(o, oBowserKeyScale, q(0.0));
    else if (animTimer < 49)
        QSETFIELD(o, oBowserKeyScale, q(0.2));
    else if (animTimer < 58)
        FSETFIELD(o, oBowserKeyScale, (animTimer - 53) * 0.11875f + 0.2f); // 0.11875?
    else if (animTimer < 59)
        QSETFIELD(o, oBowserKeyScale, q(1.1));
    else if (animTimer < 60)
        QSETFIELD(o, oBowserKeyScale, q(1.05));
    else
        QSETFIELD(o, oBowserKeyScale, q(1.0));
    if (o->oTimer > 150)
        obj_mark_for_deletion(o);
}

void bhv_bowser_key_course_exit_loop(void) {
    s32 animTimer = o->header.gfx.animInfo.animFrame;
    cur_obj_init_animation_with_sound(1);
    if (animTimer < 38)
        QSETFIELD(o, oBowserKeyScale, q(0.2));
    else if (animTimer < 52)
        FSETFIELD(o, oBowserKeyScale, (animTimer - 42) * 0.042857f + 0.2); // TODO 3/70?
    else if (animTimer < 94)
        QSETFIELD(o, oBowserKeyScale, q(0.8));
    else if (animTimer < 101)
        FSETFIELD(o, oBowserKeyScale, (101 - animTimer) * 0.085714f + 0.2); // TODO 6/70?
    else
        QSETFIELD(o, oBowserKeyScale, q(0.2));
    if (o->oTimer > 138)
        obj_mark_for_deletion(o);
}
