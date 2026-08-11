// falling_rising_platform.c.inc

void bhv_squishable_platform_loop(void) {
    o->header.gfx.scaleq[1] = (sinqs(o->oBitfsPlatformTimer) + QONE) * 3 / 10 + q(0.4);
    o->oBitfsPlatformTimer += 0x80;
}

void bhv_bitfs_sinking_platform_loop(void) {
    QMODFIELD(o, oPosY, -=
        qmul(sinqs(o->oBitfsPlatformTimer), q(0.58))); //! f32 double conversion error accumulates on Wii VC causing the platform to rise up
    o->oBitfsPlatformTimer += 0x100;
}

// TODO: Named incorrectly. fix
void bhv_ddd_moving_pole_loop(void) {
    obj_copy_pos_and_angle(o, o->parentObj);
}

void bhv_bitfs_sinking_cage_platform_loop(void) {
    if (o->oBehParams2ndByte != 0) {
        if (o->oTimer == 0)
            QMODFIELD(o, oPosY, -= q(300));
        QMODFIELD(o, oPosY, += sinqs(o->oBitfsPlatformTimer) * 7);
    } else
        QMODFIELD(o, oPosY, -= sinqs(o->oBitfsPlatformTimer) * 3);
    o->oBitfsPlatformTimer += 0x100;
}
