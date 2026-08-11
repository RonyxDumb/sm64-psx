// bowser_bomb.c.inc

void bhv_bowser_bomb_loop(void) {
    if (obj_check_if_collided_with_object(o, gMarioObject) == 1) {
        o->oInteractStatus &= ~INT_STATUS_INTERACTED;
        spawn_object(o, MODEL_EXPLOSION, bhvExplosion);
        o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
    }

    if (o->oInteractStatus & INT_STATUS_HIT_MINE)
    {
        spawn_object(o, MODEL_BOWSER_FLAMES, bhvBowserBombExplosion);
        create_sound_spawner(SOUND_GENERAL_BOWSER_BOMB_EXPLOSION);
        set_camera_shake_from_pointq(SHAKE_POS_LARGE, QFIELD(o, oPosX), QFIELD(o, oPosY), QFIELD(o, oPosZ));
        o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
    }

    set_object_visibility(o, 7000);
}

void bhv_bowser_bomb_explosion_loop(void) {
    struct Object *mineSmoke;

    cur_obj_scaleq(q(o->oTimer) * 9 / 14 + q(1));
    if ((o->oTimer % 4 == 0) && (o->oTimer < 20)) {
        mineSmoke = spawn_object(o, MODEL_BOWSER_SMOKE, bhvBowserBombSmoke);
        FMODFIELD(mineSmoke, oPosX, += random_float() * 600.0f - 400.0f);
        FMODFIELD(mineSmoke, oPosZ, += random_float() * 600.0f - 400.0f);
        FMODFIELD(mineSmoke, oVelY, += random_float() * 10.0f);
    }

    if (o->oTimer % 2 == 0)
        o->oAnimState++;
    if (o->oTimer == 28)
        o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
}

void bhv_bowser_bomb_smoke_loop(void) {
    cur_obj_scaleq(q(o->oTimer) * 9 / 14 + q(1));
    if (o->oTimer % 2 == 0)
        o->oAnimState++;

    o->oOpacity -= 10;
    if (o->oOpacity < 10)
        o->oOpacity = 0;

    QMODFIELD(o, oPosY, += QFIELD(o, oVelY));

    if (o->oTimer == 28)
        o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
}
