#ifndef SURFACE_COLLISION_H
#define SURFACE_COLLISION_H

#include <PR/ultratypes.h>

#include "types.h"

// Range level area is 16384x16384 (-8192 to +8192 in x and z)
#define LEVEL_BOUNDARY_MAX  0x2000 // 8192

#define CELL_SIZE           (1 << 10) // 0x400

#define CELL_HEIGHT_LIMIT           20000
#define FLOOR_LOWER_LIMIT           -11000
#define FLOOR_LOWER_LIMIT_MISC      (FLOOR_LOWER_LIMIT + 1000)
// same as FLOOR_LOWER_LIMIT_MISC, explicitly for shadow.c 
// It doesn't match if ".0" is removed or ".f" is added
#define FLOOR_LOWER_LIMIT_SHADOW    (FLOOR_LOWER_LIMIT + 1000.0)

struct WallCollisionData
{
    ///*0x00*/ f32 x, y, z;
	q32 xq, yq, zq;
    ///*0x0C*/ f32 offsetY;
	q32 offsetYq;
    ///*0x10*/ f32 radius;
	q32 radiusq;
    /*0x14*/ s16 unused;
    /*0x16*/ s16 numWalls;
    /*0x18*/ struct Surface *walls[4];
};

struct FloorGeometry
{
    q32 normalXq;
    q32 normalYq;
    q32 normalZq;
    q32 originOffsetq;
};

s32 q32_find_wall_collision(q32 *xPtrq, q32 *yPtrq, q32 *zPtrq, q32 offsetYq, q32 radiusq);
s32 find_wall_collisions(struct WallCollisionData *colData);
q32 find_ceilq(q32 posXq, q32 posYq, q32 posZq, struct Surface **pceil);
q32 find_floor_height_and_dataq(q32 xPosq, q32 yPosq, q32 zPosq, struct FloorGeometry **floorGeo);
q32 find_floor_heightq(q32 xq, q32 yq, q32 zq);
q32 find_floorq(q32 xPosq, q32 yPosq, q32 zPosq, struct Surface **pfloor);
f32 find_floor(f32 xPos, f32 yPos, f32 zPos, struct Surface **pfloor);
q32 find_water_levelq(q32 xq, q32 zq);
f32 find_water_level(f32 x, f32 z);
q32 find_poison_gas_levelq(q32 xq, q32 zq);
void debug_surface_list_infoq(q32 xPosq, q32 zPosq);

#endif // SURFACE_COLLISION_H
