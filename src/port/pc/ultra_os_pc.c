#include <SDL3/SDL.h>
#include <lib/src/libultra_internal.h>
#include <macros.h>
#include <stdbool.h>

//#define SAVES

uintptr_t osVirtualToPhysical(void *addr) {
	return (uintptr_t) addr;
}

OSTime osGetTime(void) {
	return SDL_GetTicksNS() / 1000;
}

u32 osGetCount(void) {
	static u32 counter = 0;
	return counter++;
}

s32 osEepromProbe(UNUSED OSMesgQueue *mq) {
#ifdef SAVES
	return 1;
#else
	return 0;
#endif
}

s32 osEepromLongRead(UNUSED OSMesgQueue *mq, u8 address, u8 *buffer, int nbytes) {
#ifdef SAVES
	u8 content[512];
	FILE* fp = fopen("sm64_save_file.bin", "rb");
	if(fp == NULL) {
		return -1;
	}
	if(fread(content, 1, 512, fp) == 512) {
		memcpy(buffer, content + address * 8, nbytes);
		fclose(fp);
		return 0;
	} else {
		fclose(fp);
		return -1;
	}
#else
	return -1;
#endif
}

s32 osEepromLongWrite(UNUSED OSMesgQueue *mq, u8 address, u8 *buffer, int nbytes) {
#ifdef SAVES
	u8 content[512] = {0};
	if (address != 0 || nbytes != 512) {
		osEepromLongRead(mq, 0, content, 512);
	}
	memcpy(content + address * 8, buffer, nbytes);
	FILE* fp = fopen("sm64_save_file.bin", "wb");
	if (fp == NULL) {
		return -1;
	}
	s32 ret = fwrite(content, 1, 512, fp) == 512? 0: -1;
	fclose(fp);
	return ret;
#else
	return 0;
#endif
}
