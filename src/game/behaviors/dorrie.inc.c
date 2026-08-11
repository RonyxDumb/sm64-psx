
void dorrie_raise_head(void) {
    s16 startAngle;
    q32 xzDispq;
    q32 yDispq;

    startAngle = o->oDorrieNeckAngle;

    o->oDorrieNeckAngle -= (s16) ABS(370 * sinqs(o->oDorrieHeadRaiseSpeed));

    xzDispq = 440 * (cosqs(o->oDorrieNeckAngle) - cosqs(startAngle));
    yDispq = 440 * (sinqs(o->oDorrieNeckAngle) - sinqs(startAngle));

    set_mario_posi(IFIELD(gMarioObject, oPosX) + qtrunc(qmul(xzDispq, sinqs(o->oMoveAngleYaw))), IFIELD(gMarioObject, oPosY) - qtrunc(yDispq),
                  IFIELD(gMarioObject, oPosZ) + qtrunc(qmul(xzDispq, cosqs(o->oMoveAngleYaw))));
}

void dorrie_act_move(void) {
    s16 startYaw;
    s16 targetYaw;
    s16 targetSpeed;
    s16 circularTurn;

    startYaw = o->oMoveAngleYaw;
    o->oDorrieNeckAngle = -0x26F4;
    cur_obj_init_animation_with_sound(1);

    if (FFIELD(o, oDorrieForwardDistToMario) < 320.0f && o->oDorrieGroundPounded) {
        cur_obj_play_sound_2(SOUND_OBJ_DORRIE);
        o->collisionData = segmented_to_virtual(dorrie_seg6_collision_0600FBB8);
        o->oAction = DORRIE_ACT_LOWER_HEAD;
        QSETFIELD(o,  oForwardVel, q(0));
        o->oDorrieYawVel = 0;
    } else {
        if (gMarioObject->platform == o) {
            targetYaw = gMarioObject->oFaceAngleYaw;
            targetSpeed = 10;
        } else {
            circularTurn = 0x4000 - atan2s(2000.0f, FFIELD(o, oDorrieDistToHome) - 2000.0f);
            if ((s16)(o->oMoveAngleYaw - o->oDorrieAngleToHome) < 0) {
                circularTurn = -circularTurn;
            }

            targetYaw = o->oDorrieAngleToHome + circularTurn;
            targetSpeed = 5;
        }

        obj_forward_vel_approachq(q(targetSpeed), q(0.5));
        o->oDorrieYawVel =
            approach_s16_symmetric(o->oDorrieYawVel, (s16)(targetYaw - o->oMoveAngleYaw) / 50, 5);
        o->oMoveAngleYaw += o->oDorrieYawVel;
    }

    o->oAngleVelYaw = o->oMoveAngleYaw - startYaw;
}

void dorrie_begin_head_raise(s32 liftingMario) {
    o->oDorrieLiftingMario = liftingMario;
    o->oAction = DORRIE_ACT_RAISE_HEAD;
    o->oDorrieHeadRaiseSpeed = 0;
}

void dorrie_act_lower_head(void) {
    if (cur_obj_init_anim_check_frame(2, 35)) {
        cur_obj_reverse_animation();

#ifdef VERSION_JP
        if (o->oTimer > 150) {
            dorrie_begin_head_raise(FALSE);
        } else if (gMarioObject->platform == o) {
            if (FFIELD(o, oDorrieForwardDistToMario) > 830.0f
                && set_mario_npc_dialog(MARIO_DIALOG_LOOK_UP) == MARIO_DIALOG_STATUS_START) {
                dorrie_begin_head_raise(TRUE);
            } else if (QFIELD(o, oDorrieForwardDistToMario) > q(320.0f)) {
                o->oTimer = 0;
            }
        }
#else
        if (gMarioObject->platform == o) {
            if (FFIELD(o, oDorrieOffsetY) == -17.0f && FFIELD(o, oDorrieForwardDistToMario) > 780.0f
                && set_mario_npc_dialog(MARIO_DIALOG_LOOK_UP) == MARIO_DIALOG_STATUS_START) {
                dorrie_begin_head_raise(TRUE);
            } else if (QFIELD(o, oDorrieForwardDistToMario) > q(320.0f)) {
                o->oTimer = 0;
            }
        } else if (o->oTimer > 150) {
            dorrie_begin_head_raise(FALSE);
        }
#endif

    } else {
        o->oDorrieNeckAngle += 0x115;
    }
}

