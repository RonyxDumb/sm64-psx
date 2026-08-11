// mr_i.c.inc

// this is actually the MrI particle loop function. piranha
// plant code later on reuses this function.
void bhv_piranha_particle_loop(void) {
    if (o->oTimer == 0) {
        FSETFIELD(o, oVelY, 20.0f + 20.0f * random_float());
        FSETFIELD(o, oForwardVel, 20.0f + 20.0f * random_float());
        o->oMoveAngleYaw = random_u16();
    }
    cur_obj_move_using_fvel_and_gravity();
}

void mr_i_piranha_particle_act_0(void) {
    cur_obj_scaleq(q(3.0f));
    QSETFIELD(o,  oForwardVel, q(20));
    cur_obj_update_floor_and_walls();
    if (0x8000 & o->oInteractStatus)
        o->oAction = 1;
    else if ((o->oTimer >= 101) || (o->oMoveFlags & OBJ_MOVE_HIT_WALL) || o->activeFlags & ACTIVE_FLAG_IN_DIFFERENT_ROOM) {
        obj_mark_for_deletion(o);
        spawn_mist_particles();
    }
}

void mr_i_piranha_particle_act_1(void) {
    s32 i;
    obj_mark_for_deletion(o);
    for (i = 0; i < 10; i++)
        spawn_object(o, MODEL_PURPLE_MARBLE, bhvPurpleParticle);
}

void (*sMrIParticleActions[])(void) = { mr_i_piranha_particle_act_0, mr_i_piranha_particle_act_1 };

void bhv_mr_i_particle_loop(void) {
    cur_obj_call_action_function(sMrIParticleActions);
}

void spawn_mr_i_particle(void) {
    struct Object *particle;
    q32 sp18q = o->header.gfx.scaleq[1];
    particle = spawn_object(o, MODEL_PURPLE_MARBLE, bhvMrIParticle);
    QMODFIELD(particle, oPosY, += 50 * sp18q);
    QMODFIELD(particle, oPosX, += qmul(sinqs(o->oMoveAngleYaw) * 90, sp18q));
    QMODFIELD(particle, oPosZ, += qmul(cosqs(o->oMoveAngleYaw) * 90, sp18q));
    cur_obj_play_sound_2(SOUND_OBJ_MRI_SHOOT);
}

void bhv_mr_i_body_loop(void) {
    obj_copy_pos_and_angle(o, o->parentObj);
    if (!(o->activeFlags & ACTIVE_FLAG_IN_DIFFERENT_ROOM)) {
        obj_copy_scale(o, o->parentObj);
        obj_set_parent_relative_pos(o, 0, 0, qtof(o->header.gfx.scaleq[1] * 100));
        obj_build_transform_from_pos_and_angle(o, 44, 15);
        obj_translate_local(o, 6, 44);
        o->oFaceAnglePitch = o->oMoveAnglePitch;
        QSETFIELD(o, oGraphYOffset, o->header.gfx.scaleq[1] * 100);
    }
    if (o->parentObj->oMrIUnk110 != 1)
        o->oAnimState = -1;
    else {
        o->oAnimState++;
        if (o->oAnimState == 15)
            o->parentObj->oMrIUnk110 = 0;
    }
    if (o->parentObj->activeFlags == ACTIVE_FLAG_DEACTIVATED)
        obj_mark_for_deletion(o);
}

void mr_i_act_3(void) {
    s16 sp36;
    s16 sp34;
    q32 sp30q;
    f32 sp2C;
    UNUSED u8 pad[8];
    q32 sp20q;
    s32 sp1Ci;
    if (o->oBehParams2ndByte)
        sp1Ci = 2;
    else
        sp1Ci = 1;
    if (o->oMrIUnk100 < 0)
        sp34 = 0x1000;
    else
        sp34 = -0x1000;
    sp2C = (o->oTimer + 1) / 96.0f;
    if (o->oTimer < 64) {
        sp36 = o->oMoveAngleYaw;
        o->oMoveAngleYaw += sp34 * coss(0x4000 * sp2C);
        if (sp36 < 0 && o->oMoveAngleYaw >= 0)
            cur_obj_play_sound_2(SOUND_OBJ2_MRI_SPINNING);
        o->oMoveAnglePitch = (1.0 - coss(0x4000 * sp2C)) * -0x4000;
        cur_obj_shake_yq(q(4.0f));
    } else if (o->oTimer < 96) {
        if (o->oTimer == 64)
            cur_obj_play_sound_2(SOUND_OBJ_MRI_DEATH);
        sp30q = q(o->oTimer - 63) / 32;
        o->oMoveAngleYaw += sp34 * coss(0x4000 * sp2C);
        o->oMoveAnglePitch = (1.0 - coss(0x4000 * sp2C)) * -0x4000;
        cur_obj_shake_yq(q(ONE - sp30q) * 4);
        sp20q = cosqs(0x4000 * sp30q / ONE) * 2 / 5 + q(0.6);
        cur_obj_scaleq(sp20q * sp1Ci);
    } else if (o->oTimer < 104) {
        // do nothing
    } else if (o->oTimer < 168) {
        if (o->oTimer == 104) {
            cur_obj_become_intangible();
            spawn_mist_particles();
            QSETFIELD(o, oMrISize, q(sp1Ci) * 3 / 5);
            if (o->oBehParams2ndByte) {
                QMODFIELD(o, oPosY, += q(100.0f));
                spawn_default_star(1370, 2000.0f, -320.0f);
                obj_mark_for_deletion(o);
            } else
                cur_obj_spawn_loot_blue_coin();
        }
        QMODFIELD(o, oMrISize, -= q(sp1Ci) / 5);
        if (QFIELD(o, oMrISize) < 0)
            QSETFIELD(o, oMrISize, q(0));
        cur_obj_scaleq(QFIELD(o, oMrISize));
    } else
        obj_mark_for_deletion(o);
}

