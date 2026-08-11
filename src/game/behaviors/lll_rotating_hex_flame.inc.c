// lll_rotating_hex_flame.c.inc

void bhv_lll_rotating_hex_flame_loop(void) {
    f32 sp24 = FFIELD(o, oLllRotatingHexFlameUnkF4);
    f32 sp20 = FFIELD(o, oLllRotatingHexFlameUnkF8);
    f32 sp1C = FFIELD(o, oLllRotatingHexFlameUnkFC);
    cur_obj_set_pos_relative(o->parentObj, sp24, sp20, sp1C);
    QSETFIELD(o, oPosY, QFIELD(o->parentObj, oPosY) + q(100));
    if (o->parentObj->oAction == 3)
        obj_mark_for_deletion(o);
}

void fire_bar_spawn_flames(s16 a0) {
    struct Object *sp2C;
    s32 i;
    s32 sp20;
    q32 sp1Cq = sinqs(a0) * 200;
    q32 sp18q = cosqs(a0) * 200;
    sp20 = (o->oBehParams2ndByte == 0) ? 4 : 3;
    for (i = 0; i < sp20; i++) {
        sp2C = spawn_object(o, MODEL_RED_FLAME, bhvLllRotatingHexFlame);
        QMODFIELD(sp2C, oLllRotatingHexFlameUnkF4, += sp1Cq);
        QSETFIELD(sp2C, oLllRotatingHexFlameUnkF8, QFIELD(o, oPosY) - q(200));
        QMODFIELD(sp2C, oLllRotatingHexFlameUnkFC, += sp18q);
        obj_scale_xyzq(sp2C, q(6), q(6), q(6));
        sp1Cq += sinqs(a0) * 150;
        sp18q += cosqs(a0) * 150;
    }
}

void fire_bar_act_0(void) {
    if (QFIELD(o, oDistanceToMario) < q(3000.0))
        o->oAction = 1;
}

void fire_bar_act_1(void) {
    fire_bar_spawn_flames(0);
    fire_bar_spawn_flames(-0x8000);
    o->oAngleVelYaw = 0;
    o->oMoveAngleYaw = 0;
    o->oAction = 2;
}

void fire_bar_act_2(void) {
    o->oAngleVelYaw = -0x100;
    o->oMoveAngleYaw += o->oAngleVelYaw;
    if (QFIELD(o, oDistanceToMario) > q(3200.0))
        o->oAction = 3;
}

void fire_bar_act_3(void) {
    o->oAction = 0;
}

void (*sRotatingCwFireBarsActions[])(void) = { fire_bar_act_0, fire_bar_act_1,
                                               fire_bar_act_2, fire_bar_act_3 };

void bhv_lll_rotating_block_fire_bars_loop(void) {
    cur_obj_call_action_function(sRotatingCwFireBarsActions);
    if (o->oBehParams2ndByte == 0)
        load_object_collision_model();
}
