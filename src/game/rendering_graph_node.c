#include <PR/ultratypes.h>

#include "area.h"
#include "engine/graph_node.h"
#include "engine/math_util.h"
#include "engine/surface_collision.h"
#include "game_init.h"
#include "gfx_dimensions.h"
#include "main.h"
#include "memory.h"
#include "port/gfx/gfx_internal.h"
#include "print.h"
#include "rendering_graph_node.h"
#include "shadow.h"
#include "sm64.h"

// this file is a rewrite of rendering_graph_node.c that is more optimized for the port
// and tries to use fixed point math only. wish me luck

#include <port/gfx/gfx.h>

struct GraphNodeRoot* gCurGraphNodeRoot = NULL;
struct GraphNodeMasterList* gCurGraphNodeMasterList = NULL;
struct GraphNodePerspective* gCurGraphNodeCamFrustum = NULL;
struct GraphNodeCamera* gCurGraphNodeCamera = NULL;
struct GraphNodeObject* gCurGraphNodeObject = NULL;
struct GraphNodeHeldObject* gCurGraphNodeHeldObject = NULL;
u16 gAreaUpdateCounter = 0;

static void geo_process_node_and_siblings(struct GraphNode* firstNode);
static void geo_process_object(struct Object* node);
static void geo_process_display_list(struct GraphNodeDisplayList* node);
static void geo_process_generated_list(struct GraphNodeGenerated* node);
static void geo_process_ortho_projection(struct GraphNodeOrthoProjection* node);
static void geo_process_perspective(struct GraphNodePerspective* node);
static void geo_process_camera(struct GraphNodeCamera* node);
static void geo_process_translation_rotation(struct GraphNodeTranslationRotation* node);
static void geo_process_translation(struct GraphNodeTranslation* node);
static void geo_process_rotation(struct GraphNodeRotation* node);
static void geo_process_scale(struct GraphNodeScale* node);
static void geo_process_animated_part(struct GraphNodeAnimatedPart* node);
static void geo_process_switch(struct GraphNodeSwitchCase* node);
static void geo_process_level_of_detail(struct GraphNodeLevelOfDetail* node);
static void geo_process_billboard(struct GraphNodeBillboard* node);
static void geo_process_background(struct GraphNodeBackground* node);
static void geo_process_shadow(struct GraphNodeShadow* node);
static void geo_process_master_list(struct GraphNodeMasterList* node);

//bool geo_using_zbuffer = false;

static ShortMatrix* save_matrix(ShortMatrix mtx) {
	ShortMatrix* alloc = alloc_display_list(sizeof(ShortMatrix));
	*alloc = mtx;
	return alloc;
}

bool matrix_changed = false;

void update_matrix() {
	if(matrix_changed) {
		matrix_changed = false;
		gfx_emit_mtx_set(save_matrix(gfx_modelview_get()));
	}
}

static void geo_append_display_list(Gfx* display_list, UNUSED s16 layer) {
	update_matrix();
	gfx_emit_call(segmented_to_virtual(display_list));
}

// the entry point: process the root of the graph
void geo_process_root(struct GraphNodeRoot* node, UNUSED Vp* b, UNUSED Vp* c, UNUSED s32 clearColor) {
	if(node->node.flags & GRAPH_RENDER_ACTIVE) {
		// prep work for a frame
		//geo_using_zbuffer = false;
		gfx_modelview_identity();
		matrix_changed = true;
		if(node->node.children) {
			gCurGraphNodeRoot = node;
			geo_process_node_and_siblings(node->node.children);
		}
		gCurGraphNodeRoot = NULL;
	}
}

static inline void geo_try_process_children(struct GraphNode *node) {
	if(node->children) {
		geo_process_node_and_siblings(node->children);
	}
}

