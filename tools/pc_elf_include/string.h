#ifndef PC_ELF_STRING_H
#define PC_ELF_STRING_H

#include <stddef.h>

void *memcpy(void *restrict dest, const void *restrict src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *dest, int c, size_t n);
int memcmp(const void *lhs, const void *rhs, size_t n);
size_t strlen(const char *s);

#endif
