// celebration_star.c.inc

void bhv_celebration_star_init(void) {
    ISETFIELD(o, oHomeX, gMarioObject->header.gfx.posi[0]);
    ISETFIELD(o, oPosY, gMarioObject->header.gfx.posi[1] + 30);
    ISETFIELD(o, oHomeZ, gMarioObject->header.gfx.posi[2]);
    o->oMoveAngleYaw = gMarioObject->header.gfx.angle[1] + 0x8000;
    o->oCelebStarDiameterOfRotation = 100;
#if BUGFIX_STAR_BOWSER_KEY
    if (gCurrLevelNum == LEVEL_BOWSER_1 || gCurrLevelNum == LEVEL_BOWSER_2) {
        o->header.gfx.sharedChild = gLoadedGraphNodes[MODEL_BOWSER_KEY];
        o->oFaceAnglePitch = 0;
        o->oFaceAngleRoll = 49152;
        cur_obj_scaleq(q(0.1f));
        o->oCelebStarUnkF4 = 1;
    } else {
        o->header.gfx.sharedChild = gLoadedGraphNodes[MODEL_STAR];
        o->oFaceAnglePitch = 0;
        o->oFaceAngleRoll = 0;
        cur_obj_scaleq(q(0.4f));
        o->oCelebStarUnkF4 = 0;
    }
#else
    o->header.gfx.sharedChild = gLoadedGraphNodes[MODEL_STAR];
    cur_obj_scaleq(q(0.4f));
    o->oFaceAnglePitch = 0;
    o->oFaceAngleRoll = 0;
#endif
}

void celeb_star_act_spin_around_mario(void) {
    FSETFIELD(o, oPosX, FFIELD(o, oHomeX) + sins(o->oMoveAngleYaw) * (f32)(o->oCelebStarDiameterOfRotation / 2));
    FSETFIELD(o, oPosZ, FFIELD(o, oHomeZ) + coss(o->oMoveAngleYaw) * (f32)(o->oCelebStarDiameterOfRotation / 2));
    QMODFIELD(o, oPosY, += q(5.0f));
    o->oFaceAngleYaw += 0x1000;
    o->oMoveAngleYaw += 0x2000;

    if (o->oTimer == 40)
        o->oAction = CELEB_STAR_ACT_FACE_CAMERA;
    if (o->oTimer < 35) {
        spawn_object(o, MODEL_SPARKLES, bhvCelebrationStarSparkle);
        o->oCelebStarDiameterOfRotation++;
    } else
        o->oCelebStarDiameterOfRotation -= 20;
}

void celeb_star_act_face_camera(void) {

    if (o->oTimer < 10) {
#if BUGFIX_STAR_BOWSER_KEY
        if (o->oCelebStarUnkF4 == 0) {
            cur_obj_scaleq(q(o->oTimer) / 10);
        } else {
            cur_obj_scaleq(q(o->oTimer) / 30);
        }
#else
        cur_obj_scaleq(q(o->oTimer) / 10);
#endif
        o->oFaceAngleYaw += 0x1000;
    } else {
        o->oFaceAngleYaw = gMarioObject->header.gfx.angle[1];
    }

    if (o->oTimer == 59)
        o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
}

void bhv_celebration_star_loop(void) {
    switch (o->oAction) {
        case CELEB_STAR_ACT_SPIN_AROUND_MARIO:
            celeb_star_act_spin_around_mario();
            break;

        case CELEB_STAR_ACT_FACE_CAMERA:
            celeb_star_act_face_camera();
            break;
    }
}

void bhv_celebration_star_sparkle_loop(void) {
    QMODFIELD(o, oPosY, -= q(15.0f));

    if (o->oTimer == 12)
        o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
}

void bhv_star_key_collection_puff_spawner_loop(void) {
    spawn_mist_particles_variable(0, 10, 30.0f);
    o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
}
