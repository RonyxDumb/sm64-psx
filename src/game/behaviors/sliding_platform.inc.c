// sliding_platform.c.inc

void bhv_wf_sliding_platform_init(void) {
    o->oFaceAngleYaw -= 0x4000;
    QMODFIELD(o, oPosX, += q(2.0f));
    QSETFIELD(o,  oHomeX, QFIELD(o,  oPosX));

    switch (o->oBehParams2ndByte) {
        case WF_SLID_BRICK_PTFM_BP_MOV_VEL_10:
            QSETFIELD(o,  oWFSlidBrickPtfmMovVel, q(10));
            break;

        case WF_SLID_BRICK_PTFM_BP_MOV_VEL_15:
            QSETFIELD(o,  oWFSlidBrickPtfmMovVel, q(15));
            break;

        case WF_SLID_BRICK_PTFM_BP_MOV_VEL_20:
            QSETFIELD(o,  oWFSlidBrickPtfmMovVel, q(20));
            break;
    }

    o->oTimer = random_float() * 100.0f;
}

void bhv_wf_sliding_platform_loop(void) {
    switch (o->oAction) {
        case WF_SLID_BRICK_PTFM_ACT_WAIT:
            if (o->oTimer >= 101) {
                o->oAction = WF_SLID_BRICK_PTFM_ACT_EXTEND;
                QSETFIELD(o,  oForwardVel, QFIELD(o,  oWFSlidBrickPtfmMovVel));
            }
            break;

        case WF_SLID_BRICK_PTFM_ACT_EXTEND:
            if (o->oTimer >= 500.0f / FFIELD(o, oWFSlidBrickPtfmMovVel)) {
                QSETFIELD(o,  oForwardVel, q(0));
                FSETFIELD(o, oPosX, FFIELD(o, oHomeX) + 510.0f);
            }

            if (o->oTimer == 60) {
                o->oAction = WF_SLID_BRICK_PTFM_ACT_RETRACT;
                QSETFIELD(o,  oForwardVel, QFIELD(o,  oWFSlidBrickPtfmMovVel));
                o->oMoveAngleYaw -= 0x8000;
            }
            break;

        case WF_SLID_BRICK_PTFM_ACT_RETRACT:
            if (o->oTimer >= 500.0f / FFIELD(o, oWFSlidBrickPtfmMovVel)) {
                QSETFIELD(o,  oForwardVel, q(0));
                QSETFIELD(o,  oPosX, QFIELD(o,  oHomeX));
            }

            if (o->oTimer == 90) {
                o->oAction = WF_SLID_BRICK_PTFM_ACT_EXTEND;
                QSETFIELD(o,  oForwardVel, QFIELD(o,  oWFSlidBrickPtfmMovVel));
                o->oMoveAngleYaw -= 0x8000;
            }
            break;
    }
}
