// floating_platform.c.inc

q32 floating_platform_find_home_yq(void) {
    struct Surface *sp24;
    q32 sp20q;
    q32 sp1Cq;

    sp20q = find_water_levelq(QFIELD(o, oPosX), QFIELD(o, oPosZ));
    sp1Cq = find_floorq(QFIELD(o, oPosX), QFIELD(o, oPosY), QFIELD(o, oPosZ), &sp24);
    if (sp20q > sp1Cq + QFIELD(o, oFloatingPlatformUnkFC)) {
        o->oFloatingPlatformUnkF4 = 0;
        return sp20q + QFIELD(o, oFloatingPlatformUnkFC);
    } else {
        o->oFloatingPlatformUnkF4 = 1;
        return sp1Cq + QFIELD(o, oFloatingPlatformUnkFC);
    }
}

void floating_platform_act_0(void) {
	s16 sp6 =
        (s32) (gMarioObject->header.gfx.posi[0] - qtrunc(QFIELD(o, oPosX))) * cosqs(-1*o->oMoveAngleYaw) +
        (s32) (gMarioObject->header.gfx.posi[2] - qtrunc(QFIELD(o, oPosZ))) * sinqs(-1*o->oMoveAngleYaw);
    s16 sp4 =
        (s32) (gMarioObject->header.gfx.posi[2] - qtrunc(QFIELD(o, oPosZ))) * cosqs(-1*o->oMoveAngleYaw) -
        (s32) (gMarioObject->header.gfx.posi[0] - qtrunc(QFIELD(o, oPosX))) * sinqs(-1*o->oMoveAngleYaw);

    if (gMarioObject->platform == o) {
        o->oFaceAnglePitch = sp4 * 2;
        o->oFaceAngleRoll = -sp6 * 2;
        q32 velyq = QFIELD(o, oVelY) - ONE;
        if(velyq < 0) {
            velyq = 0;
        }
        QSETFIELD(o, oVelY, velyq);

        q32 unkf8q = QFIELD(o, oFloatingPlatformUnkF8) + velyq;
        if(unkf8q > q(90)) {
            unkf8q = q(90);
        }
        QSETFIELD(o, oFloatingPlatformUnkF8, unkf8q);
    } else {
        o->oFaceAnglePitch /= 2;
        o->oFaceAngleRoll /= 2;
        QMODFIELD(o, oFloatingPlatformUnkF8, -= q(5));
        QSETFIELD(o,  oVelY, q(10));
        if (QFIELD(o, oFloatingPlatformUnkF8) < 0)
            QSETFIELD(o,  oFloatingPlatformUnkF8, q(0));
    }

    FSETFIELD(o, oPosY, FFIELD(o, oHomeY) - 64.0f - FFIELD(o, oFloatingPlatformUnkF8) + sins(o->oFloatingPlatformUnk100 * 0x800) * 10.0f);
    o->oFloatingPlatformUnk100++;
    if (o->oFloatingPlatformUnk100 == 32)
        o->oFloatingPlatformUnk100 = 0;
}

void bhv_floating_platform_loop(void) {
    QSETFIELD(o, oHomeY, floating_platform_find_home_yq());
    if (o->oFloatingPlatformUnkF4 == 0)
        o->oAction = 0;
    else
        o->oAction = 1;

    switch (o->oAction) {
        case 0:
            floating_platform_act_0();
            break;

        case 1:
            QSETFIELD(o,  oPosY, QFIELD(o,  oHomeY));
            break;
    }
}
