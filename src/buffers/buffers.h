#ifndef BUFFERS_H
#define BUFFERS_H

#include <PR/ultratypes.h>

#include "game/save_file.h"
#include "game/game_init.h"
#include "config.h"

extern u8 gAudioHeap[];

//extern u8 gUnusedThread2Stack[];

//extern u8 gIdleThreadStack[];
//extern u8 gThread3Stack[];
//extern u8 gThread4Stack[];
//extern u8 gThread5Stack[];
//#if ENABLE_RUMBLE
//extern u8 gThread6Stack[];
//#endif

extern struct SaveBuffer gSaveBuffer;

#endif // BUFFERS_H
