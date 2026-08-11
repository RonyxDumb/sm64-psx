#include <ultra64.h>
#include "sm64.h"
#include "game/level_update.h"
#include "level_commands.h"
#include "game/area.h"

#include "make_const_nonconst.h"

#include "segment_symbols.h"

#include "actors/common0.h"
#include "actors/common1.h"
#include "actors/group0.h"
#include "actors/group1.h"
#include "actors/group2.h"
#include "actors/group3.h"
#include "actors/group4.h"
#include "actors/group5.h"
#include "actors/group6.h"
#include "actors/group7.h"
#include "actors/group8.h"
#include "actors/group9.h"
#include "actors/group10.h"
#include "actors/group11.h"
#include "actors/group12.h"
#include "actors/group13.h"
#include "actors/group14.h"
#include "actors/group15.h"
#include "actors/group16.h"
#include "actors/group17.h"

#include "levels/menu/header.h"
#include "levels/intro/header.h"

#include "level_headers.h"

#include "level_table.h"

#define STUB_LEVEL(_0, _1, _2, _3, _4, _5, _6, _7, _8)
#define DEFINE_LEVEL(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10) + 3
#define DEFINE_LEVEL_REMOVED(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10) + 3

static const LevelScript script_exec_level_table[2
  #include "level_defines.h"
];

#undef DEFINE_LEVEL_REMOVED
#undef DEFINE_LEVEL
#undef STUB_LEVEL

static const LevelScript script_L1[4];
static const LevelScript script_L2[4];
static const LevelScript goto_mario_head_regular[4];
static const LevelScript goto_mario_head_dizzy[4];
static const LevelScript script_L5[4];

#define STUB_LEVEL(_0, _1, _2, _3, _4, _5, _6, _7, _8)
#define DEFINE_LEVEL(_0, _1, _2, folder, _4, _5, _6, _7, _8, _9, _10) static const LevelScript script_exec_ ## folder [4 + 1];
#define DEFINE_LEVEL_REMOVED(_0, _1, _2, folder, _4, _5, _6, _7, _8, _9, _10) static const LevelScript script_exec_ ## folder [0];
#include "level_defines.h"
#undef DEFINE_LEVEL_REMOVED
#undef DEFINE_LEVEL
#undef STUB_LEVEL

