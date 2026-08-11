#include "macros.h"
#include <PR/ultratypes.h>
#ifndef TARGET_N64
#include <string.h>
#endif
#include <assert.h>

#include "sm64.h"

#define INCLUDED_FROM_MEMORY_C

#include "buffers/buffers.h"
#include "decompress.h"
#include "game_init.h"
#include "main.h"
#include "memory.h"
#include "segment_symbols.h"
#include "segments.h"
#include "platform_info.h"
#include <port/cd.h>
#ifdef TARGET_PSX
#include <ps1/gpu.h>
#endif

// round up to the next multiple
#define ALIGN4(val) (((val) + 0x3) & ~0x3)
#define ALIGN8(val) (((val) + 0x7) & ~0x7)
#define ALIGN16(val) (((val) + 0xF) & ~0xF)
#if IS_64_BIT
#define ALIGNPTR(val) ALIGN8(val)
#else
#define ALIGNPTR(val) ALIGN4(val)
#endif

struct MainPoolState {
	struct MainPoolBlock* listHeadL;
	struct MainPoolBlock* listHeadR;
	struct MainPoolState* prev;
};

struct MainPoolBlock {
	struct MainPoolBlock* prev;
};

struct MemoryBlock {
	struct MemoryBlock* prev;
	struct MemoryBlock* next;
	u32 size;
};

struct MemoryPool {
	struct MemoryBlock* first_free;
};

/**
 * Memory pool for small graphical effects that aren't connected to Objects.
 * Used for colored text, paintings, and environmental snow and bubbles.
 */
struct MemoryPool* gEffectsMemoryPool;

static uintptr_t sSegmentTable[25];
struct MainPoolBlock* main_pool_head_left;
struct MainPoolBlock* main_pool_head_right;
void* main_pool_start_addr;
void* main_pool_end_addr;

static struct MainPoolState* gMainPoolState = NULL;

uintptr_t set_segment_base_addr(s32 segment, void *addr) {
	assert(segment < 25);
	sSegmentTable[segment] = (uintptr_t) addr;
	//assert(segment > 1 || addr == (void*) (segment << 24));
	//assert((uintptr_t) addr < 0xFFFFFF);
	return sSegmentTable[segment];
}

void *get_segment_base_addr(s32 segment) {
	return (void *) (sSegmentTable[segment]);
}

void *segmented_to_virtual(const void *addr) {
	size_t segment_idx = (uintptr_t) addr >> 24;
	if((uintptr_t) (segment_idx - 1) > 24) {
		return (void*) addr;
	}
	size_t offset = (uintptr_t) addr & 0x00FFFFFF;
	uintptr_t segment_start = segment_idx >= 25? 0: sSegmentTable[segment_idx];
	//assert((sSegmentTable[segment] + offset) < 0xFFFFFFFF);
	return (void*) (segment_start + offset);
}

void *virtual_to_segmented(u32 segment, const void *addr) {
	assert((uintptr_t) addr >= sSegmentTable[segment]);
	uintptr_t seg_addr = sSegmentTable[segment];
	if(seg_addr == 0x80000000 || seg_addr == 0) {
		return (void*) addr;
	}
	size_t offset = (uintptr_t) addr - seg_addr;

	return (void *) ((segment << 24) + offset);
}

/**
 * Initialize the main memory pool. This pool is conceptually a pair of stacks
 * that grow inward from the left and right. It therefore only supports
 * freeing the object that was most recently allocated from a side.
 */
void main_pool_init(void* start, void* end) {
	main_pool_head_left = (struct MainPoolBlock*) start;
	main_pool_head_right = (struct MainPoolBlock*) end - 1;
	main_pool_start_addr = start;
	main_pool_end_addr = end;
}

/**
 * Allocate a block of memory from the pool of given size, and from the
 * specified side of the pool (MEMORY_POOL_LEFT or MEMORY_POOL_RIGHT).
 * If there is not enough space, return NULL.
 */
