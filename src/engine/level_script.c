#include "macros.h"
#include <ultra64.h>
#ifdef NO_SEGMENTED_MEMORY
#include <string.h>
#endif

#include <stdio.h>
#include <levels/intro/header.h>
extern u8 _introSegmentRomStart[];
extern u8 _introSegmentRomEnd[];
#include <actors/common1.h>
#include <game/level_geo.h>
#include <game/moving_texture.h>
#include <stdbool.h>
#include "sm64.h"
#include "audio/external.h"
#include "buffers/framebuffers.h"
#include "buffers/zbuffer.h"
#include "game/area.h"
#include "game/game_init.h"
#include "game/mario.h"
#include "game/memory.h"
#include "game/object_helpers.h"
#include "game/object_list_processor.h"
#include "game/profiler.h"
#include "game/save_file.h"
#include "game/sound_init.h"
#include "goddard/renderer.h"
#include "geo_layout.h"
#include "graph_node.h"
#include "level_script.h"
#include "level_misc_macros.h"
#include "math_util.h"
#include "surface_collision.h"
#include "surface_load.h"
#include "command_macros_base.h"
#include <behavior_data.h>
#include <game/level_update.h>
#include <actors/group0.h>
#include <actors/group1.h>
#include <actors/group2.h>
#include <actors/group3.h>
#include <actors/group4.h>
#include <actors/group5.h>
#include <actors/group6.h>
#include <actors/group7.h>
#include <actors/group8.h>
#include <actors/group9.h>
#include <actors/group10.h>
#include <actors/group11.h>
#include <actors/group12.h>
#include <actors/group13.h>
#include <actors/group14.h>
#include <actors/group15.h>
#include <actors/group16.h>
#include <actors/group17.h>
#include <actors/common0.h>
#include <actors/common1.h>
#include <game/segment2.h>
#include <menu/intro_geo.h>
#include <menu/file_select.h>
#include <menu/star_select.h>
#include <menu/title_screen.h>
#include <game/screen_transition.h>
#include <game/behavior_actions.h>
#include <game/mario_misc.h>
#include <game/geo_misc.h>
#include <game/paintings.h>
#include <levels/scripts.h>
#include <game/mario_actions_cutscene.h>
#include <game/main.h>
#include <assert.h>

#define CMD_GET(type, offset) (*(type *) (CMD_PROCESS_OFFSET(offset) + (u8 *) sCurrentCmd))

// These are equal
#define CMD_NEXT ((struct LevelCommand *) ((u8 *) sCurrentCmd + (sCurrentCmd->size << CMD_SIZE_SHIFT)))
#define NEXT_CMD ((struct LevelCommand *) ((sCurrentCmd->size << CMD_SIZE_SHIFT) + (u8 *) sCurrentCmd))

struct LevelCommand {
    /*00*/ u8 type;
    /*01*/ u8 size;
    /*02*/ // variable sized argument data
};

enum ScriptStatus { SCRIPT_RUNNING = 1, SCRIPT_PAUSED = 0, SCRIPT_PAUSED2 = -1 };

static uintptr_t sStack[32];

struct AllocOnlyPool *sLevelPool = NULL;

static u16 sDelayFrames = 0;
static u16 sDelayFrames2 = 0;

static s16 sCurrAreaIndex = -1;

static uintptr_t *sStackTop = sStack;
static uintptr_t *sStackBase = NULL;

static s16 sScriptStatus;
static s32 sRegister;
static struct LevelCommand *sCurrentCmd;

static s32 eval_script_op(s8 op, s32 arg) {
    s32 result = 0;

    switch (op) {
        case 0:
            result = sRegister & arg;
            break;
        case 1:
            result = !(sRegister & arg);
            break;
        case 2:
            result = sRegister == arg;
            break;
        case 3:
            result = sRegister != arg;
            break;
        case 4:
            result = sRegister < arg;
            break;
        case 5:
            result = sRegister <= arg;
            break;
        case 6:
            result = sRegister > arg;
            break;
        case 7:
            result = sRegister >= arg;
            break;
        default: unreachable();
    }

    return result;
}

static void level_cmd_load_and_execute(void) {
    main_pool_push_state();
    load_segment(CMD_GET(s16, 2), CMD_GET(void *, 4), CMD_GET(void *, 8), MEMORY_POOL_LEFT);

    *sStackTop++ = (uintptr_t) NEXT_CMD;
    *sStackTop++ = (uintptr_t) sStackBase;
    sStackBase = sStackTop;
    void* segmented = CMD_GET(void *, 12);
    void* virtual = segmented_to_virtual(segmented);
    //printf("loading and jumping to (seg 0x%x) (vrt 0x%x)\n", (u32) segmented, (u32) virtual);
    sCurrentCmd = virtual;
}

static void level_cmd_exit_and_execute(void) {
    void *targetAddr = CMD_GET(void *, 12);

    main_pool_pop_state();
    main_pool_push_state();

    load_segment(CMD_GET(s16, 2), CMD_GET(void *, 4), CMD_GET(void *, 8),
            MEMORY_POOL_LEFT);

    sStackTop = sStackBase;
    void* virtual = segmented_to_virtual(targetAddr);
    //printf("exiting and jumping to (seg 0x%x) (vrt 0x%x)\n", (u32) targetAddr, (u32) virtual);
    sCurrentCmd = virtual;
}
static void level_cmd_exit_and_execute_dyn(void) {
    const void* targetAddr = geo_dyn_map[CMD_GET(u32, 12)];

    main_pool_pop_state();
    main_pool_push_state();

    load_segment(CMD_GET(s16, 2), CMD_GET(void *, 4), CMD_GET(void *, 8),
            MEMORY_POOL_LEFT);

    sStackTop = sStackBase;
    void* virtual = segmented_to_virtual(targetAddr);
    //printf("exiting and jumping dyn to (seg 0x%x) (vrt 0x%x)\n", (u32) targetAddr, (u32) virtual);
    sCurrentCmd = virtual;
}

static void level_cmd_exit(void) {
    main_pool_pop_state();

    sStackTop = sStackBase;
    sStackBase = (uintptr_t *) *(--sStackTop);
    sCurrentCmd = (struct LevelCommand *) *(--sStackTop);
}

static void level_cmd_sleep(void) {
    sScriptStatus = SCRIPT_PAUSED;

    if (sDelayFrames == 0) {
        sDelayFrames = CMD_GET(s16, 2);
    } else if (--sDelayFrames == 0) {
        sCurrentCmd = CMD_NEXT;
        sScriptStatus = SCRIPT_RUNNING;
    }
}

