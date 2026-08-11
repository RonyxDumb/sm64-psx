// hoot.c.inc

void bhv_hoot_init(void) {
    cur_obj_init_animation(0);

    FSETFIELD(o, oHomeX, FFIELD(o, oPosX) + 800.0f);
    FSETFIELD(o, oHomeY, FFIELD(o, oPosY) - 150.0f);
    FSETFIELD(o, oHomeZ, FFIELD(o, oPosZ) + 300.0f);
    o->header.gfx.node.flags |= GRAPH_RENDER_INVISIBLE;

    cur_obj_become_intangible();
}

// sp28 = arg0
// sp2c = arg1

q32 hoot_find_next_floorq(struct FloorGeometry **arg0, q32 arg1q) {
    q32 sp24q = qmul(arg1q, sinqs(o->oMoveAngleYaw)) + FFIELD(o, oPosX);
    q32 sp1cq = qmul(arg1q, cosqs(o->oMoveAngleYaw)) + FFIELD(o, oPosZ);
    q32 floorY = find_floor_height_and_dataq(sp24q, q(10000), sp1cq, arg0);
    return floorY;
}

void hoot_floor_bounce(void) {
    struct FloorGeometry *sp1c;
    q32 floorYq;

    floorYq = hoot_find_next_floorq(&sp1c, q(375));
    if (floorYq + q(75) > QFIELD(o, oPosY))
        o->oMoveAnglePitch -= 3640.8888;

    floorYq = hoot_find_next_floorq(&sp1c, q(200));
    if (floorYq + q(125) > QFIELD(o, oPosY))
        o->oMoveAnglePitch -= 7281.7776;

    floorYq = hoot_find_next_floorq(&sp1c, 0);
    if (floorYq + q(125) > QFIELD(o, oPosY))
        FSETFIELD(o, oPosY, qtof(floorYq + q(125)));
    if (o->oMoveAnglePitch < -21845.3328)
        o->oMoveAnglePitch = -21845;
}

// sp30 = fastOscY
// sp34 = speed

void hoot_free_step(s16 fastOscY, s32 speed) {
    struct FloorGeometry *sp2c;
    s16 yaw = o->oMoveAngleYaw;
    s16 pitch = o->oMoveAnglePitch;
    s16 sp26 = o->header.gfx.animInfo.animFrame;
    f32 xPrev = FFIELD(o, oPosX);
    f32 zPrev = FFIELD(o, oPosZ);
    f32 hSpeed;

    FSETFIELD(o, oVelY, sins(pitch) * speed);
    hSpeed = coss(pitch) * speed;
    FSETFIELD(o, oVelX, sins(yaw) * hSpeed);
    FSETFIELD(o, oVelZ, coss(yaw) * hSpeed);

    QMODFIELD(o, oPosX,  += QFIELD(o, oVelX));
    if (fastOscY == 0)
        QMODFIELD(o, oPosY, -= QFIELD(o, oVelY) + cosqs((s32)(sp26 * 3276.8)) * (50 / 4));
    else
        QMODFIELD(o, oPosY, -= QFIELD(o, oVelY) + cosqs((s32)(sp26 * 6553.6)) * (50 / 4));
    QMODFIELD(o, oPosZ, += QFIELD(o, oVelZ));

    find_floor_height_and_dataq(QFIELD(o, oPosX), QFIELD(o, oPosY), QFIELD(o, oPosZ), &sp2c);
    if (sp2c == NULL) {
        FSETFIELD(o, oPosX, xPrev);
        FSETFIELD(o, oPosZ, zPrev);
    }

    if (sp26 == 0)
        cur_obj_play_sound_2(SOUND_GENERAL_SWISH_WATER);
}

