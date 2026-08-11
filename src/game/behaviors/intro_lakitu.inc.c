/**
 * @file intro_lakitu.inc.c
 * This file implements lakitu's behvaior during the intro cutscene.
 * It's also used during the ending cutscene.
 */


/**
 * Add the camera's position to `offset`, rotate the point to be relative to the camera's focus, then
 * set lakitu's location.
 */
void intro_lakitu_set_offset_from_cameraq(struct Object *o, Vec3q offsetq) {
    q32 distq;
    Vec3s focusAngles;
    s16 offsetPitch, offsetYaw;

    vec3q_add(offsetq, gCamera->posq);
    vec3q_get_dist_and_angle(gCamera->posq, gCamera->focusq, &distq, &focusAngles[0], &focusAngles[1]);
    vec3q_get_dist_and_angle(gCamera->posq, offsetq, &distq, &offsetPitch, &offsetYaw);
    vec3q_set_dist_and_angle(gCamera->posq, offsetq, distq, focusAngles[0] + offsetPitch, focusAngles[1] + offsetYaw);
    vec3q_to_object_pos(o, offsetq);
}

void intro_lakitu_set_focusq(struct Object *o, Vec3q newFocusq) {
    Vec3q originq;
    q32 distq;
    s16 pitch, yaw;

    // newFocus is an offset from lakitu's origin, not a point in the world.
    vec3q_set(originq, 0, 0, 0);
    vec3q_get_dist_and_angle(originq, newFocusq, &distq, &pitch, &yaw);
    o->oFaceAnglePitch = pitch;
    o->oFaceAngleYaw = yaw;
}

/**
 * Move lakitu along the spline `offset`, relative to the camera, and face him towards the corresponding
 * location along the spline `focus`.
 */
s32 intro_lakitu_set_pos_and_focus(struct Object *o, struct CutsceneSplinePoint offset[],
                                   struct CutsceneSplinePoint focus[]) {
    Vec3q newOffsetq, newFocusq;
    s32 splineFinished = 0;
    s16 splineSegment = qtrunc(QFIELD(o, oIntroLakituSplineSegment));

	q32 oIntroLakituSplineSegmentProgressq = QFIELD(o, oIntroLakituSplineSegmentProgress);
    if ((move_point_along_splineq(newFocusq, offset, &splineSegment, &oIntroLakituSplineSegmentProgressq) == 1)
        || (move_point_along_splineq(newOffsetq, focus, &splineSegment, &oIntroLakituSplineSegmentProgressq) == 1))
		splineFinished += 1;
	QSETFIELD(o, oIntroLakituSplineSegmentProgress, oIntroLakituSplineSegmentProgressq);

    QSETFIELD(o, oIntroLakituSplineSegment, q(splineSegment));
    intro_lakitu_set_offset_from_cameraq(o, newOffsetq);
    intro_lakitu_set_focusq(o, newFocusq);
    return splineFinished;
}

