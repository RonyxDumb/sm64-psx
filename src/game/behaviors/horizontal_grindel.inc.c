
void bhv_horizontal_grindel_init(void) {
    o->oHorizontalGrindelTargetYaw = o->oMoveAngleYaw;
}

void bhv_horizontal_grindel_update(void) {
    if (o->oMoveFlags & OBJ_MOVE_MASK_ON_GROUND) {
        if (!o->oHorizontalGrindelOnGround) {
            cur_obj_play_sound_2(SOUND_OBJ_THWOMP);
            o->oHorizontalGrindelOnGround = TRUE;
            set_camera_shake_from_pointq(SHAKE_POS_SMALL, QFIELD(o, oPosX), QFIELD(o, oPosY), QFIELD(o, oPosZ));

            FSETFIELD(o, oHorizontalGrindelDistToHome, cur_obj_lateral_dist_to_home());
            QSETFIELD(o,  oForwardVel, q(0));
            o->oTimer = 0;
        }

        if (cur_obj_rotate_yaw_toward(o->oHorizontalGrindelTargetYaw, 0x400)) {
            if (o->oTimer > 60) {
                if (QFIELD(o, oHorizontalGrindelDistToHome) > q(300.0f)) {
                    o->oHorizontalGrindelTargetYaw += 0x8000;
                    QSETFIELD(o,  oHorizontalGrindelDistToHome, q(0));
                } else {
                    cur_obj_play_sound_2(SOUND_OBJ_KING_BOBOMB_JUMP);
                    QSETFIELD(o,  oForwardVel, q(11));
                    QSETFIELD(o,  oVelY, q(70));
                    QSETFIELD(o, oGravity, q(-4.0f));
                    o->oMoveFlags = 0;
                }
            }
        } else {
            o->oTimer = 0;
        }
    } else {
        o->oHorizontalGrindelOnGround = FALSE;
        if (QFIELD(o, oVelY) < 0) {
            QSETFIELD(o, oGravity, q(-16.0f));
        }
    }

    o->oFaceAngleYaw = o->oMoveAngleYaw + 0x4000;
    cur_obj_move_standard(78);
}