const LevelScript level_main_scripts_entry[] = {
    LOAD_MIO0(/*seg*/ 0x04, _group0_mio0SegmentRomStart, _group0_mio0SegmentRomEnd),
    LOAD_MIO0(/*seg*/ 0x03, _common1_mio0SegmentRomStart, _common1_mio0SegmentRomEnd),
    LOAD_RAW( /*seg*/ 0x17, _group0_geoSegmentRomStart, _group0_geoSegmentRomEnd),
    LOAD_RAW( /*seg*/ 0x16, _common1_geoSegmentRomStart, _common1_geoSegmentRomEnd),
    LOAD_RAW( /*seg*/ 0x13, _behaviorSegmentRomStart, _behaviorSegmentRomEnd),
    ALLOC_LEVEL_POOL(),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_MARIO,                   DYN_mario_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_SMOKE,                   DYN_smoke_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_SPARKLES,                DYN_sparkles_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BUBBLE,                  DYN_bubble_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_SMALL_WATER_SPLASH,      DYN_small_water_splash_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_IDLE_WATER_WAVE,         DYN_idle_water_wave_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_WATER_SPLASH,            DYN_water_splash_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_WAVE_TRAIL,              DYN_wave_trail_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_YELLOW_COIN,             DYN_yellow_coin_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_STAR,                    DYN_star_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_TRANSPARENT_STAR,        DYN_transparent_star_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_WOODEN_SIGNPOST,         DYN_wooden_signpost_geo),
    LOAD_MODEL_FROM_DL( MODEL_WHITE_PARTICLE_SMALL,    white_particle_small_dl,     LAYER_ALPHA),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_RED_FLAME,               DYN_red_flame_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BLUE_FLAME,              DYN_blue_flame_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BURN_SMOKE,              DYN_burn_smoke_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_LEAVES,                  DYN_leaves_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_PURPLE_MARBLE,           DYN_purple_marble_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_FISH,                    DYN_fish_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_FISH_SHADOW,             DYN_fish_shadow_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_SPARKLES_ANIMATION,      DYN_sparkles_animation_geo),
    LOAD_MODEL_FROM_DL( MODEL_SAND_DUST,               sand_seg3_dl_0302BCD0,       LAYER_ALPHA),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BUTTERFLY,               DYN_butterfly_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BURN_SMOKE_UNUSED,       DYN_burn_smoke_geo),
    LOAD_MODEL_FROM_DL( MODEL_PEBBLE,                  pebble_seg3_dl_0301CB00,     LAYER_ALPHA),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_MIST,                    DYN_mist_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_WHITE_PUFF,              DYN_white_puff_geo),
    LOAD_MODEL_FROM_DL( MODEL_WHITE_PARTICLE_DL,       white_particle_dl,           LAYER_ALPHA),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_WHITE_PARTICLE,          DYN_white_particle_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_YELLOW_COIN_NO_SHADOW,   DYN_yellow_coin_no_shadow_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BLUE_COIN,               DYN_blue_coin_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BLUE_COIN_NO_SHADOW,     DYN_blue_coin_no_shadow_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_MARIOS_WINGED_METAL_CAP, DYN_marios_winged_metal_cap_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_MARIOS_METAL_CAP,        DYN_marios_metal_cap_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_MARIOS_WING_CAP,         DYN_marios_wing_cap_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_MARIOS_CAP,              DYN_marios_cap_geo),
    //LOAD_MODEL_FROM_GEO_DYN(MODEL_MARIOS_CAP,              DYN_marios_cap_geo), // repeated
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BOWSER_KEY_CUTSCENE,     DYN_bowser_key_cutscene_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BOWSER_KEY,              DYN_bowser_key_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_RED_FLAME_SHADOW,        DYN_red_flame_shadow_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_1UP,                     DYN_mushroom_1up_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_RED_COIN,                DYN_red_coin_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_RED_COIN_NO_SHADOW,      DYN_red_coin_no_shadow_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_NUMBER,                  DYN_number_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_EXPLOSION,               DYN_explosion_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_DIRT_ANIMATION,          DYN_dirt_animation_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_CARTOON_STAR,            DYN_cartoon_star_geo),
    FREE_LEVEL_POOL(),
    CALL(/*arg*/ 0, /*func*/ lvl_init_from_save_file),
    LOOP_BEGIN(),
#ifndef BENCH
        EXECUTE(/*seg*/ 0x14, _menuSegmentRomStart, _menuSegmentRomEnd, level_main_menu_entry_2),
#endif
        JUMP_LINK(script_exec_level_table),
        SLEEP(/*frames*/ 1),
    LOOP_UNTIL(/*op*/ OP_LT, /*arg*/ 0),
    JUMP_IF(/*op*/ OP_EQ, /*arg*/ -1, script_L2),
    JUMP_IF(/*op*/ OP_EQ, /*arg*/ -2, goto_mario_head_regular),
    JUMP_IF(/*op*/ OP_EQ, /*arg*/ -3, goto_mario_head_dizzy),
    JUMP_IF(/*op*/ OP_EQ, /*arg*/ -8, script_L1),
    JUMP_IF(/*op*/ OP_EQ, /*arg*/ -9, script_L5),
};

static const LevelScript script_L1[] = {
    EXIT_AND_EXECUTE(/*seg*/ 0x14, _introSegmentRomStart, _introSegmentRomEnd, level_intro_splash_screen),
};

static const LevelScript script_L2[] = {
#ifndef BENCH
    EXIT_AND_EXECUTE(/*seg*/ 0x0E, _endingSegmentRomStart, _endingSegmentRomEnd, level_ending_entry),
#endif
};

