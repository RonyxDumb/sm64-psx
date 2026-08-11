#include <PR/ultratypes.h>

#include "sm64.h"
#include "game/debug.h"
#include "game/level_update.h"
#include "game/mario.h"
#include "game/object_list_processor.h"
#include "surface_collision.h"
#include "surface_load.h"

/**************************************************
 *                      WALLS                     *
 **************************************************/

/**
 * Iterate through the list of walls until all walls are checked and
 * have given their wall push.
 */
static s32 find_wall_collisions_from_list(struct SurfaceNode *surfaceNode,
                                          struct WallCollisionData *data) {
    register struct Surface *surf;
    register q32 offsetq;
    register q32 radiusq = data->radiusq;
    register q32 xq = data->xq;
    register q32 yq = data->yq + data->offsetYq;
    register q32 zq = data->zq;
    register q32 pxq, pzq;
    register q32 w1q, w2q, w3q;
    register q32 y1q, y2q, y3q;
    s32 numCols = 0;

    // Max collision radius = 200
    if (radiusq > q(200)) {
        radiusq = q(200);
    }

    // Stay in this loop until out of walls.
    while (surfaceNode != NULL) {
        surf = surfaceNode->surface;
        surfaceNode = surfaceNode->next;

        // Exclude a large number of walls immediately to optimize.
        if (yq < q(surf->lowerY) || yq > q(surf->upperY)) {
            continue;
        }

        offsetq = q(
            xq / ONE * surf->compressed_normal.x / COMPRESSED_NORMAL_ONE +
            yq / ONE * surf->compressed_normal.y / COMPRESSED_NORMAL_ONE +
            zq / ONE * surf->compressed_normal.z / COMPRESSED_NORMAL_ONE
        ) + surf->originOffsetq;

        if (offsetq < -radiusq || offsetq > radiusq) {
            continue;
        }

        pxq = xq;
        pzq = zq;

        //! (Quantum Tunneling) Due to issues with the vertices walls choose and
        //  the fact they are floating point, certain floating point positions
        //  along the seam of two walls may collide with neither wall or both walls.
        if (surf->flags & SURFACE_FLAG_X_PROJECTION) {
            w1q = -q(surf->vertex1[2]);            w2q = -q(surf->vertex2[2]);            w3q = q(-surf->vertex3[2]);
            y1q = q(surf->vertex1[1]);            y2q = q(surf->vertex2[1]);            y3q = q(surf->vertex3[1]);

            if (surf->compressed_normal.x > 0) {
                if (qmul((y1q - yq) / (q32) 16, (w2q - w1q) / (q32) 16) - qmul((w1q - -pzq) / (q32) 16, (y2q - y1q) / (q32) 16) > 0) {
                    continue;
                }
                if (qmul((y2q - yq) / (q32) 16, (w3q - w2q) / (q32) 16) - qmul((w2q - -pzq) / (q32) 16, (y3q - y2q) / (q32) 16) > 0) {
                    continue;
                }
                if (qmul((y3q - yq) / (q32) 16, (w1q - w3q) / (q32) 16) - qmul((w3q - -pzq) / (q32) 16, (y1q - y3q) / (q32) 16) > 0) {
                    continue;
                }
            } else {
                if (qmul((y1q - yq) / (q32) 16, (w2q - w1q) / (q32) 16) - qmul((w1q - -pzq) / (q32) 16, (y2q - y1q) / (q32) 16) < 0) {
                    continue;
                }
                if (qmul((y2q - yq) / (q32) 16, (w3q - w2q) / (q32) 16) - qmul((w2q - -pzq) / (q32) 16, (y3q - y2q) / (q32) 16) < 0) {
                    continue;
                }
                if (qmul((y3q - yq) / (q32) 16, (w1q - w3q) / (q32) 16) - qmul((w3q - -pzq) / (q32) 16, (y1q - y3q) / (q32) 16) < 0) {
                    continue;
                }
            }
        } else {
            w1q = q(surf->vertex1[0]);            w2q = q(surf->vertex2[0]);            w3q = q(surf->vertex3[0]);
            y1q = q(surf->vertex1[1]);            y2q = q(surf->vertex2[1]);            y3q = q(surf->vertex3[1]);

            if (surf->compressed_normal.z > 0) {
                if (qmul((y1q - yq) / (q32) 16, (w2q - w1q) / (q32) 16) - qmul((w1q - pxq) / (q32) 16, (y2q - y1q) / (q32) 16) > 0) {
                    continue;
                }
                if (qmul((y2q - yq) / (q32) 16, (w3q - w2q) / (q32) 16) - qmul((w2q - pxq) / (q32) 16, (y3q - y2q) / (q32) 16) > 0) {
                    continue;
                }
                if (qmul((y3q - yq) / (q32) 16, (w1q - w3q) / (q32) 16) - qmul((w3q - pxq) / (q32) 16, (y1q - y3q) / (q32) 16) > 0) {
                    continue;
                }
            } else {
                if (qmul((y1q - yq) / (q32) 16, (w2q - w1q) / (q32) 16) - qmul((w1q - pxq) / (q32) 16, (y2q - y1q) / (q32) 16) < 0) {
                    continue;
                }
                if (qmul((y2q - yq) / (q32) 16, (w3q - w2q) / (q32) 16) - qmul((w2q - pxq) / (q32) 16, (y3q - y2q) / (q32) 16) < 0) {
                    continue;
                }
                if (qmul((y3q - yq) / (q32) 16, (w1q - w3q) / (q32) 16) - qmul((w3q - pxq) / (q32) 16, (y1q - y3q) / (q32) 16) < 0) {
                    continue;
                }
            }
        }

        // Determine if checking for the camera or not.
        if (gCheckingSurfaceCollisionsForCamera) {
            if (surf->flags & SURFACE_FLAG_NO_CAM_COLLISION) {
                continue;
            }
        } else {
            // Ignore camera only surfaces.
            if (surf->type == SURFACE_CAMERA_BOUNDARY) {
                continue;
            }

            // If an object can pass through a vanish cap wall, pass through.
            if (surf->type == SURFACE_VANISH_CAP_WALLS) {
                // If an object can pass through a vanish cap wall, pass through.
                if (gCurrentObject != NULL
                    && (gCurrentObject->activeFlags & ACTIVE_FLAG_MOVE_THROUGH_GRATE)) {
                    continue;
                }

                // If Mario has a vanish cap, pass through the vanish cap wall.
                if (gCurrentObject != NULL && gCurrentObject == gMarioObject
                    && (gMarioState->flags & MARIO_VANISH_CAP)) {
                    continue;
                }
            }
        }

        //! (Wall Overlaps) Because this doesn't update the x and z local variables,
        //  multiple walls can push mario more than is required.
        data->xq += surf->compressed_normal.x * (radiusq - offsetq) / COMPRESSED_NORMAL_ONE;
        data->zq += surf->compressed_normal.z * (radiusq - offsetq) / COMPRESSED_NORMAL_ONE;

        //! (Unreferenced Walls) Since this only returns the first four walls,
        //  this can lead to wall interaction being missed. Typically unreferenced walls
        //  come from only using one wall, however.
        if (data->numWalls < 4) {
            data->walls[data->numWalls++] = surf;
        }

        numCols++;
    }

    return numCols;
}

