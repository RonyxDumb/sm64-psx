// mushroom_1up.c.inc

void bhv_1up_interact(void) {
    UNUSED s32 sp1C;

    if (obj_check_if_collided_with_object(o, gMarioObject) == 1) {
        play_sound(SOUND_GENERAL_COLLECT_1UP, gGlobalSoundSource);
        gMarioState->numLives++;
        o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
#if ENABLE_RUMBLE
        queue_rumble_data(5, 80);
#endif
    }
}

void bhv_1up_common_init(void) {
    o->oMoveAnglePitch = -0x4000;
    QSETFIELD(o, oGravity, q(3.0f));
    QSETFIELD(o,  oFriction, q(1));
    QSETFIELD(o,  oBuoyancy, q(1));
}

void bhv_1up_init(void) {
    bhv_1up_common_init();
    if (o->oBehParams2ndByte == 1) {
        if (!(save_file_get_flags() & (SAVE_FLAG_HAVE_KEY_1 | SAVE_FLAG_UNLOCKED_BASEMENT_DOOR))) {
            o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
        }
    } else if (o->oBehParams2ndByte == 2) {
        if (!(save_file_get_flags() & (SAVE_FLAG_HAVE_KEY_2 | SAVE_FLAG_UNLOCKED_UPSTAIRS_DOOR))) {
            o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
        }
    }
}

void one_up_loop_in_air(void) {
    if (o->oTimer < 5) {
        QSETFIELD(o, oVelY, q(40));
    } else {
        o->oAngleVelPitch = -0x1000;
        o->oMoveAnglePitch += o->oAngleVelPitch;
        QSETFIELD(o, oVelY, cosqs(o->oMoveAnglePitch) * 30 + q(2));
        QSETFIELD(o, oForwardVel, -sinqs(o->oMoveAnglePitch) * 30);
    }
}

void pole_1up_move_towards_mario(void) {
    s32 sp34 = gMarioObject->header.gfx.posi[0] - qtrunc(QFIELD(o, oPosX));
    s32 sp30 = gMarioObject->header.gfx.posi[1] + 120 - qtrunc(QFIELD(o, oPosY));
    s32 sp2C = gMarioObject->header.gfx.posi[2] - qtrunc(QFIELD(o, oPosZ));
    s16 sp2A = atan2s(sqrtf(sqr(sp34) + sqr(sp2C)), sp30);

    obj_turn_toward_object(o, gMarioObject, 16, 0x1000);
    o->oMoveAnglePitch = approach_s16_symmetric(o->oMoveAnglePitch, sp2A, 0x1000);
    QSETFIELD(o, oVelY, sinqs(o->oMoveAnglePitch) * q(30));
    QSETFIELD(o, oForwardVel, cosqs(o->oMoveAnglePitch) * q(30));
    bhv_1up_interact();
}

void one_up_move_away_from_mario(s16 sp1A) {
    QSETFIELD(o, oForwardVel, q(8));
    o->oMoveAngleYaw = o->oAngleToMario + 0x8000;
    bhv_1up_interact();
    if (sp1A & 0x02)
        o->oAction = 2;

    if (!is_point_within_radius_of_mario(IFIELD(o, oPosX), IFIELD(o, oPosY), IFIELD(o, oPosZ), 3000))
        o->oAction = 2;
}

void bhv_1up_walking_loop(void) {
    object_step();

    switch (o->oAction) {
        case 0:
            if (o->oTimer >= 18)
                spawn_object(o, MODEL_NONE, bhvSparkleSpawn);

            if (o->oTimer == 0)
                play_sound(SOUND_GENERAL2_1UP_APPEAR, gGlobalSoundSource);

            one_up_loop_in_air();

            if (o->oTimer == 37) {
                cur_obj_become_tangible();
                o->oAction = 1;
                QSETFIELD(o,  oForwardVel, q(2));
            }
            break;

        case 1:
            if (o->oTimer > 300)
                o->oAction = 2;

            bhv_1up_interact();
            break;

        case 2:
            obj_flicker_and_disappear(o, 30);
            bhv_1up_interact();
            break;
    }

    set_object_visibility(o, 3000);
}

void bhv_1up_running_away_loop(void) {
    s16 sp26;

    sp26 = object_step();
    switch (o->oAction) {
        case 0:
            if (o->oTimer >= 18)
                spawn_object(o, MODEL_NONE, bhvSparkleSpawn);

            if (o->oTimer == 0)
                play_sound(SOUND_GENERAL2_1UP_APPEAR, gGlobalSoundSource);

            one_up_loop_in_air();

            if (o->oTimer == 37) {
                cur_obj_become_tangible();
                o->oAction = 1;
                QSETFIELD(o,  oForwardVel, q(8));
            }
            break;

        case 1:
            spawn_object(o, MODEL_NONE, bhvSparkleSpawn);
            one_up_move_away_from_mario(sp26);
            break;

        case 2:
            obj_flicker_and_disappear(o, 30);
            bhv_1up_interact();
            break;
    }

    set_object_visibility(o, 3000);
}

void sliding_1up_move(void) {
    s16 sp1E;

    sp1E = object_step();
    if (sp1E & 0x01) {
        QMODFIELD(o, oForwardVel, += q(25.0f));
        QSETFIELD(o, oVelY, q(0));
    } else {
        QSETFIELD(o, oForwardVel, QFIELD(o, oForwardVel) * 49 / 50);
    }

    if (QFIELD(o, oForwardVel) > q(40.0))
        QSETFIELD(o,  oForwardVel, q(40));

    if (!is_point_within_radius_of_mario(IFIELD(o, oPosX), IFIELD(o, oPosY), IFIELD(o, oPosZ), 5000))
        o->oAction = 2;
}

