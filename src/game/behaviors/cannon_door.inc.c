// cannon.c.inc

void bhv_cannon_closed_init(void) {
    struct Object *cannon;

    if (save_file_is_cannon_unlocked() == 1) {
        // If the cannon is open, spawn a cannon and despawn the object.
        cannon = spawn_object(o, MODEL_CANNON_BASE, bhvCannon);
        cannon->oBehParams2ndByte = o->oBehParams2ndByte;
        QSETFIELD(cannon,  oPosX, QFIELD(o,  oHomeX));
        QSETFIELD(cannon,  oPosY, QFIELD(o,  oHomeY));
        QSETFIELD(cannon,  oPosZ, QFIELD(o,  oHomeZ));

        o->oAction = CANNON_TRAP_DOOR_ACT_OPEN;
        o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
    }
}

void cannon_door_act_opening(void) {
    if (o->oTimer == 0)
        cur_obj_play_sound_2(SOUND_GENERAL_CANNON_UP);

    if (o->oTimer < 30) {
        QSETFIELD(o,  oVelY, q(-0.5));
        QMODFIELD(o, oPosY, += QFIELD(o, oVelY));
        QSETFIELD(o,  oVelX, q(0));
    } else {
        if (o->oTimer == 80) {
            bhv_cannon_closed_init();
            return;
        }

        QSETFIELD(o,  oVelX, q(4));
        QSETFIELD(o,  oVelY, q(0));
        QMODFIELD(o, oPosX, += QFIELD(o, oVelX));
    }
}

void bhv_cannon_closed_loop(void) {
    switch (o->oAction) {
        case CANNON_TRAP_DOOR_ACT_CLOSED:
            QSETFIELD(o,  oVelX, q(0));
            QSETFIELD(o,  oVelY, q(0));
            QSETFIELD(o,  oDrawingDistance, q(4000));

            if (save_file_is_cannon_unlocked() == 1)
                o->oAction = CANNON_TRAP_DOOR_ACT_CAM_ZOOM;
            break;

        case CANNON_TRAP_DOOR_ACT_CAM_ZOOM:
            if (o->oTimer == 60)
                o->oAction = CANNON_TRAP_DOOR_ACT_OPENING;

            QSETFIELD(o,  oDrawingDistance, q(20000));
            break;

        case CANNON_TRAP_DOOR_ACT_OPENING:
            cannon_door_act_opening();
            break;
    }
}
