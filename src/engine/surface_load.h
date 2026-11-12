#ifndef SURFACE_LOAD_H
#define SURFACE_LOAD_H

#include <PR/ultratypes.h>

#include "surface_collision.h"
#include "types.h"

#define NUM_CELLS       (2 * LEVEL_BOUNDARY_MAX / CELL_SIZE)
#define NUM_CELLS_INDEX (NUM_CELLS - 1)

struct SurfaceNode
{
#ifdef TARGET_PSX
    u16 nextIndex; // 1-based pool index, 0 at the end of a list
    u16 surfaceOffsetWords; // Offset from sSurfacePool in 32-bit words
#else
    struct SurfaceNode *next;
    struct Surface *surface;
#endif
};

enum
{
    SPATIAL_PARTITION_FLOORS,
    SPATIAL_PARTITION_CEILS,
    SPATIAL_PARTITION_WALLS
};

typedef struct SurfaceNode SpatialPartitionCell[3];

// Needed for bs bss reordering memes.
extern s32 unused8038BE90;

extern SpatialPartitionCell gStaticSurfacePartition[NUM_CELLS][NUM_CELLS];
extern SpatialPartitionCell gDynamicSurfacePartition[NUM_CELLS][NUM_CELLS];
extern struct SurfaceNode *sSurfaceNodePool;
extern struct Surface *sSurfacePool;

#ifdef TARGET_PSX
#define SURFACE_NODE_POOL_LEN 8192
#else
#define SURFACE_NODE_POOL_LEN 6000 // originally 7000
#endif
#define SURFACE_POOL_LEN 2300 // originally 2300

#ifdef TARGET_PSX
STATIC_ASSERT(sizeof(struct SurfaceNode) == 4, "PS1 collision node size");
STATIC_ASSERT(SURFACE_NODE_POOL_LEN <= 0xFFFF, "collision node index range");
STATIC_ASSERT(sizeof(struct Surface) % sizeof(u32) == 0, "collision surface alignment");
STATIC_ASSERT(SURFACE_POOL_LEN * sizeof(struct Surface) / sizeof(u32) <= 0x10000,
              "collision surface offset range");
#endif

static inline struct SurfaceNode *surface_node_next(const struct SurfaceNode *node) {
#ifdef TARGET_PSX
    return node->nextIndex ? &sSurfaceNodePool[node->nextIndex - 1] : NULL;
#else
    return node->next;
#endif
}

static inline void surface_node_set_next(struct SurfaceNode *node, struct SurfaceNode *next) {
#ifdef TARGET_PSX
    node->nextIndex = next ? (u16) (next - sSurfaceNodePool + 1) : 0;
#else
    node->next = next;
#endif
}

static inline struct Surface *surface_node_surface(const struct SurfaceNode *node) {
#ifdef TARGET_PSX
    return (struct Surface *) ((u8 *) sSurfacePool + node->surfaceOffsetWords * sizeof(u32));
#else
    return node->surface;
#endif
}

static inline void surface_node_set_surface(struct SurfaceNode *node, struct Surface *surface) {
#ifdef TARGET_PSX
    node->surfaceOffsetWords = (u16) (((uintptr_t) surface - (uintptr_t) sSurfacePool) / sizeof(u32));
#else
    node->surface = surface;
#endif
}

void alloc_surface_pools(void);
#ifdef NO_SEGMENTED_MEMORY
u32 get_area_terrain_size(s16 *data);
#endif
void load_area_terrain(s16 index, s16 *data, s8 *surfaceRooms, s16 *macroObjects);
void clear_dynamic_surfaces(void);
void load_object_collision_model(void);

#endif // SURFACE_LOAD_H
