// tree_particles.c.inc

void bhv_tree_snow_or_leaf_loop(void) {
    cur_obj_update_floor_height();
    if (o->oTimer == 0) {
        o->oAngleVelPitch = (random_float() - 0.5) * 0x1000;
        o->oAngleVelRoll = (random_float() - 0.5) * 0x1000;
        o->oTreeSnowOrLeafUnkF8 = 4;
        o->oTreeSnowOrLeafUnkFC = random_float() * 0x400 + 0x600;
    }
    if (QFIELD(o, oPosY) < QFIELD(o, oFloorHeight))
        obj_mark_for_deletion(o);
    if (QFIELD(o, oFloorHeight) < q(FLOOR_LOWER_LIMIT))
        obj_mark_for_deletion(o);
    if (o->oTimer > 100)
        obj_mark_for_deletion(o);
    if (gPrevFrameObjectCount > (OBJECT_POOL_CAPACITY - 28))
        obj_mark_for_deletion(o);
    o->oFaceAnglePitch += o->oAngleVelPitch;
    o->oFaceAngleRoll += o->oAngleVelRoll;
    QMODFIELD(o, oVelY, -= q(3));
        QSETFIELD(o, oVelY, q(-8));
    if (QFIELD(o, oForwardVel) > 0)
        QMODFIELD(o, oForwardVel, -= q(0.3));
    else
        QSETFIELD(o, oForwardVel, 0);
    FMODFIELD(o, oPosX, += sins(o->oMoveAngleYaw) * sins(o->oTreeSnowOrLeafUnkF4) * o->oTreeSnowOrLeafUnkF8);
    FMODFIELD(o, oPosZ, += coss(o->oMoveAngleYaw) * sins(o->oTreeSnowOrLeafUnkF4) * o->oTreeSnowOrLeafUnkF8);
    o->oTreeSnowOrLeafUnkF4 += o->oTreeSnowOrLeafUnkFC;
    QMODFIELD(o, oPosY, += QFIELD(o, oVelY));
}

void bhv_snow_leaf_particle_spawn_init(void) {
    struct Object *obj; // Either snow or leaf
    UNUSED s32 unused;
    s32 isSnow;
    q32 scaleq;
    UNUSED s32 unused2;
    gMarioObject->oActiveParticleFlags &= ~0x2000;
    if (gCurrLevelNum == LEVEL_CCM || gCurrLevelNum == LEVEL_SL)
        isSnow = 1;
    else
        isSnow = 0;
    if (isSnow) {
        if (random_float() < 0.5) {
            obj = spawn_object(o, MODEL_WHITE_PARTICLE_DL, bhvTreeSnow);
            scaleq = random_q32();
            obj_scale_xyzq(obj, scaleq, scaleq, scaleq);
            obj->oMoveAngleYaw = random_u16();
            QSETFIELD(obj, oForwardVel, random_q32() * 5);
            QSETFIELD(obj, oVelY, random_q32() * 15);
        }
    } else {
        if (random_float() < 0.3) {
            obj = spawn_object(o, MODEL_LEAVES, bhvTreeLeaf);
            scaleq = random_q32() * 3;
            obj_scale_xyzq(obj, scaleq, scaleq, scaleq);
            obj->oMoveAngleYaw = random_u16();
            QSETFIELD(obj, oForwardVel, random_q32() * 5 + q(5));
            QSETFIELD(obj, oVelY, random_q32() * 15);
            obj->oFaceAnglePitch = random_u16();
            obj->oFaceAngleRoll = random_u16();
            obj->oFaceAngleYaw = random_u16();
        }
    }
}
