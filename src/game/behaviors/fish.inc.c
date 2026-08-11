/**
 * @file fish.inc.c
 * Implements behaviour and spawning for fish located in the Secret Aquarium and other levels.
 */

/**
 * Spawns fish with settings chosen by oBehParams2ndByte.
 * These settings are animations, colour, and spawn quantity.
 */
static void fish_spawner_act_spawn(void) {
    s32 i;
    s32 schoolQuantity;
    s16 model;
    f32 minDistToMario;
    const struct Animation * const *fishAnimation;
    struct Object *fishObject;

    switch (o->oBehParams2ndByte) {

        // Cases need to be on one line to match with and without optimizations.
        case FISH_SPAWNER_BP_MANY_BLUE:
            model = MODEL_FISH;      schoolQuantity = 20; minDistToMario = 1500.0f; fishAnimation = blue_fish_seg3_anims_0301C2B0;
            break;

        case FISH_SPAWNER_BP_FEW_BLUE:
            model = MODEL_FISH;      schoolQuantity = 5;  minDistToMario = 1500.0f; fishAnimation = blue_fish_seg3_anims_0301C2B0;
            break;

        case FISH_SPAWNER_BP_MANY_CYAN:
            model = MODEL_CYAN_FISH; schoolQuantity = 20; minDistToMario = 1500.0f; fishAnimation = cyan_fish_seg6_anims_0600E264;
            break;

        case FISH_SPAWNER_BP_FEW_CYAN:
            model = MODEL_CYAN_FISH; schoolQuantity = 5;  minDistToMario = 1500.0f; fishAnimation = cyan_fish_seg6_anims_0600E264;
            break;

        default: unreachable();
    }


    // Spawn and animate the schoolQuantity of fish if Mario enters render distance
    // or the stage is Secret Aquarium.
    // Fish moves randomly within a range of 700.0f.
    if (FFIELD(o, oDistanceToMario) < minDistToMario || gCurrLevelNum == LEVEL_SA) {
        for (i = 0; i < schoolQuantity; i++) {
            fishObject = spawn_object(o, model, bhvFish);
            fishObject->oBehParams2ndByte = o->oBehParams2ndByte;
            obj_init_animation_with_sound(fishObject, fishAnimation, 0);
            obj_translate_xyz_random(fishObject, 700.0f);
        }
        o->oAction = FISH_SPAWNER_ACT_IDLE;
    }
}

/**
 * Sets the spawner to respawn fish if the stage is not Secret Aquarium and
 * Mario is more than 2000 units higher.
 */
static void fish_spawner_act_idle(void) {
    if ((gCurrLevelNum != LEVEL_SA) && (QFIELD(gMarioObject, oPosY) - QFIELD(o, oPosY) > q(2000))) {
        o->oAction = FISH_SPAWNER_ACT_RESPAWN;
    }
}

/**
 * Temp action that sets the action to spawn fish. This triggers the old fish to despawn.
 */
static void fish_spawner_act_respawn(void) {
    o->oAction = FISH_SPAWNER_ACT_SPAWN;
}

static void (*sFishSpawnerActions[])(void) = {
    fish_spawner_act_spawn, fish_spawner_act_idle, fish_spawner_act_respawn,
};

void bhv_fish_spawner_loop(void) {
    cur_obj_call_action_function(sFishSpawnerActions);
}

/**
 * Allows the fish to swim vertically.
 */
static void fish_vertical_roam(s32 speed) {
    q32 parentYq = QFIELD(o->parentObj, oPosY);

    // If the stage is Secret Aquarium, the fish can
    // travel as far vertically as they wish.
    if (gCurrLevelNum == LEVEL_SA) {
        if (q(500) < ABS(QFIELD(o, oPosY) - QFIELD(o, oFishGoalY))) {
            speed = 10;
        }
        QSETFIELD(o, oPosY, approach_q32_symmetric(QFIELD(o, oPosY), QFIELD(o, oFishGoalY), q(speed)));

     // Allow the fish to roam vertically if within
     // range of the fish spawner.
     } else if (parentYq - q(100) - QFIELD(o, oFishDepthDistance) < QFIELD(o, oPosY)
               && QFIELD(o, oPosY) < parentYq + q(1000) + QFIELD(o, oFishDepthDistance)) {
        QSETFIELD(o, oPosY, approach_q32_symmetric(QFIELD(o, oPosY), QFIELD(o, oFishGoalY), speed));
    }
}

/**
 * Fish action that randomly roams within a set range.
 */
static void fish_act_roam(void) {
    q32 fishYq = QFIELD(o, oPosY) - QFIELD(gMarioObject, oPosY);

    // Alters speed of animation for natural movement.
    if (o->oTimer < 10) {
        cur_obj_init_animation_with_accel_and_sound(0, 2.0f);
    } else {
        cur_obj_init_animation_with_accel_and_sound(0, 1.0f);
    }

    // Initializes some variables when the fish first begins roaming.
    if (o->oTimer == 0) {
        QSETFIELD(o, oForwardVel, random_q32() * 2 + q(3));
        if (gCurrLevelNum == LEVEL_SA) {
            QSETFIELD(o, oFishHeightOffset, random_q32() * 700);
        } else {
            QSETFIELD(o, oFishHeightOffset, random_q32() * 100);
        }
        QSETFIELD(o, oFishRoamDistance, random_q32() * 500 + q(200));
    }

    QSETFIELD(o, oFishGoalY, QFIELD(gMarioObject, oPosY) + QFIELD(o, oFishHeightOffset));

    // Rotate the fish towards Mario.
    cur_obj_rotate_yaw_toward(o->oAngleToMario, 0x400);

    if (QFIELD(o, oPosY) < QFIELD(o, oFishWaterLevel) - q(50)) {
        if (fishYq < 0) {
            fishYq = -fishYq;
        }
        if (fishYq < q(500)) {
            fish_vertical_roam(2);
        } else {
            fish_vertical_roam(4);
        }

    // Don't let the fish leave the water vertically.
    } else {
        QSETFIELD(o, oPosY, QFIELD(o, oFishWaterLevel) - q(50));
        if (fishYq > q(300)) {
            QSETFIELD(o, oPosY, QFIELD(o, oPosY) - QONE);
        }
    }

    // Flee from Mario if the fish gets too close.
    if (QFIELD(o, oDistanceToMario) < QFIELD(o, oFishRoamDistance) + q(150)) {
        o->oAction = FISH_ACT_FLEE;
    }
}

