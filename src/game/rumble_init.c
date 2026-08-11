#include <ultra64.h>
#include "macros.h"

#include "buffers/buffers.h"
#include "main.h"
#include "rumble_init.h"
#include "config.h"

#if ENABLE_RUMBLE

OSThread gRumblePakThread;

s32 gRumblePakPfs; // Actually an OSPfs but we don't have that header yet

s8 D_SH_8031D8F8[0x60];

OSMesg gRumblePakSchedulerMesgBuf[1];
OSMesgQueue gRumblePakSchedulerMesgQueue;
OSMesg gRumbleThreadVIMesgBuf[1];
OSMesgQueue gRumbleThreadVIMesgQueue;

struct RumbleData gRumbleDataQueue[3];
struct RumbleState gCurrRumbleState;

s32 gRumblePakTimer = 0;

static void update_rumble_pak(u8* analog, u8* digital) {
    if (gCurrRumbleState.attack_duration > 0) {
   	    gCurrRumbleState.attack_duration--;
        *analog = 255;
        *digital = 1;
    } else if (gCurrRumbleState.duration > 0) {
        gCurrRumbleState.duration--;

        gCurrRumbleState.intensity -= gCurrRumbleState.decay;
        if (gCurrRumbleState.intensity < 0) {
            gCurrRumbleState.intensity = 0;
        }

        if (gCurrRumbleState.state == RUMBLE_CONSTANT) {
            *analog = 255;
            *digital = 1;
        } else {
       		*analog = gCurrRumbleState.intensity * 255 / 80;
	        if (gCurrRumbleState.modulation_timer >= 0x100) {
	            gCurrRumbleState.modulation_timer -= 0x100;
	            *digital = 1;
	        } else {
	            gCurrRumbleState.modulation_timer +=
	                ((gCurrRumbleState.intensity * gCurrRumbleState.intensity * gCurrRumbleState.intensity) / (1 << 9)) + 4;
	            *digital = 0;
	        }
        }
    } else {
        gCurrRumbleState.duration = 0;

        if (gCurrRumbleState.swim_timer >= 5) {
   	        *analog = 255;
            *digital = 1;
        } else if (gCurrRumbleState.swim_timer >= 2) {
       	    *analog = 255 / gCurrRumbleState.swim_divider;
            *digital = gNumVblanks % gCurrRumbleState.swim_divider == 0;
        } else {
            *analog = 0;
            *digital = 0;
        }
    }

    if (gCurrRumbleState.swim_timer > 0) {
        gCurrRumbleState.swim_timer--;
    }
}

static void update_rumble_data_queue(void) {
    if (gRumbleDataQueue[0].state != RUMBLE_OFF) {
        gCurrRumbleState.modulation_timer = 0;
        gCurrRumbleState.attack_duration = 4;
        gCurrRumbleState.state = gRumbleDataQueue[0].state;
        gCurrRumbleState.duration = gRumbleDataQueue[0].duration;
        gCurrRumbleState.intensity = gRumbleDataQueue[0].intensity;
        gCurrRumbleState.decay = gRumbleDataQueue[0].decay;
    }

    gRumbleDataQueue[0] = gRumbleDataQueue[1];
    gRumbleDataQueue[1] = gRumbleDataQueue[2];
    gRumbleDataQueue[2].state = RUMBLE_OFF;
}

void queue_rumble_data(s16 duration, s16 intensity) {
    if (gCurrDemoInput != NULL) {
        return;
    }

    if (intensity > 70) {
        gRumbleDataQueue[2].state = RUMBLE_CONSTANT;
    } else {
        gRumbleDataQueue[2].state = RUMBLE_MODULATE;
    }

    gRumbleDataQueue[2].intensity = intensity;
    gRumbleDataQueue[2].duration = duration;
    gRumbleDataQueue[2].decay = 0;
}

void set_rumble_decay(s16 decay) {
    gRumbleDataQueue[2].decay = decay;
}

u8 is_rumble_finished_and_queue_empty(void) {
    if (gCurrRumbleState.attack_duration + gCurrRumbleState.duration >= 4) {
        return FALSE;
    }

    if (gRumbleDataQueue[0].state != RUMBLE_OFF) {
        return FALSE;
    }

    if (gRumbleDataQueue[1].state != RUMBLE_OFF) {
        return FALSE;
    }

    if (gRumbleDataQueue[2].state != RUMBLE_OFF) {
        return FALSE;
    }

    return TRUE;
}

void reset_rumble_timers(void) {
    if (gCurrDemoInput != NULL) {
        return;
    }

    if (gCurrRumbleState.swim_timer == 0) {
        gCurrRumbleState.swim_timer = 7;
    }

    if (gCurrRumbleState.swim_timer < 4) {
        gCurrRumbleState.swim_timer = 4;
    }

    gCurrRumbleState.swim_divider = 7;
}

void reset_rumble_timers_2(s32 swim_speed) {
    if (gCurrDemoInput != NULL) {
        return;
    }

    if (gCurrRumbleState.swim_timer == 0) {
        gCurrRumbleState.swim_timer = 7;
    }

    if (gCurrRumbleState.swim_timer < 4) {
        gCurrRumbleState.swim_timer = 4;
    }

    gCurrRumbleState.swim_divider = 5 - swim_speed;
}

void rumble_for_swimming(void) {
    if (gCurrDemoInput != NULL) {
        return;
    }

    gCurrRumbleState.swim_timer = 4;
    gCurrRumbleState.swim_divider = 4;
}

void cancel_rumble(void) {
    gRumbleDataQueue[0].state = RUMBLE_OFF;
    gRumbleDataQueue[1].state = RUMBLE_OFF;
    gRumbleDataQueue[2].state = RUMBLE_OFF;

    gCurrRumbleState.duration = 0;
    gCurrRumbleState.swim_timer = 0;

    gRumblePakTimer = 0;
}

void update_rumble(u8* analog, u8* digital) {
	update_rumble_data_queue();
	update_rumble_pak(analog, digital);

    if (gRumblePakTimer > 0) {
        gRumblePakTimer--;
    }
}

#endif
