// explosion.c.inc

void bhv_explosion_init(void) {
    create_sound_spawner(SOUND_GENERAL2_BOBOMB_EXPLOSION);
    set_environmental_camera_shake(SHAKE_ENV_EXPLOSION);

    o->oOpacity = 255;
}

void bhv_explosion_loop(void) {
    s32 i;

    if (o->oTimer == 9) {
        if (find_water_levelq(QFIELD(o, oPosX), QFIELD(o, oPosZ)) > QFIELD(o, oPosY)) {
            for (i = 0; i < 40; i++)
                spawn_object(o, MODEL_WHITE_PARTICLE_SMALL, bhvBobombExplosionBubble);
        } else
            spawn_object(o, MODEL_SMOKE, bhvBobombBullyDeathSmoke);

        o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
    }

    o->oOpacity -= 14;

    cur_obj_scaleq(q(o->oTimer) / 9 + q(1));
}