void hoot_player_set_yaw(void) {
    s16 stickX = gPlayer3Controller->rawStickX;
    s16 stickY = gPlayer3Controller->rawStickY;
    UNUSED s16 pitch = o->oMoveAnglePitch;
    if (stickX < 10 && stickX >= -9)
        stickX = 0;
    if (stickY < 10 && stickY >= -9)
        stickY = 0;

    o->oMoveAngleYaw -= 5 * stickX;
}

// sp28 = speed
// sp2c = xPrev
// sp30 = zPrev

void hoot_carry_step(s32 speed, UNUSED f32 xPrev, UNUSED f32 zPrev) {
    s16 yaw = o->oMoveAngleYaw;
    s16 pitch = o->oMoveAnglePitch;
    s16 sp22 = o->header.gfx.animInfo.animFrame;

    QSETFIELD(o, oVelY, sinqs(pitch) * speed);
    q32 hSpeedq = cosqs(pitch) * speed;
    QSETFIELD(o, oVelX, qmul(sinqs(yaw), hSpeedq));
    QSETFIELD(o, oVelZ, qmul(cosqs(yaw), hSpeedq));

    QMODFIELD(o, oPosX, += QFIELD(o, oVelX));
    QMODFIELD(o, oPosY, -= QFIELD(o, oVelY) + cosqs((s32)(sp22 * 6553.6)) * (50 / 4));
    QMODFIELD(o, oPosZ, += QFIELD(o, oVelZ));

    if (sp22 == 0)
        cur_obj_play_sound_2(SOUND_GENERAL_SWISH_WATER);
}

// sp48 = xPrev
// sp4c = yPrev
// sp50 = zPrev

void hoot_surface_collision(f32 xPrev, UNUSED f32 yPrev, f32 zPrev) {
    struct FloorGeometry *sp44;
    struct WallCollisionData hitbox;
    q32 floorYq;

    hitbox.xq = QFIELD(o, oPosX);
    hitbox.yq = QFIELD(o, oPosY);
    hitbox.zq = QFIELD(o, oPosZ);
    hitbox.offsetYq = q(10);
    hitbox.radiusq = q(50);

    if (find_wall_collisions(&hitbox) != 0) {
        QSETFIELD(o, oPosX, hitbox.xq);
        QSETFIELD(o, oPosY, hitbox.yq);
        QSETFIELD(o, oPosZ, hitbox.zq);
        gMarioObject->oInteractStatus |= INT_STATUS_MARIO_UNK7; /* bit 7 */
    }

    floorYq = find_floor_height_and_dataq(QFIELD(o, oPosX), QFIELD(o, oPosY), QFIELD(o, oPosZ), &sp44);
    if (sp44 == NULL) {
        FSETFIELD(o, oPosX, xPrev);
        FSETFIELD(o, oPosZ, zPrev);
        return;
    }

    if (absf_2(FFIELD(o, oPosX)) > 8000.0f)
        FSETFIELD(o, oPosX, xPrev);
    if (absf_2(FFIELD(o, oPosZ)) > 8000.0f)
        FSETFIELD(o, oPosZ, zPrev);
    if (floorYq + q(125) > QFIELD(o, oPosY))
        QSETFIELD(o, oPosY, floorYq + q(125));
}

// sp28 = xPrev
// sp2c = zPrev

void hoot_act_ascent(f32 xPrev, f32 zPrev) {
    s16 angleToOrigin = atan2s(-QFIELD(o, oPosZ), -QFIELD(o, oPosX));

    o->oMoveAngleYaw = approach_s16_symmetric(o->oMoveAngleYaw, angleToOrigin, 0x500);
    o->oMoveAnglePitch = 0xCE38;

    if (o->oTimer >= 29) {
        cur_obj_play_sound_1(SOUND_ENV_WIND2);
        o->header.gfx.animInfo.animFrame = 1;
    }

    if (QFIELD(o, oPosY) > q(6500))
        o->oAction = HOOT_ACT_CARRY;

    hoot_carry_step(60, xPrev, zPrev);
}

