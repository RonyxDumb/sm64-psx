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

// First 3 controller slots
struct Controller gControllers[3];

// OS Controllers
OSContPad gControllerPads[4];
s8 gEepromProbe; // Save Data Probe

// OS Messages
OSMesgQueue gGameVblankQueue;
OSMesgQueue gGfxVblankQueue;
OSMesg gGameMesgBuf[1];
OSMesg gGfxMesgBuf[1];

// Vblank Handler
struct VblankHandler gGameVblankHandler;

// Mario Anims and Demo allocation
void *gMarioAnimsMemAlloc;
void *gDemoInputsMemAlloc;
struct DmaHandlerList gMarioAnimsBuf;
struct DmaHandlerList gDemoInputsBuf;

// fillers
UNUSED static u8 sfillerGameInit[0x90];
//static s32 sUnusedGameInitValue = 0;

// General timer that runs as the game starts
u32 gGlobalTimer = 0;


// Defined controller slots
struct Controller *gPlayer1Controller = &gControllers[0];
struct Controller *gPlayer2Controller = &gControllers[1];
struct Controller *gPlayer3Controller = &gControllers[2]; // Probably debug only, see note below

// Title Screen Demo Handler
struct DemoInput *gCurrDemoInput = NULL;
u16 gDemoInputListID = 0;
struct DemoInput gRecordedDemoInput = { 0 };

// Display
// ----------------------------------------------------------------------------------------------------

/**
 * This function:
 * - Sends the current master display list out to be rendered.
 * - Tells the VI which color framebuffer to be displayed.
 * - Yields to the VI framerate twice, locking the game at 30 FPS.
 * - Selects which framebuffer will be rendered and displayed to next time.
 */
void display_and_vsync(void) {
    profiler_log_thread5_time(BEFORE_DISPLAY_LISTS);
    gfx_flush_global_dl();
    profiler_log_thread5_time(AFTER_DISPLAY_LISTS);
    gfx_end_frame(true);
    profiler_log_thread5_time(THREAD5_END);
    gGlobalTimer++;
}

// Controls
// ----------------------------------------------------------------------------------------------------

/**
 * This function records distinct inputs over a 255-frame interval to RAM locations and was likely
 * used to record the demo sequences seen in the final game. This function is unused.
 */
UNUSED static void record_demo(void) {
    // Record the player's button mask and current rawStickX and rawStickY.
    u8 buttonMask =
        ((gPlayer1Controller->buttonDown & (A_BUTTON | B_BUTTON | Z_TRIG | START_BUTTON)) >> 8)
        | (gPlayer1Controller->buttonDown & (U_CBUTTONS | D_CBUTTONS | L_CBUTTONS | R_CBUTTONS));
    s8 rawStickX = gPlayer1Controller->rawStickX;
    s8 rawStickY = gPlayer1Controller->rawStickY;

    // If the stick is in deadzone, set its value to 0 to
    // nullify the effects. We do not record deadzone inputs.
    if (rawStickX > -8 && rawStickX < 8) {
        rawStickX = 0;
    }

    if (rawStickY > -8 && rawStickY < 8) {
        rawStickY = 0;
    }

    // Rrecord the distinct input and timer so long as they are unique.
    // If the timer hits 0xFF, reset the timer for the next demo input.
    if (gRecordedDemoInput.timer == 0xFF || buttonMask != gRecordedDemoInput.buttonMask
        || rawStickX != gRecordedDemoInput.rawStickX || rawStickY != gRecordedDemoInput.rawStickY) {
        gRecordedDemoInput.timer = 0;
        gRecordedDemoInput.buttonMask = buttonMask;
        gRecordedDemoInput.rawStickX = rawStickX;
        gRecordedDemoInput.rawStickY = rawStickY;
    }
    gRecordedDemoInput.timer++;
}

/**
 * Take the updated controller struct and calculate the new x, y, and distance floats.
 */
static void adjust_analog_stick(s8 raw_x, s8 raw_y, float* x, float* y, float* mag) {
    UNUSED u8 pad[8];

    // Reset the controller's x and y floats.
    *x = 0;
    *y = 0;

    // Modulate the rawStickX and rawStickY to be the new f32 values by adding/subtracting 6.
    if (raw_x <= -8) {
        *x = raw_x + 6;
    }

    if (raw_x >= 8) {
        *x = raw_x - 6;
    }

    if (raw_y <= -8) {
        *y = raw_y + 6;
    }

    if (raw_y >= 8) {
        *y = raw_y - 6;
    }

    // Calculate f32 magnitude from the center by vector length.
    *mag = sqrtf(*x * *x + *y * *y);
	// on PS1, Mario would keep slowly walking down on neutral stick (due to imprecision of the reimplemented math?)
	// so we add an extra deadzone check here
	if(*mag < 8) {
		*mag = 0;
	} else if (*mag > 64) {
		// Magnitude cannot exceed 64.0f: if it does, modify the values
		// appropriately to flatten the values down to the allowed maximum value.
        *x *= 64 / *mag;
        *y *= 64 / *mag;
        *mag = 64;
    }
}