static void geo_process_object_parent(struct GraphNodeObjectParent *node) {
	if(node->sharedChild) {
		node->sharedChild->parent = (struct GraphNode*) node;
		geo_process_node_and_siblings(node->sharedChild);
		node->sharedChild->parent = NULL;
	}
	if(node->node.children) {
		geo_process_node_and_siblings(node->node.children);
	}
}

static void geo_process_node_and_siblings(struct GraphNode* first_node) {
	struct GraphNode* node = first_node;
	struct GraphNode* parent = node->parent;
	bool iterate_children = !parent || parent->type != GRAPH_NODE_TYPE_SWITCH_CASE;
	do {
		if(node->flags & GRAPH_RENDER_ACTIVE) {
			if(node->flags & GRAPH_RENDER_CHILDREN_FIRST) {
				geo_try_process_children(node);
			} else {
				switch(node->type) {
					case GRAPH_NODE_TYPE_OBJECT:
						geo_process_object((struct Object*) node);
						break;
					case GRAPH_NODE_TYPE_OBJECT_PARENT:
						geo_process_object_parent((struct GraphNodeObjectParent*) node);
						break;
					case GRAPH_NODE_TYPE_ORTHO_PROJECTION:
						geo_process_ortho_projection((struct GraphNodeOrthoProjection*) node);
						break;
					case GRAPH_NODE_TYPE_PERSPECTIVE:
						geo_process_perspective((struct GraphNodePerspective*) node);
						break;
					case GRAPH_NODE_TYPE_CAMERA:
						geo_process_camera((struct GraphNodeCamera*) node);
						break;
					case GRAPH_NODE_TYPE_SWITCH_CASE:
						geo_process_switch((struct GraphNodeSwitchCase*) node);
						break;
					case GRAPH_NODE_TYPE_LEVEL_OF_DETAIL:
						geo_process_level_of_detail((struct GraphNodeLevelOfDetail*) node);
						break;
					case GRAPH_NODE_TYPE_DISPLAY_LIST:
						geo_process_display_list((struct GraphNodeDisplayList*) node);
						break;
					case GRAPH_NODE_TYPE_GENERATED_LIST:
						geo_process_generated_list((struct GraphNodeGenerated*) node);
						break;
					case GRAPH_NODE_TYPE_SCALE:
						geo_process_scale((struct GraphNodeScale*) node);
						break;
					case GRAPH_NODE_TYPE_TRANSLATION:
						geo_process_translation((struct GraphNodeTranslation*) node);
						break;
					case GRAPH_NODE_TYPE_ROTATION:
						geo_process_rotation((struct GraphNodeRotation*) node);
						break;
					case GRAPH_NODE_TYPE_TRANSLATION_ROTATION:
						geo_process_translation_rotation((struct GraphNodeTranslationRotation*) node);
						break;
					case GRAPH_NODE_TYPE_ANIMATED_PART:
						geo_process_animated_part((struct GraphNodeAnimatedPart*) node);
						break;
					case GRAPH_NODE_TYPE_BILLBOARD:
						geo_process_billboard((struct GraphNodeBillboard*) node);
						break;
					case GRAPH_NODE_TYPE_BACKGROUND:
						geo_process_background((struct GraphNodeBackground*) node);
						break;
					case GRAPH_NODE_TYPE_MASTER_LIST:
						geo_process_master_list((struct GraphNodeMasterList*) node);
						break;
					case GRAPH_NODE_TYPE_SHADOW:
						geo_process_shadow((struct GraphNodeShadow*) node);
						break;
					default:
						geo_try_process_children(node);
				}
			}
		} else if(node->type == GRAPH_NODE_TYPE_OBJECT) {
			((struct GraphNodeObject*) node)->throwMatrixq = NULL;
		}
	} while(iterate_children && node->next && (node = node->next) != first_node);
}

// specific node type processing functions

