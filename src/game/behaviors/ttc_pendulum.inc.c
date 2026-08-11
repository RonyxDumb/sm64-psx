
/**
 * Behavior for bhvTTCPendulum. This is not the pendulum inside the clock in the
 * castle, but rather the one in TTC itself.
 */

/**
 * Initial angle acceleration.
 */
static f32 sTTCPendulumInitialAccels[] = {
    /* TTC_SPEED_SLOW    */ 13.0f,
    /* TTC_SPEED_FAST    */ 22.0f,
    /* TTC_SPEED_RANDOM  */ 13.0f,
    /* TTC_SPEED_STOPPED */ 0.0f,
};

/**
 * Init function for bhvTTCPendulum.
 */
void bhv_ttc_pendulum_init(void) {
    if (gTTCSpeedSetting != TTC_SPEED_STOPPED) {
        FSETFIELD(o, oTTCPendulumAngleAccel, sTTCPendulumInitialAccels[gTTCSpeedSetting]);
        QSETFIELD(o,  oTTCPendulumAngle, q(6500));
    } else {
        QSETFIELD(o,  oTTCPendulumAngle, q(6371.5557));
    }
}

/**
 * Update function for bhvTTCPendulum.
 */
void bhv_ttc_pendulum_update(void) {
    if (gTTCSpeedSetting != TTC_SPEED_STOPPED) {
        UNUSED f32 startVel = FFIELD(o, oTTCPendulumAngleVel);

        // Play sound
        if (o->oTTCPendulumSoundTimer != 0) {
            if (--o->oTTCPendulumSoundTimer == 0) {
                cur_obj_play_sound_2(SOUND_GENERAL_PENDULUM_SWING);
            }
        }

        // Stay still for a while
        if (o->oTTCPendulumDelay != 0) {
            o->oTTCPendulumDelay -= 1;
        } else {
            // Accelerate in the direction that moves angle to zero
            if (FFIELD(o, oTTCPendulumAngle) * FFIELD(o, oTTCPendulumAccelDir) > 0.0f) {
                QSETFIELD(o,  oTTCPendulumAccelDir, QFIELD(-o,  oTTCPendulumAccelDir));
            }
            FMODFIELD(o, oTTCPendulumAngleVel, += FFIELD(o, oTTCPendulumAngleAccel) * FFIELD(o, oTTCPendulumAccelDir));

            // Ignoring floating point imprecision, angle vel should always be
            // a multiple of angle accel, and so it will eventually reach zero
            //! If the pendulum is moving fast enough, the vel could fail to
            //  be a multiple of angle accel, and so the pendulum would continue
            //  oscillating forever
            if (QFIELD(o, oTTCPendulumAngleVel) == 0) {
                if (gTTCSpeedSetting == TTC_SPEED_RANDOM) {
                    // Select a new acceleration
                    //! By manipulating this, we can cause the pendulum to reach
                    //  extreme angles and speeds
                    if (random_u16() % 3 != 0) {
                        QSETFIELD(o,  oTTCPendulumAngleAccel, q(13));
                    } else {
                        QSETFIELD(o,  oTTCPendulumAngleAccel, q(42));
                    }

                    // Pick a random delay
                    if (random_u16() % 2 == 0) {
                        o->oTTCPendulumDelay = random_linear_offset(5, 30);
                    }
                }

                // Play the sound 15 frames after beginning to move
                o->oTTCPendulumSoundTimer = o->oTTCPendulumDelay + 15;
            }

            QMODFIELD(o, oTTCPendulumAngle, += QFIELD(o, oTTCPendulumAngleVel));
        }
    } else {
    }

    o->oFaceAngleRoll = IFIELD(o, oTTCPendulumAngle);
    // Note: no platform displacement
}