static void level_cmd_sleep2(void) {
    sScriptStatus = SCRIPT_PAUSED2;

    if (sDelayFrames2 == 0) {
        sDelayFrames2 = CMD_GET(s16, 2);
    } else if (--sDelayFrames2 == 0) {
        sCurrentCmd = CMD_NEXT;
        sScriptStatus = SCRIPT_RUNNING;
    }
}

static void level_cmd_jump(void) {
    sCurrentCmd = segmented_to_virtual(CMD_GET(void *, 4));
    //printf("jumping to (seg 0x%x) (vrt 0x%x)\n", (u32) CMD_GET(void *, 4), (u32) sCurrentCmd);
}

static void level_cmd_jump_and_link(void) {
    *sStackTop++ = (uintptr_t) NEXT_CMD;
    if(CMD_GET(u16, 2)) {
        sCurrentCmd = segmented_to_virtual(geo_dyn_map[CMD_GET(u32, 4)]);
        //printf("jumping and linking to (seg 0x%lx) (vrt 0x%lx)\n", (u64) geo_dyn_map[CMD_GET(u32, 4)], (u64) sCurrentCmd);
    } else {
        sCurrentCmd = segmented_to_virtual(CMD_GET(void *, 4));
        //printf("jumping and linking to (seg 0x%lx) (vrt 0x%lx)\n", (u64) CMD_GET(void *, 4), (u64) sCurrentCmd);
    }
}

static void level_cmd_return(void) {
    sCurrentCmd = (struct LevelCommand *) *(--sStackTop);
}

