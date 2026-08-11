// wind.c.inc

void spawn_wind_particles(s16 pitch, s16 yaw) {
    s32 i;
    for (i = 0; i < 3; i++) {
        struct Object *wind = spawn_object(o, MODEL_MIST, bhvWind);
        wind->oMoveAngleYaw = yaw;
        wind->oMoveAnglePitch = pitch;
    }
}

void bhv_wind_loop(void) {
    s16 sp2E = 500;
    q32 sp28q = q(1);
    if (o->oTimer == 0) {
        o->oOpacity = 100;
        if (o->oMoveAnglePitch == 0) {
            obj_translate_xz_random(o, 900.0f);
            QMODFIELD(o, oPosX,  += sinqs(o->oMoveAngleYaw + 0x8000) * sp2E); // NOP as Pitch is 0
            QMODFIELD(o, oPosY,  += q(80 + random_s16_around_zero(200)));
            QMODFIELD(o, oPosZ,  += cosqs(o->oMoveAngleYaw + 0x8000) * sp2E); // -coss(a) * sp2E
            o->oMoveAngleYaw += random_s16_around_zero(4000);
            QSETFIELD(o, oForwardVel, random_q32() * 70 + q(50));
        } else {
            obj_translate_xz_random(o, 600.0f);
            QMODFIELD(o, oPosY, -= q(sp2E - 200)); // 300
            QSETFIELD(o, oVelY, random_q32() * 30 + q(50));
            o->oMoveAngleYaw = random_u16();
            QSETFIELD(o,  oForwardVel, q(10));
        }
        obj_set_billboard(o);
        cur_obj_scaleq(sp28q);
    }
    if (o->oTimer > 8)
        obj_mark_for_deletion(o);
    o->oFaceAnglePitch += 4000.0f + 2000.0f * random_float();
    o->oFaceAngleYaw += 4000.0f + 2000.0f * random_float();
    cur_obj_move_using_fvel_and_gravity();
}
