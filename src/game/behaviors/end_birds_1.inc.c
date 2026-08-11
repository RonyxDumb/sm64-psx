// end_birds_1.inc.c

void bhv_end_birds_1_loop(void) {
    Vec3q sp34q;
    UNUSED f32 sp30 = random_float();

    switch (gCurrentObject->oAction) {
        case 0:
            cur_obj_scaleq(q(0.7f));
            QSETFIELD(gCurrentObject, oIntroLakituUnk110, q(-554));
            QSETFIELD(gCurrentObject, oIntroLakituUnk10C, q(3044));
            QSETFIELD(gCurrentObject, oIntroLakituUnk108, q(-1314));
            gCurrentObject->oAction += 1;
            break;
        case 1:
            vec3q_set(sp34q, QFIELD(gCurrentObject, oIntroLakituUnk110), QFIELD(gCurrentObject, oIntroLakituUnk10C),
                      QFIELD(gCurrentObject, oIntroLakituUnk108));

            if (gCurrentObject->oTimer < 100)
                obj_rotate_towards_pointq(gCurrentObject, sp34q, 0, 0, 0x20, 0x20);
            if ((QFIELD(gCurrentObject, oEndBirdUnk104) == 0) && (gCurrentObject->oTimer == 0))
                cur_obj_play_sound_2(SOUND_GENERAL_BIRDS_FLY_AWAY);
            if (gCutsceneTimer == 0)
                obj_mark_for_deletion(gCurrentObject);
            break;
    }

    cur_obj_set_pos_via_transform();
}