void *main_pool_alloc(u32 size, u32 side) {
	size = ALIGNPTR(size);
	assertmf(main_pool_available() >= size, "main pool full", "required %d bytes, only %d available\n", size, main_pool_available());
	if(side == MEMORY_POOL_LEFT) {
		struct MainPoolBlock* new_block = main_pool_head_left;
		main_pool_head_left = (struct MainPoolBlock*) ((u8*) (main_pool_head_left + 1) + size);
		main_pool_head_left->prev = new_block;
		return new_block + 1;
	} else {
		struct MainPoolBlock* prev = main_pool_head_right;
		main_pool_head_right = (struct MainPoolBlock*) ((u8*) (main_pool_head_right - 1) - size);
		struct MainPoolBlock* new_block = main_pool_head_right;
		new_block->prev = prev;
		return new_block + 1;
	}
}

/**
 * Free a block of memory that was allocated from the pool. The block must be
 * the most recently allocated block from its end of the pool, otherwise all
 * newer blocks are freed as well. edit: otherwise it errors
 * Return the amount of free space left in the pool.
 */
u32 main_pool_free(void* addr) {
	if(addr == main_pool_head_left->prev + 1) {
		main_pool_head_left = main_pool_head_left->prev;
	} else if(addr == main_pool_head_right + 1) {
		main_pool_head_right = main_pool_head_right->prev;
	} else {
		assert(false);
	}
	return main_pool_available();
}

/**
 * Resize a block of memory that was allocated from the left side of the pool.
 * If the block is increasing in size, it must be the most recently allocated
 * block from the left side.
 * The block does not move.
 */
void* main_pool_realloc(void* addr, u32 size) {
	size = ALIGNPTR(size);
	struct MainPoolBlock* prev = main_pool_head_left->prev;
	assert(addr == prev + 1);
	main_pool_head_left = (struct MainPoolBlock*) ((u8*) (prev + 1) + size);
	main_pool_head_left->prev = prev;
	assert(main_pool_head_left + 1 <= main_pool_head_right);
	return addr;
}

/**
 * Return the size of the largest block that can currently be allocated from the
 * pool.
 */
u32 main_pool_available(void) {
	assert((uintptr_t) (main_pool_head_left + 1) < (uintptr_t) main_pool_head_right);
	return (uintptr_t) main_pool_head_right - (uintptr_t) (main_pool_head_left + 1);
}

/**
 * Push pool state, to be restored later. Return the amount of free space left
 * in the pool.
 */
u32 main_pool_push_state(void) {
	struct MainPoolState* prevState = gMainPoolState;
	gMainPoolState = main_pool_alloc(sizeof(*gMainPoolState), MEMORY_POOL_LEFT);
	gMainPoolState->listHeadL = main_pool_head_left;
	gMainPoolState->listHeadR = main_pool_head_right;
	gMainPoolState->prev = prevState;
	return main_pool_available();
}

/**
 * Restore pool state from a previous call to main_pool_push_state. Return the
 * amount of free space left in the pool.
 */
u32 main_pool_pop_state(void) {
	main_pool_head_left = gMainPoolState->listHeadL;
	main_pool_head_right = gMainPoolState->listHeadR;
	gMainPoolState = gMainPoolState->prev;
	return main_pool_available();
}

/**
 * Perform a DMA read from ROM. The transfer is split into 4KB blocks, and this
 * function blocks until completion.
 */
void dma_read(u8 *dest, const u8 *srcStart, const u8 *srcEnd) {
	if((uintptr_t) srcStart <= 1 || (uintptr_t) srcStart >= (uintptr_t) srcEnd) {
		return;
	}
	assert(!((uintptr_t) dest % 4));
	assert((uintptr_t) srcStart >= 4096);
	cd_read(dest, (uintptr_t) srcStart - 4096, (uintptr_t) srcEnd - (uintptr_t) srcStart);
}

/**
 * Perform a DMA read from ROM, allocating space in the memory pool to write to.
 * Return the destination address.
 */
static void *dynamic_dma_read(const u8 *srcStart, const u8 *srcEnd, u32 side) {
	//if(srcEnd <= srcStart) {
	//	printf("invalid dynamic_dma_read with srcStart %x, srcEnd %x\n", (u32) srcStart, (u32) srcEnd);
	//}
	assert(srcEnd > srcStart);
	void *dest;
	u32 size = ALIGN16(srcEnd - srcStart);

	dest = main_pool_alloc(size, side);
	assert(dest);
	dma_read(dest, srcStart, srcEnd);
	return dest;
}