/**
 * Interactively maneuver fish in relation to its distance from other fish and Mario.
 */
static void fish_act_flee(void) {
    q32 fishYq = QFIELD(o, oPosY) - QFIELD(gMarioObject, oPosY);
    QSETFIELD(o, oFishGoalY, QFIELD(gMarioObject, oPosY) + QFIELD(o, oFishHeightOffset));

    // Initialize some variables when the flee action first starts.
    if (o->oTimer == 0) {
        QSETFIELD(o, oFishActiveDistance, random_q32() * 300);
        o->oFishYawVel = random_float() * 1024.0f + 1024.0f;
        QSETFIELD(o, oFishGoalVel, random_q32() * 4 + q(13));
        cur_obj_play_sound_2(SOUND_GENERAL_MOVING_WATER);
    }

    // Speed the animation up over time.
    if (o->oTimer < 20) {
        cur_obj_init_animation_with_accel_and_sound(0, 4.0f);
    } else {
        cur_obj_init_animation_with_accel_and_sound(0, 1.0f);
    }

    // Accelerate over time.
    if (QFIELD(o, oForwardVel) < QFIELD(o, oFishGoalVel)) {
        QSETFIELD(o, oForwardVel, QFIELD(o, oForwardVel) + q(0.5));
    }
    QSETFIELD(o, oFishGoalY, QFIELD(gMarioObject, oPosY) + QFIELD(o, oFishHeightOffset));

    // Rotate fish away from Mario.
    cur_obj_rotate_yaw_toward(o->oAngleToMario + 0x8000, o->oFishYawVel);

    if (QFIELD(o, oPosY) < QFIELD(o, oFishWaterLevel) - q(50)) {
        if (fishYq < 0) {
            fishYq = -fishYq;
        }

        if (fishYq < q(500)) {
            fish_vertical_roam(2);
        } else {
            fish_vertical_roam(4);
        }

    // Don't let the fish leave the water vertically.
    } else {
        QSETFIELD(o, oPosY, QFIELD(o, oFishWaterLevel) - q(50));
        if (fishYq > q(300)) {
            QMODFIELD(o, oPosY, -= QONE);
        }
    }

    // If distance to Mario is too great, then set fish to active.
    if (QFIELD(o, oDistanceToMario) > QFIELD(o, oFishActiveDistance) + q(500)) {
        o->oAction = FISH_ACT_ROAM;
    }
}

/**
 * Animate fish and alter scaling at random for a magnifying effect from the water.
 */
static void fish_act_init(void) {
    cur_obj_init_animation_with_accel_and_sound(0, 1.0f);
    o->header.gfx.animInfo.animFrame = qtrunc(random_q32() * 28);
    QSETFIELD(o, oFishDepthDistance, random_q32() * 300);
    cur_obj_scaleq(random_q32() * 2 / 5 + q(0.8));
    o->oAction = FISH_ACT_ROAM;
}

static void (*sFishActions[])(void) = {
    fish_act_init, fish_act_roam, fish_act_flee,
};

/**
 * Main loop for fish
 */
void bhv_fish_loop(void)
{
    cur_obj_scaleq(q(1.0f));

    // oFishWaterLevel tracks if a fish has roamed out of water.
    // This can't happen in Secret Aquarium, so set it to 0.
    if (gCurrLevelNum == LEVEL_SA) {
        QSETFIELD(o, oFishWaterLevel, 0);
    } else {
		QSETFIELD(o, oFishWaterLevel, find_water_levelq(QFIELD(o, oPosX), QFIELD(o, oPosZ)));
	}

    // Apply hitbox and resolve wall collisions
    QSETFIELD(o, oWallHitboxRadius, q(30));
    cur_obj_resolve_wall_collisions();

    // Delete fish if it's drifted to an area with no water.
    if (gCurrLevelNum != LEVEL_UNKNOWN_32) {
        if (QFIELD(o, oFishWaterLevel) < q(FLOOR_LOWER_LIMIT_MISC)) {
            obj_mark_for_deletion(o);
            return;
        }

    // Unreachable code, perhaps for debugging or testing.
    } else {
        QSETFIELD(o, oFishWaterLevel, q(1000));
    }

    // Call fish action methods and apply physics engine.
    cur_obj_call_action_function(sFishActions);
    cur_obj_move_using_fvel_and_gravity();

    // If the parent object has action set to two, then delete the fish object.
    if (o->parentObj->oAction == FISH_SPAWNER_ACT_RESPAWN) {
        obj_mark_for_deletion(o);
    }
}
