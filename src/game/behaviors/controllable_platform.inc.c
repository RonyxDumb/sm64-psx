// controllable_platform.c.inc

static s8 D_80331694 = 0;

void controllable_platform_act_1(void) {
    QMODFIELD(o, oParentRelativePosY, -= q(4.0f));
    if (QFIELD(o, oParentRelativePosY) < q(41.0f)) {
        QSETFIELD(o,  oParentRelativePosY, q(41));
        o->oAction = 2;
    }
}

void controllable_platform_act_2(void) {
    if (o->oBehParams2ndByte == D_80331694)
        return;

    QMODFIELD(o, oParentRelativePosY, += q(4.0f));
    if (QFIELD(o, oParentRelativePosY) > q(51.0f)) {
        QSETFIELD(o,  oParentRelativePosY, q(51));
        o->oAction = 0;
    }
}

void bhv_controllable_platform_sub_loop(void) {
    switch (o->oAction) {
        case 0:
            if (o->oTimer < 30)
                break;

            if (gMarioObject->platform == o) {
                D_80331694 = o->oBehParams2ndByte;
#ifdef VERSION_SH
                o->parentObj->header.gfx.node.flags &= ~GRAPH_RENDER_INVISIBLE;
#endif
                o->oAction = 1;
                cur_obj_play_sound_2(SOUND_GENERAL_MOVING_PLATFORM_SWITCH);
            }
            break;

        case 1:
            controllable_platform_act_1();
            break;

        case 2:
            controllable_platform_act_2();
            break;
    }

    QSETFIELD(o,  oVelX, QFIELD(o->parentObj,  oVelX));
    QSETFIELD(o,  oVelZ, QFIELD(o->parentObj,  oVelZ));

    if (o->parentObj->activeFlags == ACTIVE_FLAG_DEACTIVATED)
        o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
}

void bhv_controllable_platform_init(void) {
    struct Object *sp34;
    sp34 = spawn_object_rel_with_rot(o, MODEL_HMC_METAL_ARROW_PLATFORM, bhvControllablePlatformSub, 0,
                                     51, 204, 0, 0, 0);
    sp34->oBehParams2ndByte = 1;
    sp34 = spawn_object_rel_with_rot(o, MODEL_HMC_METAL_ARROW_PLATFORM, bhvControllablePlatformSub, 0,
                                     51, -204, 0, -0x8000, 0);
    sp34->oBehParams2ndByte = 2;
    sp34 = spawn_object_rel_with_rot(o, MODEL_HMC_METAL_ARROW_PLATFORM, bhvControllablePlatformSub, 204,
                                     51, 0, 0, 0x4000, 0);
    sp34->oBehParams2ndByte = 3;
    sp34 = spawn_object_rel_with_rot(o, MODEL_HMC_METAL_ARROW_PLATFORM, bhvControllablePlatformSub,
                                     -204, 51, 0, 0, -0x4000, 0);
    sp34->oBehParams2ndByte = 4;

    D_80331694 = 0;

    QSETFIELD(o,  oControllablePlatformUnkFC, QFIELD(o,  oPosY));
}

void controllable_platform_hit_wall(s8 sp1B) {
    o->oControllablePlatformUnkF8 = sp1B;
    o->oTimer = 0;
    D_80331694 = 5;

    cur_obj_play_sound_2(SOUND_GENERAL_QUIET_POUND1);
#if ENABLE_RUMBLE
    queue_rumble_data(50, 80);
#endif
}

void controllable_platform_check_walls(s8 sp1B, s8 sp1C[3], Vec3f sp20, UNUSED Vec3f sp24, Vec3f sp28) {
    if (sp1C[1] == 1 || (sp1C[0] == 1 && sp1C[2] == 1))
        controllable_platform_hit_wall(sp1B);
    else {
        if (sp1C[0] == 1) {
            if (((sp1B == 1 || sp1B == 2) && (s32) sp20[2] != 0)
                || ((sp1B == 3 || sp1B == 4) && (s32) sp20[0] != 0)) {
                controllable_platform_hit_wall(sp1B);
            } else {
                FMODFIELD(o, oPosX,  += sp20[0]);
                FMODFIELD(o, oPosZ,  += sp20[2]);
            }
        }

        if (sp1C[2] == 1) {
            if (((sp1B == 1 || sp1B == 2) && (s32) sp28[2] != 0)
                || ((sp1B == 3 || sp1B == 4) && (s32) sp28[0] != 0)) {
                controllable_platform_hit_wall(sp1B);
            } else {
                FMODFIELD(o, oPosX,  += sp28[0]);
                FMODFIELD(o, oPosZ,  += sp28[2]);
            }
        }
    }

    if (!is_point_within_radius_of_mario(IFIELD(o, oPosX), IFIELD(o, oPosY), IFIELD(o, oPosZ), 400)) {
        D_80331694 = 6;
        o->oControllablePlatformUnk100 = 1;
        o->oTimer = 0;
    }
}

void controllable_platform_shake_on_wall_hit(void) {
    if (o->oControllablePlatformUnkF8 == 1 || o->oControllablePlatformUnkF8 == 2) {
        o->oFaceAnglePitch = sins(o->oTimer * 0x1000) * 182.04444 * 10.0;
        FSETFIELD(o, oPosY, FFIELD(o, oControllablePlatformUnkFC) + sins(o->oTimer * 0x2000) * 20.0f);
    } else {
        o->oFaceAngleRoll = sins(o->oTimer * 0x1000) * 182.04444 * 10.0;
        FSETFIELD(o, oPosY, FFIELD(o, oControllablePlatformUnkFC) + sins(o->oTimer * 0x2000) * 20.0f);
    }

    if (o->oTimer == 32) {
        D_80331694 = o->oControllablePlatformUnkF8;
        o->oFaceAnglePitch = 0;
        o->oFaceAngleRoll = 0;
        QSETFIELD(o,  oPosY, QFIELD(o,  oControllablePlatformUnkFC));
    }
}