#ifndef NO_SEGMENTED_MEMORY
/**
 * Load data from ROM into a newly allocated block, and set the segment base
 * address to this block.
 */
#include <stdio.h>
void *load_segment(s32 segment, const u8 *srcStart, const u8 *srcEnd, u32 side) {
	if((uintptr_t) srcStart <= 1) {
		if((uintptr_t) srcStart == 0) {
			set_segment_base_addr(segment, (void*) 0);
		}
		return 0;
	}
	//printf("loading segment %02x from %x:%x\n", segment, srcStart, srcEnd);
	void *addr = dynamic_dma_read(srcStart, srcEnd, side);
	//printf("loaded segment %02x from %x:%x into %x\n", segment, srcStart, srcEnd, addr);

	if (addr != NULL) {
		set_segment_base_addr(segment, addr);
	}
	return addr;
}

void *load_segment_decompress(s32 segment, const u8 *srcStart, const u8 *srcEnd) {
	// no longer using any compression
	return load_segment(segment, srcStart, srcEnd, MEMORY_POOL_LEFT);
}

void load_engine_code_segment(void) {
	// why would the game ever not have its own code loaded?
}
#endif

/**
 * Allocate an allocation-only pool from the main pool. This pool doesn't
 * support freeing allocated memory.
 * Return NULL if there is not enough space in the main pool.
 */
struct AllocOnlyPool *alloc_only_pool_init(u32 size, u32 side) {
	size = ALIGNPTR(size);
	struct AllocOnlyPool* sub_pool = main_pool_alloc(size + sizeof(struct AllocOnlyPool), side);
	assert(sub_pool);
	sub_pool->size = size;
	sub_pool->free_ptr = sub_pool + 1;
	return sub_pool;
}

/**
 * Allocate from an allocation-only pool.
 * Return NULL if there is not enough space.
 */
void* alloc_only_pool_alloc(struct AllocOnlyPool* pool, s32 size) {
	size = ALIGNPTR(size);
	assert(size > 0);
	void* addr = pool->free_ptr;
	pool->free_ptr += size;
	assert((uintptr_t) pool->free_ptr - (uintptr_t) pool < pool->size);
	return addr;
}

/**
 * Resize an allocation-only pool.
 * If the pool is increasing in size, the pool must be the last thing allocated
 * from the left end of the main pool.
 * The pool does not move.
 */
struct AllocOnlyPool* alloc_only_pool_resize(struct AllocOnlyPool* pool, u32 size) {
	size = ALIGNPTR(size);
	pool = main_pool_realloc(pool, size + sizeof(struct AllocOnlyPool));
	pool->size = size;
	return pool;
}

static void* blk_data_ptr(struct MemoryBlock* blk) {
	return blk + 1;
}

static void merge_blocks(struct MemoryBlock* first, struct MemoryBlock* second) {
	if(second->next) {
		second->next->prev = first;
	}
	first->next = second->next;
	first->size += second->size + sizeof(struct MemoryBlock);
}

/**
 * Allocate a memory pool from the main pool. This pool supports arbitrary
 * order for allocation/freeing.
 * Return NULL if there is not enough space in the main pool.
 */
struct MemoryPool* mem_pool_init(u32 size, u32 side) {
	size = ALIGNPTR(size);
	struct MemoryPool* pool = main_pool_alloc(size + sizeof(struct MemoryPool), side);
	assert(pool);
	pool->first_free = (struct MemoryBlock*) (pool + 1);
	pool->first_free->prev = NULL;
	pool->first_free->next = NULL;
	pool->first_free->size = size;
	return pool;
}

/**
 * Allocate from a memory pool. Return NULL if there is not enough space.
 */
void* mem_pool_alloc(struct MemoryPool* pool, u32 size) {
	size = ALIGNPTR(size);
	struct MemoryBlock* search = pool->first_free;
	assert(!search->prev);
	while(search) {
		if(search->size >= size) {
			u32 remaining_size = search->size - size;
			if(remaining_size >= sizeof(struct MemoryBlock)) {
				search->size = size;
				struct MemoryBlock* split_off = (struct MemoryBlock*) ((u8*) (search + 1) + size);
				split_off->size = remaining_size - sizeof(struct MemoryBlock);
				split_off->prev = search->prev;
				split_off->next = search->next;
				if(search->prev) {
					search->prev->next = split_off;
				} else if(pool->first_free == search) {
					pool->first_free = split_off;
				}
				if(search->next) {
					search->next->prev = split_off;
				}
			} else {
				if(search->prev) {
					search->prev->next = search->next;
				} else if(pool->first_free == search) {
					pool->first_free = search->next;
				}
				if(search->next) {
					search->next->prev = search->prev;
				}
			}
			return search + 1;
		}
		search = search->next;
	}
	unreachable();
}