static bool obj_is_in_view(struct GraphNodeObject* node, s16 translation[3]) {
	if(node->node.flags & GRAPH_RENDER_INVISIBLE) {
		return false;
	}
	struct GraphNode* geo = node->sharedChild;
	s32 culling_radius;
	if(geo && geo->type == GRAPH_NODE_TYPE_CULLING_RADIUS) {
		culling_radius = ((struct GraphNodeCullingRadius*) geo)->cullingRadius;
		if(culling_radius > MAX_Z) {
			culling_radius = MAX_Z;
		}
	} else {
		culling_radius = 300;
	}
	s32 depth = -translation[2];
	// no if the object is close to or far from the camera; calc this before the more complicated math
	if(depth > culling_radius - 100 || depth < -culling_radius - 20000) {
		return false;
	}
	q32 fovq = gCurGraphNodeCamFrustum->fovq;
	// TODO: check if the aspect ratio bug mentioned in the decomp still happens, if so, add a multiply by 4:3 here
	s16 half_fov = qtrunc((fovq / 2 + 1) * (32768 / 180));
	s32 h_screen_edge = -depth * sinqs(half_fov) / cosqs(half_fov);
	// no if off either edge of the screen
	if(translation[0] > h_screen_edge + culling_radius || translation[0] < -h_screen_edge - culling_radius) {
		return false;
	}
	return true;
}

u8 gCurAnimType;
u8 gCurAnimEnabled;
s16 gCurrAnimFrame;
q32 gCurAnimTranslationMultiplierq;
u16* gCurrAnimAttribute;
const s16* gCurAnimData;
static bool is_anim_compressed = false;

static void geo_set_animation_globals(struct AnimInfo* node, s32 hasAnimation) {
	struct Animation* anim = node->curAnim;
	if(hasAnimation) {
		node->animFrame = geo_update_animation_frame(node, &node->animFrameAccelAssist);
	}
	node->animTimer = gAreaUpdateCounter;
	if(anim->flags & ANIM_FLAG_HOR_TRANS) {
		gCurAnimType = ANIM_TYPE_VERTICAL_TRANSLATION;
	} else if(anim->flags & ANIM_FLAG_VERT_TRANS) {
		gCurAnimType = ANIM_TYPE_LATERAL_TRANSLATION;
	} else if(anim->flags & ANIM_FLAG_6) {
		gCurAnimType = ANIM_TYPE_NO_TRANSLATION;
	} else {
		gCurAnimType = ANIM_TYPE_TRANSLATION;
	}
	is_anim_compressed = (anim->flags & ANIM_FLAG_7) != 0;

	gCurrAnimFrame = node->animFrame;
	gCurAnimEnabled = !(anim->flags & ANIM_FLAG_5);
	gCurrAnimAttribute = segmented_to_virtual(anim->index);
	gCurAnimData = segmented_to_virtual(anim->values);

	if (anim->animYTransDivisor) {
		gCurAnimTranslationMultiplierq = q(node->animYTrans) / anim->animYTransDivisor;
	} else {
		gCurAnimTranslationMultiplierq = q(1);
	}
}

