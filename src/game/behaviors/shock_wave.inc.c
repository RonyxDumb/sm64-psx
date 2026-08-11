// shock_wave.c.inc

/**
 * Shockwave scale distance hit points
 */
f32 sBowserShockwaveHitPoints[] = { 1.9f, 2.4f, 4.0f, 4.8f };

/**
 * Bowser's shockwave attack main loop
 */
void bhv_bowser_shock_wave_loop(void) {
    f32 distMin1, distMax1, distMin2, distMax2;
    s16 fadeFrames = 70;
    // Scale shockwave as the timer goes on
    QSETFIELD(o, oBowserShockWaveScale, q(10 * o->oTimer));
    cur_obj_scaleq(QFIELD(o, oBowserShockWaveScale));
    // Slightly reduce opacity each 3 frames
    if (gGlobalTimer % 3)
        o->oOpacity -= 1;
    // Reduce opacity faster after 70 frames have passed
    if (o->oTimer > fadeFrames)
        o->oOpacity -= 5;
    // Delete object when it's fully transparent
    if (o->oOpacity <= 0)
        obj_mark_for_deletion(o);
    // If object times is less than 70 frame and Mario is not in the air...
    if (o->oTimer < fadeFrames && mario_is_in_air_action() == 0) {
        // ..define distance values depending of the scale multiplied by hit points
        distMin1 = FFIELD(o, oBowserShockWaveScale) * sBowserShockwaveHitPoints[0];
        distMax1 = FFIELD(o, oBowserShockWaveScale) * sBowserShockwaveHitPoints[1];
        distMin2 = FFIELD(o, oBowserShockWaveScale) * sBowserShockwaveHitPoints[2];
        distMax2 = FFIELD(o, oBowserShockWaveScale) * sBowserShockwaveHitPoints[3];
        // If Mario is in between distMin and distMax values, shock him
        if ((distMin1 < FFIELD(o, oDistanceToMario) && FFIELD(o, oDistanceToMario) < distMax1)
            || (distMin2 < FFIELD(o, oDistanceToMario) && FFIELD(o, oDistanceToMario) < distMax2))
            gMarioObject->oInteractStatus |= INT_STATUS_MARIO_SHOCKWAVE;
    }
}
