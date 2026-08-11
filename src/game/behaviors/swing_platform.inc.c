
void bhv_swing_platform_init(void) {
    FSETFIELD(o, oSwingPlatformAngle, 0x2000);
}

void bhv_swing_platform_update(void) {
    s32 startRoll = o->oFaceAngleRoll;

    if (o->oFaceAngleRoll < 0) {
        QMODFIELD(o, oSwingPlatformSpeed, += q(4.0f));
    } else {
        QMODFIELD(o, oSwingPlatformSpeed, -= q(4.0f));
    }

    QMODFIELD(o, oSwingPlatformAngle, += QFIELD(o, oSwingPlatformSpeed));
    o->oFaceAngleRoll = FFIELD(o, oSwingPlatformAngle);
    o->oAngleVelRoll = o->oFaceAngleRoll - startRoll;
}