/**
 * If a demo sequence exists, this will run the demo input list until it is complete.
 */
void run_demo_inputs(void) {
    // Eliminate the unused bits.
    gControllers[0].controllerData->button &= VALID_BUTTONS;

    // Check if a demo inputs list exists and if so,
    // run the active demo input list.
    if (gCurrDemoInput != NULL) {
        // Clear player 2's inputs if they exist. Player 2's controller
        // cannot be used to influence a demo. At some point, Nintendo
        // may have planned for there to be a demo where 2 players moved
        // around instead of just one, so clearing player 2's influence from
        // the demo had to have been necessary to perform this. Co-op mode, perhaps?
        if (gControllers[1].controllerData != NULL) {
            gControllers[1].controllerData->stick_x = 0;
            gControllers[1].controllerData->stick_y = 0;
            gControllers[1].controllerData->button = 0;
        }

        // The timer variable being 0 at the current input means the demo is over.
        // Set the button to the END_DEMO mask to end the demo.
        if (gCurrDemoInput->timer == 0) {
            gControllers[0].controllerData->stick_x = 0;
            gControllers[0].controllerData->stick_y = 0;
            gControllers[0].controllerData->button = END_DEMO;
        } else {
            // Backup the start button if it is pressed, since we don't want the
            // demo input to override the mask where start may have been pressed.
            u16 startPushed = gControllers[0].controllerData->button & START_BUTTON;

            // Perform the demo inputs by assigning the current button mask and the stick inputs.
            gControllers[0].controllerData->stick_x = gCurrDemoInput->rawStickX;
            gControllers[0].controllerData->stick_y = gCurrDemoInput->rawStickY;

            // To assign the demo input, the button information is stored in
            // an 8-bit mask rather than a 16-bit mask. this is because only
            // A, B, Z, Start, and the C-Buttons are used in a demo, as bits
            // in that order. In order to assign the mask, we need to take the
            // upper 4 bits (A, B, Z, and Start) and shift then left by 8 to
            // match the correct input mask. We then add this to the masked
            // lower 4 bits to get the correct button mask.
            gControllers[0].controllerData->button =
                ((gCurrDemoInput->buttonMask & 0xF0) << 8) + ((gCurrDemoInput->buttonMask & 0xF));

            // If start was pushed, put it into the demo sequence being input to end the demo.
            gControllers[0].controllerData->button |= startPushed;

            // Run the current demo input's timer down. if it hits 0, advance the demo input list.
            if (--gCurrDemoInput->timer == 0) {
                gCurrDemoInput++;
            }
        }
    }
}

void controller_backend_read(OSContPad* pad, u32 port);

/**
 * Update the controller struct with available inputs if present.
 */
