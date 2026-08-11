// intro_scene.inc.c

void spawn_child_obj_relative(struct Object *parent, s16 xOffset, s16 yOffset, s16 zOffset, s16 pitchOffset,
                   s16 yawOffset, s16 rollOffset, s16 forwardVel,
                   s32 model, const BehaviorScript *behavior) {
    struct Object *sp1C = spawn_object(parent, model, behavior);

    sp1C->header.gfx.animInfo.animFrame = random_u16() % 6; //random_float() * 6.f;
    QSETFIELD(sp1C, oEndBirdUnk104, sCutsceneVars[9].pointq[0]);
    sCutsceneVars[9].pointq[0] += QONE;
    QMODFIELD(sp1C, oPosX, += q(xOffset));
    QMODFIELD(sp1C, oPosY, += q(yOffset));
    if (gCutsceneTimer > 700)
        QMODFIELD(sp1C, oPosY, += q(-150));
    QMODFIELD(sp1C, oPosZ, += q(zOffset));
    sp1C->oMoveAnglePitch += pitchOffset;
    sp1C->oMoveAngleYaw += yawOffset;
    sp1C->oMoveAngleRoll += rollOffset;
    QSETFIELD(sp1C, oForwardVel, q(forwardVel));
}

void bhv_intro_scene_loop(void) {
    UNUSED struct Object *sp34;

    if (gCutsceneObjSpawn != 0) {
        QSETFIELD(gCurrentObject, oPosX, gCamera->posq[0]);
        QSETFIELD(gCurrentObject, oPosY, gCamera->posq[1]);
        QSETFIELD(gCurrentObject, oPosZ, gCamera->posq[2]);
        gCurrentObject->oMoveAnglePitch = 0;
        gCurrentObject->oMoveAngleYaw = 0;

        switch (gCutsceneObjSpawn) {
            case 6:
                sp34 = spawn_object(gCurrentObject, MODEL_LAKITU, bhvBeginningLakitu);
                break;
            case 5:
                sp34 = spawn_object(gCurrentObject, MODEL_PEACH, bhvBeginningPeach);
                break;
            case 7:
                spawn_child_obj_relative(gCurrentObject, 0, 205, 500, 0x1000, 0x6000, -0x1E00, 25, MODEL_BIRDS,
                              bhvEndBirds1);
                spawn_child_obj_relative(gCurrentObject, 0, 205, 800, 0x1800, 0x6000, -0x1400, 35, MODEL_BIRDS,
                              bhvEndBirds1);
                spawn_child_obj_relative(gCurrentObject, -100, 300, 500, 0x800, 0x6000, 0, 25, MODEL_BIRDS,
                              bhvEndBirds1);
                spawn_child_obj_relative(gCurrentObject, 100, -200, 800, 0, 0x4000, 0x1400, 45, MODEL_BIRDS,
                              bhvEndBirds1);
                spawn_child_obj_relative(gCurrentObject, -80, 300, 350, 0x1800, 0x5000, 0xA00, 35, MODEL_BIRDS,
                              bhvEndBirds1);
                spawn_child_obj_relative(gCurrentObject, -300, 300, 500, 0x800, 0x6000, 0x2800, 25, MODEL_BIRDS,
                              bhvEndBirds1);
                spawn_child_obj_relative(gCurrentObject, -400, -200, 800, 0, 0x4000, -0x1400, 45, MODEL_BIRDS,
                              bhvEndBirds1);
                break;
            case 9:
                spawn_child_obj_relative(gCurrentObject, 50, 205, 500, 0x1000, 0x6000, 0, 35, MODEL_BIRDS,
                              bhvEndBirds1);
                spawn_child_obj_relative(gCurrentObject, 0, 285, 800, 0x1800, 0x6000, 0, 35, MODEL_BIRDS,
                              bhvEndBirds1);
                break;
            case 8:
                spawn_child_obj_relative(gCurrentObject, -100, -100, -700, 0, 0, -0xF00, 25, MODEL_BIRDS,
                              bhvEndBirds2);
                spawn_child_obj_relative(gCurrentObject, -250, 255, -200, 0, 0, -0x1400, 25, MODEL_BIRDS,
                              bhvEndBirds2);
                spawn_child_obj_relative(gCurrentObject, -100, 155, -600, 0, 0, -0x500, 35, MODEL_BIRDS,
                              bhvEndBirds2);
                spawn_child_obj_relative(gCurrentObject, 250, 200, -1200, 0, 0, -0x700, 25, MODEL_BIRDS,
                              bhvEndBirds2);
                spawn_child_obj_relative(gCurrentObject, -250, 255, -700, 0, 0, 0, 25, MODEL_BIRDS, bhvEndBirds2);
                break;
        }

        gCutsceneObjSpawn = 0;
    }
}
