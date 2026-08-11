// seaweed.c.inc

void bhv_seaweed_init(void) {
    o->header.gfx.animInfo.animFrame = random_float() * 80.0f;
}

void bhv_seaweed_bundle_init(void) {
    struct Object *seaweed;

    seaweed = spawn_object(o, MODEL_SEAWEED, bhvSeaweed);
    seaweed->oFaceAngleYaw = 14523;
    seaweed->oFaceAnglePitch = 5500;
    seaweed->oFaceAngleRoll = 9600;
    seaweed->header.gfx.scaleq[0] = QONE;
    seaweed->header.gfx.scaleq[1] = QONE;
    seaweed->header.gfx.scaleq[2] = QONE;
    //! gfx.animFrame uninitialized

    seaweed = spawn_object(o, MODEL_SEAWEED, bhvSeaweed);
    seaweed->oFaceAngleYaw = 41800;
    seaweed->oFaceAnglePitch = 6102;
    seaweed->oFaceAngleRoll = 0;
    seaweed->header.gfx.scaleq[0] = q(0.8);
    seaweed->header.gfx.scaleq[1] = q(0.9);
    seaweed->header.gfx.scaleq[2] = q(0.8);
    seaweed->header.gfx.animInfo.animFrame = random_float() * 80.0f;

    seaweed = spawn_object(o, MODEL_SEAWEED, bhvSeaweed);
    seaweed->oFaceAngleYaw = 40500;
    seaweed->oFaceAnglePitch = 8700;
    seaweed->oFaceAngleRoll = 4100;
    seaweed->header.gfx.scaleq[0] = q(0.8);
    seaweed->header.gfx.scaleq[1] = q(0.8);
    seaweed->header.gfx.scaleq[2] = q(0.8);
    seaweed->header.gfx.animInfo.animFrame = random_float() * 80.0f;

    seaweed = spawn_object(o, MODEL_SEAWEED, bhvSeaweed);
    seaweed->oFaceAngleYaw = 57236;
    seaweed->oFaceAnglePitch = 9500;
    seaweed->oFaceAngleRoll = 0;
    seaweed->header.gfx.scaleq[0] = q(1.2);
    seaweed->header.gfx.scaleq[1] = q(1.2);
    seaweed->header.gfx.scaleq[2] = q(1.2);
    seaweed->header.gfx.animInfo.animFrame = random_float() * 80.0f;
}