/**
 * Free a block that was allocated using mem_pool_alloc.
 */
void mem_pool_free(struct MemoryPool* pool, void* addr) {
	struct MemoryBlock* blk = (struct MemoryBlock*) addr - 1;
	struct MemoryBlock* search = pool->first_free;
	if(search) {
		if((uintptr_t) search > (uintptr_t) blk) {
			blk->prev = NULL;
			blk->next = search;
			blk->next->prev = blk;
			if(blk_data_ptr(blk) + blk->size == blk->next) {
				merge_blocks(blk, blk->next);
			}
			pool->first_free = blk;
		} else {
			do {
				if((uintptr_t) search->next - 1 > (uintptr_t) blk) {
					blk->prev = search;
					blk->next = search->next;
					search->next = blk;
					if(blk_data_ptr(blk->prev) + blk->prev->size == blk) {
						merge_blocks(blk->prev, blk);
					}
					if(blk->next && blk_data_ptr(blk) + blk->size == blk->next) {
						merge_blocks(blk, blk->next);
					}
					return;
				}
				search = search->next;
			} while(search);
		}
	} else {
		blk->prev = NULL;
		blk->next = NULL;
		pool->first_free = blk;
	}
}

void setup_dma_table_list(struct DmaHandlerList *list, void *segment_start, void *segment_end, void *buffer) {
	ALIGNED8 u8 seg_buf[segment_end - segment_start];
	dma_read(seg_buf, segment_start, segment_end);
	u32 table_size = ((struct DmaTable*) seg_buf)->count * sizeof(struct OffsetSizePair) + sizeof(struct DmaTable);
	void* dest = main_pool_alloc(table_size, MEMORY_POOL_LEFT);
	assert(dest);
	memcpy(dest, seg_buf, table_size);

	struct DmaTable* table = (struct DmaTable*) dest;
	list->dmaTable = table;
	table->srcAddr = segment_start;
	list->currentAddr = NULL;
	list->bufTarget = buffer;
}

s32 load_patchable_table(struct DmaHandlerList *list, s32 index) {
	struct DmaTable *table = list->dmaTable;
	if ((u32)index >= table->count) {
		return FALSE;
	}
	void* segmented_addr = table->srcAddr + table->anim[index].offset;
	if(segmented_addr != list->currentAddr) {
		list->currentAddr = segmented_addr;
		u32 size = table->anim[index].size;
		u32 start = (u32) (uintptr_t) segmented_addr;
		// if it doesn't begin aligned to a sector, load that part separately
		u32 unaligned_part_size = 0;
		if(start % SECTOR_SIZE != 0) {
			u32 aligned_down = start / SECTOR_SIZE * SECTOR_SIZE;
			ALIGNED4 u8 tmp[SECTOR_SIZE];
			dma_read(tmp, segmented_addr, segmented_addr + SECTOR_SIZE);
			u32 start_in_sector = start - aligned_down;
			unaligned_part_size = SECTOR_SIZE - start_in_sector;
			if(unaligned_part_size > size) {
				unaligned_part_size = size;
			}
			memcpy(list->bufTarget, tmp + start_in_sector, unaligned_part_size);
		}
		if(size > unaligned_part_size) {
			dma_read(list->bufTarget + unaligned_part_size, segmented_addr + unaligned_part_size, segmented_addr + size);
		}
	}
	return TRUE;
}

#define ANIM_TABLE_SIZE 1680
#define ANIM_SEGMENT_SIZE 190324
#define ANIM_MAIN_DATA_SIZE (ANIM_SEGMENT_SIZE - ANIM_TABLE_SIZE)

#ifdef TARGET_PSX

#if 0

static u8 mario_anim_buffer[(ANIM_SEGMENT_SIZE + 2047) / 2048 * 2048];

