// collide_particles.c.inc

static s16 sTinyTriMovementParams[] = { 0xD000, 0,      0x3000, 0,      0xDE67, 0x2199,
                                        0x2199, 0x2199, 0xDE67, 0xDE67, 0x2199, 0xDE67 };

static s16 sTinyStarMovementParams[] = { 0xE000, 0,      0,      0,      0x2000, 0,      0xE99A,
                                         0x1666, 0x1666, 0x1666, 0xE99A, 0xE99A, 0x1666, 0xE99A };

void bhv_punch_tiny_triangle_loop(void) {
    s16 sp1E;
    if (o->oTimer == 0) {
        sp1E = o->oMoveAngleYaw;
        QSETFIELD(o, oCollisionParticleUnkF4, q(1.28));
        cur_obj_set_pos_relative(gMarioObject, 0.0f, 60.0f, 100.0f);
        o->oMoveAngleYaw = sp1E;
    }
    cur_obj_move_using_fvel_and_gravity();
    o->oAnimState = 5;
    cur_obj_scaleq(QFIELD(o, oCollisionParticleUnkF4));
    QMODFIELD(o, oCollisionParticleUnkF4, -= q(0.2));
    if (gDebugInfo[4][0] + 6 < o->oTimer)
        obj_mark_for_deletion(o);
}

void bhv_punch_tiny_triangle_init(void) {
    s32 i;
    struct Object *triangle;
    for (i = 0; i < 6; i++) {
        triangle = spawn_object(o, MODEL_DIRT_ANIMATION, bhvPunchTinyTriangle);
        triangle->oMoveAngleYaw = gMarioObject->oMoveAngleYaw + sTinyTriMovementParams[2 * i] + 0x8000;
        QSETFIELD(triangle, oVelY, sinqs(sTinyTriMovementParams[2 * i + 1]) * 25);
        QSETFIELD(triangle, oForwardVel, cosqs(sTinyTriMovementParams[2 * i + 1]) * 25);
    }
}

void bhv_wall_tiny_star_particle_loop(void) {
    s16 sp1E;
    if (o->oTimer == 0) {
        sp1E = o->oMoveAngleYaw;
        QSETFIELD(o, oCollisionParticleUnkF4, q(0.28f));
        cur_obj_set_pos_relative(gMarioObject, 0.0f, 30.0f, 110.0f);
        o->oMoveAngleYaw = sp1E;
    }
    cur_obj_move_using_fvel_and_gravity();
    o->oAnimState = 4;
    cur_obj_scaleq(QFIELD(o, oCollisionParticleUnkF4));
    QMODFIELD(o, oCollisionParticleUnkF4, -= q(0.015));
}

void bhv_tiny_star_particles_init(void) {
    s32 i;
    UNUSED s32 unused;
    struct Object *particle;
    for (i = 0; i < 7; i++) {
        particle = spawn_object(o, MODEL_CARTOON_STAR, bhvWallTinyStarParticle);
        particle->oMoveAngleYaw = gMarioObject->oMoveAngleYaw + sTinyStarMovementParams[2 * i] + 0x8000;
        QSETFIELD(particle, oVelY, sinqs(sTinyStarMovementParams[2 * i + 1]) * 25);
        QSETFIELD(particle, oForwardVel, cosqs(sTinyStarMovementParams[2 * i + 1]) * 25);
    }
}

void bhv_pound_tiny_star_particle_loop(void) {
    if (o->oTimer == 0) {
        QSETFIELD(o, oCollisionParticleUnkF4, q(0.28));
        QSETFIELD(o,  oForwardVel, q(25));
        QMODFIELD(o, oPosY, -= q(20.0f));
        QSETFIELD(o,  oVelY, q(14));
    }
    cur_obj_move_using_fvel_and_gravity();
    o->oAnimState = 4;
    cur_obj_scaleq(QFIELD(o, oCollisionParticleUnkF4));
    QMODFIELD(o, oCollisionParticleUnkF4, -= q(0.015));
}

void bhv_pound_tiny_star_particle_init(void) {
    s32 sp24;
    s32 sp20 = 8;
    struct Object *particle;
    for (sp24 = 0; sp24 < sp20; sp24++) {
        particle = spawn_object(o, MODEL_CARTOON_STAR, bhvPoundTinyStarParticle);
        particle->oMoveAngleYaw = (sp24 * 65536) / sp20;
    }
}
