#ifndef STDDEF_H
#define STDDEF_H

#include "PR/ultratypes.h"

#ifdef TARGET_PC

#include_next <stddef.h>

#else

#ifndef offsetof
#define offsetof(st, m) ((size_t)&(((st *)0)->m))
#endif

typedef unsigned size_t;
typedef int ssize_t;

#endif

#endif
