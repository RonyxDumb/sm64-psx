
/**
 * Behaviors for bhvWaterBombSpawner, bhvWaterBomb, and bhvWaterBombShadow.
 * The spawner spawns the water bombs that fall on mario from above. These ones
 * start in the WATER_BOMB_ACT_INIT action.
 * Water bombs can also be shot by cannons. These ones stay in the
 * WATER_BOMB_ACT_SHOT_FROM_CANNON action.
 * The water bomb shadow is only spawned by water bomb spawners.
 */

/**
 * Hitbox for water bombs that are spawned by a water bomb spawner. The water
 * bombs that are shot from cannons are intangible.
 */
static struct ObjectHitbox sWaterBombHitbox = {
    /* interactType:      */ INTERACT_MR_BLIZZARD,
    /* downOffset:        */ 25,
    /* damageOrCoinValue: */ 1,
    /* health:            */ 99,
    /* numLootCoins:      */ 0,
    /* radius:            */ 80,
    /* height:            */ 50,
    /* hurtboxRadius:     */ 60,
    /* hurtboxHeight:     */ 50,
};

/**
 * Update function for bhvWaterBombSpawner.
 * Spawn water bombs targeting mario when he comes in range.
 */
void bhv_water_bomb_spawner_update(void) {
    q32 latDistToMarioq;
    q32 spawnerRadiusq;

    spawnerRadiusq = q(50 * (u16)(o->oBehParams >> 16) + 200);
    latDistToMarioq = lateral_dist_between_objectsq(o, gMarioObject);

    // When mario is in range and a water bomb isn't already active
    if (!o->oWaterBombSpawnerBombActive && latDistToMarioq < spawnerRadiusq
        && QFIELD(gMarioObject, oPosY) - QFIELD(o, oPosY) < q(1000)) {
        if (o->oWaterBombSpawnerTimeToSpawn != 0) {
            o->oWaterBombSpawnerTimeToSpawn -= 1;
        } else {
            struct Object *waterBomb =
                spawn_object_relative(0, 0, 2000, 0, o, MODEL_WATER_BOMB, bhvWaterBomb);

            if (waterBomb != NULL) {
                // Drop farther ahead of mario when he is moving faster
                f32 waterBombDistToMario = 28.0f * gMarioStates[0].forwardVel + 100.0f;

                waterBomb->oAction = WATER_BOMB_ACT_INIT;

                FSETFIELD(waterBomb, oPosX,
                    FFIELD(gMarioObject, oPosX) + waterBombDistToMario * sins(gMarioObject->oMoveAngleYaw));
                FSETFIELD(waterBomb, oPosZ,
                    FFIELD(gMarioObject, oPosZ) + waterBombDistToMario * coss(gMarioObject->oMoveAngleYaw));

                spawn_object(waterBomb, MODEL_WATER_BOMB_SHADOW, bhvWaterBombShadow);

                o->oWaterBombSpawnerBombActive = TRUE;
                o->oWaterBombSpawnerTimeToSpawn = random_linear_offset(0, 50);
            }
        }
    }
}

/**
 * Spawn particles when the water bomb explodes.
 */
void water_bomb_spawn_explode_particles(s8 offsetY, s8 forwardVelRange, s8 velYBase) {
    static struct SpawnParticlesInfo sWaterBombExplodeParticles = {
        /* behParam:        */ 0,
        /* count:           */ 5,
        /* model:           */ MODEL_BUBBLE,
        /* offsetY:         */ 20,
        /* forwardVelBase:  */ 20,
        /* forwardVelRange: */ 60,
        /* velYBase:        */ 10,
        /* velYRange:       */ 10,
        /* gravity:         */ -2,
        /* dragStrength:    */ 0,
        /* sizeBase:        */ 35.0f,
        /* sizeRange:       */ 10.0f,
    };

    sWaterBombExplodeParticles.offsetY = offsetY;
    sWaterBombExplodeParticles.forwardVelRange = forwardVelRange;
    sWaterBombExplodeParticles.velYBase = velYBase;
    cur_obj_spawn_particles(&sWaterBombExplodeParticles);
}

/**
 * Enter the drop action with -40 y vel.
 */
static void water_bomb_act_init(void) {
    cur_obj_play_sound_2(SOUND_OBJ_SOMETHING_LANDING);

    o->oAction = WATER_BOMB_ACT_DROP;
    o->oMoveFlags = 0;
    QSETFIELD(o, oVelY, q(-40));
}

/**
 * Explode on impact, and otherwise bounce a few times on the ground and then
 * explode.
 */
