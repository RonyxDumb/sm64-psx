#include "platform_info.h"
#include <stdlib.h>
#include <stdio.h>

#ifdef TARGET_WEB
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include "sm64.h"

#include "game/memory.h"

#include "gfx/gfx.h"

#include "configfile.h"

#include "compat.h"

#define CONFIG_FILE "sm64config.txt"

OSMesg gMainReceivedMesg;
OSMesgQueue gSIEventMesgQueue;

s8 gDebugLevelSelect = true;
s8 gShowProfiler = false;
s8 gShowDebugText = false;

extern void thread5_game_loop(void *arg);
void game_loop_one_iteration(void);

#include "game/game_init.h" // for gGlobalTimer

#ifdef USE_CONFIG
static void save_config(void) {
    configfile_save(CONFIG_FILE);
}
#endif

#ifdef TARGET_PC
extern bool close_requested;
ALIGNED8 static u8 main_pool[DOUBLE_SIZE_ON_64_BIT(0x200000)];
#else
#define close_requested false
#if defined(BIG_RAM) && !defined(BENCH)
ALIGNED8 static u8 main_pool[DOUBLE_SIZE_ON_64_BIT(0x200000)];
#else
ALIGNED8 static u8 main_pool[DOUBLE_SIZE_ON_64_BIT(0x0C0000)]; // the original size was 0x165000 (~1.4 MiB)- carefully lower it as much as possible
#endif
#endif

[[gnu::noinline]] int main(UNUSED int argc, UNUSED char *argv[]) {
	main_pool_init(main_pool, (u8*) main_pool + sizeof(main_pool));
	gEffectsMemoryPool = mem_pool_init(DOUBLE_SIZE_ON_64_BIT(0x4000), MEMORY_POOL_LEFT);

#ifdef USE_CONFIG
    configfile_load(CONFIG_FILE);
    atexit(save_config);
#endif

    gfx_init();

    thread5_game_loop(NULL);
    while(!close_requested) {
		game_loop_one_iteration();
    }
    return 0;
}
