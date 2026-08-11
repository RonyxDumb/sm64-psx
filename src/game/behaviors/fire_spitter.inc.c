
static void fire_spitter_act_idle(void) {
    approach_q32_ptr(&o->header.gfx.scaleq[0], q(0.2), q(0.002));
    if (o->oTimer > 150 && QFIELD(o, oDistanceToMario) < q(800.0) && !(o->oMoveFlags & OBJ_MOVE_MASK_IN_WATER)) {
        o->oAction = FIRE_SPITTER_ACT_SPIT_FIRE;
        QSETFIELD(o,  oFireSpitterScaleVel, q(0.05));
    }
}

static void fire_spitter_act_spit_fire(void) {
    s32 scaleStatus;

    o->oMoveAngleYaw = o->oAngleToMario;

    // Increase scale by 0.05, 0.04, ..., -0.03. Then wait ~8 frames, then
    // starting moving scale by 0.05 each frame toward 0.1. The first time
    // it becomes below 0.15 during this latter portion, shoot fire.
	f32 fireSpitterScaleVel = FFIELD(o, oFireSpitterScaleVel);
    scaleStatus = obj_grow_then_shrink(&fireSpitterScaleVel, 0.15f, 0.1f);
	FSETFIELD(o, oFireSpitterScaleVel, fireSpitterScaleVel);
    if (scaleStatus != 0) {
        if (scaleStatus < 0) {
            o->oAction = FIRE_SPITTER_ACT_IDLE;
        } else {
            cur_obj_play_sound_2(SOUND_OBJ_FLAME_BLOWN);
            obj_spit_fire(0, 0, 0, 5.0f, MODEL_RED_FLAME_SHADOW, 20.0f, 15.0f, 0x1000);
        }
    }
}

void bhv_fire_spitter_update(void) {
    cur_obj_scaleq(o->header.gfx.scaleq[0]);
    QSETFIELD(o, oGraphYOffset, q(40));
    cur_obj_update_floor_and_walls();

    switch (o->oAction) {
        case FIRE_SPITTER_ACT_IDLE:
            fire_spitter_act_idle();
            break;
        case FIRE_SPITTER_ACT_SPIT_FIRE:
            fire_spitter_act_spit_fire();
            break;
    }

    cur_obj_move_standard(78);
}
