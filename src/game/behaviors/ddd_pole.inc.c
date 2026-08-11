
void bhv_ddd_pole_init(void) {
    if (!(save_file_get_flags() & (SAVE_FLAG_HAVE_KEY_2 | SAVE_FLAG_UNLOCKED_UPSTAIRS_DOOR))) {
        obj_mark_for_deletion(o);
    } else {
        o->hitboxDownOffset_s16 = 100.0f;
        FSETFIELD(o, oDDDPoleMaxOffset, 100.0f * o->oBehParams2ndByte);
    }
}

void bhv_ddd_pole_update(void) {
    if (o->oTimer > 20) {
        QMODFIELD(o, oDDDPoleOffset, += QFIELD(o, oDDDPoleVel));

        if (CLAMP_F32_FIELD(o, oDDDPoleOffset, 0.0f, FFIELD(o, oDDDPoleMaxOffset))) {
            QSETFIELD(o,  oDDDPoleVel, QFIELD(-o,  oDDDPoleVel));
            o->oTimer = 0;
        }
    }

    obj_set_dist_from_homeq(QFIELD(o, oDDDPoleOffset));
}
