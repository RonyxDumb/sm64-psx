#include <PR/ultratypes.h>
#include <PR/os_time.h>
#include <PR/gbi.h>

#include "sm64.h"
#include "profiler.h"
#include "game_init.h"
#include <port/gfx/gfx.h>
#include <game/print.h>

// the thread 3 info is logged on the opposite profiler from what is used by
// the thread4 and 5 loggers. It's likely because the sound thread runs at a
// much faster rate and shouldn't be flipping the index for the "slower" game
// threads, which could leave the frame data in a possibly corrupt or incomplete
// state.
s16 gCurrentFrameIndex1 = 1;
s16 gCurrentFrameIndex2 = 0;

struct ProfilerFrameData gProfilerFrameData[2];
#define XPOS(i) (48 * i)
// if the display width represents 1000000/fps us (one frame), each us is width/(1000000/fps) pixels long
// that'd just divide into 0, so multiply by width and then divide by 1000000/fps instead
#define PROFILER_FRAME_US (1000000 / 30)
#define PROFILER_FRAME_WIDTH (XPOS(4))

// log the current osTime to the appropriate idx for current thread5 processes.
void profiler_log_thread5_time(enum ProfilerGameEvent eventID) {
    gProfilerFrameData[gCurrentFrameIndex1].gameTimes[eventID] = osGetTime();

    if (eventID == THREAD5_END) {
        gCurrentFrameIndex1 ^= 1;
    }
}

// draw the specified profiler given the information passed.
void draw_profiler_bar(OSTime clockBase, OSTime clockStart, OSTime clockEnd, s16 posY, s16 width_off) {
    s64 durationStart, durationEnd;
    s32 rectX1, rectX2;

    // set the duration to start, and floor to 0 if the result is below 0.
    if ((durationStart = clockStart - clockBase) < 0) {
        durationStart = 0;
    }
    // like the above, but with end.
    if ((durationEnd = clockEnd - clockBase) < 0) {
        durationEnd = 0;
    }

    // calculate the x coordinates of where start and end begins, respectively.
    rectX1 = XPOS(0) + (durationStart * PROFILER_FRAME_WIDTH / PROFILER_FRAME_US);
    rectX2 = XPOS(0) + (durationEnd * PROFILER_FRAME_WIDTH / PROFILER_FRAME_US) + width_off;

    if (rectX1 > 319) {
        rectX1 = 319;
    }
    if (rectX2 > 319) {
        rectX2 = 319;
    }

    // perform the render if start is less than end. in most cases, it should be.
    if (rectX1 < rectX2) {
        gfx_emit_tex(NULL);
        gfx_emit_screen_quad(rectX1, posY, rectX2, posY + 2);
    }
}

void draw_reference_profiler_bars(void) {
    // Draws the reference "max" bars underneath the real thing.
    struct ProfilerFrameData *profiler;
    profiler = &gProfilerFrameData[gCurrentFrameIndex1 ^ 1];
	//OSTime clockBase = profiler->gameTimes[0];

    gfx_emit_tex(NULL);

    // Blue
    gfx_emit_env_color_alpha_full(0xFF5028);
	gfx_emit_screen_quad(XPOS(0), 220 - 22, XPOS(1), 222 - 22);
	//print_text(XPOS(0), 24, "SND");
	//print_text_fmt_int(XPOS(0), 8, "%dMS", (profiler->gameTimes[THREAD5_START] - clockBase) / 1000);
	print_text(XPOS(0), 24, "SCR");
	print_text_fmt_int(XPOS(0), 8, "%dMS", (profiler->gameTimes[LEVEL_SCRIPT_EXECUTE] - profiler->gameTimes[THREAD5_START]) / 1000);

    // Yellow
    gfx_emit_env_color_alpha_full(0x28FFFF);
    gfx_emit_screen_quad(XPOS(1), 220 - 22, XPOS(2), 222 - 22);
    print_text(XPOS(1), 24, "GRPH");
	print_text_fmt_int(XPOS(1), 8, "%dMS", (profiler->gameTimes[BEFORE_DISPLAY_LISTS] - profiler->gameTimes[LEVEL_SCRIPT_EXECUTE]) / 1000);

    // Orange
    gfx_emit_env_color_alpha_full(0x2878FF);
	gfx_emit_screen_quad(XPOS(2), 220 - 22, XPOS(3), 222 - 22);
	print_text(XPOS(2), 24, "RNDR");
	print_text_fmt_int(XPOS(2), 8, "%dMS", (profiler->gameTimes[AFTER_DISPLAY_LISTS] - profiler->gameTimes[BEFORE_DISPLAY_LISTS]) / 1000);

    // Green
    gfx_emit_env_color_alpha_full(0x28FF28);
	gfx_emit_screen_quad(XPOS(3), 220 - 22, XPOS(4), 222 - 22);
	print_text(XPOS(3), 24, "WAIT");
	print_text_fmt_int(XPOS(3), 8, "%dMS", (profiler->gameTimes[THREAD5_END] - profiler->gameTimes[AFTER_DISPLAY_LISTS]) / 1000);
}

// Draw the Profiler per frame. Toggle the mode if the player presses L while this
// renderer is active.
void draw_profiler(void) {
    // the profiler logs 2 frames of data: last frame and current frame. Indexes are used
    // to keep track of the current frame, so the index is xor'd to retrieve the last frame's
    // data.
    struct ProfilerFrameData *profiler = &gProfilerFrameData[gCurrentFrameIndex1 ^ 1];

	OSTime clockBase = profiler->gameTimes[0];

    gfx_emit_tex(NULL);

    draw_reference_profiler_bars();

    // bar for the time level scripts took to execute (yellow)
    gfx_emit_env_color_alpha_full(0xFF5028);
    draw_profiler_bar(clockBase, profiler->gameTimes[0], profiler->gameTimes[1], 212 - 16, 0);

    // bar for the time the game took to process (after level scripts and before running display lists) (orange)
    gfx_emit_env_color_alpha_full(0x28FFFF);
    draw_profiler_bar(clockBase, profiler->gameTimes[1], profiler->gameTimes[2], 212 - 16, 0);

    // bar for the time the display lists took to process (blue)
    gfx_emit_env_color_alpha_full(0x2878FF);
    draw_profiler_bar(clockBase, profiler->gameTimes[2], profiler->gameTimes[3], 212 - 16, 0);

    // bar for the time vsync took (green)
    gfx_emit_env_color_alpha_full(0x28FF28);
    draw_profiler_bar(clockBase, profiler->gameTimes[3], profiler->gameTimes[4], 212 - 16, -1);
}