u32 setup_mario_anims(struct DmaHandlerList *list, void *segment_start, void *segment_end, void *buffer) {
	dma_read(mario_anim_buffer, segment_start, segment_end);
	list->dmaTable = (struct DmaTable*) mario_anim_buffer;
	list->currentAddr = NULL;
	list->bufTarget = buffer;
	return segment_end - segment_start;
}

static void receive(uint8_t* buf, uint32_t start, uint32_t len) {
	memcpy(buf, mario_anim_buffer + ANIM_TABLE_SIZE + start, len);
}

#else

#define VRAM_X 640
#define VRAM_Y 0
#define VRAM_WIDTH 384
#define VRAM_ROW_BYTES (VRAM_WIDTH * 2)
#define VRAM_HEIGHT ((ANIM_MAIN_DATA_SIZE + VRAM_ROW_BYTES - 1) / VRAM_ROW_BYTES)
#define VRAM_PART (VRAM_ROW_BYTES * VRAM_HEIGHT)
STATIC_ASSERT(VRAM_HEIGHT < 256, "mario animations won't fit in the designated vram area");

u32 setup_mario_anims(struct DmaHandlerList *list, void *segment_start, UNUSED void *segment_end, void *buffer) {
	void* seg_buf = main_pool_alloc((ANIM_SEGMENT_SIZE + 2047) / 2048 * 2048, MEMORY_POOL_LEFT);
	dma_read(seg_buf, segment_start, segment_start + ANIM_SEGMENT_SIZE);
	u32 just_table_size = ((struct DmaTable*) seg_buf)->count * sizeof(struct OffsetSizePair) + sizeof(struct DmaTable);

	sendVRAMData(seg_buf + just_table_size, VRAM_X, VRAM_Y, VRAM_WIDTH, VRAM_HEIGHT);

	seg_buf = main_pool_realloc(seg_buf, just_table_size);
	list->dmaTable = (struct DmaTable*) seg_buf;
	list->currentAddr = NULL;
	list->bufTarget = buffer;
	return ANIM_MAIN_DATA_SIZE;
}

static void receive(uint8_t* buf, uint32_t start, uint32_t len) {
	assert(start % 2 == 0);
	if(len % 2 != 0) {
		len++;
	}
	assert(start + len < VRAM_ROW_BYTES * VRAM_HEIGHT);
	u32 y = VRAM_Y + start / VRAM_ROW_BYTES;
	while(len) {
		u32 inner_x = start % VRAM_ROW_BYTES / 2;
		u32 w = MIN(len / 2, VRAM_WIDTH - inner_x);
		receiveVRAMData(buf, VRAM_X + inner_x, y, ((w + 1) / 2 + DMA_MAX_CHUNK_SIZE - 1) / DMA_MAX_CHUNK_SIZE * DMA_MAX_CHUNK_SIZE * 2, 1);
		start += w * 2;
		len -= w * 2;
		buf += w * 2;
		y++;
	}
}

#endif

#elifdef TARGET_PC

static u8* mario_anim_buffer;

u32 setup_mario_anims(struct DmaHandlerList *list, void *segment_start, void *segment_end, void *buffer) {
	mario_anim_buffer = malloc(segment_end - segment_start);
	dma_read(mario_anim_buffer, segment_start, segment_end);
	list->dmaTable = (struct DmaTable*) mario_anim_buffer;
	list->currentAddr = NULL;
	list->bufTarget = buffer;
	return segment_end - segment_start;
}

static void receive(uint8_t* buf, uint32_t start, uint32_t len) {
	memcpy(buf, mario_anim_buffer + ANIM_TABLE_SIZE + start, len);
}

#else
#error mario animation storage not implemented for target
#endif

s32 load_mario_anim(struct DmaHandlerList *list, s32 index) {
	struct DmaTable *table = list->dmaTable;

	if((u32) index < table->count) {
		u32 full_off = table->anim[index].offset;
		if((u32) (uintptr_t) list->currentAddr != full_off) {
			list->currentAddr = (void*) (uintptr_t) full_off;
			u32 size = table->anim[index].size;
			receive(list->bufTarget, full_off - ANIM_TABLE_SIZE, size);
			return true;
		}
	}
	return false;
}