void hoot_action_loop(void) {
    f32 xPrev = FFIELD(o, oPosX);
    f32 yPrev = FFIELD(o, oPosY);
    f32 zPrev = FFIELD(o, oPosZ);

    switch (o->oAction) {
        case HOOT_ACT_ASCENT:
            hoot_act_ascent(xPrev, zPrev);
            break;

        case HOOT_ACT_CARRY:
            hoot_player_set_yaw();

            o->oMoveAnglePitch = 0x71C;

            if (QFIELD(o, oPosY) < q(2700.0f)) {
                set_time_stop_flags(TIME_STOP_ENABLED | TIME_STOP_MARIO_AND_DOORS);

                if (cutscene_object_with_dialog(CUTSCENE_DIALOG, o, DIALOG_045)) {
                    clear_time_stop_flags(TIME_STOP_ENABLED | TIME_STOP_MARIO_AND_DOORS);

                    o->oAction = HOOT_ACT_TIRED;
                }
            }

            hoot_carry_step(20, xPrev, zPrev);
            break;

        case HOOT_ACT_TIRED:
            hoot_player_set_yaw();

            o->oMoveAnglePitch = 0;

            hoot_carry_step(20, xPrev, zPrev);

            if (o->oTimer >= 61)
                gMarioObject->oInteractStatus |= INT_STATUS_MARIO_UNK7; /* bit 7 */
            break;
    }

    hoot_surface_collision(xPrev, yPrev, zPrev);
}

void hoot_turn_to_home(void) {
    f32 homeDistX = FFIELD(o, oHomeX) - FFIELD(o, oPosX);
    f32 homeDistY = FFIELD(o, oHomeY) - FFIELD(o, oPosY);
    f32 homeDistZ = FFIELD(o, oHomeZ) - FFIELD(o, oPosZ);
    s16 hAngleToHome = atan2s(homeDistZ, homeDistX);
    s16 vAngleToHome = atan2s(sqrtf(homeDistX * homeDistX + homeDistZ * homeDistZ), -homeDistY);

    o->oMoveAngleYaw = approach_s16_symmetric(o->oMoveAngleYaw, hAngleToHome, 0x140);
    o->oMoveAnglePitch = approach_s16_symmetric(o->oMoveAnglePitch, vAngleToHome, 0x140);
}

void hoot_awake_loop(void) {
    if (o->oInteractStatus == TRUE) { //! Note: Not a flag, treated as a TRUE/FALSE statement
        hoot_action_loop();
        cur_obj_init_animation(1);
    } else {
        cur_obj_init_animation(0);

        hoot_turn_to_home();
        hoot_floor_bounce();
        hoot_free_step(0, 10);

        o->oAction = 0;
        o->oTimer = 0;
    }

    set_object_visibility(o, 2000);
}

void bhv_hoot_loop(void) {
    switch (o->oHootAvailability) {
        case HOOT_AVAIL_ASLEEP_IN_TREE:
            if (is_point_within_radius_of_mario(IFIELD(o, oPosX), IFIELD(o, oPosY), IFIELD(o, oPosZ), 50)) {
                o->header.gfx.node.flags &= ~GRAPH_RENDER_INVISIBLE;
                o->oHootAvailability = HOOT_AVAIL_WANTS_TO_TALK;
            }
            break;

        case HOOT_AVAIL_WANTS_TO_TALK:
            hoot_awake_loop();

            if (set_mario_npc_dialog(MARIO_DIALOG_LOOK_UP) == MARIO_DIALOG_STATUS_SPEAK
                && cutscene_object_with_dialog(CUTSCENE_DIALOG, o, DIALOG_044)) {
                set_mario_npc_dialog(MARIO_DIALOG_STOP);

                cur_obj_become_tangible();

                o->oHootAvailability = HOOT_AVAIL_READY_TO_FLY;
            }
            break;

        case HOOT_AVAIL_READY_TO_FLY:
            hoot_awake_loop();
            break;
    }
}
