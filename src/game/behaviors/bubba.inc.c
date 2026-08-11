// bubba.inc.c

static struct ObjectHitbox sBubbaHitbox = {
    /* interactType:      */ INTERACT_CLAM_OR_BUBBA,
    /* downOffset:        */ 0,
    /* damageOrCoinValue: */ 1,
    /* health:            */ 99,
    /* numLootCoins:      */ 0,
    /* radius:            */ 300,
    /* height:            */ 200,
    /* hurtboxRadius:     */ 300,
    /* hurtboxHeight:     */ 200,
};

void bubba_act_0(void) {
    f32 sp24;

    sp24 = cur_obj_lateral_dist_to_home();
    treat_far_home_as_marioq(q(2000.0f));
    o->oAnimState = 0;

    o->oBubbaUnk1AC = obj_get_pitch_to_home(sp24);

	q32 bubbaUnkF4q = QFIELD(o, oBubbaUnkF4);
    approach_q32_ptr(&bubbaUnkF4q, q(5.0), q(0.5));
	QSETFIELD(o, oBubbaUnkF4, bubbaUnkF4q);

    if (o->oBubbaUnkFC != 0) {
        if (abs_angle_diff(o->oMoveAngleYaw, o->oBubbaUnk1AE) < 800) {
            o->oBubbaUnkFC = 0;
        }
    } else {
        if (QFIELD(o, oDistanceToMario) >= q(25000)) {
            o->oBubbaUnk1AE = o->oAngleToMario;
            o->oBubbaUnkF8 = random_linear_offset(20, 30);
        }

        if ((o->oBubbaUnkFC = o->oMoveFlags & OBJ_MOVE_HIT_WALL) != 0) {
            o->oBubbaUnk1AE = cur_obj_reflect_move_angle_off_wall();
        } else if (o->oTimer > 30 && QFIELD(o, oDistanceToMario) < q(2000)) {
            o->oAction = 1;
        } else if (o->oBubbaUnkF8 != 0) {
            o->oBubbaUnkF8 -= 1;
        } else {
            o->oBubbaUnk1AE = obj_random_fixed_turn(0x2000);
            o->oBubbaUnkF8 = random_linear_offset(100, 100);
        }
    }
}

void bubba_act_1(void) {
    treat_far_home_as_marioq(q(2500.0f));
    if (QFIELD(o, oDistanceToMario) > q(2500.0)) {
        o->oAction = 0;
    } else if (o->oBubbaUnk100 != 0) {
        if (--o->oBubbaUnk100 == 0) {
            cur_obj_play_sound_2(SOUND_OBJ_BUBBA_CHOMP);
            o->oAction = 0;
        } else if (o->oBubbaUnk100 < 15) {
            o->oAnimState = 1;
        } else if (o->oBubbaUnk100 == 20) {
            s16 val06 = 10000 - qtrunc(20 * (find_water_levelq(QFIELD(o, oPosX), QFIELD(o, oPosZ)) - QFIELD(o, oPosY)));
            o->oBubbaUnk1AC -= val06;
            o->oMoveAnglePitch = o->oBubbaUnk1AC;
            QSETFIELD(o, oBubbaUnkF4, q(40));
            obj_compute_vel_from_move_pitchq(QFIELD(o, oBubbaUnkF4));
            o->oAnimState = 0;
        } else {
            o->oBubbaUnk1AE = o->oAngleToMario;
            o->oBubbaUnk1AC = o->oBubbaUnk104;

            cur_obj_rotate_yaw_toward(o->oBubbaUnk1AE, 400);
            obj_move_pitch_approach(o->oBubbaUnk1AC, 400);
        }
    } else {
        if (abs_angle_diff(gMarioObject->oFaceAngleYaw, o->oAngleToMario) < 0x3000) {
            s16 val04 = 0x4000 - atan2sq(q(800), QFIELD(o, oDistanceToMario) - q(800));
            if ((s16)(o->oMoveAngleYaw - o->oAngleToMario) < 0) {
                val04 = -val04;
            }

            o->oBubbaUnk1AE = o->oAngleToMario + val04;
        } else {
            o->oBubbaUnk1AE = o->oAngleToMario;
        }

        o->oBubbaUnk1AC = o->oBubbaUnk104;

        if (obj_is_near_to_and_facing_mario(500.0f, 3000) && abs_angle_diff(o->oBubbaUnk1AC, o->oMoveAnglePitch) < 3000) {
            o->oBubbaUnk100 = 30;
            QSETFIELD(o, oBubbaUnkF4, 0);
            o->oAnimState = 1;
        } else {
			q32 bubbaUnkF4q = QFIELD(o, oBubbaUnkF4);
            approach_q32_ptr(&bubbaUnkF4q, q(20), q(0.5));
			QSETFIELD(o, oBubbaUnkF4, bubbaUnkF4q);
        }
    }
}

