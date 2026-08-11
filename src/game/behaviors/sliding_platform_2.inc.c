// sliding_platform_2.inc.c

static void const *sSlidingPlatform2CollisionData[] = {
    bits_seg7_collision_0701A9A0,
    bits_seg7_collision_0701AA0C,
    bitfs_seg7_collision_07015714,
    bitfs_seg7_collision_07015768,
    rr_seg7_collision_070295F8,
    rr_seg7_collision_0702967C,
    NULL,
    bitdw_seg7_collision_0700F688,
};

void bhv_sliding_plat_2_init(void) {
    s32 collisionDataIndex;

    collisionDataIndex = ((u16)(o->oBehParams >> 16) & 0x0380) >> 7;
    o->collisionData = segmented_to_virtual(sSlidingPlatform2CollisionData[collisionDataIndex]);
    FSETFIELD(o, oBackAndForthPlatformPathLength, 50.0f * ((u16)(o->oBehParams >> 16) & 0x003F));

    if (collisionDataIndex < 5 || collisionDataIndex > 6) {
        QSETFIELD(o,  oBackAndForthPlatformVel, q(15));
        if ((u16)(o->oBehParams >> 16) & 0x0040) {
            o->oMoveAngleYaw += 0x8000;
        }
    } else {
        QSETFIELD(o,  oBackAndForthPlatformVel, q(10));
        if ((u16)(o->oBehParams >> 16) & 0x0040) {
            QSETFIELD(o,  oBackAndForthPlatformDirection, q(-1));
        } else {
            QSETFIELD(o,  oBackAndForthPlatformDirection, q(1));
        }
    }
}

void bhv_sliding_plat_2_loop(void) {
    if (o->oTimer > 10) {
        QMODFIELD(o, oBackAndForthPlatformDistance, += QFIELD(o, oBackAndForthPlatformVel));
        if (CLAMP_F32_FIELD(o, oBackAndForthPlatformDistance, FFIELD(-o, oBackAndForthPlatformPathLength), 0.0f)) {
            QSETFIELD(o,  oBackAndForthPlatformVel, QFIELD(-o,  oBackAndForthPlatformVel));
            o->oTimer = 0;
        }
    }

    obj_perform_position_op(0);

    if (QFIELD(o, oBackAndForthPlatformDirection) != 0) {
        QSETFIELD(o, oPosY, QFIELD(o, oHomeY) + qmul(QFIELD(o, oBackAndForthPlatformDistance), QFIELD(o, oBackAndForthPlatformDirection)));
    } else {
        obj_set_dist_from_homeq(QFIELD(o, oBackAndForthPlatformDistance));
    }

    obj_perform_position_op(1);
}
