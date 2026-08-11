struct ObjectHitbox sGrowingBowserFlameHitbox = {
    /* interactType: */ INTERACT_FLAME,
    /* downOffset: */ 20,
    /* damageOrCoinValue: */ 1,
    /* health: */ 0,
    /* numLootCoins: */ 0,
    /* radius: */ 10,
    /* height: */ 40,
    /* hurtboxRadius: */ 0,
    /* hurtboxHeight: */ 0,
};

struct ObjectHitbox sBowserFlameHitbox = {
    /* interactType: */ INTERACT_FLAME,
    /* downOffset: */ 0,
    /* damageOrCoinValue: */ 1,
    /* health: */ 0,
    /* numLootCoins: */ 0,
    /* radius: */ 10,
    /* height: */ 40,
    /* hurtboxRadius: */ 0,
    /* hurtboxHeight: */ 0,
};

void bowser_flame_despawn(void) {
    obj_mark_for_deletion(o);
    spawn_object_with_scale(o, MODEL_NONE, bhvBlackSmokeUpward, 1.0f);
    if (random_float() < 0.1) {
        spawn_object(o, MODEL_YELLOW_COIN, bhvTemporaryYellowCoin);
    }
}

s32 bowser_flame_should_despawn(s32 maxTime) {
    if (maxTime < o->oTimer) {
        return TRUE;
    }

    // Flames should despawn if they fall off the arena.
    if (o->oFloorType == SURFACE_BURNING) {
        return TRUE;
    }
    if (o->oFloorType == SURFACE_DEATH_PLANE) {
        return TRUE;
    }

    return FALSE;
}

void bhv_flame_bowser_init(void) {
    o->oAnimState = (s32)(random_float() * 10.0f);
    o->oMoveAngleYaw = random_u16();
    if (random_q32() < q(0.2)) {
        QSETFIELD(o, oVelY, q(80));
    } else {
        QSETFIELD(o, oVelY, q(20));
    }
    QSETFIELD(o, oForwardVel, q(10));
    QSETFIELD(o, oGravity, q(-1));
    QSETFIELD(o, oFlameScale, random_q32() + QONE);
}

void bhv_flame_large_burning_out_init(void) {
    o->oAnimState = (s32)(random_float() * 10.0f);
    o->oMoveAngleYaw = random_u16();
    QSETFIELD(o, oVelY, q(10));
    QSETFIELD(o, oForwardVel, 0);
    QSETFIELD(o, oFlameScale, q(7));
}

void bowser_flame_move(void) {
    s32 timer;
    timer = ((o->oFlameSpeedTimerOffset + gGlobalTimer) & 0x3F) << 10;
    FMODFIELD(o, oPosX,  += sins(o->oMoveAngleYaw) * sins(timer) * 4.0f);
    FMODFIELD(o, oPosZ,  += coss(o->oMoveAngleYaw) * sins(timer) * 4.0f);
}

void bhv_flame_bowser_loop(void) {
    cur_obj_update_floor_and_walls();
    cur_obj_move_standard(78);
    if (QFIELD(o, oVelY) < q(-4)) {
        QSETFIELD(o, oVelY, q(-4));
    }
    if (o->oAction == 0) {
        cur_obj_become_intangible();
        bowser_flame_move();
        if (o->oMoveFlags & OBJ_MOVE_LANDED) {
            o->oAction++;
            if (cur_obj_has_behavior(bhvFlameLargeBurningOut)) {
                QSETFIELD(o, oFlameScale, q(8));
            } else {
                QSETFIELD(o, oFlameScale, random_q32() * 2 + q(6));
            }
            QSETFIELD(o, oForwardVel, 0);
            QSETFIELD(o, oVelY, 0);
            QSETFIELD(o, oGravity, 0);
        }
    } else {
        cur_obj_become_tangible();
        if (o->oTimer > qtrunc(QFIELD(o, oFlameScale) * 10) + q(5)) {
            QMODFIELD(o, oFlameScale, -= q(0.15));
            if (QFIELD(o, oFlameScale) <= 0) {
                bowser_flame_despawn();
            }
        }
    }
    cur_obj_scaleq(QFIELD(o, oFlameScale));
    QSETFIELD(o, oGraphYOffset, o->header.gfx.scaleq[1] * 14);
    obj_set_hitbox(o, &sBowserFlameHitbox);
}

void bhv_flame_moving_forward_growing_init(void) {
    QSETFIELD(o,  oForwardVel, q(30));
    obj_translate_xz_random(o, 80.0f);
    o->oAnimState = (s32)(random_float() * 10.0f);
	QSETFIELD(o, oFlameScale, q(3));
}

void bhv_flame_moving_forward_growing_loop(void) {
    UNUSED s32 unused;
    UNUSED struct Object *flame;
    obj_set_hitbox(o, &sGrowingBowserFlameHitbox);
    QSETFIELD(o, oFlameScale, QFIELD(o, oFlameScale) + q(0.5));
    cur_obj_scaleq(QFIELD(o, oFlameScale));
    if (o->oMoveAnglePitch > 0x800) {
        o->oMoveAnglePitch -= 0x200;
    }
    cur_obj_set_pos_via_transform();
    cur_obj_update_floor_height();
    if (QFIELD(o, oFlameScale) > q(30)) {
        obj_mark_for_deletion(o);
    }
    if (QFIELD(o, oPosY) < QFIELD(o, oFloorHeight)) {
        QSETFIELD(o, oPosY, QFIELD(o, oFloorHeight));
        flame = spawn_object(o, MODEL_RED_FLAME, bhvFlameBowser);
        obj_mark_for_deletion(o);
    }
}

