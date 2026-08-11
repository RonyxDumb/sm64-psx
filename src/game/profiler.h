#ifndef PROFILER_H
#define PROFILER_H

#include <PR/ultratypes.h>
#include <PR/os_time.h>

#include "types.h"

extern u64 osClockRate;

struct ProfilerFrameData {
    // gameTimes:
    // 0: thread 5 start
    // 1: level script execution
    // 2: render
    // 3: display lists
    // 4: thread 4 end (0 terminated)
    /* 0x08 */ OSTime gameTimes[5];
};

// thread event IDs
enum ProfilerGameEvent {
    THREAD5_START,
    LEVEL_SCRIPT_EXECUTE,
    BEFORE_DISPLAY_LISTS,
    AFTER_DISPLAY_LISTS,
    THREAD5_END
};

void profiler_log_thread5_time(enum ProfilerGameEvent eventID);
void draw_profiler(void);

#endif // PROFILER_H