void read_controller_inputs(void) {
	gControllers[0].controllerData = &gControllerPads[0];
    for (u32 port = 0; port < 2; port++) {
    	gControllerPads[0].errnum = 1;
        controller_backend_read(&gControllerPads[0], port);
        if(gControllerPads[0].errnum == 0) {
            break;
        }
    }
    run_demo_inputs();
    //for (u32 port = 0; port < 2; port++) {
        struct Controller *controller = &gControllers[0];

        // if we're receiving inputs, update the controller struct with the new button info.
        if (controller->controllerData && controller->controllerData->errnum != 1) {
            controller->rawStickX = controller->controllerData->stick_x;
            controller->rawStickY = controller->controllerData->stick_y;
            controller->rawRightStickX = controller->controllerData->right_stick_x;
            controller->rawRightStickY = controller->controllerData->right_stick_y;
            controller->buttonPressed = controller->controllerData->button
                                        & (controller->controllerData->button ^ controller->buttonDown);
            // 0.5x A presses are a good meme
            controller->buttonDown = controller->controllerData->button;
            adjust_analog_stick(controller->rawStickX, controller->rawStickY, &controller->stickX, &controller->stickY, &controller->stickMag);
            adjust_analog_stick(controller->rawRightStickX, controller->rawRightStickY, &controller->rightStickX, &controller->rightStickY, &controller->rightStickMag);
        } else // otherwise, if the controllerData is NULL, 0 out all of the inputs.
        {
            controller->rawStickX = 0;
            controller->rawStickY = 0;
            controller->buttonPressed = 0;
            controller->buttonDown = 0;
            controller->stickX = 0;
            controller->stickY = 0;
            controller->stickMag = 0;
            controller->rawRightStickX = 0;
            controller->rawRightStickY = 0;
            controller->rightStickX = 0;
            controller->rightStickY = 0;
            controller->rightStickMag = 0;
        }
    //}

    // For some reason, player 1's inputs are copied to player 3's port.
    // This potentially may have been a way the developers "recorded"
    // the inputs for demos, despite record_demo existing.
    // note: despite being obviously useless, menus and dialogs read input from player 3, so if this is deleted they will stop working
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

// Game thread core
// ----------------------------------------------------------------------------------------------------

/**
 * Setup main segments and framebuffers.
 */
extern char _mario_anim_dataSegmentRomStart[];
extern char _mario_anim_dataSegmentRomEnd[];
extern char _demo_dataSegmentRomStart[];
extern char _demo_dataSegmentRomEnd[];
u32 mario_anims_buf_size;
void setup_game_memory(void) {
    UNUSED u64 padding;

    // Setup general Segment 0
    //set_segment_base_addr(0, (void *) 0x80000000);
	for(int i = 0; i < 25; i++) {
		set_segment_base_addr(i, (void*) (i << 24));
	}
    // Setup Mario Animations
    gMarioAnimsMemAlloc = main_pool_alloc(0x3000, MEMORY_POOL_LEFT); // originally 0x4000
    set_segment_base_addr(17, (void *) gMarioAnimsMemAlloc);
    mario_anims_buf_size = setup_mario_anims(&gMarioAnimsBuf, _mario_anim_dataSegmentRomStart, _mario_anim_dataSegmentRomEnd, gMarioAnimsMemAlloc);
#ifndef BENCH
    // Setup Demo Inputs List
    gDemoInputsMemAlloc = main_pool_alloc(0x800, MEMORY_POOL_LEFT);
    set_segment_base_addr(24, (void *) gDemoInputsMemAlloc);
    setup_dma_table_list(&gDemoInputsBuf, _demo_dataSegmentRomStart, _demo_dataSegmentRomEnd, gDemoInputsMemAlloc);
#endif
    // Setup Level Script Entry
    //load_segment(0x10, _entrySegmentRomStart, _entrySegmentRomEnd, MEMORY_POOL_LEFT);
	set_segment_base_addr(0x10, (void*) 0);
    // Setup Segment 2 (Fonts, Text, etc)
    load_segment_decompress(2, _segment2_mio0SegmentRomStart, _segment2_mio0SegmentRomEnd);
    can_show_screen_message = true;
}

static struct LevelCommand *levelCommandAddr;

extern const LevelScript level_bob_entry[];
/**
 * Main game loop thread. Runs forever as long as the game continues.
 */
void thread5_game_loop(UNUSED void *arg) {
    setup_game_memory();
    save_file_load_all();

	//set_vblank_handler(2, &gGameVblankHandler, &gGameVblankQueue, (OSMesg) 1);

    // Point levelCommandAddr to the entry point into the level script data.
    //levelCommandAddr = segmented_to_virtual(level_script_entry);
	levelCommandAddr = (void*) level_script_entry;

    play_music(SEQ_PLAYER_SFX, SEQUENCE_ARGS(0, SEQ_SOUND_PLAYER), 0);
    set_sound_mode(save_file_get_sound_mode());

    gGlobalTimer++;
}

extern struct AllocOnlyPool* sLevelPool;

extern char _bssEnd[];
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

	// when debug info is enabled, print the "BUF %d" information.
	if (gShowDebugText) {
		u32 avail = main_pool_available();
		if(sLevelPool) {
			uintptr_t used = (uintptr_t) sLevelPool->free_ptr - (uintptr_t) (sLevelPool + 1);
			avail += sLevelPool->size - used;
		}
		u32 y = 208;
		//print_text_fmt_int(0, y -= 16, "G USED %d", (uintptr_t) gDisplayListHead - (uintptr_t) gGfxPool);
		//print_text_fmt_int(176, y, "FREE %d", (uintptr_t) gGfxPoolEnd - (uintptr_t) gDisplayListHead);
		print_text_fmt_int(0, y -= 16, "M USED %d", (main_pool_end_addr - main_pool_start_addr) - avail);
		print_text_fmt_int(176, y, "FREE %d", avail);
        //u32 sp;
        //asm volatile("move %0, $sp" : "=r"(sp));
		//print_text_fmt_int(0, y -= 16, "UNUSED RAM %d", ((uintptr_t) sp & 0xFFFFFF) - ((uintptr_t) _bssEnd & 0xFFFFFF));
		//print_text_fmt_int(0, y -= 16, "FROM %d", (uintptr_t) _bssEnd & 0xFFFFFF);
		//print_text_fmt_int(160, y, "TO %d", (uintptr_t) sp & 0xFFFFFF);
        if(compilation_happened_this_frame) {
            print_text(0, y -= 16, "COMPILED SOME LISTS");
            compilation_happened_this_frame = false;
        }
        print_text_fmt_int(0, y -= 16, "POLY %d", debug_processed_poly_count);
        debug_processed_poly_count = 0;
	}

	display_and_vsync();
}
