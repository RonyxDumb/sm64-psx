#include <ultra64.h>

#include "sm64.h"
#include "gfx_dimensions.h"
#include "audio/external.h"
#include "buffers/buffers.h"
#include "buffers/gfx_output_buffer.h"
#include "engine/level_script.h"
#include "game_init.h"
#include "main.h"
#include "memory.h"
#include "profiler.h"
#include "save_file.h"
#include "seq_ids.h"
#include "sound_init.h"
#include "print.h"
#include "segment2.h"
#include "segment_symbols.h"
#include "rumble_init.h"
#include <game/object_list_processor.h>
#include <engine/surface_load.h>
#include <prevent_bss_reordering.h>
#include <levels/scripts.h>
#include <port/gfx/gfx.h>

struct Controller gControllers[3];
OSContPad gControllerPads[4];
s8 gEepromProbe;

OSMesgQueue gGameVblankQueue;
OSMesgQueue gGfxVblankQueue;
OSMesg gGameMesgBuf[1];
OSMesg gGfxMesgBuf[1];

struct VblankHandler gGameVblankHandler;

void *gMarioAnimsMemAlloc;
void *gDemoInputsMemAlloc;
struct DmaHandlerList gMarioAnimsBuf;
struct DmaHandlerList gDemoInputsBuf;

UNUSED static u8 sfillerGameInit[0x90];
u32 gGlobalTimer = 0;

struct Controller *gPlayer1Controller = &gControllers[0];
struct Controller *gPlayer2Controller = &gControllers[1];
struct Controller *gPlayer3Controller = &gControllers[2];

struct DemoInput *gCurrDemoInput = NULL;
u16 gDemoInputListID = 0;
struct DemoInput gRecordedDemoInput = { 0 };

void display_and_vsync(void) {
    profiler_log_thread5_time(BEFORE_DISPLAY_LISTS);
    gfx_flush_global_dl();
    profiler_log_thread5_time(AFTER_DISPLAY_LISTS);
    gfx_end_frame(true);
    profiler_log_thread5_time(THREAD5_END);
    gGlobalTimer++;
}

static void adjust_analog_stick(s8 raw_x, s8 raw_y, float* x, float* y, float* mag) {
    *x = (raw_x <= -8) ? raw_x + 6 : ((raw_x >= 8) ? raw_x - 6 : 0);
    *y = (raw_y <= -8) ? raw_y + 6 : ((raw_y >= 8) ? raw_y - 6 : 0);

    *mag = sqrtf(*x * *x + *y * *y);
    if (*mag < 8) {
        *mag = 0;
    } else if (*mag > 64) {
        *x *= 64 / *mag;
        *y *= 64 / *mag;
        *mag = 64;
    }
}

void run_demo_inputs(void) {
    gControllers[0].controllerData->button &= VALID_BUTTONS;

    if (gCurrDemoInput != NULL) {
        if (gControllers[1].controllerData != NULL) {
            gControllers[1].controllerData->stick_x = 0;
            gControllers[1].controllerData->stick_y = 0;
            gControllers[1].controllerData->button = 0;
        }

        if (gCurrDemoInput->timer == 0) {
            gControllers[0].controllerData->stick_x = 0;
            gControllers[0].controllerData->stick_y = 0;
            gControllers[0].controllerData->button = END_DEMO;
        } else {
            u16 startPushed = gControllers[0].controllerData->button & START_BUTTON;

            gControllers[0].controllerData->stick_x = gCurrDemoInput->rawStickX;
            gControllers[0].controllerData->stick_y = gCurrDemoInput->rawStickY;
            gControllers[0].controllerData->button =
                (((gCurrDemoInput->buttonMask & 0xF0) << 8) + (gCurrDemoInput->buttonMask & 0xF)) | startPushed;

            if (--gCurrDemoInput->timer == 0) {
                gCurrDemoInput++;
            }
        }
    }
}

void controller_backend_read(OSContPad* pad, u32 port);