static const LevelScript goto_mario_head_regular[] = {
    EXIT_AND_EXECUTE(/*seg*/ 0x14, _introSegmentRomStart, _introSegmentRomEnd, level_intro_mario_head_regular),
};

static const LevelScript goto_mario_head_dizzy[] = {
    EXIT_AND_EXECUTE(/*seg*/ 0x14, _introSegmentRomStart, _introSegmentRomEnd, level_intro_mario_head_dizzy),
};

static const LevelScript script_L5[] = {
    EXIT_AND_EXECUTE(/*seg*/ 0x14, _introSegmentRomStart, _introSegmentRomEnd, level_intro_entry_4),
};

// Include the level jumptable.

#define STUB_LEVEL(_0, _1, _2, _3, _4, _5, _6, _7, _8)

#define DEFINE_LEVEL(_0, levelenum, _2, folder, _4, _5, _6, _7, _8, _9, _10) JUMP_IF(OP_EQ, levelenum, script_exec_ ## folder),

#define DEFINE_LEVEL_REMOVED(_0, levelenum, _2, folder, _4, _5, _6, _7, _8, _9, _10) JUMP_IF(OP_EQ, levelenum, script_exec_bob),

static const LevelScript script_exec_level_table[] = {
    GET_OR_SET(/*op*/ OP_GET, /*var*/ VAR_CURR_LEVEL_NUM),
    #include "levels/level_defines.h"
    EXIT(),
};
#undef DEFINE_LEVEL_REMOVED
#undef DEFINE_LEVEL

#define DEFINE_LEVEL(_0, _1, _2, folder, _4, _5, _6, _7, _8, _9, _10) \
static const LevelScript script_exec_ ## folder [] = { \
    EXECUTE(0x0E, _ ## folder ## SegmentRomStart, _ ## folder ## SegmentRomEnd, level_ ## folder ## _entry), \
    RETURN(), \
};
#define DEFINE_LEVEL_REMOVED(_0, _1, _2, folder, _4, _5, _6, _7, _8, _9, _10) /*\
static const LevelScript script_exec_ ## folder [] = { \
    EXECUTE(0x0E, _bobSegmentRomStart, _bobSegmentRomEnd, level_bob_entry), \
    RETURN(), \
};*/

#include "levels/level_defines.h"
#undef STUB_LEVEL
#undef DEFINE_LEVEL
#undef DEFINE_LEVEL_REMOVED

const LevelScript script_func_global_1[] = {
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BLUE_COIN_SWITCH,        DYN_blue_coin_switch_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_AMP,                     DYN_dAmpGeo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_PURPLE_SWITCH,           DYN_purple_switch_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_CHECKERBOARD_PLATFORM,   DYN_checkerboard_platform_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BREAKABLE_BOX,           DYN_breakable_box_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BREAKABLE_BOX_SMALL,     DYN_breakable_box_small_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_EXCLAMATION_BOX_OUTLINE, DYN_exclamation_box_outline_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_EXCLAMATION_BOX,         DYN_exclamation_box_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_GOOMBA,                  DYN_goomba_geo),
    LOAD_MODEL_FROM_DL( MODEL_EXCLAMATION_POINT,       exclamation_box_outline_seg8_dl_08025F08, LAYER_ALPHA),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_KOOPA_SHELL,             DYN_koopa_shell_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_METAL_BOX,               DYN_metal_box_geo),
    LOAD_MODEL_FROM_DL( MODEL_METAL_BOX_DL,            metal_box_dl,                             LAYER_OPAQUE),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BLACK_BOBOMB,            DYN_black_bobomb_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BOBOMB_BUDDY,            DYN_bobomb_buddy_geo),
    LOAD_MODEL_FROM_DL( MODEL_DL_CANNON_LID,           cannon_lid_seg8_dl_080048E0,              LAYER_OPAQUE),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BOWLING_BALL,            DYN_bowling_ball_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_CANNON_BARREL,           DYN_cannon_barrel_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_CANNON_BASE,             DYN_cannon_base_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_HEART,                   DYN_heart_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_FLYGUY,                  DYN_flyguy_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_CHUCKYA,                 DYN_chuckya_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_TRAJECTORY_MARKER_BALL,      DYN_bowling_ball_track_geo),
    RETURN(),
};