void controllable_platform_tilt_from_mario(void) {
    s16 sp1E = gMarioObject->header.gfx.posi[0] - IFIELD(o, oPosX);
    s16 sp1C = gMarioObject->header.gfx.posi[2] - IFIELD(o, oPosZ);

    if (gMarioObject->platform == o
        || gMarioObject->platform == cur_obj_nearest_object_with_behavior(bhvControllablePlatformSub)) {
        o->oFaceAnglePitch = sp1C * 4;
        o->oFaceAngleRoll = -sp1E * 4;
        if (D_80331694 == 6) {
            D_80331694 = 0;
            o->oTimer = 0;
            o->header.gfx.node.flags &= ~GRAPH_RENDER_INVISIBLE;
        }
    } else {
    }
}

void bhv_controllable_platform_loop(void) {
    s8 sp54[3];
    Vec3f sp48;
    Vec3f sp3C;
    Vec3f sp30;

    o->oAngleVelRoll = 0;
    o->oAngleVelPitch = 0;
    QSETFIELD(o,  oVelX, q(0));
    QSETFIELD(o,  oVelZ, q(0));

    switch (D_80331694) {
        case 0:
            o->oFaceAnglePitch /= 2;
            o->oFaceAngleRoll /= 2;
            if (o->oControllablePlatformUnk100 == 1 && o->oTimer > 30) {
                D_80331694 = 6;
                o->oTimer = 0;
            }
            break;

        case 1:
            QSETFIELD(o,  oVelZ, q(10));
            sp54[0] = obj_find_wall_displacement(sp48, FFIELD(o, oPosX) + 250.0, FFIELD(o, oPosY), FFIELD(o, oPosZ) + 300.0, 50.0f);
            sp54[1] = obj_find_wall_displacement(sp3C, FFIELD(o, oPosX), FFIELD(o, oPosY), FFIELD(o, oPosZ) + 300.0, 50.0f);
            sp54[2] = obj_find_wall_displacement(sp30, FFIELD(o, oPosX) - 250.0, FFIELD(o, oPosY), FFIELD(o, oPosZ) + 300.0, 50.0f);
            controllable_platform_check_walls(2, sp54, sp48, sp3C, sp30);
            break;

        case 2:
            QSETFIELD(o,  oVelZ, q(-10));
            sp54[0] = obj_find_wall_displacement(sp48, FFIELD(o, oPosX) + 250.0, FFIELD(o, oPosY), FFIELD(o, oPosZ) - 300.0, 50.0f);
            sp54[1] = obj_find_wall_displacement(sp3C, FFIELD(o, oPosX), FFIELD(o, oPosY), FFIELD(o, oPosZ) - 300.0, 50.0f);
            sp54[2] = obj_find_wall_displacement(sp30, FFIELD(o, oPosX) - 250.0, FFIELD(o, oPosY), FFIELD(o, oPosZ) - 300.0, 50.0f);
            controllable_platform_check_walls(1, sp54, sp48, sp3C, sp30);
            break;

        case 3:
            QSETFIELD(o,  oVelX, q(10));
            sp54[0] = obj_find_wall_displacement(sp48, FFIELD(o, oPosX) + 300.0, FFIELD(o, oPosY), FFIELD(o, oPosZ) + 250.0, 50.0f);
            sp54[1] = obj_find_wall_displacement(sp3C, FFIELD(o, oPosX) + 300.0, FFIELD(o, oPosY), FFIELD(o, oPosZ), 50.0f);
            sp54[2] = obj_find_wall_displacement(sp30, FFIELD(o, oPosX) + 300.0, FFIELD(o, oPosY), FFIELD(o, oPosZ) - 250.0, 50.0f);
            controllable_platform_check_walls(4, sp54, sp48, sp3C, sp30);
            break;

        case 4:
            QSETFIELD(o,  oVelX, q(-10));
            sp54[0] = obj_find_wall_displacement(sp48, FFIELD(o, oPosX) - 300.0, FFIELD(o, oPosY), FFIELD(o, oPosZ) + 250.0, 50.0f);
            sp54[1] = obj_find_wall_displacement(sp3C, FFIELD(o, oPosX) - 300.0, FFIELD(o, oPosY), FFIELD(o, oPosZ), 50.0f);
            sp54[2] = obj_find_wall_displacement(sp30, FFIELD(o, oPosX) - 300.0, FFIELD(o, oPosY), FFIELD(o, oPosZ) - 250.0, 50.0f);
            controllable_platform_check_walls(3, sp54, sp48, sp3C, sp30);
            break;

        case 5:
            controllable_platform_shake_on_wall_hit();
            return;
            break;

        case 6:
            if (obj_flicker_and_disappear(o, 150))
                spawn_object_abs_with_rot(o, 0, MODEL_HMC_METAL_PLATFORM, bhvControllablePlatform,
                                          FFIELD(o, oHomeX), FFIELD(o, oHomeY), FFIELD(o, oHomeZ), 0, 0, 0);
            break;
    }

    controllable_platform_tilt_from_mario();
    QMODFIELD(o, oPosX, += QFIELD(o, oVelX));
    QMODFIELD(o, oPosZ, += QFIELD(o, oVelZ));
    if (D_80331694 != 0 && D_80331694 != 6)
        cur_obj_play_sound_1(SOUND_ENV_ELEVATOR2);
}
