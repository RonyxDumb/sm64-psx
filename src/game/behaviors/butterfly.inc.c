// butterfly.c.inc

void bhv_butterfly_init(void) {
    cur_obj_init_animation(1);

    o->oButterflyYPhase = random_float() * 100;
    o->header.gfx.animInfo.animFrame = random_float() * 7;
    QSETFIELD(o, oHomeX, QFIELD(o, oPosX));
    QSETFIELD(o, oHomeY, QFIELD(o, oPosY));
    QSETFIELD(o, oHomeZ, QFIELD(o, oPosZ));
}

// sp28 = speed

void butterfly_step(s32 speed) {
    struct FloorGeometry *sp24;
    s16 yaw = o->oMoveAngleYaw;
    s16 pitch = o->oMoveAnglePitch;
    s16 yPhase = o->oButterflyYPhase;
    q32 floorYq;

    QSETFIELD(o, oVelX, sinqs(yaw) * speed);
    QSETFIELD(o, oVelY, sinqs(pitch) * speed);
    QSETFIELD(o, oVelZ, cosqs(yaw) * speed);

    QMODFIELD(o, oPosX, += FFIELD(o, oVelX));
    QMODFIELD(o, oPosZ, += FFIELD(o, oVelZ));

    if (o->oAction == BUTTERFLY_ACT_FOLLOW_MARIO)
        QMODFIELD(o, oPosY, -= QFIELD(o, oVelY) + cosqs((s32)(yPhase * 655.36)) * (20 / 4));
    else
        QMODFIELD(o, oPosY, -= QFIELD(o, oVelY));

    floorYq = find_floor_height_and_dataq(QFIELD(o, oPosX), QFIELD(o, oPosY), QFIELD(o, oPosZ), &sp24);

    if (QFIELD(o, oPosY) < floorYq + q(2))
        QSETFIELD(o, oPosY, floorYq + q(2));

    o->oButterflyYPhase++;
    if (o->oButterflyYPhase >= 101)
        o->oButterflyYPhase = 0;
}

void butterfly_calculate_angle(void) {
    QMODFIELD(gMarioObject, oPosX, += q(5) * o->oButterflyYPhase / 4);
    QMODFIELD(gMarioObject, oPosZ, += q(5) * o->oButterflyYPhase / 4);
    obj_turn_toward_object(o, gMarioObject, 16, 0x300);
    QMODFIELD(gMarioObject, oPosX, -= q(5) * o->oButterflyYPhase / 4);
    QMODFIELD(gMarioObject, oPosZ, -= q(5) * o->oButterflyYPhase / 4);

    QMODFIELD(gMarioObject, oPosY, += (q(5) * o->oButterflyYPhase + 0x100) / 4);
    obj_turn_toward_object(o, gMarioObject, 15, 0x500);
    QMODFIELD(gMarioObject, oPosY, -= (q(5) * o->oButterflyYPhase + 0x100) / 4);
}

void butterfly_act_rest(void) {
    if (is_point_within_radius_of_mario(IFIELD(o, oPosX), IFIELD(o, oPosY), IFIELD(o, oPosZ), 1000)) {
        cur_obj_init_animation(0);

        o->oAction = BUTTERFLY_ACT_FOLLOW_MARIO;
        o->oMoveAngleYaw = gMarioObject->header.gfx.angle[1];
    }
}

void butterfly_act_follow_mario(void) {
    butterfly_calculate_angle();

    butterfly_step(7);

    if (!is_point_within_radius_of_mario(IFIELD(o, oHomeX), IFIELD(o, oHomeY), IFIELD(o, oHomeZ), 1200))
        o->oAction = BUTTERFLY_ACT_RETURN_HOME;
}

void butterfly_act_return_home(void) {
    s32 homeDistXi = IFIELD(o, oHomeX) - IFIELD(o, oPosX);
    s32 homeDistYi = IFIELD(o, oHomeY) - IFIELD(o, oPosY);
    s32 homeDistZi = IFIELD(o, oHomeZ) - IFIELD(o, oPosZ);
    s16 hAngleToHome = atan2sq(homeDistZi, homeDistXi);
    s16 vAngleToHome = atan2sq(sqrtu(homeDistXi * homeDistXi + homeDistZi * homeDistZi), -homeDistYi);

    o->oMoveAngleYaw = approach_s16_symmetric(o->oMoveAngleYaw, hAngleToHome, 0x800);
    o->oMoveAnglePitch = approach_s16_symmetric(o->oMoveAnglePitch, vAngleToHome, 0x50);

    butterfly_step(7);

    if (homeDistXi * homeDistXi + homeDistYi * homeDistYi + homeDistZi * homeDistZi < 144) {
        cur_obj_init_animation(1);

        o->oAction = BUTTERFLY_ACT_RESTING;
        QSETFIELD(o, oPosX, QFIELD(o, oHomeX));
        QSETFIELD(o, oPosY, QFIELD(o, oHomeY));
        QSETFIELD(o, oPosZ, QFIELD(o, oHomeZ));
    }
}

void bhv_butterfly_loop(void) {
    switch (o->oAction) {
        case BUTTERFLY_ACT_RESTING:
            butterfly_act_rest();
            break;

        case BUTTERFLY_ACT_FOLLOW_MARIO:
            butterfly_act_follow_mario();
            break;

        case BUTTERFLY_ACT_RETURN_HOME:
            butterfly_act_return_home();
            break;
    }

    set_object_visibility(o, 3000);
}