const LevelScript script_func_global_2[] = {
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BULLET_BILL,             DYN_bullet_bill_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_YELLOW_SPHERE,           DYN_yellow_sphere_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_HOOT,                    DYN_hoot_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_YOSHI_EGG,               DYN_yoshi_egg_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_THWOMP,                  DYN_thwomp_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_HEAVE_HO,                DYN_heave_ho_geo),
    RETURN(),
};

const LevelScript script_func_global_3[] = {
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BLARGG,                  DYN_blargg_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BULLY,                   DYN_bully_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BULLY_BOSS,              DYN_bully_boss_geo),
    RETURN(),
};

const LevelScript script_func_global_4[] = {
    LOAD_MODEL_FROM_GEO_DYN(MODEL_WATER_BOMB,              DYN_water_bomb_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_WATER_BOMB_SHADOW,       DYN_water_bomb_shadow_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_KING_BOBOMB,             DYN_king_bobomb_geo),
    RETURN(),
};

const LevelScript script_func_global_5[] = {
    LOAD_MODEL_FROM_GEO_DYN(MODEL_MANTA_RAY,               DYN_manta_seg5_geo_05008D14),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_UNAGI,                   DYN_unagi_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_SUSHI,                   DYN_sushi_geo),
    LOAD_MODEL_FROM_DL( MODEL_DL_WHIRLPOOL,            whirlpool_seg5_dl_05013CB8, LAYER_TRANSPARENT),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_CLAM_SHELL,              DYN_clam_shell_geo),
    RETURN(),
};

const LevelScript script_func_global_6[] = {
    LOAD_MODEL_FROM_GEO_DYN(MODEL_POKEY_HEAD,              DYN_pokey_head_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_POKEY_BODY_PART,         DYN_pokey_body_part_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_TWEESTER,                DYN_tweester_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_KLEPTO,                  DYN_klepto_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_EYEROK_LEFT_HAND,        DYN_eyerok_left_hand_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_EYEROK_RIGHT_HAND,       DYN_eyerok_right_hand_geo),
    RETURN(),
};

const LevelScript script_func_global_7[] = {
    LOAD_MODEL_FROM_DL( MODEL_DL_MONTY_MOLE_HOLE,      monty_mole_hole_seg5_dl_05000840, LAYER_TRANSPARENT_DECAL),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_MONTY_MOLE,              DYN_monty_mole_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_UKIKI,                   DYN_ukiki_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_FWOOSH,                  DYN_fwoosh_geo),
    RETURN(),
};

const LevelScript script_func_global_8[] = {
    LOAD_MODEL_FROM_GEO_DYN(MODEL_SPINDRIFT,               DYN_spindrift_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_MR_BLIZZARD_HIDDEN,      DYN_mr_blizzard_hidden_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_MR_BLIZZARD,             DYN_mr_blizzard_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_PENGUIN,                 DYN_penguin_geo),
    RETURN(),
};

const LevelScript script_func_global_9[] = {
    LOAD_MODEL_FROM_DL( MODEL_CAP_SWITCH_EXCLAMATION,  cap_switch_exclamation_seg5_dl_05002E00, LAYER_ALPHA),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_CAP_SWITCH,              DYN_cap_switch_geo),
    LOAD_MODEL_FROM_DL( MODEL_CAP_SWITCH_BASE,         cap_switch_base_seg5_dl_05003120,        LAYER_OPAQUE),
    RETURN(),
};

