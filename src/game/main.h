#ifndef MAIN_H
#define MAIN_H

#include "types.h"
#include "config.h"

enum {
	RUMBLE_OFF = 0,
	RUMBLE_CONSTANT,
	RUMBLE_MODULATE
};

struct RumbleData {
    u8 state;
    u8 intensity;
    s16 duration;
    s16 decay;
};

struct RumbleState {
    s16 state;
    s16 intensity;
    s16 duration;
    s16 modulation_timer;
    s16 attack_duration;
    s16 swim_timer;
    s16 swim_divider;
    s16 decay;
};

extern OSThread D_80339210;
extern OSThread gIdleThread;
extern OSThread gMainThread;
extern OSThread gGameLoopThread;
extern OSThread gSoundThread;
#if ENABLE_RUMBLE
extern OSThread gRumblePakThread;

extern s32 gRumblePakPfs; // Actually an OSPfs but we don't have that header yet
#endif

extern OSMesgQueue gPIMesgQueue;
extern OSMesgQueue gIntrMesgQueue;
extern OSMesgQueue gSPTaskMesgQueue;
#if ENABLE_RUMBLE
extern OSMesgQueue gRumblePakSchedulerMesgQueue;
extern OSMesgQueue gRumbleThreadVIMesgQueue;
#endif
extern OSMesg gDmaMesgBuf[1];
extern OSMesg gPIMesgBuf[32];
extern OSMesg gSIEventMesgBuf[1];
extern OSMesg gIntrMesgBuf[16];
extern OSMesg gUnknownMesgBuf[16];
extern OSIoMesg gDmaIoMesg;
extern OSMesg gMainReceivedMesg;
extern OSMesgQueue gDmaMesgQueue;
extern OSMesgQueue gSIEventMesgQueue;
#if ENABLE_RUMBLE
extern struct RumbleData gRumbleDataQueue[3];
extern struct RumbleState gCurrRumbleState;
#endif

extern struct VblankHandler *gVblankHandler1;
extern struct VblankHandler *gVblankHandler2;
extern struct SPTask *gActiveSPTask;
extern u32 gNumVblanks;
extern s8 gDebugLevelSelect;
extern s8 D_8032C650;
extern s8 gShowProfiler;
extern s8 gShowDebugText;

void set_vblank_handler(s32 index, struct VblankHandler *handler, OSMesgQueue *queue, OSMesg *msg);
void dispatch_audio_sptask(struct SPTask *spTask);

#endif // MAIN_H
