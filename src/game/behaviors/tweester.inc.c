/**
 * Behavior file for bhvTweester and bhvTweesterSandParticle
 * Tweester swaps between twhree action- an idle action, a chasing
 * Mario action, and an action that hides it. At certain times the
 * Tweester spawns the sand particles also in this file.
 */

struct ObjectHitbox sTweesterHitbox = {
    /* interactType:      */ INTERACT_TORNADO,
    /* downOffset:        */ 0,
    /* damageOrCoinValue: */ 0,
    /* health:            */ 0,
    /* numLootCoins:      */ 0,
    /* radius:            */ 1500,
    /* height:            */ 4000,
    /* hurtboxRadius:     */ 0,
    /* hurtboxHeight:     */ 0,
};

/**
 * Controls the scaling of the tweester as well as
 * its forward velocity.
 */
void tweester_scale_and_moveq(q32 preScaleq) {
    s16 dYaw  = 0x2C00;
    q32 scaleq = preScaleq * 2 / 5;

    o->header.gfx.scaleq[0] = qmul((( cosqs(o->oTweesterScaleTimer) + ONE) * 3 / 20 + ONE), scaleq);
    o->header.gfx.scaleq[1] = qmul(((-cosqs(o->oTweesterScaleTimer) + ONE) / 4 + q(0.5)), scaleq);
    o->header.gfx.scaleq[2] = qmul((( cosqs(o->oTweesterScaleTimer) + ONE) / 4 + ONE), scaleq);

    o->oTweesterScaleTimer += 0x200;
    QSETFIELD(o, oForwardVel, q(14));
    o->oFaceAngleYaw += dYaw;
}

/**
 * Tweester's idle action. Basically stays in active until Mario enters a 1500
 * radius, at which point it appears and grows for 60 frames at which point it
 * it enters the chasing action.
 */
void tweester_act_idle(void) {
    if (o->oSubAction == TWEESTER_SUB_ACT_WAIT) {
        cur_obj_become_tangible();
        cur_obj_set_pos_to_home();
        cur_obj_scaleq(0);

        // Hard to have any idea of this purpose, only set here.
        o->oTweesterUnused = 0;

        // If Mario is within range, change to the growth sub-action.
        if (QFIELD(o, oDistanceToMario) < q(1500))
            o->oSubAction++;

        o->oTimer = 0;
    } else {
        cur_obj_play_sound_1(SOUND_ENV_WIND1);
        tweester_scale_and_moveq(q(o->oTimer) / 60);
        if (o->oTimer > 59)
            o->oAction = TWEESTER_ACT_CHASE;
    }
}

/**
 * Action where the tweester "chases" Mario.
 * After Mario is twirling, then return home.
 */
void tweester_act_chase(void) {
    f32 activationRadius = o->oBehParams2ndByte * 100;

    o->oAngleToHome = cur_obj_angle_to_home();
    cur_obj_play_sound_1(SOUND_ENV_WIND1);

    if (cur_obj_lateral_dist_from_mario_to_home() < activationRadius
        && o->oSubAction == TWEESTER_SUB_ACT_CHASE) {

        QSETFIELD(o,  oForwardVel, q(20));
        cur_obj_rotate_yaw_toward(o->oAngleToMario, 0x200);
        print_debug_top_down_objectinfo("off ", 0);

        if (gMarioStates[0].action == ACT_TWIRLING)
            o->oSubAction++;
    } else {
        QSETFIELD(o,  oForwardVel, q(20));
        cur_obj_rotate_yaw_toward(o->oAngleToHome, 0x200);

        if (cur_obj_lateral_dist_to_home() < 200.0f)
            o->oAction = TWEESTER_ACT_HIDE;
    }

    if (QFIELD(o, oDistanceToMario) > q(3000.0))
        o->oAction = TWEESTER_ACT_HIDE;

    cur_obj_update_floor_and_walls();
    if (o->oMoveFlags & OBJ_MOVE_HIT_WALL)
        o->oMoveAngleYaw = o->oWallAngle;

    cur_obj_move_standard(60);
    tweester_scale_and_moveq(q(1.0f));
    spawn_object(o, MODEL_SAND_DUST, bhvTweesterSandParticle);
}

/**
 * Shrinks the tweester until it is invisible, then returns to the idle
 * action if Mario is 2500 units away or 12 seconds passed.
 */
void tweester_act_hide(void) {
    f32 shrinkTimer = 60.0f - o->oTimer;

    if (shrinkTimer >= 0.0f)
        tweester_scale_and_moveq(q(shrinkTimer) / 60);
    else {
        cur_obj_become_intangible();
        if (cur_obj_lateral_dist_from_mario_to_home() > 2500.0f)
            o->oAction = TWEESTER_ACT_IDLE;
        if (o->oTimer > 360)
            o->oAction = TWEESTER_ACT_IDLE;
    }
}

// Array of Tweester action functions.
void (*sTweesterActions[])(void) = { tweester_act_idle, tweester_act_chase, tweester_act_hide };

/**
 * Loop behavior for Tweester.
 * Loads the hitbox and calls its relevant action.
 */
void bhv_tweester_loop(void) {
    obj_set_hitbox(o, &sTweesterHitbox);
    cur_obj_call_action_function(sTweesterActions);
    o->oInteractStatus = 0;
}

/**
 * Loop behavior for the particles Tweesters create.
 * Floats upwards semi-randomly.
 */
void bhv_tweester_sand_particle_loop(void) {
    o->oMoveAngleYaw += 0x3700;
    QMODFIELD(o, oForwardVel, += q(15.0f));
    QMODFIELD(o, oPosY, += q(22.0f));

    cur_obj_scaleq(random_q32() + q(1));

    if (o->oTimer == 0) {
        obj_translate_xz_random(o, 100.0f);
        o->oFaceAnglePitch = random_u16();
        o->oFaceAngleYaw = random_u16();
    }

    if (o->oTimer > 15)
        obj_mark_for_deletion(o);
}
