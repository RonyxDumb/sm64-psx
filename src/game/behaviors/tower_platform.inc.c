// tower_platform.c.inc

void bhv_wf_solid_tower_platform_loop(void) {
    if (o->parentObj->oAction == 3)
        obj_mark_for_deletion(o);
}

void bhv_wf_elevator_tower_platform_loop(void) {
    switch (o->oAction) {
        case 0:
            if (gMarioObject->platform == o)
                o->oAction++;
            break;
        case 1:
            cur_obj_play_sound_1(SOUND_ENV_ELEVATOR1);
            if (o->oTimer > 140)
                o->oAction++;
            else
                QMODFIELD(o, oPosY, += q(5));
            break;
        case 2:
            if (o->oTimer > 60)
                o->oAction++;
            break;
        case 3:
            cur_obj_play_sound_1(SOUND_ENV_ELEVATOR1);
            if (o->oTimer > 140)
                o->oAction = 0;
            else
                QMODFIELD(o, oPosY, -= q(5));
            break;
    }
    if (o->parentObj->oAction == 3)
        obj_mark_for_deletion(o);
}

void bhv_wf_sliding_tower_platform_loop(void) {
    s32 sp24 = FFIELD(o, oPlatformUnk110) / FFIELD(o, oPlatformUnk10C);
    switch (o->oAction) {
        case 0:
            if (o->oTimer > sp24)
                o->oAction++;
            QSETFIELD(o, oForwardVel, -QFIELD(o, oPlatformUnk10C));
            break;
        case 1:
            if (o->oTimer > sp24)
                o->oAction = 0;
            QSETFIELD(o, oForwardVel, QFIELD(o, oPlatformUnk10C));
            break;
    }
    cur_obj_compute_vel_xz();
    QMODFIELD(o, oPosX, += QFIELD(o, oVelX));
    QMODFIELD(o, oPosZ, += QFIELD(o, oVelZ));
    if (o->parentObj->oAction == 3)
        obj_mark_for_deletion(o);
}

void spawn_and_init_wf_platforms(s16 a, const BehaviorScript *bhv) {
    s16 yaw;
    struct Object *platform = spawn_object(o, a, bhv);
    yaw = o->oPlatformSpawnerUnkF4 * o->oPlatformSpawnerUnkFC + o->oPlatformSpawnerUnkF8;
    platform->oMoveAngleYaw = yaw;
    QMODFIELD(platform, oPosX, += qmul(QFIELD(o, oPlatformSpawnerUnk100), sinqs(yaw)));
    QMODFIELD(platform, oPosY, += q(o->oPlatformSpawnerUnkF4 * 100));
    QMODFIELD(platform, oPosZ, += qmul(QFIELD(o, oPlatformSpawnerUnk100), cosqs(yaw)));
    QSETFIELD(platform, oPlatformUnk110, QFIELD(o, oPlatformSpawnerUnk104));
    QSETFIELD(platform, oPlatformUnk10C, QFIELD(o, oPlatformSpawnerUnk108));
    o->oPlatformSpawnerUnkF4++;
}

void spawn_wf_platform_group(void) {
    UNUSED s32 unused = 8;
    o->oPlatformSpawnerUnkF4 = 0;
    o->oPlatformSpawnerUnkF8 = 0;
    o->oPlatformSpawnerUnkFC = 0x2000;
    QSETFIELD(o, oPlatformSpawnerUnk100, q(704));
    QSETFIELD(o, oPlatformSpawnerUnk104, q(380));
    QSETFIELD(o, oPlatformSpawnerUnk108, q(3));
    spawn_and_init_wf_platforms(MODEL_WF_TOWER_SQUARE_PLATORM, bhvWfSolidTowerPlatform);
    spawn_and_init_wf_platforms(MODEL_WF_TOWER_SQUARE_PLATORM, bhvWfSlidingTowerPlatform);
    spawn_and_init_wf_platforms(MODEL_WF_TOWER_SQUARE_PLATORM, bhvWfSolidTowerPlatform);
    spawn_and_init_wf_platforms(MODEL_WF_TOWER_SQUARE_PLATORM, bhvWfSlidingTowerPlatform);
    spawn_and_init_wf_platforms(MODEL_WF_TOWER_SQUARE_PLATORM, bhvWfSolidTowerPlatform);
    spawn_and_init_wf_platforms(MODEL_WF_TOWER_SQUARE_PLATORM, bhvWfSlidingTowerPlatform);
    spawn_and_init_wf_platforms(MODEL_WF_TOWER_SQUARE_PLATORM, bhvWfSolidTowerPlatform);
    spawn_and_init_wf_platforms(MODEL_WF_TOWER_SQUARE_PLATORM_ELEVATOR, bhvWfElevatorTowerPlatform);
}

void bhv_tower_platform_group_loop(void) {
    q32 marioYq = QFIELD(gMarioObject, oPosY);
    QSETFIELD(o, oDistanceToMario, dist_between_objectsq(o, gMarioObject));
    switch (o->oAction) {
        case 0:
            if (marioYq > QFIELD(o, oHomeY) - q(1000))
                o->oAction++;
            break;
        case 1:
            spawn_wf_platform_group();
            o->oAction++;
            break;
        case 2:
            if (marioYq < QFIELD(o, oHomeY) - q(1000))
                o->oAction++;
            break;
        case 3:
            o->oAction = 0;
            break;
    }
}