/**
 * Formats the position and wall search for find_wall_collisions.
 */
s32 q32_find_wall_collision(q32 *xPtrq, q32 *yPtrq, q32 *zPtrq, q32 offsetYq, q32 radiusq) {
    struct WallCollisionData collision;
    s32 numCollisions = 0;

	collision.offsetYq = offsetYq;
	collision.radiusq = radiusq;

    collision.xq = *xPtrq;
    collision.yq = *yPtrq;
    collision.zq = *zPtrq;

    collision.numWalls = 0;

    numCollisions = find_wall_collisions(&collision);

    *xPtrq = collision.xq;
    *yPtrq = collision.yq;
    *zPtrq = collision.zq;

    return numCollisions;
}

/**
 * Find wall collisions and receive their push.
 */
s32 find_wall_collisions(struct WallCollisionData *colData) {
    struct SurfaceNode *node;
    s16 cellX, cellZ;
    s32 numCollisions = 0;
    s16 x = qtrunc(colData->xq);
    s16 z = qtrunc(colData->zq);

    colData->numWalls = 0;

    if (x <= -LEVEL_BOUNDARY_MAX || x >= LEVEL_BOUNDARY_MAX) {
        return numCollisions;
    }
    if (z <= -LEVEL_BOUNDARY_MAX || z >= LEVEL_BOUNDARY_MAX) {
        return numCollisions;
    }

    // World (level) consists of a 16x16 grid. Find where the collision is on
    // the grid (round toward -inf)
    cellX = ((x + LEVEL_BOUNDARY_MAX) / CELL_SIZE) & NUM_CELLS_INDEX;
    cellZ = ((z + LEVEL_BOUNDARY_MAX) / CELL_SIZE) & NUM_CELLS_INDEX;

    // Check for surfaces belonging to objects.
    node = gDynamicSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_WALLS].next;
    numCollisions += find_wall_collisions_from_list(node, colData);

    // Check for surfaces that are a part of level geometry.
    node = gStaticSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_WALLS].next;
    numCollisions += find_wall_collisions_from_list(node, colData);

    // Increment the debug tracker.
    gNumCalls.wall += 1;

    return numCollisions;
}

