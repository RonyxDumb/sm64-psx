// corkbox.c.inc
// TODO: This split seems weird. Investigate further?

void bhv_bobomb_bully_death_smoke_init(void) {
    QMODFIELD(o, oPosY, -= q(300));

    cur_obj_scaleq(q(10.0f));
}

void bhv_bobomb_explosion_bubble_init(void) {
    obj_scale_xyzq(o, q(2), q(2), q(1));

    o->oBobombExpBubGfxExpRateX = qtrunc(random_q32() * 2048) + 0x800;
    o->oBobombExpBubGfxExpRateY = qtrunc(random_q32() * 2048) + 0x800;
    o->oTimer = qtrunc(random_q32() * 10);
    ISETFIELD(o, oVelY, qtrunc(random_q32() * 4) + 4);
}

void bhv_bobomb_explosion_bubble_loop(void) {
    s32 waterYq = q(gMarioStates[0].waterLevel);

    o->header.gfx.scaleq[0] = sinqs(o->oBobombExpBubGfxScaleFacX) / 2 + q(2);
    o->oBobombExpBubGfxScaleFacX += o->oBobombExpBubGfxExpRateX;

    o->header.gfx.scaleq[1] = sinqs(o->oBobombExpBubGfxScaleFacY) / 2 + q(2);
    o->oBobombExpBubGfxScaleFacY += o->oBobombExpBubGfxExpRateY;

    if (QFIELD(o, oPosY) > waterYq) {
        o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
        QMODFIELD(o, oPosY, += q(5));
        spawn_object(o, MODEL_SMALL_WATER_SPLASH, bhvObjectWaterSplash);
    }

    if (o->oTimer >= 61)
        o->activeFlags = ACTIVE_FLAG_DEACTIVATED;

    QMODFIELD(o, oPosY, += QFIELD(o, oVelY));
    o->oTimer++;
}

void bhv_respawner_loop(void) {
    struct Object *spawnedObject;

    if (!is_point_within_radius_of_mario(IFIELD(o, oPosX), IFIELD(o, oPosY), IFIELD(o, oPosZ), IFIELD(o, oRespawnerMinSpawnDist))) {
        spawnedObject = spawn_object(o, o->oRespawnerModelToRespawn, o->oRespawnerBehaviorToRespawn);
        spawnedObject->oBehParams = o->oBehParams;
        o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
    }
}

void create_respawner(s32 model, const BehaviorScript *behToSpawn, s32 minSpawnDist) {
    struct Object *respawner = spawn_object_abs_with_rot(o, 0, MODEL_NONE, bhvRespawner, IFIELD(o, oHomeX), IFIELD(o, oHomeY), IFIELD(o, oHomeZ), 0, 0, 0);
    respawner->oBehParams = o->oBehParams;
    respawner->oRespawnerModelToRespawn = model;
    ISETFIELD(respawner, oRespawnerMinSpawnDist, minSpawnDist);
    respawner->oRespawnerBehaviorToRespawn = behToSpawn;
}