void mr_i_act_2(void) {
    s16 sp1E;
    s16 sp1C;
    sp1E = o->oMoveAngleYaw;
    if (o->oTimer == 0) {
        if (o->oBehParams2ndByte)
            o->oMrIUnkF4 = 200;
        else
            o->oMrIUnkF4 = 120;
        o->oMrIUnkFC = 0;
        o->oMrIUnk100 = 0;
        o->oMrIUnk104 = 0;
    }
    obj_turn_toward_object(o, gMarioObject, 0x10, 0x800);
    obj_turn_toward_object(o, gMarioObject, 0x0F, 0x400);
    sp1C = sp1E - (s16)(o->oMoveAngleYaw);
    if (!sp1C) {
        o->oMrIUnkFC = 0;
        o->oMrIUnk100 = 0;
    } else if (sp1C > 0) {
        if (o->oMrIUnk100 > 0)
            o->oMrIUnkFC += sp1C;
        else
            o->oMrIUnkFC = 0;
        o->oMrIUnk100 = 1;
    } else {
        if (o->oMrIUnk100 < 0)
            o->oMrIUnkFC -= sp1C;
        else
            o->oMrIUnkFC = 0;
        o->oMrIUnk100 = -1;
    }
    if (!o->oMrIUnkFC)
        o->oMrIUnkF4 = 120;
    if (o->oMrIUnkFC > 1 << 16)
        o->oAction = 3;
    o->oMrIUnkF4 -= 1;
    if (!o->oMrIUnkF4) {
        o->oMrIUnkF4 = 120;
        o->oMrIUnkFC = 0;
    }
    if (o->oMrIUnkFC < 5000) {
        if (o->oMrIUnk104 == o->oMrIUnk108)
            o->oMrIUnk110 = 1;
        if (o->oMrIUnk104 == o->oMrIUnk108 + 20) {
            spawn_mr_i_particle();
            o->oMrIUnk104 = 0;
            o->oMrIUnk108 = (s32)(random_float() * 50.0f + 50.0f);
        }
        o->oMrIUnk104++;
    } else {
        o->oMrIUnk104 = 0;
        o->oMrIUnk108 = (s32)(random_float() * 50.0f + 50.0f);
    }
    if (QFIELD(o, oDistanceToMario) > q(800.0))
        o->oAction = 1;
}

void mr_i_act_1(void) {
    s16 sp1E;
    s16 sp1C;
    s16 sp1A;
    sp1E = obj_angle_to_object(o, gMarioObject);
    sp1C = abs_angle_diff(o->oMoveAngleYaw, sp1E);
    sp1A = abs_angle_diff(o->oMoveAngleYaw, gMarioObject->oFaceAngleYaw);
    if (o->oTimer == 0) {
        cur_obj_become_tangible();
        o->oMoveAnglePitch = 0;
        o->oMrIUnk104 = 30;
        o->oMrIUnk108 = random_float() * 20.0f;
        if (o->oMrIUnk108 & 1)
            o->oAngleVelYaw = -256;
        else
            o->oAngleVelYaw = 256;
    }
    if (sp1C < 1024 && sp1A > 0x4000) {
        if (QFIELD(o, oDistanceToMario) < q(700.0))
            o->oAction = 2;
        else
            o->oMrIUnk104++;
    } else {
        o->oMoveAngleYaw += o->oAngleVelYaw;
        o->oMrIUnk104 = 30;
    }
    if (o->oMrIUnk104 == o->oMrIUnk108 + 60)
        o->oMrIUnk110 = 1;
    if (o->oMrIUnk108 + 80 < o->oMrIUnk104) {
        o->oMrIUnk104 = 0;
        o->oMrIUnk108 = random_float() * 80.0f;
        spawn_mr_i_particle();
    }
}

void mr_i_act_0(void) {
#ifndef VERSION_JP
    obj_set_angle(o, 0, 0, 0);
#else
    o->oMoveAnglePitch = 0;
    o->oMoveAngleYaw = 0;
    o->oMoveAngleRoll = 0;
#endif
    cur_obj_scaleq(q(o->oBehParams2ndByte + 1));
    if (o->oTimer == 0)
        cur_obj_set_pos_to_home();
    if (QFIELD(o, oDistanceToMario) < q(1500.0))
        o->oAction = 1;
}

void (*sMrIActions[])(void) = { mr_i_act_0, mr_i_act_1, mr_i_act_2, mr_i_act_3 };

struct ObjectHitbox sMrIHitbox = {
    /* interactType: */ INTERACT_DAMAGE,
    /* downOffset: */ 0,
    /* damageOrCoinValue: */ 2,
    /* health: */ 2,
    /* numLootCoins: */ 5,
    /* radius: */ 80,
    /* height: */ 150,
    /* hurtboxRadius: */ 0,
    /* hurtboxHeight: */ 0,
};

void bhv_mr_i_loop(void) {
    obj_set_hitbox(o, &sMrIHitbox);
    cur_obj_call_action_function(sMrIActions);
    if (o->oAction != 3)
        if (QFIELD(o, oDistanceToMario) > q(3000.0) || o->activeFlags & ACTIVE_FLAG_IN_DIFFERENT_ROOM)
            o->oAction = 0;
    o->oInteractStatus = 0;
}