void read_controller_inputs(void) {
    gControllers[0].controllerData = &gControllerPads[0];
    for (u32 port = 0; port < 2; port++) {
        gControllerPads[0].errnum = 1;
        controller_backend_read(&gControllerPads[0], port);
        if (gControllerPads[0].errnum == 0) break;
    }
    
    run_demo_inputs();
    
    struct Controller *controller = &gControllers[0];
    if (controller->controllerData && controller->controllerData->errnum != 1) {
        controller->rawStickX = controller->controllerData->stick_x;
        controller->rawStickY = controller->controllerData->stick_y;
        controller->rawRightStickX = controller->controllerData->right_stick_x;
        controller->rawRightStickY = controller->controllerData->right_stick_y;
        controller->buttonPressed = controller->controllerData->button & (controller->controllerData->button ^ controller->buttonDown);
        controller->buttonDown = controller->controllerData->button;
        
        adjust_analog_stick(controller->rawStickX, controller->rawStickY, &controller->stickX, &controller->stickY, &controller->stickMag);
        adjust_analog_stick(controller->rawRightStickX, controller->rawRightStickY, &controller->rightStickX, &controller->rightStickY, &controller->rightStickMag);
    } else {
        controller->rawStickX = controller->rawStickY = 0;
        controller->buttonPressed = controller->buttonDown = 0;
        controller->stickX = controller->stickY = controller->stickMag = 0;
        controller->rawRightStickX = controller->rawRightStickY = 0;
        controller->rightStickX = controller->rightStickY = controller->rightStickMag = 0;
    }

    gPlayer3Controller->rawStickX = gPlayer1Controller->rawStickX;
    gPlayer3Controller->rawStickY = gPlayer1Controller->rawStickY;
    gPlayer3Controller->stickX = gPlayer1Controller->stickX;
    gPlayer3Controller->stickY = gPlayer1Controller->stickY;
    gPlayer3Controller->stickMag = gPlayer1Controller->stickMag;
    gPlayer3Controller->rightStickX = gPlayer1Controller->rightStickX;
    gPlayer3Controller->rightStickY = gPlayer1Controller->rightStickY;
    gPlayer3Controller->buttonPressed = gPlayer1Controller->buttonPressed;
    gPlayer3Controller->buttonDown = gPlayer1Controller->buttonDown;
}

extern char _mario_anim_dataSegmentRomStart[];
extern char _mario_anim_dataSegmentRomEnd[];
extern char _demo_dataSegmentRomStart[];
extern char _demo_dataSegmentRomEnd[];
u32 mario_anims_buf_size;

void setup_game_memory(void) {
    for (int i = 0; i < 25; i++) {
        set_segment_base_addr(i, (void*) (i << 24));
    }
    
    gMarioAnimsMemAlloc = main_pool_alloc(0x3000, MEMORY_POOL_LEFT);
    set_segment_base_addr(17, (void *) gMarioAnimsMemAlloc);
    mario_anims_buf_size = setup_mario_anims(&gMarioAnimsBuf, _mario_anim_dataSegmentRomStart, _mario_anim_dataSegmentRomEnd, gMarioAnimsMemAlloc);

#ifndef BENCH
    gDemoInputsMemAlloc = main_pool_alloc(0x800, MEMORY_POOL_LEFT);
    set_segment_base_addr(24, (void *) gDemoInputsMemAlloc);
    setup_dma_table_list(&gDemoInputsBuf, _demo_dataSegmentRomStart, _demo_dataSegmentRomEnd, gDemoInputsMemAlloc);
#endif

    set_segment_base_addr(0x10, (void*) 0);
    load_segment_decompress(2, _segment2_mio0SegmentRomStart, _segment2_mio0SegmentRomEnd);
    can_show_screen_message = true;
}

static struct LevelCommand *levelCommandAddr;

void thread5_game_loop(UNUSED void *arg) {
    setup_game_memory();
    save_file_load_all();

    levelCommandAddr = (void*) level_script_entry;

    play_music(SEQ_PLAYER_SFX, SEQUENCE_ARGS(0, SEQ_SOUND_PLAYER), 0);
    set_sound_mode(save_file_get_sound_mode());

    gGlobalTimer++;
}

extern struct AllocOnlyPool* sLevelPool;
extern void* main_pool_start_addr;
extern void* main_pool_end_addr;
extern bool compilation_happened_this_frame;

void game_loop_one_iteration(void) {
    profiler_log_thread5_time(THREAD5_START);

#ifndef NO_AUDIO
    audio_game_loop_tick();
#endif
    read_controller_inputs();
    levelCommandAddr = level_script_execute(levelCommandAddr);

    if (gShowDebugText) {
        u32 avail = main_pool_available();
        if (sLevelPool) {
            uintptr_t used = (uintptr_t) sLevelPool->free_ptr - (uintptr_t) (sLevelPool + 1);
            avail += sLevelPool->size - used;
        }
        
        u32 y = 208;
        
        print_text_fmt_int(0, y -= 16, "MEM USATA %d", (main_pool_end_addr - main_pool_start_addr) - avail);
        print_text_fmt_int(176, y, "LIBERA %d", avail);
        
        if (compilation_happened_this_frame) {
            print_text(0, y -= 16, "LISTE COMPILATE");
            compilation_happened_this_frame = false;
        }
        
        print_text_fmt_int(0, y -= 16, "POLIGONI %d", debug_processed_poly_count);
        
        debug_processed_poly_count = 0;
    }

    display_and_vsync();
}