const LevelScript script_func_global_10[] = {
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BOO,                     DYN_boo_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BETA_BOO_KEY,               DYN_small_key_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_HAUNTED_CHAIR,           DYN_haunted_chair_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_MAD_PIANO,               DYN_mad_piano_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BOOKEND_PART,            DYN_bookend_part_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BOOKEND,                 DYN_bookend_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_HAUNTED_CAGE,            DYN_haunted_cage_geo),
    RETURN(),
};

const LevelScript script_func_global_11[] = {
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BIRDS,                   DYN_birds_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_PEACH,                   DYN_peach_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_YOSHI,                   DYN_yoshi_geo),
    RETURN(),
};

const LevelScript script_func_global_12[] = {
    LOAD_MODEL_FROM_GEO_DYN(MODEL_ENEMY_LAKITU,            DYN_enemy_lakitu_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_SPINY_BALL,              DYN_spiny_ball_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_SPINY,                   DYN_spiny_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_WIGGLER_HEAD,            DYN_wiggler_head_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_WIGGLER_BODY,            DYN_wiggler_body_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BUBBA,                   DYN_bubba_geo),
    RETURN(),
};

const LevelScript script_func_global_13[] = {
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BOWSER,                  DYN_bowser_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BOWSER_BOMB_CHILD_OBJ,   DYN_bowser_bomb_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BOWSER_BOMB,             DYN_bowser_bomb_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BOWSER_SMOKE,            DYN_bowser_impact_smoke_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BOWSER_FLAMES,           DYN_bowser_flames_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BOWSER_WAVE,             DYN_invisible_bowser_accessory_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BOWSER_NO_SHADOW,        DYN_bowser_geo_no_shadow),
    RETURN(),
};

const LevelScript script_func_global_14[] = {
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BUB,                     DYN_bub_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_TREASURE_CHEST_BASE,     DYN_treasure_chest_base_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_TREASURE_CHEST_LID,      DYN_treasure_chest_lid_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_CYAN_FISH,               DYN_cyan_fish_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_WATER_RING,              DYN_water_ring_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_WATER_MINE,              DYN_water_mine_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_SEAWEED,                 DYN_seaweed_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_SKEETER,                 DYN_skeeter_geo),
    RETURN(),
};

const LevelScript script_func_global_15[] = {
    LOAD_MODEL_FROM_GEO_DYN(MODEL_PIRANHA_PLANT,           DYN_piranha_plant_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_WHOMP,                   DYN_whomp_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_KOOPA_WITH_SHELL,        DYN_koopa_with_shell_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_KOOPA_WITHOUT_SHELL,     DYN_koopa_without_shell_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_METALLIC_BALL,           DYN_metallic_ball_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_CHAIN_CHOMP,             DYN_chain_chomp_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_KOOPA_FLAG,              DYN_koopa_flag_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_WOODEN_POST,             DYN_wooden_post_geo),
    RETURN(),
};

const LevelScript script_func_global_16[] = {
    LOAD_MODEL_FROM_GEO_DYN(MODEL_MIPS,                    DYN_mips_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BOO_CASTLE,              DYN_boo_castle_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_LAKITU,                  DYN_lakitu_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_TOAD,                    DYN_toad_geo),
    RETURN(),
};

const LevelScript script_func_global_17[] = {
    LOAD_MODEL_FROM_GEO_DYN(MODEL_CHILL_BULLY,             DYN_chilly_chief_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_BIG_CHILL_BULLY,         DYN_chilly_chief_big_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_MONEYBAG,                DYN_moneybag_geo),
    RETURN(),
};

const LevelScript script_func_global_18[] = {
    LOAD_MODEL_FROM_GEO_DYN(MODEL_SWOOP,                   DYN_swoop_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_SCUTTLEBUG,              DYN_scuttlebug_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_MR_I_IRIS,               DYN_mr_i_iris_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_MR_I,                    DYN_mr_i_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_DORRIE,                  DYN_dorrie_geo),
    LOAD_MODEL_FROM_GEO_DYN(MODEL_SNUFIT,                  DYN_snufit_geo),
    RETURN(),
};
