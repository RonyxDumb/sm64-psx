// flame.inc.c

void bhv_small_piranha_flame_loop(void) {
    if ((u16)(o->oBehParams >> 16) == 0) {
        if (o->oTimer > 0) {
            obj_mark_for_deletion(o);
        } else {
            q32 rndq = random_q32() - q(0.5);
            o->header.gfx.scaleq[1] = o->header.gfx.scaleq[2] * (q(1.0f) + qmul(q(0.7), rndq));
            o->header.gfx.scaleq[0] = o->header.gfx.scaleq[2] * (q(0.9f) - qmul(q(0.5), rndq));

            o->oAnimState = random_u16();
        }
    } else {
        cur_obj_update_floor_and_walls();
        if (APPROACH_F32_FIELD(o, oSmallPiranhaFlameStartSpeed, FFIELD(o, oSmallPiranhaFlameEndSpeed), 0.6f)) {
            cur_obj_rotate_yaw_toward(o->oAngleToMario, 0x200);
        }

        obj_compute_vel_from_move_pitchq(QFIELD(o, oSmallPiranhaFlameStartSpeed));
        cur_obj_move_standard(-78);
        spawn_object_with_scale(o, o->oSmallPiranhaFlameModel, bhvSmallPiranhaFlame,
                                qtof(o->header.gfx.scaleq[0] * 2 / 5));

        if (o->oTimer > o->oSmallPiranhaFlameNextFlameTimer) {
            spawn_object_relative_with_scale(1, 0, FFIELD(o, oGraphYOffset), 0, qtof(o->header.gfx.scaleq[0]), o,
                                             o->oSmallPiranhaFlameModel, bhvFlyguyFlame);
            o->oSmallPiranhaFlameNextFlameTimer = random_linear_offset(8, 15);
            o->oTimer = 0;
        }

        obj_check_attacks(&sPiranhaPlantFireHitbox, o->oAction);
        QMODFIELD(o, oSmallPiranhaFlameSpeed, += QFIELD(o, oSmallPiranhaFlameStartSpeed));

        if (FFIELD(o, oSmallPiranhaFlameSpeed) > 1500.0f || (o->oMoveFlags & (OBJ_MOVE_HIT_WALL | OBJ_MOVE_MASK_IN_WATER))) {
            obj_die_if_health_non_positive();
        }
    }

    QSETFIELD(o, oGraphYOffset, 15 * o->header.gfx.scaleq[1]);
}

void bhv_fly_guy_flame_loop(void) {
    cur_obj_move_using_fvel_and_gravity();

    if (approach_q32_ptr(&o->header.gfx.scaleq[0], 0, q(0.6))) {
        obj_mark_for_deletion(o);
    }

    cur_obj_scaleq(o->header.gfx.scaleq[0]);
}