/**************************************************
 *                     CEILINGS                   *
 **************************************************/

/**
 * Iterate through the list of ceilings and find the first ceiling over a given point.
 */
static struct Surface *find_ceil_from_list(struct SurfaceNode *surfaceNode, s32 x, s32 y, s32 z, q32 *pheightq) {
    register struct Surface *surf;
    register s32 x1, z1, x2, z2, x3, z3;
    struct Surface *ceil = NULL;

    ceil = NULL;

    // Stay in this loop until out of ceilings.
    while (surfaceNode != NULL) {
        surf = surfaceNode->surface;
        surfaceNode = surfaceNode->next;

        x1 = surf->vertex1[0];
        z1 = surf->vertex1[2];
        z2 = surf->vertex2[2];
        x2 = surf->vertex2[0];

        // Checking if point is in bounds of the triangle laterally.
        if ((z1 - z) * (x2 - x1) - (x1 - x) * (z2 - z1) > 0) {
            continue;
        }

        // Slight optimization by checking these later.
        x3 = surf->vertex3[0];
        z3 = surf->vertex3[2];
        if ((z2 - z) * (x3 - x2) - (x2 - x) * (z3 - z2) > 0) {
            continue;
        }
        if ((z3 - z) * (x1 - x3) - (x3 - x) * (z1 - z3) > 0) {
            continue;
        }

        // Determine if checking for the camera or not.
        if (gCheckingSurfaceCollisionsForCamera != 0) {
            if (surf->flags & SURFACE_FLAG_NO_CAM_COLLISION) {
                continue;
            }
        }
        // Ignore camera only surfaces.
        else if (surf->type == SURFACE_CAMERA_BOUNDARY) {
            continue;
        }

        {
            q32 nxq = (q32) surf->compressed_normal.x * QONE / COMPRESSED_NORMAL_ONE;
            q32 nyq = (q32) surf->compressed_normal.y * QONE / COMPRESSED_NORMAL_ONE;
            q32 nzq = (q32) surf->compressed_normal.z * QONE / COMPRESSED_NORMAL_ONE;
            q32 ooq = surf->originOffsetq;
            q32 heightq;

            // If a wall, ignore it. Likely a remnant, should never occur.
            if (nyq == 0) {
                continue;
            }

            // Find the ceil height at the specific point.
            heightq = qdiv((q32) -(x * nxq + nzq * z + ooq), nyq);

            // Checks for ceiling interaction with a 78 unit buffer.
            //! (Exposed Ceilings) Because any point above a ceiling counts
            //  as interacting with a ceiling, ceilings far below can cause
            // "invisible walls" that are really just exposed ceilings.
            if (y - qtrunc(heightq + q(78)) > 0) {
                continue;
            }

            *pheightq = heightq;
            ceil = surf;
            break;
        }
    }

    //! (Surface Cucking) Since only the first ceil is returned and not the lowest,
    //  lower ceilings can be "cucked" by higher ceilings.
    return ceil;
}

/**
 * Find the lowest ceiling above a given position and return the height.
 */