static void level_cmd_jump_and_link_push_arg(void) {
    *sStackTop++ = (uintptr_t) NEXT_CMD;
    *sStackTop++ = CMD_GET(s16, 2);
    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_jump_repeat(void) {
    s32 val = *(sStackTop - 1);

    if (val == 0) {
        sCurrentCmd = (struct LevelCommand *) *(sStackTop - 2);
    } else if (--val != 0) {
        *(sStackTop - 1) = val;
        sCurrentCmd = (struct LevelCommand *) *(sStackTop - 2);
    } else {
        sCurrentCmd = CMD_NEXT;
        sStackTop -= 2;
    }
}

static void level_cmd_loop_begin(void) {
    *sStackTop++ = (uintptr_t) NEXT_CMD;
    *sStackTop++ = 0;
    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_loop_until(void) {
    if (eval_script_op(CMD_GET(u8, 2), CMD_GET(s32, 4)) != 0) {
        sCurrentCmd = CMD_NEXT;
        sStackTop -= 2;
    } else {
        sCurrentCmd = (struct LevelCommand *) *(sStackTop - 2);
    }
}

static void level_cmd_jump_if(void) {
    if (eval_script_op(CMD_GET(u8, 2), CMD_GET(s32, 4)) != 0) {
        if(CMD_GET(u8, 3)) {
            sCurrentCmd = segmented_to_virtual(geo_dyn_map[CMD_GET(u32, 8)]);
        } else {
            sCurrentCmd = segmented_to_virtual(CMD_GET(void *, 8));
        }
    } else {
        sCurrentCmd = CMD_NEXT;
    }
}

static void level_cmd_jump_and_link_if(void) {
    if (eval_script_op(CMD_GET(u8, 2), CMD_GET(s32, 4)) != 0) {
        *sStackTop++ = (uintptr_t) NEXT_CMD;
        sCurrentCmd = segmented_to_virtual(CMD_GET(void *, 8));
    } else {
        sCurrentCmd = CMD_NEXT;
    }
}

static void level_cmd_skip_if(void) {
    if (eval_script_op(CMD_GET(u8, 2), CMD_GET(s32, 4)) == 0) {
        do {
            sCurrentCmd = CMD_NEXT;
        } while (sCurrentCmd->type == 0x0F || sCurrentCmd->type == 0x10);
    }

    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_skip(void) {
    do {
        sCurrentCmd = CMD_NEXT;
    } while (sCurrentCmd->type == 0x10);

    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_skippable_nop(void) {
    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_call(void) {
    typedef s32 (*Func)(s16, s32);
    Func func = CMD_GET(Func, 4);
    sRegister = func(CMD_GET(s16, 2), sRegister);
    sCurrentCmd = CMD_NEXT;
}
static void level_cmd_call_dyn(void) {
    typedef s32 (*Func)(s16, s32);
    Func func = segmented_to_virtual(geo_dyn_map[CMD_GET(u32, 4)]);
    sRegister = func(CMD_GET(s16, 2), sRegister);
    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_call_loop(void) {
    typedef s32 (*Func)(s16, s32);
    Func func = CMD_GET(Func, 4);
    sRegister = func(CMD_GET(s16, 2), sRegister);

    if (sRegister == 0) {
        sScriptStatus = SCRIPT_PAUSED;
    } else {
        sScriptStatus = SCRIPT_RUNNING;
        sCurrentCmd = CMD_NEXT;
    }
}
static void level_cmd_call_loop_dyn(void) {
    typedef s32 (*Func)(s16, s32);
    Func func = segmented_to_virtual(geo_dyn_map[CMD_GET(u32, 4)]);
    sRegister = func(CMD_GET(s16, 2), sRegister);

    if (sRegister == 0) {
        sScriptStatus = SCRIPT_PAUSED;
    } else {
        sScriptStatus = SCRIPT_RUNNING;
        sCurrentCmd = CMD_NEXT;
    }
}

static void level_cmd_set_register(void) {
    sRegister = CMD_GET(s16, 2);
    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_push_pool_state(void) {
    main_pool_push_state();
    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_pop_pool_state(void) {
    main_pool_pop_state();
    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_load_to_fixed_address(void) {
    //load_to_fixed_pool_addr(CMD_GET(void *, 4), CMD_GET(void *, 8), CMD_GET(void *, 12));
    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_load_raw(void) {
    load_segment(CMD_GET(s16, 2), CMD_GET(void *, 4), CMD_GET(void *, 8),
            MEMORY_POOL_LEFT);
    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_load_mio0(void) {
    if(CMD_GET(s16, 2) != 0x0A) { // do not load skyboxes
        load_segment_decompress(CMD_GET(s16, 2), CMD_GET(void *, 4), CMD_GET(void *, 8));
    }
    sCurrentCmd = CMD_NEXT;
}

#define MARIO_HEAD_MEM DOUBLE_SIZE_ON_64_BIT(0xA0000) // originally 0xE1000

static void level_cmd_load_mario_head(void) {
#ifdef MARIO_HEAD
    // TODO: Fix these hardcoded sizes
    void *addr = main_pool_alloc(MARIO_HEAD_MEM, MEMORY_POOL_LEFT);
    if (addr != NULL) {
        gdm_init(addr, MARIO_HEAD_MEM);
        //gd_add_to_heap(gZBuffer, sizeof(gZBuffer)); // 0x25800
        //gd_add_to_heap(gFrameBuffer0, 3 * sizeof(gFrameBuffer0)); // 0x70800
        gdm_setup();
        gdm_maketestdl(CMD_GET(s16, 2));
    }
#endif
    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_load_mio0_texture(void) {
    load_segment_decompress(CMD_GET(s16, 2), CMD_GET(void *, 4), CMD_GET(void *, 8));
    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_init_level(void) {
    init_graph_node_start(NULL, (struct GraphNodeStart *) &gObjParentGraphNode);
    clear_objects();
    clear_areas();
    main_pool_push_state();

    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_clear_level(void) {
    clear_objects();
    clear_area_graph_nodes();
    clear_areas();
    main_pool_pop_state();

    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_alloc_level_pool(void) {
    if(!sLevelPool) {
        sLevelPool = alloc_only_pool_init(main_pool_available() - sizeof(struct AllocOnlyPool), MEMORY_POOL_LEFT);
    }
    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_free_level_pool(void) {
    s32 i;

    alloc_only_pool_resize(sLevelPool, (uintptr_t) sLevelPool->free_ptr - (uintptr_t) (sLevelPool + 1));
    sLevelPool = NULL;

    for (i = 0; i < 8; i++) {
        if (gAreaData[i].terrainData != NULL) {
            alloc_surface_pools();
            break;
        }
    }

    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_begin_area(void) {
    u8 areaIndex = CMD_GET(u8, 2);
    void *geoLayoutAddr = CMD_GET(void *, 4);

    if (areaIndex < 8) {
        struct GraphNodeRoot *screenArea = (struct GraphNodeRoot *) process_geo_layout(sLevelPool, geoLayoutAddr);
        assert(screenArea);
        struct GraphNodeCamera *node = (struct GraphNodeCamera *) screenArea->views[0];

        sCurrAreaIndex = areaIndex;
        screenArea->areaIndex = areaIndex;
        gAreas[areaIndex].unk04 = screenArea;

        if (node != NULL) {
            gAreas[areaIndex].camera = (struct Camera *) node->config.camera;
        } else {
            gAreas[areaIndex].camera = NULL;
        }
    }

    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_end_area(void) {
    sCurrAreaIndex = -1;
    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_load_model_from_dl(void) {
    s16 val1 = CMD_GET(s16, 2) & 0x0FFF;
    s16 val2 = ((u16)CMD_GET(s16, 2)) >> 12;
    void *val3 = CMD_GET(void *, 4);

    if (val1 < 256) {
        gLoadedGraphNodes[val1] =
            (struct GraphNode *) init_graph_node_display_list(sLevelPool, 0, val2, val3);
    }

    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_load_model_from_geo(void) {
    s16 arg0 = CMD_GET(s16, 2);
    void *arg1 = CMD_GET(void *, 4);

    if (arg0 < 256) {
        gLoadedGraphNodes[arg0] = process_geo_layout(sLevelPool, arg1);
    }

    sCurrentCmd = CMD_NEXT;
}

extern const Gfx debug_level_select_dl_07000858[];
extern const Gfx debug_level_select_dl_07001100[];
extern const Gfx debug_level_select_dl_07001BA0[];
extern const Gfx debug_level_select_dl_070025F0[];
extern const Gfx debug_level_select_dl_07003258[];
extern const Gfx debug_level_select_dl_07003DB8[];
extern const Gfx debug_level_select_dl_070048C8[];
extern const Gfx debug_level_select_dl_07005558[];
extern const Gfx debug_level_select_dl_070059F8[];
extern const Gfx debug_level_select_dl_070063B0[];
extern Gfx *geo_intro_rumble_pak_graphic(s32 state, struct GraphNode *node, UNUSED void *context);

const void* geo_dyn_map[] = {
    star_seg3_dl_0302B870,
    star_seg3_dl_0302BA18,
    mario_cap_seg3_dl_03022F48,
    transparent_star_seg3_dl_0302C620,
    dl_billboard_num_0,
    dl_billboard_num_1,
    dl_billboard_num_2,
    dl_billboard_num_3,
    dl_billboard_num_4,
    dl_billboard_num_5,
    dl_billboard_num_6,
    dl_billboard_num_7,
    dl_billboard_num_8,
    dl_billboard_num_9,
    geo_switch_mario_cap_on_off,
    geo_switch_mario_eyes,
    geo_switch_mario_hand,
    geo_switch_mario_stand_run,
    geo_switch_mario_cap_effect,
    geo_switch_anim_state,
    geo_switch_tuxie_mother_eyes,
    geo_switch_bowser_eyes,
    geo_switch_peach_eyes,
    geo_switch_mario_hand_grab_pos,
    lvl_init_or_update,
    lvl_init_menu_values_and_cursor_pos,
    lvl_update_obj_and_load_file_selected,
    lvl_update_obj_and_load_act_button_actions,
    lvl_init_act_selector_values_and_stars,
    lvl_set_current_level,
    lvl_play_the_end_screen_sound,
    lvl_intro_update,
    warp_pipe_geo,
    geo_camera_fov,
    geo_camera_main,
    geo_skybox_main,
    geo_envfx_main,
    geo_switch_area,
    geo_movtex_pause_control,
    geo_movtex_draw_water_regions,
    bhvActSelector,
    bhvFlyingBookend,
    bhvBookendSpawn,
    bhvMerryGoRoundBooManager,
    bhvBalconyBigBoo,
    bhvBoo,
    bhvHiddenStaircaseStep,
    bhvGhostHuntBoo,
    bhvGhostHuntBigBoo,
    bhvMadPiano,
    bhvHauntedChair,
    bhvHauntedBookshelfManager,
    bhvStar,
    bhvKingBobomb,
    geo_cannon_circle_base,
    geo_mirror_mario_set_alpha,
    geo_update_held_mario_pos,
    geo_scale_bowser_key,
    geo_update_layer_transparency,
    geo_mirror_mario_backface_culling,
    geo_mario_tilt_torso,
    geo_move_mario_part_from_parent,
    geo_mario_hand_foot_scaler,
    geo_mario_rotate_wing_cap_wings,
    geo_mario_head_rotation,
    geo_update_projectile_pos_from_parent,
    geo_bits_bowser_coloring,
    geo_update_body_rot_from_parent,
    geo_snufit_scale_body,
    geo_snufit_move_mask,
    geo_offset_klepto_held_object,
    geo_movtex_draw_nocolor,
    geo_render_mirror_mario,
    geo_painting_draw,
    geo_exec_inside_castle_light,
    geo_painting_update,
    geo_intro_gameover_backdrop,
    geo_exec_cake_end_screen,
    geo_act_selector_strings,
    geo_file_select_strings_and_menu_cursor,
    geo_draw_mario_head_goddard,
    geo_intro_regular_backdrop,
    geo_intro_tm_copyright,
    geo_intro_super_mario_64_logo,
    geo_exec_flying_carpet_timer_update,
    geo_exec_flying_carpet_create,
    geo_movtex_draw_colored,
    geo_movtex_update_horizontal,
    geo_movtex_draw_colored_2_no_update,
    geo_movtex_draw_colored_no_update,
    geo_wdw_set_initial_water_level,
    bhvFlame,
    bhvBbhTiltingTrapPlatform,
    bhvBbhTumblingBridge,
    bhvHauntedBookshelf,
    bhvMeshElevator,
    bhvMerryGoRound,
    bhvCoffinSpawner,
    bhvSpinAirborneWarp,
    bhvSquarishPathMoving,
    bhvSeesawPlatform,
    bhvSlidingPlatform2,
    bhvFerrisWheelAxle,
    bhvAnimatesOnFloorSwitchPress,
    bhvFloorSwitchAnimatesObject,
    bhvFlamethrower,
    bhvBowserCourseRedCoinStar,
    bhvAirborneWarp,
    bhvWarpPipe,
    bhvDeathWarp,
    bhvPlatformOnTrack,
    bhvBitfsTiltingInvertedPyramid,
    bhvBitfsSinkingPlatforms,
    bhvBitfsSinkingCagePlatform,
    bhvActivatedBackAndForthPlatform,
    bhvSquishablePlatform,
    bhvWfTumblingBridge,
    bhvPoleGrabbing,
    bhvWarp,
    bhvOctagonalPlatformRotating,
    bhvChainChompGate,
    bhvOpenableGrill,
    bhvFloorSwitchGrills,
    bhvCheckerboardElevatorGroup,
    bhvFadingWarp,
    bhvSpinAirborneCircleWarp,
    bhvTiltingBowserLavaPlatform,
    bhvBowserBomb,
    bhvFallingBowserPlatform,
    bhvAmbientSounds,
    bhvBirdsSoundLoop,
    bhvCourtyardBooTriplet,
    bhvBooWithCage,
    bhvLaunchStarCollectWarp,
    bhvLaunchDeathWarp,
    bhvSwimmingWarp,
    bhvWaterfallSoundLoop,
    bhvMoatGrills,
    bhvInvisibleObjectsUnderBridge,
    bhvWaterMist2,
    bhvManyBlueFishSpawner,
    bhvBird,
    bhvIntroScene,
    bhvHiddenAt120Stars,
    bhvCameraLakitu,
    bhvCastleFlagWaving,
    bhvButterfly,
    bhvYoshi,
    bhvStarDoor,
    bhvDoorWarp,
    bhvInstantActiveWarp,
    bhvAirborneDeathWarp,
    bhvHardAirKnockBackWarp,
    bhvAirborneStarCollectWarp,
    bhvPaintingStarCollectWarp,
    bhvPaintingDeathWarp,
    bhvCastleFloorTrap,
    bhvFishGroup,
    bhvTankFishGroup,
    bhvBooInCastle,
    bhvToadMessage,
    bhvClockMinuteHand,
    bhvClockHourHand,
    bhvDecorativePendulum,
    bhvWaterLevelPillar,
    bhvDddWarp,
    bhvMips,
    bhvSmallPenguin,
    bhvMrBlizzard,
    bhvPenguinRaceFinishLine,
    bhvPenguinRaceShortcutCheck,
    bhvPlaysMusicTrackWhenTouched,
    bhvCapSwitch,
    bhvHiddenRedCoinStar,
    bhvSushiShark,
    bhvFewBlueFishSpawner,
    bhvChirpChirp,
    bhvWhirlpool,
    bhvBowserSubDoor,
    bhvBowsersSub,
    bhvDDDPole,
    bhvJetStream,
    bhvControllablePlatform,
    bhvHmcElevatorPlatform,
    bhvDorrie,
    bhvBigBoulderGenerator,
    bhvTreasureChestsJrb,
    bhvRockSolid,
    bhvFallingPillar,
    bhvPillarBase,
    bhvJrbFloatingPlatform,
    bhvInsideCannon,
    bhvTreasureChestsShip,
    bhvStaticObject,
    bhvLllHexagonalMesh,
    bhvLllDrawbridgeSpawner,
    bhvLllRotatingBlockWithFireBars,
    bhvLllRotatingHexagonalRing,
    bhvLllSinkingRectangularPlatform,
    bhvLllSinkingSquarePlatforms,
    bhvLllTiltingInvertedPyramid,
    bhvLllBowserPuzzle,
    bhvLllMovingOctagonalMeshPlatform,
    bhvLllSinkingRockBlock,
    bhvLllRollingLog,
    bhvLllRotatingHexagonalPlatform,
    bhvLllFloatingWoodBridge,
    bhvMrI,
    bhvBigBully,
    bhvBigBullyWithMinions,
    bhvSmallBully,
    bhvBouncingFireball,
    bhvLllVolcanoFallingTrap,
    bhvVolcanoSoundLoop,
    bhvMenuButtonManager,
    bhvYellowBackgroundInMenu,
    bhvRrRotatingBridgePlatform,
    bhvRrCruiserWing,
    bhvSwingPlatform,
    bhvDonutPlatformSpawner,
    bhvRrElevatorPlatform,
    bhvFishSpawner,
    bhvSnowMoundSpawn,
    bhvSLWalkingPenguin,
    bhvSLSnowmanWind,
    bhvIgloo,
    bhvBigChillBully,
    bhvPyramidTop,
    bhvToxBox,
    bhvTweester,
    bhvGrindel,
    bhvHorizontalGrindel,
    bhvSpindel,
    bhvSslMovingPyramidWall,
    bhvPyramidElevator,
    bhvSandSoundLoop,
    bhvEyerokBoss,
    bhvWigglerHead,
    bhvGoombaTripletSpawner,
    bhvGoomba,
    bhvFirePiranhaPlant,
    bhvThiBowlingBallSpawner,
    bhvBubba,
    bhvThiHugeIslandTop,
    bhvThiTinyIslandTop,
    bhvFlyingWarp,
    bhvThwomp2,
    bhvTtmRollingLog,
    bhvTtmBowlingBallSpawner,
    bhvMontyMoleHole,
    bhvMontyMole,
    bhvCloud,
    bhvExitPodiumWarp,
    bhvWdwSquareFloatingPlatform,
    bhvArrowLift,
    bhvInitializeChangingWaterLevel,
    bhvWaterLevelDiamond,
    bhvFloorSwitchHiddenObjects,
    bhvHiddenObject,
    bhvWdwExpressElevatorPlatform,
    bhvWdwExpressElevator,
    bhvWdwRectangularFloatingPlatform,
    bhvRotatingPlatform,
    bhvSkeeter,
    bhvGiantPole,
    bhvSmallBomp,
    bhvLargeBomp,
    bhvWfRotatingWoodenPlatform,
    bhvWfSlidingPlatform,
    bhvWfBreakableWallRight,
    bhvWfBreakableWallLeft,
    bhvThwomp,
    bhvBetaFishSplashSpawner,
    bhvPiranhaPlant,
    bhvSmallWhomp,
    bhvBobBowlingBallSpawner,
    bhvPitBowlingBall,
    bhvBobombBuddy,
    bhvBobombBuddyOpensCannon,
    bhvWaterBombCannon,
    bhvCannonClosed,
    bhvKoopaRaceEndpoint,
    bhvKoopa,
    bhvHiddenStar,
    bhvSnowmansBottom,
    bhvCcmTouchedStarSpawn,
    bhvTuxiesMother,
    bhvSnowmansHead,
    bhvRacingPenguin,
    bhvTreasureChests,
    bhvMantaRay,
    bhvJetStreamRingSpawner,
    bhvSunkenShipPart,
    bhvSunkenShipPart2,
    bhvInSunkenShip,
    bhvInSunkenShip2,
    bhvShipPart3,
    bhvInSunkenShip3,
    bhvJrbSlidingBox,
    bhvUnagi,
    bhvExclamationBox,
    bhvKlepto,
    bhvUkiki,
    bhvUkikiCage,
    bhvKickableBoard,
    bhv1Up,
    bhvBulletBill,
    bhvTower,
    bhvBulletBillCannon,
    bhvTowerPlatformGroup,
    bhvTowerDoor,
    bhvHoot,
    bhvWhompKingBoss,
    level_main_scripts_entry,
    script_func_global_1,
    script_func_global_2,
    script_func_global_3,
    script_func_global_4,
    script_func_global_5,
    script_func_global_6,
    script_func_global_7,
    script_func_global_8,
    script_func_global_9,
    script_func_global_10,
    script_func_global_11,
    script_func_global_12,
    script_func_global_13,
    script_func_global_14,
    script_func_global_15,
    script_func_global_16,
    script_func_global_17,
    script_func_global_18,
    bhvMario,
    mario_geo,
    smoke_geo,
    sparkles_geo,
    bubble_geo,
    small_water_splash_geo,
    idle_water_wave_geo,
    water_splash_geo,
    wave_trail_geo,
    yellow_coin_geo,
    star_geo,
    transparent_star_geo,
    wooden_signpost_geo,
    red_flame_geo,
    blue_flame_geo,
    burn_smoke_geo,
    leaves_geo,
    purple_marble_geo,
    fish_geo,
    fish_shadow_geo,
    sparkles_animation_geo,
    butterfly_geo,
    mist_geo,
    white_puff_geo,
    white_particle_geo,
    yellow_coin_no_shadow_geo,
    blue_coin_geo,
    blue_coin_no_shadow_geo,
    marios_winged_metal_cap_geo,
    marios_metal_cap_geo,
    marios_wing_cap_geo,
    marios_cap_geo,
    bowser_key_cutscene_geo,
    bowser_key_geo,
    bowser_1_yellow_sphere_geo,
    palm_tree_geo,
    red_flame_shadow_geo,
    mushroom_1up_geo,
    red_coin_geo,
    red_coin_no_shadow_geo,
    number_geo,
    explosion_geo,
    dirt_animation_geo,
    cartoon_star_geo,
    blue_coin_switch_geo,
    dAmpGeo,
    purple_switch_geo,
    checkerboard_platform_geo,
    breakable_box_geo,
    breakable_box_small_geo,
    exclamation_box_outline_geo,
    exclamation_box_geo,
    goomba_geo,
    koopa_shell_geo,
    metal_box_geo,
    black_bobomb_geo,
    bobomb_buddy_geo,
    bowling_ball_geo,
    cannon_barrel_geo,
    cannon_base_geo,
    heart_geo,
    flyguy_geo,
    chuckya_geo,
    bowling_ball_track_geo,
    bullet_bill_geo,
    yellow_sphere_geo,
    hoot_geo,
    yoshi_egg_geo,
    thwomp_geo,
    heave_ho_geo,
    blargg_geo,
    bully_geo,
    bully_boss_geo,
    water_bomb_geo,
    water_bomb_shadow_geo,
    king_bobomb_geo,
    manta_seg5_geo_05008D14,
    unagi_geo,
    sushi_geo,
    clam_shell_geo,
    pokey_head_geo,
    pokey_body_part_geo,
    tweester_geo,
    klepto_geo,
    eyerok_left_hand_geo,
    eyerok_right_hand_geo,
    monty_mole_geo,
    ukiki_geo,
    fwoosh_geo,
    spindrift_geo,
    mr_blizzard_hidden_geo,
    mr_blizzard_geo,
    penguin_geo,
    cap_switch_geo,
    boo_geo,
    small_key_geo,
    haunted_chair_geo,
    mad_piano_geo,
    bookend_part_geo,
    bookend_geo,
    haunted_cage_geo,
    birds_geo,
    peach_geo,
    yoshi_geo,
    enemy_lakitu_geo,
    spiny_ball_geo,
    spiny_geo,
    wiggler_head_geo,
    wiggler_body_geo,
    bubba_geo,
    bowser_geo,
    bowser_bomb_geo,
    bowser_impact_smoke_geo,
    bowser_flames_geo,
    invisible_bowser_accessory_geo,
    bowser_geo_no_shadow,
    bub_geo,
    treasure_chest_base_geo,
    treasure_chest_lid_geo,
    cyan_fish_geo,
    water_ring_geo,
    water_mine_geo,
    seaweed_geo,
    skeeter_geo,
    piranha_plant_geo,
    whomp_geo,
    koopa_with_shell_geo,
    koopa_without_shell_geo,
    metallic_ball_geo,
    chain_chomp_geo,
    koopa_flag_geo,
    wooden_post_geo,
    mips_geo,
    boo_castle_geo,
    lakitu_geo,
    toad_geo,
    chilly_chief_geo,
    chilly_chief_big_geo,
    moneybag_geo,
    swoop_geo,
    scuttlebug_geo,
    mr_i_iris_geo,
    mr_i_geo,
    dorrie_geo,
    snufit_geo,
    haunted_door_geo,
    bubbly_tree_geo,
    spiky_tree_geo,
    wooden_door_geo,
    castle_door_geo,
    metal_door_geo,
    castle_door_0_star_geo,
    castle_door_1_star_geo,
    castle_door_3_stars_geo,
    key_door_geo,
    cabin_door_geo,
    snow_tree_geo,
    hazy_maze_door_geo,
    debug_level_select_dl_07000858,
    debug_level_select_dl_07001100,
    debug_level_select_dl_07001BA0,
    debug_level_select_dl_070025F0,
    debug_level_select_dl_07003258,
    debug_level_select_dl_07003DB8,
    debug_level_select_dl_070048C8,
    debug_level_select_dl_07005558,
    debug_level_select_dl_070059F8,
    debug_level_select_dl_070063B0,
#if defined(VERSION_SH) || defined(RUMBLE_GRAPHIC)
    geo_intro_rumble_pak_graphic,
#endif
};

static void level_cmd_load_model_from_geo_dyn(void) {
    s16 arg0 = CMD_GET(s16, 2);
    volatile u32 arg1 = CMD_GET(u32, 4);

    if (arg0 < 256) {
        gLoadedGraphNodes[arg0] = process_geo_layout(sLevelPool, (void*) geo_dyn_map[arg1]);
    }

    sCurrentCmd = CMD_NEXT;
}

//static void level_cmd_23(void) {
//    union {
//        s32 i;
//        f32 f;
//    } arg2;
//
//    s16 model = CMD_GET(s16, 2) & 0x0FFF;
//    s16 arg0H = ((u16)CMD_GET(s16, 2)) >> 12;
//    void *arg1 = CMD_GET(void *, 4);
//    // load an f32, but using an integer load instruction for some reason (hence the union)
//    arg2.i = CMD_GET(s32, 8);
//
//    if (model < 256) {
//        // GraphNodeScale has a GraphNode at the top. This
//        // is being stored to the array, so cast the pointer.
//        gLoadedGraphNodes[model] =
//            (struct GraphNode *) init_graph_node_scale(sLevelPool, 0, arg0H, arg1, arg2.f);
//    }
//
//    sCurrentCmd = CMD_NEXT;
//}

static void level_cmd_init_mario(void) {
    vec3s_set(gMarioSpawnInfo->startPos, 0, 0, 0);
    vec3s_set(gMarioSpawnInfo->startAngle, 0, 0, 0);

    gMarioSpawnInfo->activeAreaIndex = -1;
    gMarioSpawnInfo->areaIndex = 0;
    gMarioSpawnInfo->behaviorArg = CMD_GET(u32, 4);
    if(CMD_GET(u8, 2)) {
        gMarioSpawnInfo->behaviorScript = segmented_to_virtual(geo_dyn_map[CMD_GET(u32, 8)]);
    } else {
        abort();
    }
    gMarioSpawnInfo->unk18 = gLoadedGraphNodes[CMD_GET(u8, 3)];
    gMarioSpawnInfo->next = NULL;

    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_place_object(void) {
    u8 val7 = 1 << (gCurrActNum - 1);
    u16 model;
    struct SpawnInfo *spawnInfo;

    if (sCurrAreaIndex != -1 && ((CMD_GET(u8, 2) & val7) || CMD_GET(u8, 2) == 0x1F)) {
        model = CMD_GET(u8, 3);
        spawnInfo = alloc_only_pool_alloc(sLevelPool, sizeof(struct SpawnInfo));

        spawnInfo->startPos[0] = CMD_GET(s16, 4);
        spawnInfo->startPos[1] = CMD_GET(s16, 6);
        spawnInfo->startPos[2] = CMD_GET(s16, 8);

        spawnInfo->startAngle[0] = CMD_GET(s16, 10) * 0x8000 / 180;
        spawnInfo->startAngle[1] = CMD_GET(s16, 12) * 0x8000 / 180;
        spawnInfo->startAngle[2] = CMD_GET(s16, 14) * 0x8000 / 180;

        spawnInfo->areaIndex = sCurrAreaIndex;
        spawnInfo->activeAreaIndex = sCurrAreaIndex;

        spawnInfo->behaviorArg = CMD_GET(u32, 16);
        spawnInfo->behaviorScript = segmented_to_virtual(CMD_GET(void *, 20));
        spawnInfo->unk18 = gLoadedGraphNodes[model];
        spawnInfo->next = gAreas[sCurrAreaIndex].objectSpawnInfos;

        gAreas[sCurrAreaIndex].objectSpawnInfos = spawnInfo;
    }

    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_place_object_dyn(void) {
    u8 val7 = 1 << (gCurrActNum - 1);
    u16 model;
    struct SpawnInfo *spawnInfo;

    if (sCurrAreaIndex != -1 && ((CMD_GET(u8, 2) & val7) || CMD_GET(u8, 2) == 0x1F)) {
        model = CMD_GET(u8, 3);
        spawnInfo = alloc_only_pool_alloc(sLevelPool, sizeof(struct SpawnInfo));

        spawnInfo->startPos[0] = CMD_GET(s16, 4);
        spawnInfo->startPos[1] = CMD_GET(s16, 6);
        spawnInfo->startPos[2] = CMD_GET(s16, 8);

        spawnInfo->startAngle[0] = CMD_GET(s16, 10) * 0x8000 / 180;
        spawnInfo->startAngle[1] = CMD_GET(s16, 12) * 0x8000 / 180;
        spawnInfo->startAngle[2] = CMD_GET(s16, 14) * 0x8000 / 180;

        spawnInfo->areaIndex = sCurrAreaIndex;
        spawnInfo->activeAreaIndex = sCurrAreaIndex;

        spawnInfo->behaviorArg = CMD_GET(u32, 16);
        spawnInfo->behaviorScript = segmented_to_virtual(geo_dyn_map[CMD_GET(u32, 20)]);
        spawnInfo->unk18 = gLoadedGraphNodes[model];
        spawnInfo->next = gAreas[sCurrAreaIndex].objectSpawnInfos;

        gAreas[sCurrAreaIndex].objectSpawnInfos = spawnInfo;
    }

    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_create_warp_node(void) {
    if (sCurrAreaIndex != -1) {
        struct ObjectWarpNode *warpNode =
            alloc_only_pool_alloc(sLevelPool, sizeof(struct ObjectWarpNode));

        warpNode->node.id = CMD_GET(u8, 2);
        warpNode->node.destLevel = CMD_GET(u8, 3) + CMD_GET(u8, 6);
        warpNode->node.destArea = CMD_GET(u8, 4);
        warpNode->node.destNode = CMD_GET(u8, 5);

        warpNode->object = NULL;

        warpNode->next = gAreas[sCurrAreaIndex].warpNodes;
        gAreas[sCurrAreaIndex].warpNodes = warpNode;
    }

    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_create_instant_warp(void) {
    s32 i;
    struct InstantWarp *warp;

    if (sCurrAreaIndex != -1) {
        if (gAreas[sCurrAreaIndex].instantWarps == NULL) {
            gAreas[sCurrAreaIndex].instantWarps =
                alloc_only_pool_alloc(sLevelPool, 4 * sizeof(struct InstantWarp));

            for (i = INSTANT_WARP_INDEX_START; i < INSTANT_WARP_INDEX_STOP; i++) {
                gAreas[sCurrAreaIndex].instantWarps[i].id = 0;
            }
        }

        warp = gAreas[sCurrAreaIndex].instantWarps + CMD_GET(u8, 2);

        warp[0].id = 1;
        warp[0].area = CMD_GET(u8, 3);

        warp[0].displacement[0] = CMD_GET(s16, 4);
        warp[0].displacement[1] = CMD_GET(s16, 6);
        warp[0].displacement[2] = CMD_GET(s16, 8);
    }

    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_set_terrain_type(void) {
    if (sCurrAreaIndex != -1) {
        gAreas[sCurrAreaIndex].terrainType |= CMD_GET(s16, 2);
    }

    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_create_painting_warp_node(void) {
    s32 i;
    struct WarpNode *node;

    if (sCurrAreaIndex != -1) {
        if (gAreas[sCurrAreaIndex].paintingWarpNodes == NULL) {
            gAreas[sCurrAreaIndex].paintingWarpNodes =
                alloc_only_pool_alloc(sLevelPool, 45 * sizeof(struct WarpNode));

            for (i = 0; i < 45; i++) {
                gAreas[sCurrAreaIndex].paintingWarpNodes[i].id = 0;
            }
        }

        node = &gAreas[sCurrAreaIndex].paintingWarpNodes[CMD_GET(u8, 2)];

        node->id = 1;
        node->destLevel = CMD_GET(u8, 3) + CMD_GET(u8, 6);
        node->destArea = CMD_GET(u8, 4);
        node->destNode = CMD_GET(u8, 5);
    }

    sCurrentCmd = CMD_NEXT;
}

//static void level_cmd_3A(void) {
//    struct UnusedArea28 *val4;
//
//    if (sCurrAreaIndex != -1) {
//        if ((val4 = gAreas[sCurrAreaIndex].unused28) == NULL) {
//            val4 = gAreas[sCurrAreaIndex].unused28 =
//                alloc_only_pool_alloc(sLevelPool, sizeof(struct UnusedArea28));
//        }
//
//        val4->unk00 = CMD_GET(s16, 2);
//        val4->unk02 = CMD_GET(s16, 4);
//        val4->unk04 = CMD_GET(s16, 6);
//        val4->unk06 = CMD_GET(s16, 8);
//        val4->unk08 = CMD_GET(s16, 10);
//    }
//
//    sCurrentCmd = CMD_NEXT;
//}

static void level_cmd_create_whirlpool(void) {
    struct Whirlpool *whirlpool;
    s32 index = CMD_GET(u8, 2);
    s32 beatBowser2 =
        (save_file_get_flags() & (SAVE_FLAG_HAVE_KEY_2 | SAVE_FLAG_UNLOCKED_UPSTAIRS_DOOR)) != 0;

    if (CMD_GET(u8, 3) == 0 || (CMD_GET(u8, 3) == 1 && !beatBowser2)
        || (CMD_GET(u8, 3) == 2 && beatBowser2) || (CMD_GET(u8, 3) == 3 && gCurrActNum >= 2)) {
        if (sCurrAreaIndex != -1 && index < 2) {
            if ((whirlpool = gAreas[sCurrAreaIndex].whirlpools[index]) == NULL) {
                whirlpool = alloc_only_pool_alloc(sLevelPool, sizeof(struct Whirlpool));
                gAreas[sCurrAreaIndex].whirlpools[index] = whirlpool;
            }

            vec3s_set(whirlpool->pos, CMD_GET(s16, 4), CMD_GET(s16, 6), CMD_GET(s16, 8));
            whirlpool->strength = CMD_GET(s16, 10);
        }
    }

    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_set_blackout(void) {
    //osViBlack(CMD_GET(u8, 2)); // TODO?
    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_set_gamma(void) {
    //osViSetSpecialFeatures(CMD_GET(u8, 2) == 0 ? OS_VI_GAMMA_OFF : OS_VI_GAMMA_ON); // TODO?
    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_set_terrain_data(void) {
    if (sCurrAreaIndex != -1) {
#ifndef NO_SEGMENTED_MEMORY
        gAreas[sCurrAreaIndex].terrainData = segmented_to_virtual(CMD_GET(void *, 4));
#else
        Collision *data;
        u32 size;

        // The game modifies the terrain data and must be reset upon level reload.
        data = segmented_to_virtual(CMD_GET(void *, 4));
        size = get_area_terrain_size(data) * sizeof(Collision);
        gAreas[sCurrAreaIndex].terrainData = alloc_only_pool_alloc(sLevelPool, size);
        memcpy(gAreas[sCurrAreaIndex].terrainData, data, size);
#endif
    }
    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_set_rooms(void) {
    if (sCurrAreaIndex != -1) {
        gAreas[sCurrAreaIndex].surfaceRooms = segmented_to_virtual(CMD_GET(void *, 4));
    }
    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_set_macro_objects(void) {
    if (sCurrAreaIndex != -1) {
#ifndef NO_SEGMENTED_MEMORY
        gAreas[sCurrAreaIndex].macroObjects = segmented_to_virtual(CMD_GET(void *, 4));
#else
        // The game modifies the macro object data (for example marking coins as taken),
        // so it must be reset when the level reloads.
        MacroObject *data = segmented_to_virtual(CMD_GET(void *, 4));
        s32 len = 0;
        while (data[len++] != MACRO_OBJECT_END()) {
            len += 4;
        }
        gAreas[sCurrAreaIndex].macroObjects = alloc_only_pool_alloc(sLevelPool, len * sizeof(MacroObject));
        memcpy(gAreas[sCurrAreaIndex].macroObjects, data, len * sizeof(MacroObject));
#endif
    }
    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_load_area(void) {
    s16 areaIndex = CMD_GET(u8, 2);

    stop_sounds_in_continuous_banks();
    load_area(areaIndex);

    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_unload_area(void) {
    unload_area();
    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_set_mario_start_pos(void) {
    gMarioSpawnInfo->areaIndex = CMD_GET(u8, 2);

#if IS_64_BIT
    vec3s_set(gMarioSpawnInfo->startPos, CMD_GET(s16, 6), CMD_GET(s16, 8), CMD_GET(s16, 10));
#else
    vec3s_copy(gMarioSpawnInfo->startPos, CMD_GET(Vec3s, 6));
#endif
    vec3s_set(gMarioSpawnInfo->startAngle, 0, CMD_GET(s16, 4) * 0x8000 / 180, 0);

    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_set_transition(void) {
    if (gCurrentArea != NULL) {
        play_transition(CMD_GET(u8, 2), CMD_GET(u8, 3), CMD_GET(u8, 4), CMD_GET(u8, 5), CMD_GET(u8, 6));
    }
    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_nop(void) {
    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_show_dialog(void) {
    if (sCurrAreaIndex != -1) {
        if (CMD_GET(u8, 2) < 2) {
            gAreas[sCurrAreaIndex].dialog[CMD_GET(u8, 2)] = CMD_GET(u8, 3);
        }
    }
    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_set_music(void) {
    if (sCurrAreaIndex != -1) {
        gAreas[sCurrAreaIndex].musicParam = CMD_GET(s16, 2);
        gAreas[sCurrAreaIndex].musicParam2 = CMD_GET(s16, 4);
    }
    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_set_menu_music(void) {
    set_background_music(0, CMD_GET(s16, 2), 0);
    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_38(void) {
    fadeout_music(CMD_GET(s16, 2));
    sCurrentCmd = CMD_NEXT;
}

static void level_cmd_get_or_set_var(void) {
    if (CMD_GET(u8, 2) == 0) {
        switch (CMD_GET(u8, 3)) {
            case 0:
                gCurrSaveFileNum = sRegister;
                break;
            case 1:
                gCurrCourseNum = sRegister;
                break;
            case 2:
                gCurrActNum = sRegister;
                break;
            case 3:
                gCurrLevelNum = sRegister;
                break;
            case 4:
                gCurrAreaIndex = sRegister;
                break;
        }
    } else {
        switch (CMD_GET(u8, 3)) {
            case 0:
                sRegister = gCurrSaveFileNum;
                break;
            case 1:
                sRegister = gCurrCourseNum;
                break;
            case 2:
                sRegister = gCurrActNum;
                break;
            case 3:
                sRegister = gCurrLevelNum;
                break;
            case 4:
                sRegister = gCurrAreaIndex;
                break;
        }
    }

    sCurrentCmd = CMD_NEXT;
}

[[gnu::noinline]] struct LevelCommand *level_script_execute(struct LevelCommand *cmd) {
    sScriptStatus = SCRIPT_RUNNING;
    sCurrentCmd = cmd;

    while (sScriptStatus == SCRIPT_RUNNING) {
    	//printf("running command %02X (%d) at 0x%08X\n", sCurrentCmd->type, sCurrentCmd->size, sCurrentCmd);
        switch(sCurrentCmd->type) {
	        case 0x00: level_cmd_load_and_execute(); break;
	        case 0x01: level_cmd_exit_and_execute(); break;
	        case 0x02: level_cmd_exit(); break;
	        case 0x03: level_cmd_sleep(); break;
	        case 0x04: level_cmd_sleep2(); break;
	        case 0x05: level_cmd_jump(); break;
	        case 0x06: level_cmd_jump_and_link(); break;
	        case 0x07: level_cmd_return(); break;
	        case 0x08: level_cmd_jump_and_link_push_arg(); break;
	        case 0x09: level_cmd_jump_repeat(); break;
	        case 0x0A: level_cmd_loop_begin(); break;
	        case 0x0B: level_cmd_loop_until(); break;
	        case 0x0C: level_cmd_jump_if(); break;
	        case 0x0D: level_cmd_jump_and_link_if(); break;
	        case 0x0E: level_cmd_skip_if(); break;
	        case 0x0F: level_cmd_skip(); break;
	        case 0x10: level_cmd_skippable_nop(); break;
	        case 0x11: level_cmd_call(); break;
	        case 0x12: level_cmd_call_loop(); break;
	        case 0x13: level_cmd_set_register(); break;
	        case 0x14: level_cmd_push_pool_state(); break;
	        case 0x15: level_cmd_pop_pool_state(); break;
	        case 0x16: level_cmd_load_to_fixed_address(); break;
	        case 0x17: level_cmd_load_raw(); break;
	        case 0x18: level_cmd_load_mio0(); break;
	        case 0x19: level_cmd_load_mario_head(); break;
	        case 0x1A: level_cmd_load_mio0_texture(); break;
	        case 0x1B: level_cmd_init_level(); break;
	        case 0x1C: level_cmd_clear_level(); break;
	        case 0x1D: level_cmd_alloc_level_pool(); break;
	        case 0x1E: level_cmd_free_level_pool(); break;
	        case 0x1F: level_cmd_begin_area(); break;
	        case 0x20: level_cmd_end_area(); break;
	        case 0x21: level_cmd_load_model_from_dl(); break;
	        case 0x22: level_cmd_load_model_from_geo(); break;
	        case 0x23: level_cmd_load_model_from_geo_dyn(); break;
	        case 0x24: level_cmd_place_object(); break;
	        case 0x25: level_cmd_init_mario(); break;
	        case 0x26: level_cmd_create_warp_node(); break;
	        case 0x27: level_cmd_create_painting_warp_node(); break;
	        case 0x28: level_cmd_create_instant_warp(); break;
	        case 0x29: level_cmd_load_area(); break;
	        case 0x2A: level_cmd_unload_area(); break;
	        case 0x2B: level_cmd_set_mario_start_pos(); break;
	        case 0x2C: level_cmd_place_object_dyn(); break;
	        case 0x2D: level_cmd_call_loop_dyn(); break;
	        case 0x2E: level_cmd_set_terrain_data(); break;
	        case 0x2F: level_cmd_set_rooms(); break;
	        case 0x30: level_cmd_show_dialog(); break;
	        case 0x31: level_cmd_set_terrain_type(); break;
	        case 0x32: level_cmd_nop(); break;
	        case 0x33: level_cmd_set_transition(); break;
	        case 0x34: level_cmd_set_blackout(); break;
	        case 0x35: level_cmd_set_gamma(); break;
	        case 0x36: level_cmd_set_music(); break;
	        case 0x37: level_cmd_set_menu_music(); break;
	        case 0x38: level_cmd_38(); break;
	        case 0x39: level_cmd_set_macro_objects(); break;
	        case 0x3A: level_cmd_call_dyn(); break;
	        case 0x3B: level_cmd_create_whirlpool(); break;
	        case 0x3C: level_cmd_get_or_set_var(); break;
	        case 0x3D: level_cmd_exit_and_execute_dyn(); break;
        }
    }

    profiler_log_thread5_time(LEVEL_SCRIPT_EXECUTE);
    render_game();
    if (gShowProfiler) {
        draw_profiler();
    }

    return sCurrentCmd;
}