void bhv_intro_lakitu_loop(void) {
    Vec3q sp64q, sp58q, sp4Cq;

    switch (gCurrentObject->oAction) {
        case 0:
            cur_obj_disable_rendering();
            QSETFIELD(gCurrentObject, oIntroLakituSplineSegment, 0);
            QSETFIELD(gCurrentObject, oIntroLakituSplineSegmentProgress, 0);
            gCurrentObject->oIntroLakituCloud =
                spawn_object_relative_with_scale(1, 0, 0, 0, 2.f, gCurrentObject, MODEL_MIST, bhvCloud);
            if (gCamera->cutscene == CUTSCENE_END_WAVING)
                gCurrentObject->oAction = 100;
            else
                gCurrentObject->oAction += 1;
            break;

        case 1:
            cur_obj_enable_rendering();
            if ((gCutsceneTimer > 350) && (gCutsceneTimer < 458)) {
                QSETFIELD(gCurrentObject, oPosX, gCamera->posq[0]);
                QSETFIELD(gCurrentObject, oPosY, gCamera->posq[1] + q(500));
                QSETFIELD(gCurrentObject, oPosZ, gCamera->posq[2]);
            }
            if (gCutsceneTimer > 52)
                cur_obj_play_sound_1(SOUND_AIR_LAKITU_FLY_HIGHPRIO);

            if (intro_lakitu_set_pos_and_focus(gCurrentObject, gIntroLakituStartToPipeOffsetFromCamera,
                                               gIntroLakituStartToPipeFocus) == 1)
                gCurrentObject->oAction += 1;

            switch (gCurrentObject->oTimer) {
#if defined(VERSION_US) || defined(VERSION_SH)
                case 534:
                    cur_obj_play_sound_2(SOUND_ACTION_FLYING_FAST);
                    break;
                case 581:
                    cur_obj_play_sound_2(SOUND_ACTION_INTRO_UNK45E);
                    break;
#endif
                case 73:
                    gCurrentObject->oAnimState += 1;
                    break;
                case 74:
                    gCurrentObject->oAnimState -= 1;
                    break;
                case 82:
                    gCurrentObject->oAnimState += 1;
                    break;
                case 84:
                    gCurrentObject->oAnimState -= 1;
                    break;
            }
#ifdef VERSION_EU
            if (gCurrentObject->oTimer == 446)
                cur_obj_play_sound_2(SOUND_ACTION_FLYING_FAST);
            if (gCurrentObject->oTimer == 485)
                cur_obj_play_sound_2(SOUND_ACTION_INTRO_UNK45E);
#endif
            break;
        case 2:
#ifdef VERSION_EU
            if (gCutsceneTimer > 599) {
#else
            if (gCutsceneTimer > 720) {
#endif
                gCurrentObject->oAction += 1;
                QSETFIELD(gCurrentObject, oIntroLakituUnk100, q(1400));
                QSETFIELD(gCurrentObject, oIntroLakituUnk104, q(-4096));
                QSETFIELD(gCurrentObject, oIntroLakituUnk108, q(2048));
                QSETFIELD(gCurrentObject, oIntroLakituUnk10C, q(-200));
                gCurrentObject->oMoveAngleYaw = 0x8000;
                gCurrentObject->oFaceAngleYaw = gCurrentObject->oMoveAngleYaw + 0x4000;
                gCurrentObject->oMoveAnglePitch = 0x800;
            }
            cur_obj_play_sound_1(SOUND_AIR_LAKITU_FLY_HIGHPRIO);
            break;

        case 3:
            cur_obj_play_sound_1(SOUND_AIR_LAKITU_FLY_HIGHPRIO);
            vec3q_set(sp58q, q(-1128), q(560), q(4664));
            gCurrentObject->oMoveAngleYaw += 0x200;
            QSETFIELD(gCurrentObject, oIntroLakituUnk100,
                approach_q32_asymptotic(QFIELD(gCurrentObject, oIntroLakituUnk100), q(100), q(0.03)));
            gCurrentObject->oFaceAnglePitch = atan2sq(q(200), QFIELD(gCurrentObject, oPosY) - q(400));
            gCurrentObject->oFaceAngleYaw = approach_s16_asymptotic(
                gCurrentObject->oFaceAngleYaw, gCurrentObject->oMoveAngleYaw + 0x8000, 4);
            vec3q_set_dist_and_angle(sp58q, sp4Cq, QFIELD(gCurrentObject, oIntroLakituUnk100), 0,
                                     gCurrentObject->oMoveAngleYaw);
            sp4Cq[1] += qmul(q(150), cosqs((s16) qtrunc(QFIELD(gCurrentObject, oIntroLakituUnk104))));
            QMODFIELD(gCurrentObject, oIntroLakituUnk104, += QFIELD(gCurrentObject, oIntroLakituUnk108));
            QSETFIELD(gCurrentObject, oIntroLakituUnk108,
                approach_q32_asymptotic(QFIELD(gCurrentObject, oIntroLakituUnk108), q(512), q(0.05)));
            sp4Cq[0] += QFIELD(gCurrentObject, oIntroLakituUnk10C);
            QSETFIELD(gCurrentObject, oIntroLakituUnk10C,
                approach_q32_asymptotic(QFIELD(gCurrentObject, oIntroLakituUnk10C), 0, q(0.05)));
            vec3q_to_object_pos(gCurrentObject, sp4Cq);

            if (gCurrentObject->oTimer == 31) {
                QMODFIELD(gCurrentObject, oPosY, -= q(158));
                // Spawn white ground particles
                spawn_mist_from_global();
                QMODFIELD(gCurrentObject, oPosY, += q(158));
            }
#ifdef VERSION_EU
#define TIMER 74
#else
#define TIMER 98
#endif

            if (gCurrentObject->oTimer == TIMER) {
                obj_mark_for_deletion(gCurrentObject);
                obj_mark_for_deletion(gCurrentObject->oIntroLakituCloud);
            }
#ifndef VERSION_JP
            if (gCurrentObject->oTimer == 14)
                cur_obj_play_sound_2(SOUND_ACTION_INTRO_UNK45F);
#endif
            break;
        case 100:
            cur_obj_enable_rendering();
            vec3q_set(sp64q, q(-100), q(100), q(300));
            offset_rotatedq(sp4Cq, gCamera->posq, sp64q, sMarioCamState->faceAngle);
            vec3q_to_object_pos(gCurrentObject, sp4Cq);
            gCurrentObject->oMoveAnglePitch = 0x1000;
            gCurrentObject->oMoveAngleYaw = 0x9000;
            gCurrentObject->oFaceAnglePitch = gCurrentObject->oMoveAnglePitch / 2;
            gCurrentObject->oFaceAngleYaw = gCurrentObject->oMoveAngleYaw;
            gCurrentObject->oAction += 1;
            break;

        case 101:
            object_pos_to_vec3q(sp4Cq, gCurrentObject);
            if (gCurrentObject->oTimer > 60) {
                QSETFIELD(gCurrentObject, oForwardVel,
                    approach_q32_asymptotic(QFIELD(gCurrentObject, oForwardVel), q(-10), q(0.05)));
                gCurrentObject->oMoveAngleYaw += 0x78;
                gCurrentObject->oMoveAnglePitch += 0x40;
                gCurrentObject->oFaceAngleYaw = camera_approach_s16_symmetric(
                    gCurrentObject->oFaceAngleYaw, (s16) calculate_yawq(sp4Cq, gCamera->posq),
                    0x200);
            }
            if (gCurrentObject->oTimer > 105) {
                gCurrentObject->oAction += 1;
                gCurrentObject->oMoveAnglePitch = 0xE00;
            }
            gCurrentObject->oFaceAnglePitch = 0;
            cur_obj_set_pos_via_transform();
            break;

        case 102:
            object_pos_to_vec3q(sp4Cq, gCurrentObject);
            QSETFIELD(gCurrentObject, oForwardVel,
                approach_q32_asymptotic(QFIELD(gCurrentObject, oForwardVel), q(60), q(0.05)));
            gCurrentObject->oFaceAngleYaw = camera_approach_s16_symmetric(
                gCurrentObject->oFaceAngleYaw, (s16) calculate_yawq(sp4Cq, gCamera->posq), 0x200);
            if (gCurrentObject->oTimer < 62)
                gCurrentObject->oMoveAngleYaw =
                    approach_s16_asymptotic(gCurrentObject->oMoveAngleYaw, 0x1800, 0x1E);
            gCurrentObject->oMoveAnglePitch =
                camera_approach_s16_symmetric(gCurrentObject->oMoveAnglePitch, -0x2000, 0x5A);
            gCurrentObject->oFaceAnglePitch = 0;
            cur_obj_set_pos_via_transform();
            break;
    }
}
#undef TIMER