void bhv_bubba_loop(void) {
    UNUSED s32 unused;

    o->oInteractionSubtype &= ~INT_SUBTYPE_EATS_MARIO;
    o->oBubbaUnk104 = obj_turn_pitch_toward_mario(120.0f, 0);

    if (abs_angle_diff(o->oAngleToMario, o->oMoveAngleYaw) < 0x1000
        && abs_angle_diff(o->oBubbaUnk104 + 0x800, o->oMoveAnglePitch) < 0x2000) {
        if (o->oAnimState != 0 && QFIELD(o, oDistanceToMario) < q(250.0)) {
            o->oInteractionSubtype |= INT_SUBTYPE_EATS_MARIO;
        }

        o->hurtboxRadius_s16 = 100;
    } else {
        o->hurtboxRadius_s16 = 150;
    }

    cur_obj_update_floor_and_walls();

    switch (o->oAction) {
        case 0:
            bubba_act_0();
            break;
        case 1:
            bubba_act_1();
            break;
    }

    if (o->oMoveFlags & OBJ_MOVE_MASK_IN_WATER) {
        if (o->oMoveFlags & OBJ_MOVE_ENTERED_WATER) {
            struct Object *sp38 = spawn_object(o, MODEL_WATER_SPLASH, bhvWaterSplash);
            if (sp38 != NULL) {
                obj_scale(sp38, 3.0f);
            }

            QSETFIELD(o,  oBubbaUnk108, QFIELD(o,  oVelY));
            QSETFIELD(o,  oBubbaUnk10C, q(0));
        } else {
			q32 bubbaUnk108q = QFIELD(o, oBubbaUnk108);
            approach_q32_ptr(&bubbaUnk108q, 0, q(4));
			QSETFIELD(o, oBubbaUnk108, bubbaUnk108q);
            if (QMODFIELD(o, oBubbaUnk10C, -= bubbaUnk108q) > QONE) {
                s16 sp36 = random_u16();
                QMODFIELD(o, oBubbaUnk10C, -= QONE);
                spawn_object_relative(0, 150.0f * coss(sp36), 0x64, 150.0f * sins(sp36), o,
                                      MODEL_WHITE_PARTICLE_SMALL, bhvSmallParticleSnow);
            }
        }

        obj_smooth_turn(&o->oBubbaUnk1B0, &o->oMoveAnglePitch, o->oBubbaUnk1AC, 0.05f, 10, 50, 2000);
        obj_smooth_turn(&o->oBubbaUnk1B2, &o->oMoveAngleYaw, o->oBubbaUnk1AE, 0.05f, 10, 50, 2000);
        obj_compute_vel_from_move_pitchq(QFIELD(o, oBubbaUnkF4));
    } else {
        FSETFIELD(o, oBubbaUnkF4, sqrtf(FFIELD(o, oForwardVel) * FFIELD(o, oForwardVel) + FFIELD(o, oVelY) * FFIELD(o, oVelY)));
        o->oMoveAnglePitch = obj_get_pitch_from_vel();
        obj_face_pitch_approach(o->oMoveAnglePitch, 400);
        o->oBubbaUnk1B0 = 0;
    }

    obj_face_pitch_approach(o->oMoveAnglePitch, 400);
    obj_check_attacks(&sBubbaHitbox, o->oAction);

    cur_obj_move_standard(78);

    QMODFIELD(o, oFloorHeight, += q(150));
    if (QFIELD(o, oPosY) < QFIELD(o, oFloorHeight)) {
        QSETFIELD(o, oPosY, QFIELD(o, oFloorHeight));
    }
}
