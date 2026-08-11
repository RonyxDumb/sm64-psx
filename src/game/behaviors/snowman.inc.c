// snowman.c.inc

static struct ObjectHitbox sRollingSphereHitbox = {
    /* interactType:      */ INTERACT_DAMAGE,
    /* downOffset:        */ 0,
    /* damageOrCoinValue: */ 3,
    /* health:            */ 0,
    /* numLootCoins:      */ 0,
    /* radius:            */ 210,
    /* height:            */ 350,
    /* hurtboxRadius:     */ 0,
    /* hurtboxHeight:     */ 0,
};

void bhv_snowmans_bottom_init(void) {
    struct Object *sp34;

    QSETFIELD(o, oHomeX, QFIELD(o, oPosX));
    QSETFIELD(o, oHomeY, QFIELD(o, oPosY));
    QSETFIELD(o, oHomeZ, QFIELD(o, oPosZ));

    QSETFIELD(o, oGravity, q(10.0f));
    QSETFIELD(o, oFriction, q(0.999));
    QSETFIELD(o, oBuoyancy, q(2));

    QSETFIELD(o, oVelY, q(0));
    QSETFIELD(o, oForwardVel, q(0));
    QSETFIELD(o, oSnowmansBottomUnkF4, q(0.4));

    sp34 = cur_obj_nearest_object_with_behavior(bhvSnowmansHead);
    if (sp34 != NULL) {
        o->parentObj = sp34;
    }
    spawn_object_abs_with_rot(o, 0, MODEL_NONE, bhvSnowmansBodyCheckpoint, -402, 461, -2898, 0, 0, 0);
}

void set_rolling_sphere_hitbox(void) {
    obj_set_hitbox(o, &sRollingSphereHitbox);

    if ((o->oInteractStatus & INT_STATUS_INTERACTED) != 0) {
        o->oInteractStatus = 0;
    }
}

void adjust_rolling_face_pitchq(q32 f12q) {
    o->oFaceAnglePitch += (s16)(QFIELD(o, oForwardVel) * 100 / f12q);
    QMODFIELD(o, oSnowmansBottomUnkF4, += QFIELD(o, oForwardVel) / 10000);

    if (QFIELD(o, oSnowmansBottomUnkF4) > q(1))
        QSETFIELD(o, oSnowmansBottomUnkF4, q(1));
}

void snowmans_bottom_act_1(void) {
    s32 sp20;
#ifdef AVOID_UB
    sp20 = 0;
#endif

    o->oPathedStartWaypoint = segmented_to_virtual(&ccm_seg7_trajectory_snowman);
    object_step_without_floor_orient();
    sp20 = cur_obj_follow_path(sp20);
    o->oSnowmansBottomUnkF8 = o->oPathedTargetYaw;
    o->oMoveAngleYaw = approach_s16_symmetric(o->oMoveAngleYaw, o->oSnowmansBottomUnkF8, 0x400);

    if (QFIELD(o, oForwardVel) > q(70.0))
        QSETFIELD(o, oForwardVel, q(70));

    if (sp20 == -1) {
        if (obj_check_if_facing_toward_angle(o->oMoveAngleYaw, o->oAngleToMario, 0x2000) == TRUE
            && o->oSnowmansBottomUnk1AC == 1) {
            o->oSnowmansBottomUnkF8 = o->oAngleToMario;
        } else {
            o->oSnowmansBottomUnkF8 = o->oMoveAngleYaw;
        }
        o->oAction = 2;
    }
}

void snowmans_bottom_act_2(void) {
    UNUSED s16 sp26;

    sp26 = object_step_without_floor_orient();
    if (QFIELD(o, oForwardVel) > q(70.0))
        QSETFIELD(o,  oForwardVel, q(70));

    o->oMoveAngleYaw = approach_s16_symmetric(o->oMoveAngleYaw, o->oSnowmansBottomUnkF8, 0x400);
    if (is_point_close_to_object(o, -4230.0f, -1344.0f, 1813.0f, 300)) {
        spawn_mist_particles_variable(0, 0, 70.0f);
        o->oMoveAngleYaw = atan2sq(q(1813) - QFIELD(o, oPosZ), q(-4230) - QFIELD(o, oPosX));
        QSETFIELD(o, oVelY, q(80));
        QSETFIELD(o, oForwardVel, q(15));
        o->oAction = 3;

        o->parentObj->oAction = 2;
        QSETFIELD(o->parentObj, oVelY, q(100));
        cur_obj_play_sound_2(SOUND_OBJ_SNOWMAN_BOUNCE);
    }

    if (o->oTimer == 200) {
        create_respawner(MODEL_CCM_SNOWMAN_BASE, bhvSnowmansBottom, 3000);
        o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
    }
}

