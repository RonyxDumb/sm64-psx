#include <stdio.h>
#include <string.h>
#include <lib/src/libultra_internal.h>
#include <macros.h>
#include <stdbool.h>


uintptr_t osVirtualToPhysical(void *addr) {
    return (uintptr_t) addr;
}

static u64 last_time_us = 0;
static u16 last_time_read = 0;
static bool inited = false;

struct Counter {
	u16 value;
	u16 _pad;
	u16 mode;
	u16 _pad2;
	u16 target;
	u16 _pad3[3];
};

#define COUNTERS ((volatile struct Counter*) 0xBF801100)

u64 osClockRate = 1000000;

OSTime osGetTime(void) {
	if(!inited) {
		COUNTERS[1].mode = 0x0100;
		inited = true;
		return 0;
	}
	u16 time_read = COUNTERS[1].value;
	while(true) {
		u16 reread = COUNTERS[1].value;
		if(time_read == reread) {
			break;
		} else {
			time_read = reread;
		}
	}
	u32 offset = time_read - last_time_read;
	if(time_read < last_time_read) {
		offset += 0x10000;
	}
	last_time_read = time_read;
	last_time_us += offset * 64;
    return last_time_us;
}

u32 osGetCount(void) {
    static u32 counter = 0;
    return counter++;
}

// stubbed for now

s32 osEepromProbe(UNUSED OSMesgQueue *mq) {
    return 1;
}

s32 osEepromLongRead(UNUSED OSMesgQueue *mq, UNUSED u8 address, UNUSED u8 *buffer, UNUSED int nbytes) {
    //u8 content[512];
    return -1;
}

s32 osEepromLongWrite(UNUSED OSMesgQueue *mq, UNUSED u8 address, UNUSED u8 *buffer, UNUSED int nbytes) {
	return 0;
}