static void geo_process_object(struct Object* node) {
	bool has_anim = node->header.gfx.node.flags & GRAPH_RENDER_HAS_ANIMATION;
	if(node->header.gfx.areaIndex == gCurGraphNodeRoot->areaIndex) {
		ShortMatrix bak = gfx_modelview_get();
		if(node->header.gfx.throwMatrixq) {
			gfx_modelview_mul(node->header.gfx.throwMatrixq);
		} else if(node->header.gfx.node.flags & GRAPH_RENDER_BILLBOARD) {
			mtx_billboard(&scratch_mtx, &bak, node->header.gfx.posi, gCurGraphNodeCamera->roll);
			gfx_modelview_set(&scratch_mtx);
		} else {
			gfx_modelview_translatei(node->header.gfx.posi);
			gfx_modelview_rotate_zxy(node->header.gfx.angle);
		}
		gfx_modelview_scaleq(node->header.gfx.scaleq);
		scratch_mtx = gfx_modelview_get();
		ShortMatrix modelview = scratch_mtx;
		node->header.gfx.throwMatrixq = &modelview;
		node->header.gfx.cameraToObject[0] = scratch_mtx.t[0];
		node->header.gfx.cameraToObject[1] = scratch_mtx.t[1];
		node->header.gfx.cameraToObject[2] = scratch_mtx.t[2];
		matrix_changed = true;

		if(node->header.gfx.animInfo.curAnim) {
			geo_set_animation_globals(&node->header.gfx.animInfo, has_anim);
		}

		if(obj_is_in_view(&node->header.gfx, modelview.t)) {
			if(node->header.gfx.sharedChild) {
				gCurGraphNodeObject = (struct GraphNodeObject*) node;
				node->header.gfx.sharedChild->parent = &node->header.gfx.node;
				geo_process_node_and_siblings(node->header.gfx.sharedChild);
				node->header.gfx.sharedChild->parent = NULL;
				gCurGraphNodeObject = NULL;
			}
			if(node->header.gfx.node.children) {
				geo_process_node_and_siblings(node->header.gfx.node.children);
			}
		}
		gfx_modelview_set(&bak);
		gCurAnimType = ANIM_TYPE_NONE;
		node->header.gfx.throwMatrixq = NULL;
	}
}

static void geo_process_display_list(struct GraphNodeDisplayList* node) {
	if(node->displayList) {
		geo_append_display_list(node->displayList, node->node.flags >> 8);
	}
	if(node->node.children) {
		geo_process_node_and_siblings(node->node.children);
	}
}

/**
 * Process a generated list. Instead of storing a pointer to a display list,
 * the list is generated on the fly by a function.
 */
static void geo_process_generated_list(struct GraphNodeGenerated* node) {
	if(node->fnNode.func) {
		ShortMatrix modelview = gfx_modelview_get();
		//mtx_transpose(&modelview);
		Gfx* list = segmented_to_virtual(node->fnNode.func(GEO_CONTEXT_RENDER, &node->fnNode.node, &modelview));
		if(list) {
			geo_append_display_list((void *) VIRTUAL_TO_PHYSICAL(list), node->fnNode.node.flags >> 8);
		}
	}
	if(node->fnNode.node.children) {
		geo_process_node_and_siblings(node->fnNode.node.children);
	}
}

//bool is_ortho = false;
//q32 ortho_xscaleq, ortho_yscaleq;
//s16 ortho_xoff, ortho_yoff;

static void geo_process_ortho_projection(struct GraphNodeOrthoProjection* node) {
	if(node->node.children != NULL) {
		//q32 scaleq = q(node->scale);
		//s16 left = (gCurGraphNodeRoot->x - gCurGraphNodeRoot->width) * scaleq / QONE;
		//s16 right = (gCurGraphNodeRoot->x + gCurGraphNodeRoot->width) * scaleq / QONE;
		//s16 top = (gCurGraphNodeRoot->y - gCurGraphNodeRoot->height) * scaleq / QONE;
		//s16 bottom = (gCurGraphNodeRoot->y + gCurGraphNodeRoot->height) * scaleq / QONE;
		//ortho_xscaleq = q(320) / (right - left);
		//ortho_yscaleq = q(240) / (bottom - top);
		//ortho_xoff = left;
		//ortho_yoff = top;
		gfx_emit_set_ortho(true);
		gfx_emit_multiplier(1);
		geo_process_node_and_siblings(node->node.children);
		//is_ortho = false;
	}
}

static void geo_process_perspective(struct GraphNodePerspective* node) {
	if(node->fnNode.func != NULL) {
		ShortMatrix modelview = gfx_modelview_get();
		node->fnNode.func(GEO_CONTEXT_RENDER, &node->fnNode.node, &modelview);
	}
	if (node->fnNode.node.children != NULL) {
		gCurGraphNodeCamFrustum = node;
		gfx_emit_set_ortho(false);
		gfx_emit_multiplier(node->fovq > 0? ONE * YRES / 2 / (node->fovq * 45 / ONE): 0);
		geo_process_node_and_siblings(node->fnNode.node.children);
		gCurGraphNodeCamFrustum = NULL;
	}
}

