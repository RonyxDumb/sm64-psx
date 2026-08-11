#ifndef MACROS_H
#define MACROS_H

#include "platform_info.h"

#ifndef __sgi
#define GLOBAL_ASM(...)
#endif

#if defined(TARGET_PSX) && !defined(NO_SCRATCHPAD)
#define scratchpad [[gnu::section(".scratchpad")]]
#else
#ifdef TARGET_PSX
#warning scratchpad usage is off!!!
#endif
#define scratchpad
#endif

#ifdef TARGET_PC
#define sdata
#define sbss
#else
#define sdata [[gnu::section(".sdata")]]
#define sbss [[gnu::section(".sbss")]]
#endif

#ifdef NO_INLINE
#ifndef TARGET_PC
#warning forced inlining is off!!
#endif
#define ALWAYS_INLINE [[gnu::noinline]] [[gnu::noipa]]
#else
#define ALWAYS_INLINE [[gnu::always_inline]] inline
#endif

#ifdef unreachable
#undef unreachable
#endif
#define unreachable __builtin_unreachable

#if !defined(__sgi) && (!defined(NON_MATCHING) || !defined(AVOID_UB))
// asm-process isn't supported outside of IDO, and undefined behavior causes
// crashes.
#error Matching build is only possible on IDO; please build with NON_MATCHING=1.
#endif

#define ARRAY_COUNT(arr) (s32)(sizeof(arr) / sizeof(arr[0]))

#define GLUE(a, b) a ## b
#define GLUE2(a, b) GLUE(a, b)

// Avoid compiler warnings for unused variables
#ifdef __GNUC__
#define UNUSED __attribute__((unused))
#else
#define UNUSED
#endif

// Avoid undefined behaviour for non-returning functions
#ifdef __GNUC__
#define NORETURN __attribute__((noreturn))
#else
#define NORETURN
#endif

// Static assertions
#ifdef __GNUC__
#define STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#else
#define STATIC_ASSERT(cond, msg) typedef char GLUE2(static_assertion_failed, __LINE__)[(cond) ? 1 : -1]
#endif

// Align to 4-byte boundary
#ifdef __GNUC__
#define ALIGNED4 __attribute__((aligned(4)))
#else
#error no
#define ALIGNED4
#endif

// Align to 8-byte boundary for DMA requirements
#ifdef __GNUC__
#define ALIGNED8 __attribute__((aligned(8)))
#else
#error no
#define ALIGNED8
#endif

// Align to 16-byte boundary for audio lib requirements
#ifdef __GNUC__
#define ALIGNED16 __attribute__((aligned(16)))
#else
#define ALIGNED16
#endif

// no conversion needed other than cast
#define VIRTUAL_TO_PHYSICAL(addr)   ((uintptr_t)(addr))
#define PHYSICAL_TO_VIRTUAL(addr)   ((uintptr_t)(addr))
#define VIRTUAL_TO_PHYSICAL2(addr)  ((void *)(addr))

#endif // MACROS_H
