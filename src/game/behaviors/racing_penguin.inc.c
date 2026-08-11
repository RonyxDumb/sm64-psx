struct RacingPenguinData {
    s16 text;
    f32 radius;
    f32 height;
};

static struct RacingPenguinData sRacingPenguinData[] = {
    { DIALOG_055, 200.0f, 200.0f },
    { DIALOG_164, 350.0f, 250.0f },
};

void bhv_racing_penguin_init(void) {
    if (gMarioState->numStars == 120) {
        cur_obj_scaleq(q(8.0f));
        o->header.gfx.scaleq[1] = q(5);
        o->oBehParams2ndByte = 1;
    }
}

static void racing_penguin_act_wait_for_mario(void) {
    if (o->oTimer > o->oRacingPenguinInitTextCooldown && QFIELD(o, oPosY) - QFIELD(gMarioObject, oPosY) <= 0
        && cur_obj_can_mario_activate_textbox_2(400.0f, 400.0f)) {
        o->oAction = RACING_PENGUIN_ACT_SHOW_INIT_TEXT;
    }
}

static void racing_penguin_act_show_init_text(void) {
    s32 response = obj_update_race_proposition_dialog(sRacingPenguinData[o->oBehParams2ndByte].text);

    if (response == DIALOG_RESPONSE_YES) {
        struct Object *child;

        child = cur_obj_nearest_object_with_behavior(bhvPenguinRaceFinishLine);
        child->parentObj = o;

        child = cur_obj_nearest_object_with_behavior(bhvPenguinRaceShortcutCheck);
        child->parentObj = o;

        o->oPathedStartWaypoint = o->oPathedPrevWaypoint =
            segmented_to_virtual(ccm_seg7_trajectory_penguin_race);
        o->oPathedPrevWaypointFlags = 0;

        o->oAction = RACING_PENGUIN_ACT_PREPARE_FOR_RACE;
        QSETFIELD(o,  oVelY, q(60));
    } else if (response == DIALOG_RESPONSE_NO) {
        o->oAction = RACING_PENGUIN_ACT_WAIT_FOR_MARIO;
        o->oRacingPenguinInitTextCooldown = 60;
    }
}

static void racing_penguin_act_prepare_for_race(void) {
    if (obj_begin_race(TRUE)) {
        o->oAction = RACING_PENGUIN_ACT_RACE;
        QSETFIELD(o,  oForwardVel, q(20));
    }

    cur_obj_rotate_yaw_toward(0x4000, 2500);
}

static void racing_penguin_act_race(void) {
    q32 targetSpeedq;
    q32 minSpeedq;

    if (cur_obj_follow_path(0) == PATH_REACHED_END) {
        o->oRacingPenguinReachedBottom = TRUE;
        o->oAction = RACING_PENGUIN_ACT_FINISH_RACE;
    } else {
        targetSpeedq = QFIELD(o, oPosY) - QFIELD(gMarioObject, oPosY);
        minSpeedq = q(70.0f);

        cur_obj_play_sound_1(SOUND_AIR_ROUGH_SLIDE);

        if (targetSpeedq < q(100) || (o->oPathedPrevWaypointFlags & WAYPOINT_MASK_00FF) >= 35) {
            if ((o->oPathedPrevWaypointFlags & WAYPOINT_MASK_00FF) >= 35) {
                minSpeedq = q(60);
            }

            APPROACH_Q32_FIELD(o, oRacingPenguinWeightedNewTargetSpeed, q(-500.0f), q(100.0f));
        } else {
            APPROACH_Q32_FIELD(o, oRacingPenguinWeightedNewTargetSpeed, q(1000.0f), q(30.0f));
        }

        targetSpeedq = (QFIELD(o, oRacingPenguinWeightedNewTargetSpeed) + targetSpeedq) / 10;
        clamp_q32(&targetSpeedq, minSpeedq, q(150.0f));
        obj_forward_vel_approachq(targetSpeedq, q(0.4f));

        cur_obj_init_animation_with_sound(1);
        cur_obj_rotate_yaw_toward(o->oPathedTargetYaw, qtrunc(15 * QFIELD(o, oForwardVel)));

        if (cur_obj_check_if_at_animation_end() && (o->oMoveFlags & OBJ_MOVE_MASK_ON_GROUND)) {
            spawn_object_relative_with_scale(0, 0, -100, 0, 4.0f, o, MODEL_SMOKE, bhvWhitePuffSmoke2);
        }
    }

    if (mario_is_in_air_action()) {
        if (o->oTimer > 60) {
            o->oRacingPenguinMarioCheated = TRUE;
        }
    } else {
        o->oTimer = 0;
    }
}