static void geo_process_camera(struct GraphNodeCamera* node) {
	ShortMatrix bak = gfx_modelview_get();
	if(node->fnNode.func) {
		node->fnNode.func(GEO_CONTEXT_RENDER, &node->fnNode.node, &bak);
	}
	// TODO
	//gfx_modelview_rotate(0, 0, 1, node->rollScreen);

	ShortMatrix transform;
	mtx_lookat(&transform, node->posq, node->focusq, node->roll);

	gfx_modelview_mul(&transform);
	matrix_changed = true;

	if(node->fnNode.node.children) {
		gCurGraphNodeCamera = node;
		ShortMatrix modelview = gfx_modelview_get();
		node->matrixPtrq = &modelview;
		geo_process_node_and_siblings(node->fnNode.node.children);
		gCurGraphNodeCamera = NULL;
	}
	gfx_modelview_set(&bak);
	matrix_changed = true;
}

static void geo_process_translation_rotation(struct GraphNodeTranslationRotation* node) {
	ShortMatrix bak = gfx_modelview_get();
	gfx_modelview_translatei(node->translation);
	gfx_modelview_rotate_zxy(node->rotation);
	matrix_changed = true;
	if(node->displayList) {
		geo_append_display_list(node->displayList, node->node.flags >> 8);
	}
	if(node->node.children) {
		geo_process_node_and_siblings(node->node.children);
	}
	gfx_modelview_set(&bak);
	matrix_changed = true;
}

static void geo_process_switch(struct GraphNodeSwitchCase *node) {
	struct GraphNode* selectedChild = node->fnNode.node.children;

	if(node->fnNode.func) {
		ShortMatrix modelview = gfx_modelview_get();
		node->fnNode.func(GEO_CONTEXT_RENDER, &node->fnNode.node, &modelview);
	}
	for(int i = 0; selectedChild && node->selectedCase > i; i++) {
		selectedChild = selectedChild->next;
	}
	if(selectedChild) {
		geo_process_node_and_siblings(selectedChild);
	}
}

static void geo_process_translation(struct GraphNodeTranslation* node) {
	ShortMatrix bak = gfx_modelview_get();
	gfx_modelview_translatei(node->translation);
	matrix_changed = true;
	if(node->displayList) {
		geo_append_display_list(node->displayList, node->node.flags >> 8);
	}
	if(node->node.children) {
		geo_process_node_and_siblings(node->node.children);
	}
	gfx_modelview_set(&bak);
	matrix_changed = true;
}

static void geo_process_rotation(struct GraphNodeRotation* node) {
	ShortMatrix bak = gfx_modelview_get();
	gfx_modelview_rotate_zxy(node->rotation);
	matrix_changed = true;
	if(node->displayList) {
		geo_append_display_list(node->displayList, node->node.flags >> 8);
	}
	if(node->node.children) {
		geo_process_node_and_siblings(node->node.children);
	}
	gfx_modelview_set(&bak);
	matrix_changed = true;
}

static void geo_process_scale(struct GraphNodeScale* node) {
	ShortMatrix bak = gfx_modelview_get();
	gfx_modelview_scale_byq(q(node->scale));
	matrix_changed = true;
	if(node->displayList) {
		geo_append_display_list(node->displayList, node->node.flags >> 8);
	}
	if(node->node.children) {
		geo_process_node_and_siblings(node->node.children);
	}
	gfx_modelview_set(&bak);
	matrix_changed = true;
}

#define POS_STEP 2
#define ROT_STEP 2

#include <assert.h>