void bhv_1up_sliding_loop(void) {
    switch (o->oAction) {
        case 0:
            set_object_visibility(o, 3000);
            if (is_point_within_radius_of_mario(IFIELD(o, oPosX), IFIELD(o, oPosY), IFIELD(o, oPosZ), 1000))
                o->oAction = 1;
            break;

        case 1:
            sliding_1up_move();
            break;

        case 2:
            obj_flicker_and_disappear(o, 30);
            bhv_1up_interact();
            break;
    }

    bhv_1up_interact();
    spawn_object(o, MODEL_NONE, bhvSparkleSpawn);
}

void bhv_1up_loop(void) {
    bhv_1up_interact();
    set_object_visibility(o, 3000);
}

void bhv_1up_jump_on_approach_loop(void) {
    s16 sp26;

    switch (o->oAction) {
        case 0:
            if (is_point_within_radius_of_mario(IFIELD(o, oPosX), IFIELD(o, oPosY), IFIELD(o, oPosZ), 1000)) {
                QSETFIELD(o,  oVelY, q(40));
                o->oAction = 1;
            }
            break;

        case 1:
            sp26 = object_step();
            one_up_move_away_from_mario(sp26);
            spawn_object(o, MODEL_NONE, bhvSparkleSpawn);
            break;

        case 2:
            sp26 = object_step();
            bhv_1up_interact();
            obj_flicker_and_disappear(o, 30);
            break;
    }

    set_object_visibility(o, 3000);
}

void bhv_1up_hidden_loop(void) {
    s16 sp26;
    switch (o->oAction) {
        case 0:
            o->header.gfx.node.flags |= GRAPH_RENDER_INVISIBLE;
            if (o->o1UpHiddenUnkF4 == o->oBehParams2ndByte) {
                QSETFIELD(o,  oVelY, q(40));
                o->oAction = 3;
                o->header.gfx.node.flags &= ~GRAPH_RENDER_INVISIBLE;
                play_sound(SOUND_GENERAL2_1UP_APPEAR, gGlobalSoundSource);
            }
            break;

        case 1:
            sp26 = object_step();
            one_up_move_away_from_mario(sp26);
            spawn_object(o, MODEL_NONE, bhvSparkleSpawn);
            break;

        case 2:
            sp26 = object_step();
            bhv_1up_interact();
            obj_flicker_and_disappear(o, 30);
            break;

        case 3:
            sp26 = object_step();
            if (o->oTimer >= 18)
                spawn_object(o, MODEL_NONE, bhvSparkleSpawn);

            one_up_loop_in_air();

            if (o->oTimer == 37) {
                cur_obj_become_tangible();
                o->oAction = 1;
                QSETFIELD(o,  oForwardVel, q(8));
            }
            break;
    }
}

void bhv_1up_hidden_trigger_loop(void) {
    struct Object *sp1C;
    if (obj_check_if_collided_with_object(o, gMarioObject) == 1) {
        sp1C = cur_obj_nearest_object_with_behavior(bhvHidden1up);
        if (sp1C != NULL)
            sp1C->o1UpHiddenUnkF4++;

        o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
    }
}

void bhv_1up_hidden_in_pole_loop(void) {
    UNUSED s16 sp26;
    switch (o->oAction) {
        case 0:
            o->header.gfx.node.flags |= GRAPH_RENDER_INVISIBLE;
            if (o->o1UpHiddenUnkF4 == o->oBehParams2ndByte) {
                QSETFIELD(o,  oVelY, q(40));
                o->oAction = 3;
                o->header.gfx.node.flags &= ~GRAPH_RENDER_INVISIBLE;
                play_sound(SOUND_GENERAL2_1UP_APPEAR, gGlobalSoundSource);
            }
            break;

        case 1:
            pole_1up_move_towards_mario();
            sp26 = object_step();
            break;

        case 3:
            sp26 = object_step();
            if (o->oTimer >= 18)
                spawn_object(o, MODEL_NONE, bhvSparkleSpawn);

            one_up_loop_in_air();

            if (o->oTimer == 37) {
                cur_obj_become_tangible();
                o->oAction = 1;
                QSETFIELD(o,  oForwardVel, q(10));
            }
            break;
    }
}

void bhv_1up_hidden_in_pole_trigger_loop(void) {
    struct Object *sp1C;

    if (obj_check_if_collided_with_object(o, gMarioObject) == 1) {
        sp1C = cur_obj_nearest_object_with_behavior(bhvHidden1upInPole);
        if (sp1C != NULL) {
            sp1C->o1UpHiddenUnkF4++;
        }

        o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
    }
}

void bhv_1up_hidden_in_pole_spawner_loop(void) {
    s8 sp2F;

    if (is_point_within_radius_of_mario(IFIELD(o, oPosX), IFIELD(o, oPosY), IFIELD(o, oPosZ), 700)) {
        spawn_object_relative(2, 0, 50, 0, o, MODEL_1UP, bhvHidden1upInPole);
        for (sp2F = 0; sp2F < 2; sp2F++) {
            spawn_object_relative(0, 0, sp2F * -200, 0, o, MODEL_NONE, bhvHidden1upInPoleTrigger);
        }

        o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
    }
}
