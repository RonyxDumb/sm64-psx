// bubble.c.inc

void bhv_object_bubble_init(void) {
    FMODFIELD(o, oPosX,  += random_float() * 30.0f);
    FMODFIELD(o, oPosY,  += random_float() * 30.0f);
    FMODFIELD(o, oPosZ,  += random_float() * 30.0f);
}

void bhv_object_bubble_loop(void) {
    struct Object *bubbleSplash;
    q32 waterYq = find_water_levelq(QFIELD(o, oPosX), QFIELD(o, oPosZ));
    q32 bubbleYq = QFIELD(o, oPosY);

    if (bubbleYq > waterYq) {
        if (gFreeObjectList.next != NULL) {
            bubbleSplash = spawn_object_at_origin(o, 0, MODEL_SMALL_WATER_SPLASH, bhvBubbleSplash);
            QSETFIELD(bubbleSplash, oPosX, QFIELD(o, oPosX));
            QSETFIELD(bubbleSplash, oPosY, bubbleYq + q(5));
            QSETFIELD(bubbleSplash, oPosZ, QFIELD(o, oPosZ));
        }

        o->activeFlags = ACTIVE_FLAG_DEACTIVATED;
    }
}