void bhv_flame_floating_landing_init(void) {
    o->oAnimState = (s32)(random_float() * 10.0f);
    o->oMoveAngleYaw = random_u16();
    if (o->oBehParams2ndByte != 0) {
        FSETFIELD(o, oForwardVel, random_float() * 5.0f);
    } else {
        FSETFIELD(o, oForwardVel, random_float() * 70.0f);
    }
    FSETFIELD(o, oVelY, random_float() * 20.0f);
    QSETFIELD(o, oGravity, q(-1.0f));
    o->oFlameSpeedTimerOffset = random_float() * 64.0f;
}

f32 sFlameFloatingYLimit[] = { -8.0f, -6.0f, -3.0f };

void bhv_flame_floating_landing_loop(void) {
    UNUSED s32 unused;
    cur_obj_update_floor_and_walls();
    cur_obj_move_standard(78);
    bowser_flame_move();
    if (bowser_flame_should_despawn(900)) {
        obj_mark_for_deletion(o);
    }
    if (FFIELD(o, oVelY) < sFlameFloatingYLimit[o->oBehParams2ndByte]) {
        FSETFIELD(o, oVelY, sFlameFloatingYLimit[o->oBehParams2ndByte]);
    }
    if (o->oMoveFlags & OBJ_MOVE_LANDED) {
        if (o->oBehParams2ndByte == 0) {
            spawn_object(o, MODEL_RED_FLAME, bhvFlameLargeBurningOut);
        } else {
            spawn_object(o, MODEL_NONE, bhvBlueFlamesGroup); //? wonder if they meant MODEL_BLUE_FLAME?
        }
        obj_mark_for_deletion(o);
    }
    QSETFIELD(o, oGraphYOffset, o->header.gfx.scaleq[1] * 14);
}

void bhv_blue_bowser_flame_init(void) {
    obj_translate_xz_random(o, 80.0f);
    o->oAnimState = (s32)(random_float() * 10.0f);
    QSETFIELD(o, oVelY, q(7));
    QSETFIELD(o, oForwardVel, q(35));
    QSETFIELD(o, oFlameScale, q(3));
    QSETFIELD(o, oFlameUnusedRand, random_q32() / 2);
    QSETFIELD(o, oGravity, q(1.0f));
    o->oFlameSpeedTimerOffset = (s32)(random_float() * 64.0f);
}

void bhv_blue_bowser_flame_loop(void) {
    s32 i;
    obj_set_hitbox(o, &sGrowingBowserFlameHitbox);
    if (QFIELD(o, oFlameScale) < q(16)) {
        QSETFIELD(o, oFlameScale, QFIELD(o, oFlameScale) + q(0.5));
    }
    cur_obj_scaleq(QFIELD(o, oFlameScale));
    cur_obj_update_floor_and_walls();
    cur_obj_move_standard(78);
    if (o->oTimer > 0x14) {
        if (o->oBehParams2ndByte == 0) {
            for (i = 0; i < 3; i++) {
                spawn_object_relative_with_scale(0, 0, 0, 0, 5.0f, o, MODEL_RED_FLAME,
                                                 bhvFlameFloatingLanding);
            }
        } else {
            spawn_object_relative_with_scale(1, 0, 0, 0, 8.0f, o, MODEL_BLUE_FLAME,
                                             bhvFlameFloatingLanding);
            spawn_object_relative_with_scale(2, 0, 0, 0, 8.0f, o, MODEL_BLUE_FLAME,
                                             bhvFlameFloatingLanding);
        }
        obj_mark_for_deletion(o);
    }
}

void bhv_flame_bouncing_init(void) {
    o->oAnimState = (s32)(random_float() * 10.0f);
    QSETFIELD(o,  oVelY, q(30));
    QSETFIELD(o,  oForwardVel, q(20));
    QSETFIELD(o, oFlameScale, o->header.gfx.scaleq[0]);
    o->oFlameSpeedTimerOffset = (s32)(random_float() * 64.0f);
}

void bhv_flame_bouncing_loop(void) {
    struct Object *bowser;
    if (o->oTimer == 0) {
        o->oFlameBowser = cur_obj_nearest_object_with_behavior(bhvBowser);
    }
    bowser = o->oFlameBowser;
    QSETFIELD(o, oForwardVel, q(15));
    QSETFIELD(o, oBounciness, -QONE);
    cur_obj_scaleq(QFIELD(o, oFlameScale));
    obj_set_hitbox(o, &sGrowingBowserFlameHitbox);
    cur_obj_update_floor_and_walls();
    cur_obj_move_standard(78);
    if (bowser_flame_should_despawn(300)) {
        obj_mark_for_deletion(o);
    }
    if (bowser != NULL) {
        if (bowser->oHeldState == HELD_FREE) {
            if (lateral_dist_between_objectsq(o, bowser) < q(300)) {
                obj_mark_for_deletion(o);
            }
        }
    }
}

void bhv_blue_flames_group_loop(void) {
    struct Object *flame;
    s32 i;
    if (o->oTimer == 0) {
        o->oMoveAngleYaw = obj_angle_to_object(o, gMarioObject);
        QSETFIELD(o, oBlueFlameNextScale, q(5));
    }
    if (o->oTimer < 16) {
        if ((o->oTimer & 1) == 0) {
            for (i = 0; i < 3; i++) {
                flame = spawn_object(o, MODEL_BLUE_FLAME, bhvFlameBouncing);
                flame->oMoveAngleYaw += i * 0x5555;
                flame->header.gfx.scaleq[0] = QFIELD(o, oBlueFlameNextScale);
            }
            QMODFIELD(o, oBlueFlameNextScale, -= q(0.5));
        }
    } else {
        obj_mark_for_deletion(o);
    }
}