q32 find_ceilq(q32 posXq, q32 posYq, q32 posZq, struct Surface **pceil) {
    s16 cellZ, cellX;
    struct Surface *ceil, *dynamicCeil;
    struct SurfaceNode *surfaceList;
    q32 heightq = q(CELL_HEIGHT_LIMIT);
    q32 dynamicHeightq = q(CELL_HEIGHT_LIMIT);
    s16 x, y, z;

    //! (Parallel Universes) Because position is casted to an s16, reaching higher
    // float locations  can return ceilings despite them not existing there.
    //(Dynamic ceilings will unload due to the range.)
    x = qtrunc(posXq);
    y = qtrunc(posYq);
    z = qtrunc(posZq);
    *pceil = NULL;

    if (x <= -LEVEL_BOUNDARY_MAX || x >= LEVEL_BOUNDARY_MAX) {
        return heightq;
    }
    if (z <= -LEVEL_BOUNDARY_MAX || z >= LEVEL_BOUNDARY_MAX) {
        return heightq;
    }

    // Each level is split into cells to limit load, find the appropriate cell.
    cellX = ((x + LEVEL_BOUNDARY_MAX) / CELL_SIZE) & NUM_CELLS_INDEX;
    cellZ = ((z + LEVEL_BOUNDARY_MAX) / CELL_SIZE) & NUM_CELLS_INDEX;

    // Check for surfaces belonging to objects.
    surfaceList = gDynamicSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_CEILS].next;
    dynamicCeil = find_ceil_from_list(surfaceList, x, y, z, &dynamicHeightq);

    // Check for surfaces that are a part of level geometry.
    surfaceList = gStaticSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_CEILS].next;
    ceil = find_ceil_from_list(surfaceList, x, y, z, &heightq);

    if (dynamicHeightq < heightq) {
        ceil = dynamicCeil;
        heightq = dynamicHeightq;
    }

    *pceil = ceil;

    // Increment the debug tracker.
    gNumCalls.ceil += 1;

    return heightq;
}

/**************************************************
 *                     FLOORS                     *
 **************************************************/

/**
 * Find the height of the highest floor below an object.
 */
/*f32 unused_obj_find_floor_height(struct Object *obj) {
    struct Surface *floor;
    f32 floorHeight = find_floor(FFIELD(obj, oPosX), FFIELD(obj, oPosY), FFIELD(obj, oPosZ), &floor);
    return floorHeight;
}*/

/**
 * Basically a local variable that passes through floor geo info.
 */
struct FloorGeometry sFloorGeo;

/**
 * Return the floor height underneath (xPos, yPos, zPos) and populate `floorGeo`
 * with data about the floor's normal vector and origin offset. Also update
 * sFloorGeo.
 */
q32 find_floor_height_and_dataq(q32 xPosq, q32 yPosq, q32 zPosq, struct FloorGeometry **floorGeo) {
    struct Surface *floor;
    q32 floorHeightq = find_floorq(xPosq, yPosq, zPosq, &floor);

    *floorGeo = NULL;

    if (floor != NULL) {
        sFloorGeo.normalXq = (q32) floor->compressed_normal.x * QONE / COMPRESSED_NORMAL_ONE;
        sFloorGeo.normalYq = (q32) floor->compressed_normal.y * QONE / COMPRESSED_NORMAL_ONE;
        sFloorGeo.normalZq = (q32) floor->compressed_normal.z * QONE / COMPRESSED_NORMAL_ONE;
        sFloorGeo.originOffsetq = floor->originOffsetq;

        *floorGeo = &sFloorGeo;
    }
    return floorHeightq;
}

/**
 * Iterate through the list of floors and find the first floor under a given point.
 */
static struct Surface *find_floor_from_listq(struct SurfaceNode *surfaceNode, s32 x, s32 y, s32 z, q32 *pheightq) {
    register struct Surface *surf;
    register s32 x1, z1, x2, z2, x3, z3;
    q32 nxq, nyq, nzq;
    q32 ooq;
    q32 heightq;
    struct Surface *floor = NULL;

