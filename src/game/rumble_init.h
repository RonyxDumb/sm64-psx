#ifndef RUMBLE_INIT_H
#define RUMBLE_INIT_H

#include <PR/ultratypes.h>

#include "config.h"

#if ENABLE_RUMBLE

extern s32 gRumblePakTimer;

void queue_rumble_data(s16 duration, s16 intensity);
void set_rumble_decay(s16 mystery);
u8 is_rumble_finished_and_queue_empty(void);
void reset_rumble_timers(void);
void reset_rumble_timers_2(s32 intensity);
void rumble_for_swimming(void);
void cancel_rumble(void);
void update_rumble(u8* analog, u8* digital);

#endif // ENABLE_RUMBLE

#endif // RUMBLE_INIT_H