void snowmans_bottom_act_3(void) {
    UNUSED s16 sp1E;

    sp1E = object_step_without_floor_orient();
    if ((sp1E & 0x09) == 0x09) {
        o->oAction = 4;
        cur_obj_become_intangible();
    }

    if ((sp1E & 0x01) != 0) {
        spawn_mist_particles_variable(0, 0, 70.0f);
        QSETFIELD(o,  oPosX, q(-4230));
        QSETFIELD(o,  oPosZ, q(1813));
        QSETFIELD(o,  oForwardVel, q(0));
    }
}

void bhv_snowmans_bottom_loop(void) {
    s16 sp1E;

    switch (o->oAction) {
        case 0:
            if (is_point_within_radius_of_mario(IFIELD(o, oPosX), IFIELD(o, oPosY), IFIELD(o, oPosZ), 400) == 1
                && set_mario_npc_dialog(MARIO_DIALOG_LOOK_FRONT) == MARIO_DIALOG_STATUS_SPEAK) {
                sp1E = cutscene_object_with_dialog(CUTSCENE_DIALOG, o, DIALOG_110);
                if (sp1E) {
                    QSETFIELD(o,  oForwardVel, q(10));
                    o->oAction = 1;
                    set_mario_npc_dialog(MARIO_DIALOG_STOP);
                }
            }
            break;

        case 1:
            snowmans_bottom_act_1();
            adjust_rolling_face_pitchq(QFIELD(o, oSnowmansBottomUnkF4));
            cur_obj_play_sound_1(SOUND_ENV_UNKNOWN2);
            break;

        case 2:
            snowmans_bottom_act_2();
            adjust_rolling_face_pitchq(QFIELD(o, oSnowmansBottomUnkF4));
            cur_obj_play_sound_1(SOUND_ENV_UNKNOWN2);
            break;

        case 3:
            snowmans_bottom_act_3();
            break;

        case 4:
            cur_obj_push_mario_away_from_cylinder(210, 550);
            break;
    }

    set_rolling_sphere_hitbox();
    set_object_visibility(o, 8000);
    cur_obj_scaleq(QFIELD(o, oSnowmansBottomUnkF4));
    QSETFIELD(o, oGraphYOffset, QFIELD(o, oSnowmansBottomUnkF4) * 180);
}

void bhv_snowmans_head_init(void) {
    u8 sp37;
    s8 sp36;

    sp37 = save_file_get_star_flags(gCurrSaveFileNum - 1, gCurrCourseNum - 1);
    sp36 = (o->oBehParams >> 24) & 0xFF;

    cur_obj_scaleq(q(0.7f));

    QSETFIELD(o, oGravity, q(5.0f));
    QSETFIELD(o,  oFriction, q(0.999));
    QSETFIELD(o,  oBuoyancy, q(2));

    if ((sp37 & (1 << sp36)) && gCurrActNum != sp36 + 1) {
        spawn_object_abs_with_rot(o, 0, MODEL_CCM_SNOWMAN_BASE, bhvBigSnowmanWhole, -4230, -1344, 1813,
                                  0, 0, 0);
        QSETFIELD(o,  oPosX, q(-4230));
        QSETFIELD(o,  oPosY, q(-994));
        QSETFIELD(o,  oPosZ, q(1813));
        o->oAction = 1;
    }
}

void bhv_snowmans_head_loop(void) {
    UNUSED s16 sp1E;
    s16 sp1C;

    switch (o->oAction) {
        case 0:
            if (trigger_obj_dialog_when_facing(&o->oSnowmansHeadDialogActive, DIALOG_109, 400.0f, MARIO_DIALOG_LOOK_FRONT))
                o->oAction = 1;
            break;

        case 1:
            break;

        case 2:
            sp1C = object_step_without_floor_orient();
            if (sp1C & 0x08)
                o->oAction = 3;
            break;

        case 3:
            object_step_without_floor_orient();
            if (QFIELD(o, oPosY) < q(-994)) {
                QSETFIELD(o,  oPosY, q(-994));
                o->oAction = 4;
                cur_obj_play_sound_2(SOUND_OBJ_SNOWMAN_EXPLODE);
                play_puzzle_jingle();
            }
            break;

        case 4:
            if (trigger_obj_dialog_when_facing(&o->oSnowmansHeadDialogActive, DIALOG_111, 700.0f, MARIO_DIALOG_LOOK_UP)) {
                spawn_mist_particles();
                spawn_default_star(-4700.0f, -1024.0f, 1890.0f);
                o->oAction = 1;
            }
            break;
    }

    cur_obj_push_mario_away_from_cylinder(180, 150);
}

void bhv_snowmans_body_checkpoint_loop(void) {
    if (is_point_within_radius_of_mario(IFIELD(o, oPosX), IFIELD(o, oPosY), IFIELD(o, oPosZ), 800)) {
        o->parentObj->oSnowmansBottomUnk1AC++;
        o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
    }

    if (o->parentObj->activeFlags == ACTIVE_FLAG_DEACTIVATED)
        o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
}