static void geo_process_animated_part(struct GraphNodeAnimatedPart* node) {
	ShortMatrix bak = gfx_modelview_get();
	if(gCurAnimType) {
		u32 frame = gCurrAnimFrame;
		if(gCurAnimType < ANIM_TYPE_NO_TRANSLATION) {
			s16 translation[3];
			for(int i = 0; i < 3; i++) {
				translation[i] = node->translation[i];
				if(gCurAnimType == ANIM_TYPE_TRANSLATION || ((gCurAnimType == ANIM_TYPE_VERTICAL_TRANSLATION) == (i == 1))) {
					s32 value;
					if(is_anim_compressed) {
						if(gCurrAnimAttribute[0] == 0) {
							value = (s16) gCurrAnimAttribute[1];
							gCurrAnimAttribute += 2;
						} else {
							u32 next_idx;
							u32 idx = retrieve_animation_index(frame / POS_STEP, &gCurrAnimAttribute, &next_idx);
							value = gCurAnimData[idx];
							assert(value > -2147400);
							s32 mid_frame = frame % POS_STEP;
							if(mid_frame != 0 && next_idx != idx) {
								s32 next_value = gCurAnimData[next_idx];
								assert(next_value > -2147400);
								s32 diff = next_value - value;
								if(ABS(diff) <= 0x7FFF) {
									value = value + diff * mid_frame / POS_STEP;
								}
								assert(value > -2147400);
							}
						}
					} else {
						u32 idx = retrieve_animation_index(frame, &gCurrAnimAttribute, NULL);
						value = gCurAnimData[idx];
						assert(value > -2147400);
					}
					translation[i] += value * gCurAnimTranslationMultiplierq / ONE;
				} else if(!is_anim_compressed) {
					gCurrAnimAttribute += 2;
				}
			}
			gfx_modelview_translatei(translation);
		} else {
			gfx_modelview_translatei(node->translation);
		}
		gCurAnimType = ANIM_TYPE_ROTATION;
		// all non-null animation types have rotation, so no need to check
		Vec3s rotation;
		for(int i = 0; i < 3; i++) {
			s32 value;
			if(is_anim_compressed) {
				if(gCurrAnimAttribute[0] == 0) {
					value = (s16) gCurrAnimAttribute[1];
					gCurrAnimAttribute += 2;
				} else {
					u32 next_idx;
					u32 idx = retrieve_animation_index(frame / ROT_STEP, &gCurrAnimAttribute, &next_idx);
					value = (s16) ((u16) ((const u8*) gCurAnimData)[idx] << 8);
					s32 mid_frame = frame % ROT_STEP;
					if(mid_frame != 0 && next_idx != idx) {
						s32 next_value = (s16) ((u16) ((const u8*) gCurAnimData)[next_idx] << 8);
						s32 diff = next_value - value;
						if(ABS(diff) <= 0x7FFF) {
							value = value + diff * mid_frame / ROT_STEP;
						}
					}
				}
			} else {
				u32 idx = retrieve_animation_index(frame, &gCurrAnimAttribute, NULL);
				value = gCurAnimData[idx];
			}
			rotation[i] = value;
		}
		gfx_modelview_rotate_xyz(rotation);
		matrix_changed = true;
	}
	if(node->displayList) {
		geo_append_display_list(node->displayList, node->node.flags >> 8);
	}
	if(node->node.children) {
		geo_process_node_and_siblings(node->node.children);
	}
	gfx_modelview_set(&bak);
	matrix_changed = true;
}

static void geo_process_level_of_detail(struct GraphNodeLevelOfDetail* node) {
	if(node->node.children) {
		ShortMatrix modelview = gfx_modelview_get();
		s32 camera_distance = -(s32) modelview.t[2];
		if(camera_distance >= node->minDistance && camera_distance < node->maxDistance) {
			geo_process_node_and_siblings(node->node.children);
		}
	}
}

