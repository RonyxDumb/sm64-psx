// exclamation_box.c.inc

struct ObjectHitbox sExclamationBoxHitbox = {
    /* interactType: */ INTERACT_BREAKABLE,
    /* downOffset: */ 5,
    /* damageOrCoinValue: */ 0,
    /* health: */ 1,
    /* numLootCoins: */ 0,
    /* radius: */ 40,
    /* height: */ 30,
    /* hurtboxRadius: */ 40,
    /* hurtboxHeight: */ 30,
};

struct Struct802C0DF0 sExclamationBoxContents[] = { { 0, 0, 0, MODEL_MARIOS_WING_CAP, bhvWingCap },
                                                    { 1, 0, 0, MODEL_MARIOS_METAL_CAP, bhvMetalCap },
                                                    { 2, 0, 0, MODEL_MARIOS_CAP, bhvVanishCap },
                                                    { 3, 0, 0, MODEL_KOOPA_SHELL, bhvKoopaShell },
                                                    { 4, 0, 0, MODEL_YELLOW_COIN,
                                                      bhvSingleCoinGetsSpawned },
                                                    { 5, 0, 0, MODEL_NONE, bhvThreeCoinsSpawn },
                                                    { 6, 0, 0, MODEL_NONE, bhvTenCoinsSpawn },
                                                    { 7, 0, 0, MODEL_1UP, bhv1upWalking },
                                                    { 8, 0, 0, MODEL_STAR, bhvSpawnedStar },
                                                    { 9, 0, 0, MODEL_1UP, bhv1upRunningAway },
                                                    { 10, 0, 1, MODEL_STAR, bhvSpawnedStar },
                                                    { 11, 0, 2, MODEL_STAR, bhvSpawnedStar },
                                                    { 12, 0, 3, MODEL_STAR, bhvSpawnedStar },
                                                    { 13, 0, 4, MODEL_STAR, bhvSpawnedStar },
                                                    { 14, 0, 5, MODEL_STAR, bhvSpawnedStar },
                                                    { 99, 0, 0, 0, NULL } };

void bhv_rotating_exclamation_box_loop(void) {
    if (o->parentObj->oAction != 1)
        obj_mark_for_deletion(o);
}

void exclamation_box_act_0(void) {
    if (o->oBehParams2ndByte < 3) {
        o->oAnimState = o->oBehParams2ndByte;
        if ((save_file_get_flags() & sCapSaveFlags[o->oBehParams2ndByte])
            || ((o->oBehParams >> 24) & 0xFF) != 0)
            o->oAction = 2;
        else
            o->oAction = 1;
    } else {
        o->oAnimState = 3;
        o->oAction = 2;
    }
}

void exclamation_box_act_1(void) {
    cur_obj_become_intangible();
    if (o->oTimer == 0) {
        spawn_object(o, MODEL_EXCLAMATION_POINT, bhvRotatingExclamationMark);
        cur_obj_set_model(MODEL_EXCLAMATION_BOX_OUTLINE);
    }
    if ((save_file_get_flags() & sCapSaveFlags[o->oBehParams2ndByte])
        || ((o->oBehParams >> 24) & 0xFF) != 0) {
        o->oAction = 2;
        cur_obj_set_model(MODEL_EXCLAMATION_BOX);
    }
}

void exclamation_box_act_2(void) {
    obj_set_hitbox(o, &sExclamationBoxHitbox);
    if (o->oTimer == 0) {
        cur_obj_unhide();
        cur_obj_become_tangible();
        o->oInteractStatus = 0;
        QSETFIELD(o, oPosY, QFIELD(o, oHomeY));
        QSETFIELD(o, oGraphYOffset, 0);
    }
    if (cur_obj_was_attacked_or_ground_pounded()) {
        cur_obj_become_intangible();
        o->oExclamationBoxUnkFC = 0x4000;
        QSETFIELD(o, oVelY, q(30));
        QSETFIELD(o, oGravity, q(-8));
        QSETFIELD(o, oFloorHeight, QFIELD(o, oPosY));
        o->oAction = 3;
#if ENABLE_RUMBLE
        queue_rumble_data(5, 80);
#endif
    }
    load_object_collision_model();
}

void exclamation_box_act_3(void) {
    UNUSED s32 unused;
    cur_obj_move_using_fvel_and_gravity();
    if (QFIELD(o, oVelY) < 0) {
        QSETFIELD(o, oVelY, 0);
        QSETFIELD(o, oGravity, 0);
    }
    QSETFIELD(o, oExclamationBoxUnkF8, (sinqs(o->oExclamationBoxUnkFC) + QONE) / 2);
    QSETFIELD(o, oExclamationBoxUnkF4, (-sinqs(o->oExclamationBoxUnkFC) + QONE) / 2 + QONE);
    QSETFIELD(o, oGraphYOffset, (-sinqs(o->oExclamationBoxUnkFC) + QONE) * 26);
    o->oExclamationBoxUnkFC += 0x1000;
    o->header.gfx.scaleq[0] = QFIELD(o, oExclamationBoxUnkF4) * 2;
    o->header.gfx.scaleq[1] = QFIELD(o, oExclamationBoxUnkF8) * 2;
    o->header.gfx.scaleq[2] = QFIELD(o, oExclamationBoxUnkF4) * 2;
    if (o->oTimer == 7)
        o->oAction = 4;
}

void exclamation_box_spawn_contents(struct Struct802C0DF0 *a0, u8 a1) {
    struct Object *sp1C = NULL;

    while (a0->unk0 != 99) {
        if (a1 == a0->unk0) {
            sp1C = spawn_object(o, a0->model, a0->behavior);
            QSETFIELD(sp1C, oVelY, q(20));
            QSETFIELD(sp1C, oForwardVel, q(3));
            sp1C->oMoveAngleYaw = gMarioObject->oMoveAngleYaw;
            o->oBehParams |= a0->unk2 << 24;
            if (a0->model == 122)
                o->oFlags |= 0x4000;
            break;
        }
        a0++;
    }
}

void exclamation_box_act_4(void) {
    exclamation_box_spawn_contents(sExclamationBoxContents, o->oBehParams2ndByte);
    spawn_mist_particles_variable(0, 0, 46.0f);
    spawn_triangle_break_particles(20, MODEL_CARTOON_STAR, 0.3f, o->oAnimState);
    create_sound_spawner(SOUND_GENERAL_BREAK_BOX);
    if (o->oBehParams2ndByte < 3) {
        o->oAction = 5;
        cur_obj_hide();
    } else
        obj_mark_for_deletion(o);
}

void exclamation_box_act_5(void) {
    if (o->oTimer > 300)
        o->oAction = 2;
}

void (*sExclamationBoxActions[])(void) = { exclamation_box_act_0, exclamation_box_act_1,
                                           exclamation_box_act_2, exclamation_box_act_3,
                                           exclamation_box_act_4, exclamation_box_act_5 };

void bhv_exclamation_box_loop(void) {
    cur_obj_scaleq(q(2.0f));
    cur_obj_call_action_function(sExclamationBoxActions);
}