    // Iterate through the list of floors until there are no more floors.
    while (surfaceNode != NULL) {
        surf = surfaceNode->surface;
        surfaceNode = surfaceNode->next;

        x1 = surf->vertex1[0];
        z1 = surf->vertex1[2];
        x2 = surf->vertex2[0];
        z2 = surf->vertex2[2];

        // Check that the point is within the triangle bounds.
        if ((z1 - z) * (x2 - x1) - (x1 - x) * (z2 - z1) < 0) {
            continue;
        }

        // To slightly save on computation time, set this later.
        x3 = surf->vertex3[0];
        z3 = surf->vertex3[2];

        if ((z2 - z) * (x3 - x2) - (x2 - x) * (z3 - z2) < 0) {
            continue;
        }
        if ((z3 - z) * (x1 - x3) - (x3 - x) * (z1 - z3) < 0) {
            continue;
        }

        // Determine if we are checking for the camera or not.
        if (gCheckingSurfaceCollisionsForCamera != 0) {
            if (surf->flags & SURFACE_FLAG_NO_CAM_COLLISION) {
                continue;
            }
        }
        // If we are not checking for the camera, ignore camera only floors.
        else if (surf->type == SURFACE_CAMERA_BOUNDARY) {
            continue;
        }

        nxq = (q32) surf->compressed_normal.x * QONE / COMPRESSED_NORMAL_ONE;
        nyq = (q32) surf->compressed_normal.y * QONE / COMPRESSED_NORMAL_ONE;
        nzq = (q32) surf->compressed_normal.z * QONE / COMPRESSED_NORMAL_ONE;
        ooq = surf->originOffsetq;

        // If a wall, ignore it. Likely a remnant, should never occur.
        if (nyq == 0) {
            continue;
        }

        // Find the height of the floor at a given location.
        heightq = qdiv((q32) -(x * nxq + nzq * z + ooq), nyq);
        // Checks for floor interaction with a 78 unit buffer.
        if (y - qtrunc(heightq - q(78)) < 0) {
            continue;
        }

        *pheightq = heightq;
        floor = surf;
        break;
    }

    //! (Surface Cucking) Since only the first floor is returned and not the highest,
    //  higher floors can be "cucked" by lower floors.
    return floor;
}

/**
 * Find the height of the highest floor below a point.
 */
q32 find_floor_heightq(q32 xq, q32 yq, q32 zq) {
    struct Surface *floor;

    q32 floorHeightq = find_floorq(xq, yq, zq, &floor);

    return floorHeightq;
}

/**
 * Find the highest dynamic floor under a given position. Perhaps originally static
 * and dynamic floors were checked separately.
 */
q32 unused_find_dynamic_floorq(q32 xPosq, q32 yPosq, q32 zPosq, struct Surface **pfloor) {
    struct SurfaceNode *surfaceList;
    struct Surface *floor;
    q32 floorHeightq = q(FLOOR_LOWER_LIMIT);

    // Would normally cause PUs, but dynamic floors unload at that range.
    s16 x = (s16) qtrunc(xPosq);
    s16 y = (s16) qtrunc(yPosq);
    s16 z = (s16) qtrunc(zPosq);

    // Each level is split into cells to limit load, find the appropriate cell.
    s16 cellX = ((x + LEVEL_BOUNDARY_MAX) / CELL_SIZE) & NUM_CELLS_INDEX;
    s16 cellZ = ((z + LEVEL_BOUNDARY_MAX) / CELL_SIZE) & NUM_CELLS_INDEX;

    surfaceList = gDynamicSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_FLOORS].next;
    floor = find_floor_from_listq(surfaceList, x, y, z, &floorHeightq);

    *pfloor = floor;

    return floorHeightq;
}

/**
 * Find the highest floor under a given position and return the height.
 */
q32 find_floorq(q32 xPosq, q32 yPosq, q32 zPosq, struct Surface **pfloor) {
    s16 cellZ, cellX;

    struct Surface *floor, *dynamicFloor;
    struct SurfaceNode *surfaceList;

    q32 heightq = q(FLOOR_LOWER_LIMIT);
    q32 dynamicHeightq = q(FLOOR_LOWER_LIMIT);

    //! (Parallel Universes) Because position is casted to an s16, reaching higher
    // float locations  can return floors despite them not existing there.
    //(Dynamic floors will unload due to the range.)
    s16 x = qtrunc(xPosq);
    s16 y = qtrunc(yPosq);
    s16 z = qtrunc(zPosq);

    *pfloor = NULL;

    if (x <= -LEVEL_BOUNDARY_MAX || x >= LEVEL_BOUNDARY_MAX) {
        return heightq;
    }
    if (z <= -LEVEL_BOUNDARY_MAX || z >= LEVEL_BOUNDARY_MAX) {
        return heightq;
    }

    // Each level is split into cells to limit load, find the appropriate cell.
    cellX = ((x + LEVEL_BOUNDARY_MAX) / CELL_SIZE) & NUM_CELLS_INDEX;
    cellZ = ((z + LEVEL_BOUNDARY_MAX) / CELL_SIZE) & NUM_CELLS_INDEX;

    // Check for surfaces belonging to objects.
    surfaceList = gDynamicSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_FLOORS].next;
    dynamicFloor = find_floor_from_listq(surfaceList, x, y, z, &dynamicHeightq);

    // Check for surfaces that are a part of level geometry.
    surfaceList = gStaticSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_FLOORS].next;
    floor = find_floor_from_listq(surfaceList, x, y, z, &heightq);

    // To prevent the Merry-Go-Round room from loading when Mario passes above the hole that leads
    // there, SURFACE_INTANGIBLE is used. This prevent the wrong room from loading, but can also allow
    // Mario to pass through.
    if (!gFindFloorIncludeSurfaceIntangible) {
        //! (BBH Crash) Most NULL checking is done by checking the height of the floor returned
        //  instead of checking directly for a NULL floor. If this check returns a NULL floor
        //  (happens when there is no floor under the SURFACE_INTANGIBLE floor) but returns the height
        //  of the SURFACE_INTANGIBLE floor instead of the typical -11000 returned for a NULL floor.
        if (floor != NULL && floor->type == SURFACE_INTANGIBLE) {
            floor = find_floor_from_listq(surfaceList, x, qtrunc(heightq) - 200, z, &heightq);
        }
    } else {
        // To prevent accidentally leaving the floor tangible, stop checking for it.
        gFindFloorIncludeSurfaceIntangible = FALSE;
    }

    // If a floor was missed, increment the debug counter.
    if (floor == NULL) {
        gNumFindFloorMisses += 1;
    }

    if (dynamicHeightq > heightq) {
        floor = dynamicFloor;
        heightq = dynamicHeightq;
    }

    *pfloor = floor;

    // Increment the debug tracker.
    gNumCalls.floor += 1;

    return heightq;
}

