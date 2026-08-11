// end_birds_2.inc.c

void bhv_end_birds_2_loop(void) {
    Vec3q sp3Cq;
    q32 sp34q;
    s16 sp32, sp30;

    QSETFIELD(gCurrentObject, oForwardVel, (random_q32() * 10) + q(25));

    switch (gCurrentObject->oAction) {
        case 0:
            cur_obj_scaleq(q(0.7f));
            gCurrentObject->oAction += 1;
            break;
        case 1:
            vec3q_get_dist_and_angle(gCamera->posq, gCamera->focusq, &sp34q, &sp32,
                                     &sp30);
            sp30 += 0x1000;
            //sp32 += 0; // nice work, Nintendo
            vec3q_set_dist_and_angle(gCamera->posq, sp3Cq, q(14000), sp32, sp30);
            obj_rotate_towards_pointq(gCurrentObject, sp3Cq, 0, 0, 8, 8);

            if (QFIELD(gCurrentObject, oEndBirdUnk104) == 0 && (gCurrentObject->oTimer == 0))
                cur_obj_play_sound_2(SOUND_GENERAL_BIRDS_FLY_AWAY);
            break;
    }

    cur_obj_set_pos_via_transform();
}