void dorrie_act_raise_head(void) {
    o->collisionData = segmented_to_virtual(dorrie_seg6_collision_0600F644);
    if (cur_obj_check_if_near_animation_end()) {
        o->oAction = DORRIE_ACT_MOVE;
    } else if (o->oDorrieLiftingMario && o->header.gfx.animInfo.animFrame < 74) {
        if (set_mario_npc_dialog(MARIO_DIALOG_LOOK_UP) == MARIO_DIALOG_STATUS_SPEAK) {
            o->oDorrieHeadRaiseSpeed += 0x1CC;
            if (cur_obj_check_anim_frame(73)) {
                set_mario_npc_dialog(MARIO_DIALOG_STOP);
            }
            dorrie_raise_head();
        } else {
            cur_obj_reverse_animation();
        }
    }
}

void bhv_dorrie_update(void) {
    f32 boundsShift;
    UNUSED s32 unused1;
    UNUSED s32 unused2;
    f32 maxOffsetY;

    if (!(o->activeFlags & ACTIVE_FLAG_IN_DIFFERENT_ROOM)) {
        FSETFIELD(o, oDorrieForwardDistToMario, FFIELD(o, oDistanceToMario) * coss(o->oAngleToMario - o->oMoveAngleYaw));

        obj_perform_position_op(0);
        cur_obj_move_using_fvel_and_gravity();

        o->oDorrieAngleToHome = cur_obj_angle_to_home();
        FSETFIELD(o, oDorrieDistToHome, cur_obj_lateral_dist_to_home());

        // Shift dorrie's bounds to account for her neck
        boundsShift =
            440.0f * coss(o->oDorrieNeckAngle) * coss(o->oMoveAngleYaw - o->oDorrieAngleToHome);

        if (CLAMP_F32_FIELD(o, oDorrieDistToHome, 1650.0f + boundsShift, 2300.0f + boundsShift)) {
            FSETFIELD(o, oPosX, FFIELD(o, oHomeX) - FFIELD(o, oDorrieDistToHome) * sins(o->oDorrieAngleToHome));
            FSETFIELD(o, oPosZ, FFIELD(o, oHomeZ) - FFIELD(o, oDorrieDistToHome) * coss(o->oDorrieAngleToHome));
        }

        o->oDorrieGroundPounded = cur_obj_is_mario_ground_pounding_platform();

        if (gMarioObject->platform == o) {
            maxOffsetY = -17.0f;
            if (QFIELD(o, oDorrieOffsetY) >= 0) {
                if (o->oDorrieGroundPounded) {
                    QSETFIELD(o,  oDorrieVelY, q(-15));
                } else {
                    QSETFIELD(o,  oDorrieVelY, q(-6));
                }
            }
        } else {
            maxOffsetY = 0.0f;
        }

        QMODFIELD(o, oDorrieOffsetY, += QFIELD(o, oDorrieVelY));
        APPROACH_F32_FIELD(o, oDorrieVelY, 3.0f, 1.0f);
        if (FFIELD(o, oDorrieVelY) > 0.0f && FFIELD(o, oDorrieOffsetY) > maxOffsetY) {
            FSETFIELD(o, oDorrieOffsetY, maxOffsetY);
        }

        QSETFIELD(o, oPosY, QFIELD(o, oHomeY) + QFIELD(o, oDorrieOffsetY));

        switch (o->oAction) {
            case DORRIE_ACT_MOVE:
                dorrie_act_move();
                break;
            case DORRIE_ACT_LOWER_HEAD:
                dorrie_act_lower_head();
                break;
            case DORRIE_ACT_RAISE_HEAD:
                dorrie_act_raise_head();
                break;
        }

        obj_perform_position_op(1);
    }
}
