// break_particles.c.inc

void spawn_triangle_break_particles(s16 numTris, s16 triModel, f32 triSize, s16 triAnimState) {
    struct Object *triangle;
    s32 i;
    for (i = 0; i < numTris; i++) {
        triangle = spawn_object(o, triModel, bhvBreakBoxTriangle);
        triangle->oAnimState = triAnimState;
        QMODFIELD(triangle, oPosY, += q(100));
        triangle->oMoveAngleYaw = random_u16();
        triangle->oFaceAngleYaw = triangle->oMoveAngleYaw;
        triangle->oFaceAnglePitch = random_u16();
        QSETFIELD(triangle, oVelY, random_s16_around_zero(50));
        if (triModel == MODEL_DIRT_ANIMATION || triModel == MODEL_SL_CRACKED_ICE_CHUNK) {
            triangle->oAngleVelPitch = 0xF00;
            triangle->oAngleVelYaw = 0x500;
            QSETFIELD(triangle, oForwardVel, q(30));
        } else {
            triangle->oAngleVelPitch = 0x80 * (s32)(random_float() + 50.0f);
            QSETFIELD(triangle, oForwardVel, q(30));
        }
        obj_scale(triangle, triSize);
    }
}