static void geo_process_billboard(struct GraphNodeBillboard* node) {
	ShortMatrix bak = gfx_modelview_get();
	ShortMatrix billboard_mtx;
	mtx_billboard(&billboard_mtx, &bak, node->translation, gCurGraphNodeCamera->roll);
	if(gCurGraphNodeHeldObject) {
		mtx_scaleq(&billboard_mtx, gCurGraphNodeHeldObject->objNode->header.gfx.scaleq);
	} else if(gCurGraphNodeObject) {
		mtx_scaleq(&billboard_mtx, gCurGraphNodeObject->scaleq);
	}
	gfx_modelview_set(&billboard_mtx);
	matrix_changed = true;
	if(node->displayList) {
		geo_append_display_list(node->displayList, node->node.flags >> 8);
	}
	if(node->node.children) {
		geo_process_node_and_siblings(node->node.children);
	}
	gfx_modelview_set(&bak);
	matrix_changed = true;
}

static void geo_process_background(struct GraphNodeBackground* node) {
	if(node->fnNode.func) {
		ShortMatrix modelview = gfx_modelview_get();
		Gfx* list = node->fnNode.func(GEO_CONTEXT_RENDER, &node->fnNode.node, &modelview);
		if(list) {
			geo_append_display_list(list, node->fnNode.node.flags >> 8);
		}
	}
	if(node->fnNode.node.children != NULL) {
		geo_process_node_and_siblings(node->fnNode.node.children);
	}
}

static s32 scale_shadow_with_distance(s32 initial_radius, s16 floor_distance) {
	if(floor_distance <= 0) {
		return initial_radius;
	} else if(floor_distance >= 600) {
		return initial_radius / 2;
	} else {
		return initial_radius * (1200 - floor_distance) / 1200;
	}
}

static u8 dim_shadow_with_distance(u8 solidity, s16 floor_distance) {
	if(solidity < 121 || floor_distance <= 0) {
		return solidity;
	} else if(floor_distance >= 600) {
		return 120;
	} else {
		return solidity + (120 - solidity) * floor_distance / 600;
	}
}

static u8 rescale_opacity(u8 opacity) {
	return opacity / 2;
}

static void geo_process_shadow(struct GraphNodeShadow* node) {
	s16 x = gCurGraphNodeObject->posi[0];
	s16 obj_y = gCurGraphNodeObject->posi[1];
	s16 z = gCurGraphNodeObject->posi[2];
    struct FloorGeometry* floor_geom;
	s16 y = qtrunc(find_floor_height_and_dataq(q(x), q(obj_y), q(z), &floor_geom));
	s32 floor_distance = obj_y - y;
	if(floor_distance < 600) { // shadow is not visible from 600+ units away
		ShortMatrix shadow_mtx = mtx_translationi((s16[]) {x, y, z});
		ShortMatrix* mtx_alloc = alloc_display_list(sizeof(ShortMatrix));
		*mtx_alloc = mtx_mul(&shadow_mtx, gCurGraphNodeCamera->matrixPtrq);
		gfx_emit_mtx_set(mtx_alloc);
		bool is_square = node->shadowType == SHADOW_SQUARE_PERMANENT || node->shadowType == SHADOW_SQUARE_SCALABLE || node->shadowType == SHADOW_SQUARE_TOGGLABLE;
		gfx_emit_shadow(is_square, scale_shadow_with_distance(node->shadowScale / 2, floor_distance), rescale_opacity(dim_shadow_with_distance(node->shadowSolidity, floor_distance)));
		if(node->node.children) {
			geo_process_node_and_siblings(node->node.children);
		}
	}
}

static void geo_process_master_list(struct GraphNodeMasterList* node) {
	if(!gCurGraphNodeMasterList && node->node.children) {
		gCurGraphNodeMasterList = node;
		//geo_using_zbuffer = (node->node.flags & GRAPH_RENDER_Z_BUFFER) != 0;
		geo_process_node_and_siblings(node->node.children);
		//geo_using_zbuffer = false;
		gCurGraphNodeMasterList = NULL;
	}
}