f32 find_floor(f32 xPos, f32 yPos, f32 zPos, struct Surface **pfloor) {
	return qtof(find_floorq(q(xPos), q(yPos), q(zPos), pfloor));
}

/**************************************************
 *               ENVIRONMENTAL BOXES              *
 **************************************************/

/**
 * Finds the height of water at a given location.
 */
q32 find_water_levelq(q32 xq, q32 zq) {
    s32 i;
    s32 numRegions;
    s16 val;
    q32 loXq, hiXq, loZq, hiZq;
    q32 waterLevelq = q(FLOOR_LOWER_LIMIT);
    s16 *p = gEnvironmentRegions;

    if (p != NULL) {
        numRegions = *p++;

        for (i = 0; i < numRegions; i++) {
            val = *p++;
            loXq = q(*p++);
            loZq = q(*p++);
            hiXq = q(*p++);
            hiZq = q(*p++);

            // If the location is within a water box and it is a water box.
            // Water is less than 50 val only, while above is gas and such.
            if (loXq < xq && xq < hiXq && loZq < zq && zq < hiZq && val < 50) {
                // Set the water height. Since this breaks, only return the first height.
                waterLevelq = q(*p);
                break;
            }
            p++;
        }
    }

    return waterLevelq;
}
f32 find_water_level(f32 x, f32 z) {
	return qtof(find_water_levelq(q(x), q(z)));
}

/**
 * Finds the height of the poison gas (used only in HMC) at a given location.
 */
q32 find_poison_gas_levelq(q32 xq, q32 zq) {
    s32 i;
    s32 numRegions;
    s16 val;
    q32 loXq, hiXq, loZq, hiZq;
    q32 gasLevelq = q(FLOOR_LOWER_LIMIT);
    s16 *p = gEnvironmentRegions;

    if (p != NULL) {
        numRegions = *p++;

        for (i = 0; i < numRegions; i++) {
            val = *p;

            if (val >= 50) {
                loXq = q(p[1]);
                loZq = q(p[2]);
                hiXq = q(p[3]);
                hiZq = q(p[4]);

                // If the location is within a gas's box and it is a gas box.
                // Gas has a value of 50, 60, etc.
                if (loXq < xq && xq < hiXq && loZq < zq && zq < hiZq && val % 10 == 0) {
                    // Set the gas height. Since this breaks, only return the first height.
                    gasLevelq = q(p[5]);
                    break;
                }
            }

            p += 6;
        }
    }

    return gasLevelq;
}

/**************************************************
 *                      DEBUG                     *
 **************************************************/

/**
 * Finds the length of a surface list for debug purposes.
 */
static s32 surface_list_length(struct SurfaceNode *list) {
    s32 count = 0;

    while (list != NULL) {
        list = list->next;
        count++;
    }

    return count;
}

