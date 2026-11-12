#include <assert.h>
#include <string.h>
#include <ultra64.h>

#include "game/memory.h"

int main(void) {
    enum { CAPACITY = 0x1000, GUARD_SIZE = 32 };
    alignas(16) u8 arena[0x2000];
    main_pool_init(arena, arena + sizeof(arena));
    struct MemoryPool *pool = mem_pool_init(CAPACITY, MEMORY_POOL_LEFT);
    u8 *guard = main_pool_alloc(GUARD_SIZE, MEMORY_POOL_LEFT);
    memset(guard, 0xA5, GUARD_SIZE);

    void *first = mem_pool_alloc(pool, 128);
    void *middle = mem_pool_alloc(pool, 96);
    void *last = mem_pool_alloc(pool, 256);
    assert(first && middle && last);
    mem_pool_free(pool, last);
    mem_pool_free(pool, first);
    assert(mem_pool_alloc(pool, CAPACITY) == NULL);
    mem_pool_free(pool, middle);

    void *full = mem_pool_alloc(pool, CAPACITY);
    assert(full == first);
    assert(mem_pool_alloc(pool, 8) == NULL);
    memset(full, 0x3C, CAPACITY);
    for (int i = 0; i < GUARD_SIZE; i++) {
        assert(guard[i] == 0xA5);
    }

    mem_pool_free(pool, full);
    assert(mem_pool_alloc(pool, CAPACITY) == first);
    return 0;
}