static void racing_penguin_act_finish_race(void) {
    if (QFIELD(o, oForwardVel) != 0) {
        if (o->oTimer > 5 && (o->oMoveFlags & OBJ_MOVE_HIT_WALL)) {
            cur_obj_play_sound_2(SOUND_OBJ_POUNDING_LOUD);
            set_camera_shake_from_pointq(SHAKE_POS_SMALL, QFIELD(o, oPosX), QFIELD(o, oPosY), QFIELD(o, oPosZ));
            QSETFIELD(o,  oForwardVel, q(0));
        }
    } else if (cur_obj_init_anim_and_check_if_end(2) != 0) {
        o->oAction = RACING_PENGUIN_ACT_SHOW_FINAL_TEXT;
    }
}

static void racing_penguin_act_show_final_text(void) {
    s32 textResult;

    if (o->oRacingPenguinFinalTextbox == 0) {
        if (cur_obj_rotate_yaw_toward(0, 200)) {
            cur_obj_init_animation_with_sound(3);
            QSETFIELD(o,  oForwardVel, q(0));

            if (cur_obj_can_mario_activate_textbox_2(400.0f, 400.0f)) {
                if (o->oRacingPenguinMarioWon) {
                    if (o->oRacingPenguinMarioCheated) {
                        o->oRacingPenguinFinalTextbox = DIALOG_132;
                        o->oRacingPenguinMarioWon = FALSE;
                    } else {
                        o->oRacingPenguinFinalTextbox = DIALOG_056;
                    }
                } else {
                    o->oRacingPenguinFinalTextbox = DIALOG_037;
                }
            }
        } else {
            cur_obj_init_animation_with_sound(0);

#ifndef VERSION_JP
            play_penguin_walking_sound(1);
#endif

            QSETFIELD(o,  oForwardVel, q(4));
        }
    } else if (o->oRacingPenguinFinalTextbox > 0) {
        if ((textResult = cur_obj_update_dialog_with_cutscene(MARIO_DIALOG_LOOK_UP,
            DIALOG_FLAG_TURN_TO_MARIO, CUTSCENE_DIALOG, o->oRacingPenguinFinalTextbox))) {
            o->oRacingPenguinFinalTextbox = -1;
            o->oTimer = 0;
        }
    } else if (o->oRacingPenguinMarioWon) {
#ifdef VERSION_JP
        spawn_default_star(-7339.0f, -5700.0f, -6774.0f);
#else
        cur_obj_spawn_star_at_y_offset(-7339.0f, -5700.0f, -6774.0f, 200.0f);
#endif
        o->oRacingPenguinMarioWon = FALSE;
    }
}

void bhv_racing_penguin_update(void) {
    cur_obj_update_floor_and_walls();

    switch (o->oAction) {
        case RACING_PENGUIN_ACT_WAIT_FOR_MARIO:
            racing_penguin_act_wait_for_mario();
            break;
        case RACING_PENGUIN_ACT_SHOW_INIT_TEXT:
            racing_penguin_act_show_init_text();
            break;
        case RACING_PENGUIN_ACT_PREPARE_FOR_RACE:
            racing_penguin_act_prepare_for_race();
            break;
        case RACING_PENGUIN_ACT_RACE:
            racing_penguin_act_race();
            break;
        case RACING_PENGUIN_ACT_FINISH_RACE:
            racing_penguin_act_finish_race();
            break;
        case RACING_PENGUIN_ACT_SHOW_FINAL_TEXT:
            racing_penguin_act_show_final_text();
            break;
    }

    cur_obj_move_standard(78);
    cur_obj_align_gfx_with_floor();
    cur_obj_push_mario_away_from_cylinder(sRacingPenguinData[o->oBehParams2ndByte].radius,
                                      sRacingPenguinData[o->oBehParams2ndByte].height);
}

void bhv_penguin_race_finish_line_update(void) {
    if (o->parentObj->oRacingPenguinReachedBottom
        || (QFIELD(o, oDistanceToMario) < q(1000.0) && QFIELD(gMarioObject, oPosZ) - QFIELD(o, oPosZ) < 0)) {
        if (!o->parentObj->oRacingPenguinReachedBottom) {
            o->parentObj->oRacingPenguinMarioWon = TRUE;
        }
    }
}

void bhv_penguin_race_shortcut_check_update(void) {
    if (QFIELD(o, oDistanceToMario) < q(500.0)) {
        o->parentObj->oRacingPenguinMarioCheated = TRUE;
    }
}