/**
 * Print the area,number of walls, how many times they were called,
 * and some allocation information.
 */
void debug_surface_list_infoq(q32 xPosq, q32 zPosq) {
    struct SurfaceNode *list;
    s32 numFloors = 0;
    s32 numWalls = 0;
    s32 numCeils = 0;

    s32 cellX = (qtrunc(xPosq) + LEVEL_BOUNDARY_MAX) / CELL_SIZE;
    s32 cellZ = (qtrunc(zPosq) + LEVEL_BOUNDARY_MAX) / CELL_SIZE;

    list = gStaticSurfacePartition[cellZ & NUM_CELLS_INDEX][cellX & NUM_CELLS_INDEX][SPATIAL_PARTITION_FLOORS].next;
    numFloors += surface_list_length(list);

    list = gDynamicSurfacePartition[cellZ & NUM_CELLS_INDEX][cellX & NUM_CELLS_INDEX][SPATIAL_PARTITION_FLOORS].next;
    numFloors += surface_list_length(list);

    list = gStaticSurfacePartition[cellZ & NUM_CELLS_INDEX][cellX & NUM_CELLS_INDEX][SPATIAL_PARTITION_WALLS].next;
    numWalls += surface_list_length(list);

    list = gDynamicSurfacePartition[cellZ & NUM_CELLS_INDEX][cellX & NUM_CELLS_INDEX][SPATIAL_PARTITION_WALLS].next;
    numWalls += surface_list_length(list);

    list = gStaticSurfacePartition[cellZ & NUM_CELLS_INDEX][cellX & NUM_CELLS_INDEX][SPATIAL_PARTITION_CEILS].next;
    numCeils += surface_list_length(list);

    list = gDynamicSurfacePartition[cellZ & NUM_CELLS_INDEX][cellX & NUM_CELLS_INDEX][SPATIAL_PARTITION_CEILS].next;
    numCeils += surface_list_length(list);

    print_debug_top_down_mapinfo("area   %x", cellZ * NUM_CELLS + cellX);

    // Names represent ground, walls, and roofs as found in SMS.
    print_debug_top_down_mapinfo("dg %d", numFloors);
    print_debug_top_down_mapinfo("dw %d", numWalls);
    print_debug_top_down_mapinfo("dr %d", numCeils);

    set_text_array_x_y(80, -3);

    print_debug_top_down_mapinfo("%d", gNumCalls.floor);
    print_debug_top_down_mapinfo("%d", gNumCalls.wall);
    print_debug_top_down_mapinfo("%d", gNumCalls.ceil);

    set_text_array_x_y(-80, 0);

    // listal- List Allocated?, statbg- Static Background?, movebg- Moving Background?
    print_debug_top_down_mapinfo("listal %d", gSurfaceNodesAllocated);
    print_debug_top_down_mapinfo("statbg %d", gNumStaticSurfaces);
    print_debug_top_down_mapinfo("movebg %d", gSurfacesAllocated - gNumStaticSurfaces);

    gNumCalls.floor = 0;
    gNumCalls.ceil = 0;
    gNumCalls.wall = 0;
}

/**
 * An unused function that finds and interacts with any type of surface.
 * Perhaps an original implementation of surfaces before they were more specialized.
 */
/*s32 unused_resolve_floor_or_ceil_collisions(s32 checkCeil, f32 *px, f32 *py, f32 *pz, f32 radius,
                                            struct Surface **psurface, f32 *surfaceHeight) {
    f32 nx, ny, nz, oo;
    f32 x = *px;
    f32 y = *py;
    f32 z = *pz;
    f32 offset, distance;

    *psurface = NULL;

    if (checkCeil) {
        *surfaceHeight = find_ceil(x, y, z, psurface);
    } else {
        *surfaceHeight = find_floor(x, y, z, psurface);
    }

    if (*psurface == NULL) {
        return -1;
    }

    nx = qtof((*psurface)->normalq.x);
    ny = qtof((*psurface)->normalq.y);
    nz = qtof((*psurface)->normalq.z);
    oo = qtof((*psurface)->originOffsetq);

    offset = nx * x + ny * y + nz * z + oo;
    distance = offset >= 0 ? offset : -offset;

    // Interesting surface interaction that should be surf type independent.
    if (distance < radius) {
        *px += nx * (radius - offset);
        *py += ny * (radius - offset);
        *pz += nz * (radius - offset);

        return 1;
    }

    return 0;
}*/