static void water_bomb_act_drop(void) {
    obj_set_hitbox(o, &sWaterBombHitbox);

    // Explode if touched or if hit water
    if ((o->oInteractStatus & INT_STATUS_INTERACTED) || (o->oMoveFlags & OBJ_MOVE_ENTERED_WATER)) {
        create_sound_spawner(SOUND_OBJ_DIVING_IN_WATER);
        set_camera_shake_from_pointq(SHAKE_POS_SMALL, QFIELD(o, oPosX), QFIELD(o, oPosY), QFIELD(o, oPosZ));
        o->oAction = WATER_BOMB_ACT_EXPLODE;
    } else if (o->oMoveFlags & OBJ_MOVE_MASK_ON_GROUND) {
        // On impact with the ground, begin getting squished
        if (!o->oWaterBombOnGround) {
            o->oWaterBombOnGround = TRUE;

            if (FMODFIELD(o, oWaterBombNumBounces, += QONE) < q(3)) {
                cur_obj_play_sound_2(SOUND_OBJ_WATER_BOMB_BOUNCING);
            } else {
                create_sound_spawner(SOUND_OBJ_DIVING_IN_WATER);
            }

            set_camera_shake_from_pointq(SHAKE_POS_SMALL, QFIELD(o, oPosX), QFIELD(o, oPosY), QFIELD(o, oPosZ));

            // Move toward mario
            o->oMoveAngleYaw = o->oAngleToMario;
            QSETFIELD(o,  oForwardVel, q(10));
            QSETFIELD(o,  oWaterBombStretchSpeed, q(-40));
        }

        // Make less of an attempt to unsquish on each bounce
        FMODFIELD(o, oWaterBombStretchSpeed, += 15.0f - FFIELD(o, oWaterBombNumBounces) * 2.8f);

        FMODFIELD(o, oWaterBombVerticalStretch, += FFIELD(o, oWaterBombStretchSpeed) * 0.01f);

        // After being too squished, explode
        if (FFIELD(o, oWaterBombVerticalStretch) < -0.8f) {
            o->oAction = WATER_BOMB_ACT_EXPLODE;
        } else if (QFIELD(o, oWaterBombVerticalStretch) > q(0.1f)) {
            // Begin bounce trajectory
            FSETFIELD(o, oVelY, 1.8f * FFIELD(o, oWaterBombStretchSpeed));
        }
    } else {
        APPROACH_F32_FIELD(o, oWaterBombVerticalStretch, 0.0f, 0.008f);
        o->oWaterBombOnGround = FALSE;
    }

    o->header.gfx.scaleq[1] = QFIELD(o, oWaterBombVerticalStretch) + QONE;

    q32 stretch = QFIELD(o, oWaterBombVerticalStretch);
    if (QFIELD(o, oWaterBombNumBounces) == q(3)) {
        stretch *= 4;
    }
    o->header.gfx.scaleq[0] = o->header.gfx.scaleq[2] = QONE - q(stretch);

    cur_obj_move_standard(78);
}

/**
 * Spawn particles, then despawn. This action informs the water bomb shadow to
 * despawn as well.
 */
static void water_bomb_act_explode(void) {
    water_bomb_spawn_explode_particles(25, 60, 10);
    o->parentObj->oWaterBombSpawnerBombActive = FALSE;
    obj_mark_for_deletion(o);
}

/**
 * Despawn after 100 frames.
 */
static void water_bomb_act_shot_from_cannon(void) {

    static struct SpawnParticlesInfo sWaterBombCannonParticle = {
        /* behParam:        */ 0,
        /* count:           */ 1,
        /* model:           */ MODEL_BUBBLE,
        /* offsetY:         */ 236,
        /* forwardVelBase:  */ 20,
        /* forwardVelRange: */ 5,
        /* velYBase:        */ 0,
        /* velYRange:       */ 0,
        /* gravity:         */ -2,
        /* dragStrength:    */ 0,
        /* sizeBase:        */ 20.0f,
        /* sizeRange:       */ 5.0f,
    };

    if (o->oTimer > 100) {
        obj_mark_for_deletion(o);
    } else {
        if (o->oTimer < 7) {
            if (o->oTimer == 1) {
                water_bomb_spawn_explode_particles(-20, 10, 30);
            }
            cur_obj_spawn_particles(&sWaterBombCannonParticle);
        }

        if (o->header.gfx.scaleq[1] > q(1.2)) {
            o->header.gfx.scaleq[1] -= q(0.1);
        }

        o->header.gfx.scaleq[0] = o->header.gfx.scaleq[2] = q(2) - o->header.gfx.scaleq[1];
        cur_obj_set_pos_via_transform();
    }
}

/**
 * Update function for bhvWaterBomb.
 */
void bhv_water_bomb_update(void) {
    if (o->oAction == WATER_BOMB_ACT_SHOT_FROM_CANNON) {
        water_bomb_act_shot_from_cannon();
    } else {
        QSETFIELD(o, oGraphYOffset, 40 * o->header.gfx.scaleq[1]);
        cur_obj_update_floor_and_walls();

        switch (o->oAction) {
            case WATER_BOMB_ACT_INIT:
                water_bomb_act_init();
                break;
            case WATER_BOMB_ACT_DROP:
                water_bomb_act_drop();
                break;
            case WATER_BOMB_ACT_EXPLODE:
                water_bomb_act_explode();
                break;
        }
    }
}

/**
 * Update function for bhvWaterBombShadow.
 * Despawn when the parent water bomb does.
 */
void bhv_water_bomb_shadow_update(void) {
    if (o->parentObj->oAction == WATER_BOMB_ACT_EXPLODE) {
        obj_mark_for_deletion(o);
    } else {
        // TODO: What is happening here
        f32 bombHeight = FFIELD(o->parentObj, oPosY) - FFIELD(o->parentObj, oFloorHeight);
        if (bombHeight > 500.0f) {
            bombHeight = 500.0f;
        }

        obj_copy_pos(o, o->parentObj);
        FSETFIELD(o, oPosY, FFIELD(o->parentObj, oFloorHeight) + bombHeight);
        obj_copy_scale(o, o->parentObj);
    }
}
