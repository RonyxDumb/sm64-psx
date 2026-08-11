#pragma once
#include <stddef.h>

#ifdef TARGET_PC

#include_next <stdint.h>

#else

typedef unsigned uintptr_t;
typedef int intptr_t;
typedef int ptrdiff_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long long int64_t;
typedef long long intmax_t;

#endif
