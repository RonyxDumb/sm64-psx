#include <ultra64.h>

#define INCLUDED_FROM_CAMERA_C

#include "prevent_bss_reordering.h"
#include "sm64.h"
#include "camera.h"
#include "seq_ids.h"
#include "dialog_ids.h"
#include "audio/external.h"
#include "mario_misc.h"
#include "game_init.h"
#include "hud.h"
#include "engine/math_util.h"
#include "area.h"
#include "engine/surface_collision.h"
#include "engine/behavior_script.h"
#include "level_update.h"
#include "ingame_menu.h"
#include "mario_actions_cutscene.h"
#include "save_file.h"
#include "object_helpers.h"
#include "print.h"
#include "spawn_sound.h"
#include "behavior_actions.h"
#include "behavior_data.h"
#include "object_list_processor.h"
#include "paintings.h"
#include "engine/graph_node.h"
#include "level_table.h"

#define CBUTTON_MASK (U_CBUTTONS | D_CBUTTONS | L_CBUTTONS | R_CBUTTONS)

/**
 * @file camera.c
 * Implements the camera system, including C-button input, camera modes, camera triggers, and cutscenes.
 *
 * When working with the camera, you should be familiar with sm64's coordinate system.
 * Relative to the camera, the coordinate system follows the right hand rule:
 *          +X points right.
 *          +Y points up.
 *          +Z points out of the screen.
 *
 * You should also be familiar with Euler angles: 'pitch', 'yaw', and 'roll'.
 *      pitch: rotation about the X-axis, measured from +Y.
 *          Unlike yaw and roll, pitch is bounded in +-0x4000 (90 degrees).
 *          Pitch is 0 when the camera points parallel to the xz-plane (+Y points straight up).
 *
 *      yaw: rotation about the Y-axis, measured from (absolute) +Z.
 *          Positive yaw rotates clockwise, towards +X.
 *
 *      roll: rotation about the Z-axis, measured from the camera's right direction.
 *          Unfortunately, it's weird: For some reason, roll is flipped. Positive roll makes the camera
 *          rotate counterclockwise, which means the WORLD rotates clockwise. Luckily roll is rarely
 *          used.
 *
 *      Remember the right hand rule: make a thumbs-up with your right hand, stick your thumb in the
 *      +direction (except for roll), and the angle follows the rotation of your curled fingers.
 *
 * Illustrations:
 * Following the right hand rule, each hidden axis's positive direction points out of the screen.
 *
 *       YZ-Plane (pitch)        XZ-Plane (yaw)          XY-Plane (roll -- Note flipped)
 *          +Y                      -Z                      +Y
 *           ^                       ^ (into the             ^
 *         --|--                     |   screen)             |<-
 * +pitch /  |  \ -pitch             |                       |  \ -roll
 *       v   |   v                   |                       |   |
 * +Z <------O------> -Z   -X <------O------> +X   -X <------O------> +X
 *           |                   ^   |   ^                   |   |
 *           |                    \  |  /                    |  / +roll
 *           |               -yaw  --|--  +yaw               |<-
 *           v                       v                       v
 *          -Y                      +Z                      -Y
 *
 */

// BSS
/**
 * Stores Lakitu's position from the last frame, used for transitioning in next_lakitu_state()
 */
Vec3q sOldPositionq;
/**
 * Stores Lakitu's focus from the last frame, used for transitioning in next_lakitu_state()
 */
Vec3q sOldFocusq;
/**
 * Global array of PlayerCameraState.
 * L is real.
 */
struct PlayerCameraState gPlayerCameraState[2];
/**
 * Direction controlled by player 2, moves the focus during the credits.
 */
Vec3q sPlayer2FocusOffsetq;
/**
 * The pitch used for the credits easter egg.
 */
s16 sCreditsPlayer2Pitch;
/**
 * The yaw used for the credits easter egg.
 */
s16 sCreditsPlayer2Yaw;
/**
 * Used to decide when to zoom out in the pause menu.
 */
u8 sFramesPaused;

static s16 stick_pitch = 0;

extern struct CameraFOVStatus sFOVState;
extern struct TransitionInfo sModeTransition;
extern struct PlayerGeometry sMarioGeometry;
extern s16 unusedFreeRoamWallYaw;
extern s16 sAvoidYawVel;
extern s16 sCameraYawAfterDoorCutscene;
//extern s16 unusedSplinePitch;
//extern s16 unusedSplineYaw;
extern struct HandheldShakePoint sHandheldShakeSpline[4];
extern s16 sHandheldShakeMag;
extern q32 sHandheldShakeTimerq;
extern q32 sHandheldShakeIncq;
extern s16 sHandheldShakePitch;
extern s16 sHandheldShakeYaw;
extern s16 sHandheldShakeRoll;
extern u32 unused8033B30C;
extern u32 unused8033B310;
extern s16 sSelectionFlags;
extern s16 unused8033B316;
extern s16 s2ndRotateFlags;
extern s16 unused8033B31A;
extern s16 sCameraSoundFlags;
extern u16 sCButtonsPressed;
extern s16 sCutsceneDialogID;
extern struct LakituState gLakituState;
extern s16 unused8033B3E8;
extern s16 sAreaYaw;
extern s16 sAreaYawChange;
extern s16 sLakituDist;
extern s16 sLakituPitch;
extern q32 sZoomAmountq;
extern s16 sCSideButtonYaw;
extern s16 sBehindMarioSoundTimer;
extern q32 sZeroZoomDistq;
extern s16 sCUpCameraPitch;
extern s16 sModeOffsetYaw;
extern s16 sSpiralStairsYawOffset;
extern s16 s8DirModeBaseYaw;
extern s16 s8DirModeYawOffset;
extern q32 sPanDistanceq;
extern q32 sCannonYOffsetq;
extern struct ModeTransitionInfo sModeInfo;
extern Vec3q sCastleEntranceOffsetq;
extern u32 sParTrackIndex;
extern struct ParallelTrackingPoint *sParTrackPath;
extern struct CameraStoredInfo sParTrackTransOff;
extern struct CameraStoredInfo sCameraStoreCUp;
extern struct CameraStoredInfo sCameraStoreCutscene;
extern s16 gCameraMovementFlags;
extern s16 sStatusFlags;
extern struct CutsceneSplinePoint sCurCreditsSplinePos[32];
extern struct CutsceneSplinePoint sCurCreditsSplineFocus[32];
extern s16 sCutsceneSplineSegment;
extern q32 sCutsceneSplineSegmentProgressq;
extern s16 sCutsceneShot;
extern s16 gCutsceneTimer;
extern struct CutsceneVariable sCutsceneVars[10];
extern s32 gObjCutsceneDone;
extern u32 gCutsceneObjSpawn;
extern struct Camera *gCamera;

/**
 * Lakitu's position and focus.
 * @see LakituState
 */
struct LakituState gLakituState;
struct CameraFOVStatus sFOVState;
struct TransitionInfo sModeTransition;
struct PlayerGeometry sMarioGeometry;
struct Camera *gCamera;
s16 unusedFreeRoamWallYaw;
s16 sAvoidYawVel;
s16 sCameraYawAfterDoorCutscene;
/**
 * The current spline that controls the camera's position during the credits.
 */
struct CutsceneSplinePoint sCurCreditsSplinePos[32];

/**
 * The current spline that controls the camera's focus during the credits.
 */
struct CutsceneSplinePoint sCurCreditsSplineFocus[32];

//s16 unusedSplinePitch;
//s16 unusedSplineYaw;

/**
 * The progress (from 0 to 1) through the current spline segment.
 * When it becomes >= 1, 1.0 is subtracted from it and sCutsceneSplineSegment is increased.
 */
q32 sCutsceneSplineSegmentProgressq;

/**
 * The current segment of the CutsceneSplinePoint[] being used.
 */
s16 sCutsceneSplineSegment;
s16 unused8033B6E8;

// Shaky Hand-held Camera effect variables
struct HandheldShakePoint sHandheldShakeSpline[4];
s16 sHandheldShakeMag;
q32 sHandheldShakeTimerq;
q32 sHandheldShakeIncq;
s16 sHandheldShakePitch;
s16 sHandheldShakeYaw;
s16 sHandheldShakeRoll;

/**
 * Controls which object to spawn in the intro and ending cutscenes.
 */
u32 gCutsceneObjSpawn;
/**
 * Controls when an object-based cutscene should end. It's only used in the star spawn cutscenes, but
 * Yoshi also toggles this.
 */
s32 gObjCutsceneDone;

u32 unused8033B30C;
u32 unused8033B310;

/**
 * Determines which R-Trigger mode is selected in the pause menu.
 */
s16 sSelectionFlags;

/**
 * Flags that determine what movements the camera should start / do this frame.
 */
s16 gCameraMovementFlags;
s16 unused8033B316;

/**
 * Flags that change how modes operate and how Lakitu moves.
 * The most commonly used flag is CAM_FLAG_SMOOTH_MOVEMENT, which makes Lakitu fly to the next position,
 * instead of warping.
 */
s16 sStatusFlags;
/**
 * Flags that determine whether the player has already rotated left or right. Used in radial mode to
 * determine whether to rotate all the way, or just to 60 degrees.
 */
s16 s2ndRotateFlags;
s16 unused8033B31A;
/**
 * Flags that control buzzes and sounds that play, mostly for C-button input.
 */
s16 sCameraSoundFlags;
/**
 * Stores what C-Buttons are pressed this frame.
 */
u16 sCButtonsPressed;
/**
 * A copy of gDialogID, the dialog displayed during the cutscene.
 */
s16 sCutsceneDialogID;
/**
 * The currently playing shot in the cutscene.
 */
s16 sCutsceneShot;
/**
 * The current frame of the cutscene shot.
 */
s16 gCutsceneTimer;
s16 unused8033B3E8;
#if defined(VERSION_EU) || defined(VERSION_SH)
s16 unused8033B3E82;
#endif
/**
 * The angle of the direction vector from the area's center to Mario's position.
 */
s16 sAreaYaw;

/**
 * How much sAreaYaw changed when Mario moved.
 */
s16 sAreaYawChange;

/**
 * Lakitu's distance from Mario in C-Down mode
 */
s16 sLakituDist;

/**
 * How much Lakitu looks down in C-Down mode
 */
s16 sLakituPitch;

/**
 * The amount of distance left to zoom out
 */
q32 sZoomAmountq;

s16 sCSideButtonYaw;

/**
 * Sound timer used to space out sounds in behind Mario mode
 */
s16 sBehindMarioSoundTimer;

/**
 * Virtually unused aside being set to 0 and compared with gCameraZoomDist (which is never < 0)
 */
q32 sZeroZoomDistq;

/**
 * The camera's pitch in C-Up mode. Mainly controls Mario's head rotation.
 */
s16 sCUpCameraPitch;
/**
 * The current mode's yaw, which gets added to the camera's yaw.
 */
s16 sModeOffsetYaw;

/**
 * Stores Mario's yaw around the stairs, relative to the camera's position.
 *
 * Used in update_spiral_stairs_camera()
 */
s16 sSpiralStairsYawOffset;

/**
 * The constant offset to 8-direction mode's yaw.
 */
s16 s8DirModeBaseYaw;
/**
 * Player-controlled yaw offset in 8-direction mode, a multiple of 45 degrees.
 */
s16 s8DirModeYawOffset;

/**
 * The distance that the camera will look ahead of Mario in the direction Mario is facing.
 */
q32 sPanDistanceq;

/**
 * When Mario gets in the cannon, it is pointing straight up and rotates down.
 * This is used to make the camera start up and rotate down, like the cannon.
 */
q32 sCannonYOffsetq;
/**
 * These structs are used by the cutscenes. Most of the fields are unused, and some (all?) of the used
 * ones have multiple uses.
 * Check the cutscene_start functions for documentation on the cvars used by a specific cutscene.
 */
struct CutsceneVariable sCutsceneVars[10];
struct ModeTransitionInfo sModeInfo;
/**
 * Offset added to sFixedModeBasePosition when Mario is inside, near the castle lobby entrance
 */
Vec3q sCastleEntranceOffsetq;

/**
 * The index into the current parallel tracking path
 */
u32 sParTrackIndex;

/**
 * The current list of ParallelTrackingPoints used in update_parallel_tracking_camera()
 */
struct ParallelTrackingPoint *sParTrackPath;

/**
 * On the first frame after the camera changes to a different parallel tracking path, this stores the
 * displacement between the camera's calculated new position and its previous positions
 *
 * This transition offset is then used to smoothly interpolate the camera's position between the two
 * paths
 */
struct CameraStoredInfo sParTrackTransOff;

/**
 * The information stored when C-Up is active, used to update Lakitu's rotation when exiting C-Up
 */
struct CameraStoredInfo sCameraStoreCUp;

/**
 * The information stored during cutscenes
 */
struct CameraStoredInfo sCameraStoreCutscene;

// first iteration of data
u32 unused8032CFC0 = 0;
struct Object *gCutsceneFocus = NULL;

u32 unused8032CFC8 = 0;
u32 unused8032CFCC = 0;

/**
 * The information of a second focus camera used by some objects
 */
struct Object *gSecondCameraFocus = NULL;

/**
 * How fast the camera's yaw should approach the next yaw.
 */
s16 sYawSpeed = 0x400;
s32 gCurrLevelArea = 0;
u32 gPrevLevel = 0;

q32 gCameraZoomDistq = q(800);

/**
 * A cutscene that plays when the player interacts with an object
 */
u8 sObjectCutscene = 0;

/**
 * The ID of the cutscene that ended. It's set to 0 if no cutscene ended less than 8 frames ago.
 *
 * It is only used to prevent the same cutscene from playing twice before 8 frames have passed.
 */
u8 gRecentCutscene = 0;

/**
 * A timer that increments for 8 frames when a cutscene ends.
 * When it reaches 8, it sets gRecentCutscene to 0.
 */
u8 sFramesSinceCutsceneEnded = 0;
/**
 * Mario's response to a dialog.
 * 0 = No response yet
 * 1 = Yes
 * 2 = No
 * 3 = Dialog doesn't have a response
 */
u8 sCutsceneDialogResponse = DIALOG_RESPONSE_NONE;
struct PlayerCameraState *sMarioCamState = &gPlayerCameraState[0];
struct PlayerCameraState *sLuigiCamState = &gPlayerCameraState[1];
//u32 unused8032D008 = 0;
Vec3q sFixedModeBasePositionq    = { q(646), q(143), q(-1513) };
//Vec3q sUnusedModeBasePosition_2q = { q(646), q(143), q(-1513) };
//Vec3q sUnusedModeBasePosition_3q = { q(646), q(143), q(-1513) };
//Vec3q sUnusedModeBasePosition_4q = { q(646), q(143), q(-1513) };
//Vec3q sUnusedModeBasePosition_5q = { q(646), q(143), q(-1513) };

s32 update_radial_cameraq(struct Camera *c, Vec3q, Vec3q);
s32 update_outward_radial_cameraq(struct Camera *c, Vec3q, Vec3q);
s32 update_behind_mario_cameraq(struct Camera *c, Vec3q, Vec3q);
s32 update_mario_cameraq(struct Camera *c, Vec3q, Vec3q);
s32 unused_update_mode_5_cameraq(struct Camera *c, Vec3q, Vec3q);
s32 update_c_upq(struct Camera *c, Vec3q, Vec3q);
s32 nop_update_water_cameraq(struct Camera *c, Vec3q, Vec3q);
s32 update_slide_or_0f_cameraq(struct Camera *c, Vec3q, Vec3q);
s32 update_in_cannonq(struct Camera *c, Vec3q, Vec3q);
s32 update_boss_fight_cameraq(struct Camera *c, Vec3q, Vec3q);
s32 update_parallel_tracking_cameraq(struct Camera *c, Vec3q, Vec3q);
s32 update_fixed_cameraq(struct Camera *c, Vec3q, Vec3q);
s32 update_8_directions_cameraq(struct Camera *c, Vec3q, Vec3q);
s32 update_slide_or_0f_cameraq(struct Camera *c, Vec3q, Vec3q);
s32 update_spiral_stairs_cameraq(struct Camera *c, Vec3q, Vec3q);

typedef s32 (*CameraTransition)(struct Camera *c, Vec3q, Vec3q);
CameraTransition sModeTransitions[] = {
    NULL,
    update_radial_cameraq,
    update_outward_radial_cameraq,
    update_behind_mario_cameraq,
    update_mario_cameraq,
    NULL, //unused_update_mode_5_cameraq,
    update_c_upq,
    update_mario_cameraq,
    nop_update_water_cameraq,
    update_slide_or_0f_cameraq,
    update_in_cannonq,
    update_boss_fight_cameraq,
    update_parallel_tracking_cameraq,
    update_fixed_cameraq,
    update_8_directions_cameraq,
    update_slide_or_0f_cameraq,
    update_mario_cameraq,
    update_spiral_stairs_cameraq
};

// Move these two tables to another include file?
extern u8 sDanceCutsceneIndexTable[][4];
extern u8 sZoomOutAreaMasks[];

/**
 * Starts a camera shake triggered by an interaction
 */
void set_camera_shake_from_hit(s16 shake) {
    switch (shake) {
        // Makes the camera stop for a bit
        case SHAKE_ATTACK:
            gLakituState.focHSpeedq = 0;
            gLakituState.posHSpeedq = 0;
            break;

        case SHAKE_FALL_DAMAGE:
            set_camera_pitch_shake(0x60, 0x3, 0x8000);
            set_camera_roll_shake(0x60, 0x3, 0x8000);
            break;

        case SHAKE_GROUND_POUND:
            set_camera_pitch_shake(0x60, 0xC, 0x8000);
            break;

        case SHAKE_SMALL_DAMAGE:
            if (sMarioCamState->action & (ACT_FLAG_SWIMMING | ACT_FLAG_METAL_WATER)) {
                set_camera_yaw_shake(0x200, 0x10, 0x1000);
                set_camera_roll_shake(0x400, 0x20, 0x1000);
                set_fov_shake(0x100, 0x30, 0x8000);
            } else {
                set_camera_yaw_shake(0x80, 0x8, 0x4000);
                set_camera_roll_shake(0x80, 0x8, 0x4000);
                set_fov_shake(0x100, 0x30, 0x8000);
            }

            gLakituState.focHSpeedq = 0;
            gLakituState.posHSpeedq = 0;
            break;

        case SHAKE_MED_DAMAGE:
            if (sMarioCamState->action & (ACT_FLAG_SWIMMING | ACT_FLAG_METAL_WATER)) {
                set_camera_yaw_shake(0x400, 0x20, 0x1000);
                set_camera_roll_shake(0x600, 0x30, 0x1000);
                set_fov_shake(0x180, 0x40, 0x8000);
            } else {
                set_camera_yaw_shake(0x100, 0x10, 0x4000);
                set_camera_roll_shake(0x100, 0x10, 0x4000);
                set_fov_shake(0x180, 0x40, 0x8000);
            }

            gLakituState.focHSpeedq = 0;
            gLakituState.posHSpeedq = 0;
            break;

        case SHAKE_LARGE_DAMAGE:
            if (sMarioCamState->action & (ACT_FLAG_SWIMMING | ACT_FLAG_METAL_WATER)) {
                set_camera_yaw_shake(0x600, 0x30, 0x1000);
                set_camera_roll_shake(0x800, 0x40, 0x1000);
                set_fov_shake(0x200, 0x50, 0x8000);
            } else {
                set_camera_yaw_shake(0x180, 0x20, 0x4000);
                set_camera_roll_shake(0x200, 0x20, 0x4000);
                set_fov_shake(0x200, 0x50, 0x8000);
            }

            gLakituState.focHSpeedq = 0;
            gLakituState.posHSpeedq = 0;
            break;

        case SHAKE_HIT_FROM_BELOW:
            gLakituState.focHSpeedq = 0.07;
            gLakituState.posHSpeedq = 0.07;
            break;

        case SHAKE_SHOCK:
            set_camera_pitch_shake(random_u16() % 64, 0x8, 0x8000);
            set_camera_yaw_shake(random_u16() % 64, 0x8, 0x8000);
            break;
    }
}

/**
 * Start a shake from the environment
 */
void set_environmental_camera_shake(s16 shake) {
    switch (shake) {
        case SHAKE_ENV_EXPLOSION:
            set_camera_pitch_shake(0x60, 0x8, 0x4000);
            break;

        case SHAKE_ENV_BOWSER_THROW_BOUNCE:
            set_camera_pitch_shake(0xC0, 0x8, 0x4000);
            break;

        case SHAKE_ENV_BOWSER_JUMP:
            set_camera_pitch_shake(0x100, 0x8, 0x3000);
            break;

        case SHAKE_ENV_UNUSED_6:
            set_camera_roll_shake(0x80, 0x10, 0x3000);
            break;

        case SHAKE_ENV_UNUSED_7:
            set_camera_pitch_shake(0x20, 0x8, 0x8000);
            break;

        case SHAKE_ENV_PYRAMID_EXPLODE:
            set_camera_pitch_shake(0x40, 0x8, 0x8000);
            break;

        case SHAKE_ENV_JRB_SHIP_DRAIN:
            set_camera_pitch_shake(0x20, 0x8, 0x8000);
            set_camera_roll_shake(0x400, 0x10, 0x100);
            break;

        case SHAKE_ENV_FALLING_BITS_PLAT:
            set_camera_pitch_shake(0x40, 0x2, 0x8000);
            break;

        case SHAKE_ENV_UNUSED_5:
            set_camera_yaw_shake(-0x200, 0x80, 0x200);
            break;
    }
}

/**
 * Starts a camera shake, but scales the amplitude by the point's distance from the camera
 */
void set_camera_shake_from_pointq(s16 shake, q32 posXq, q32 posYq, q32 posZq) {
    switch (shake) {
        case SHAKE_POS_BOWLING_BALL:
            set_pitch_shake_from_pointq(0x28, 0x8, 0x4000, q(2000), posXq, posYq, posZq);
            break;

        case SHAKE_POS_SMALL:
            set_pitch_shake_from_pointq(0x80, 0x8, 0x4000, q(4000), posXq, posYq, posZq);
            set_fov_shake_from_point_presetq(SHAKE_FOV_SMALL, posXq, posYq, posZq);
            break;

        case SHAKE_POS_MEDIUM:
            set_pitch_shake_from_pointq(0xC0, 0x8, 0x4000, q(6000), posXq, posYq, posZq);
            set_fov_shake_from_point_presetq(SHAKE_FOV_MEDIUM, posXq, posYq, posZq);
            break;

        case SHAKE_POS_LARGE:
            set_pitch_shake_from_pointq(0x100, 0x8, 0x3000, q(8000), posXq, posYq, posZq);
            set_fov_shake_from_point_presetq(SHAKE_FOV_LARGE, posXq, posYq, posZq);
            break;
    }
}

/**
 * Start a camera shake from an environmental source, but only shake the camera's pitch.
 */
void unused_set_camera_pitch_shake_env(s16 shake) {
    switch (shake) {
        case SHAKE_ENV_EXPLOSION:
            set_camera_pitch_shake(0x60, 0x8, 0x4000);
            break;

        case SHAKE_ENV_BOWSER_THROW_BOUNCE:
            set_camera_pitch_shake(0xC0, 0x8, 0x4000);
            break;

        case SHAKE_ENV_BOWSER_JUMP:
            set_camera_pitch_shake(0x100, 0x8, 0x3000);
            break;
    }
}

/**
 * Calculates Mario's distance to the floor, or the water level if it is above the floor. Then:
 * `posOff` is set to the distance multiplied by posMul and bounded to [-posBound, posBound]
 * `focOff` is set to the distance multiplied by focMul and bounded to [-focBound, focBound]
 *
 * Notes:
 *      posMul is always 1.0f, focMul is always 0.9f
 *      both ranges are always 200.f
 *          Since focMul is 0.9, `focOff` is closer to the floor than `posOff`
 *      posOff and focOff are sometimes the same address, which just ignores the pos calculation
 *! Doesn't return anything, but required to match on -O2
 */
BAD_RETURN(q32) calc_y_to_curr_floorq(q32 *posOffq, q32 posMulq, q32 posBoundq, q32 *focOffq, q32 focMulq, q32 focBoundq) {
    q32 floorHeightq = q(sMarioGeometry.currFloorHeight);
    q32 waterHeightq;

    if (!(sMarioCamState->action & ACT_FLAG_METAL_WATER)) {
        //! @bug this should use sMarioGeometry.waterHeight
        if (floorHeightq < (waterHeightq = find_water_levelq(sMarioCamState->posq[0], sMarioCamState->posq[2]))) {
            floorHeightq = waterHeightq;
        }
    }

    if (sMarioCamState->action & ACT_FLAG_ON_POLE) {
        if (sMarioGeometry.currFloorHeight >= FFIELD(gMarioStates[0].usedObj, oPosY) && sMarioCamState->posq[1]
                   < qmul(q(0.7), q(gMarioStates[0].usedObj->hitboxHeight_s16)) + QFIELD(gMarioStates[0].usedObj, oPosY)) {
            posBoundq = q(1200);
        }
    }

    *posOffq = qmul(floorHeightq - sMarioCamState->posq[1], posMulq);

    if (*posOffq > posBoundq) {
        *posOffq = posBoundq;
    }

    if (*posOffq < -posBoundq) {
        *posOffq = -posBoundq;
    }

    *focOffq = qmul(floorHeightq - sMarioCamState->posq[1], focMulq);

    if (*focOffq > focBoundq) {
        *focOffq = focBoundq;
    }

    if (*focOffq < -focBoundq) {
        *focOffq = -focBoundq;
    }
}

void focus_on_marioq(Vec3q focusq, Vec3q posq, q32 posYOffq, q32 focYOffq, q32 distq, s16 pitch, s16 yaw) {
    Vec3q marioPosq;

    marioPosq[0] = sMarioCamState->posq[0];
    marioPosq[1] = sMarioCamState->posq[1] + posYOffq;
    marioPosq[2] = sMarioCamState->posq[2];

    vec3q_set_dist_and_angle(marioPosq, posq, distq, pitch + sLakituPitch, yaw);

    focusq[0] = sMarioCamState->posq[0];
    focusq[1] = sMarioCamState->posq[1] + focYOffq;
    focusq[2] = sMarioCamState->posq[2];
}

/**
 * Set the camera's y coordinate to goalHeight, respecting floors and ceilings in the way
 */
void set_camera_heightq(struct Camera *c, q32 goalHeightq) {
    struct Surface *surface;
    q32 marioFloorHeightq;
    q32 marioCeilHeightq;
    q32 camFloorHeightq;
    q32 baseOffq = q(125);
    q32 camCeilHeightq = find_ceilq(c->posq[0], gLakituState.goalPosq[1] - q(50), c->posq[2], &surface);

    if (sMarioCamState->action & ACT_FLAG_HANGING) {
        marioCeilHeightq = q(sMarioGeometry.currCeilHeight);
        marioFloorHeightq = q(sMarioGeometry.currFloorHeight);

        if (marioFloorHeightq < marioCeilHeightq - q(400)) {
            marioFloorHeightq = marioCeilHeightq - q(400);
        }

        goalHeightq = marioFloorHeightq + qmul(marioCeilHeightq - marioFloorHeightq, q(0.4));

        if (sMarioCamState->posq[1] - q(400) > goalHeightq) {
            goalHeightq = sMarioCamState->posq[1] - q(400);
        }

        approach_camera_heightq(c, goalHeightq, q(5));
    } else {
        camFloorHeightq = find_floorq(c->posq[0], c->posq[1] + q(100), c->posq[2], &surface) + baseOffq;
        marioFloorHeightq = baseOffq + q(sMarioGeometry.currFloorHeight);

        if (camFloorHeightq < marioFloorHeightq) {
            camFloorHeightq = marioFloorHeightq;
        }
        if (goalHeightq < camFloorHeightq) {
            goalHeightq = camFloorHeightq;
            c->posq[1] = goalHeightq;
        }
        // Warp camera to goalHeight if further than 1000 and Mario is stuck in the ground
        if (sMarioCamState->action == ACT_BUTT_STUCK_IN_GROUND ||
            sMarioCamState->action == ACT_HEAD_STUCK_IN_GROUND ||
            sMarioCamState->action == ACT_FEET_STUCK_IN_GROUND) {
            if (ABSi(c->posq[1] - goalHeightq) > q(1000)) {
                c->posq[1] = goalHeightq;
            }
        }
        approach_camera_heightq(c, goalHeightq, q(20));
        if (camCeilHeightq != q(CELL_HEIGHT_LIMIT)) {
            camCeilHeightq -= baseOffq;
            if ((c->posq[1] > camCeilHeightq && q(sMarioGeometry.currFloorHeight) + baseOffq < camCeilHeightq)
                || (sMarioGeometry.currCeilHeight != CELL_HEIGHT_LIMIT
                    && q(sMarioGeometry.currCeilHeight) > camCeilHeightq && c->posq[1] > camCeilHeightq)) {
                c->posq[1] = camCeilHeightq;
            }
        }
    }
}

/**
 * Pitch the camera down when the camera is facing down a slope
 */
s16 look_down_slopes(s16 camYaw) {
    struct Surface *floor;
    q32 floorDYq;
    // Default pitch
    s16 pitch = 0x05B0;
    // x and z offsets towards the camera
    q32 xOffq = sMarioCamState->posq[0] + sinqs(camYaw) * 40;
    q32 zOffq = sMarioCamState->posq[2] + cosqs(camYaw) * 40;

    floorDYq = find_floorq(xOffq, sMarioCamState->posq[1], zOffq, &floor) - sMarioCamState->posq[1];

    if (floor != NULL) {
        if (floor->type != SURFACE_WALL_MISC && floorDYq > 0) {
            if (floor->compressed_normal.z == 0 && floorDYq < q(100)) {
                pitch = 0x05B0;
            } else {
                // Add the slope's angle of declination to the pitch
                pitch += atan2sq(q(40), floorDYq);
            }
        }
    }

    return pitch;
}

/**
 * Look ahead to the left or right in the direction the player is facing
 * The calculation for pan[0] could be simplified to:
 *      yaw = -yaw;
 *      pan[0] = sins(sMarioCamState->faceAngle[1] + yaw) * sins(0xC00) * dist;
 * Perhaps, early in development, the pan used to be calculated for both the x and z directions
 *
 * Since this function only affects the camera's focus, Mario's movement direction isn't affected.
 */
void pan_ahead_of_player(struct Camera *c) {
    q32 distq;
    s16 pitch;
    s16 yaw;
    Vec3q panq = { 0, 0, 0 };

    // Get distance and angle from camera to Mario.
    vec3q_get_dist_and_angle(c->posq, sMarioCamState->posq, &distq, &pitch, &yaw);

    // The camera will pan ahead up to about 30% of the camera's distance to Mario.
    panq[2] = qmul(sinqs(0xC00), distq);

    rotate_in_xzq(panq, panq, sMarioCamState->faceAngle[1]);
    // rotate in the opposite direction
    rotate_in_xzq(panq, panq, -yaw);
    // Only pan left or right
    panq[2] = 0;

    // If Mario is long jumping, or on a flag pole (but not at the top), then pan in the opposite direction
    if (sMarioCamState->action == ACT_LONG_JUMP ||
       (sMarioCamState->action != ACT_TOP_OF_POLE && (sMarioCamState->action & ACT_FLAG_ON_POLE))) {
        panq[0] = -panq[0];
    }

    // Slowly make the actual pan, sPanDistance, approach the calculated pan
    // If Mario is sleeping, then don't pan
    if (sStatusFlags & CAM_FLAG_SLEEPING) {
        approach_q32_asymptotic_bool(&sPanDistanceq, 0, q(0.025));
    } else {
        approach_q32_asymptotic_bool(&sPanDistanceq, panq[0], q(0.025));
    }

    // Now apply the pan. It's a dir vector to the left or right, rotated by the camera's yaw to Mario
    panq[0] = sPanDistanceq;
    rotate_in_xzq(panq, panq, yaw);
    vec3q_add(c->focusq, panq);
}

s16 find_in_bounds_yaw_wdw_bob_thiq(Vec3q posq, Vec3q originq, s16 yaw) {
    switch (gCurrLevelArea) {
        case AREA_WDW_MAIN:
            yaw = clamp_positions_and_find_yawq(posq, originq, q(4508), q(-3739), q(4508), q(-3739));
            break;
        case AREA_BOB:
            yaw = clamp_positions_and_find_yawq(posq, originq, q(8000), q(-8000), q(7050), q(-8000));
            break;
        case AREA_THI_HUGE:
            yaw = clamp_positions_and_find_yawq(posq, originq, q(8192), q(-8192), q(8192), q(-8192));
            break;
        case AREA_THI_TINY:
            yaw = clamp_positions_and_find_yawq(posq, originq, q(2458), q(-2458), q(2458), q(-2458));
            break;
    }
    return yaw;
}

/**
 * Rotates the camera around the area's center point.
 */
s32 update_radial_cameraq(struct Camera *c, Vec3q focusq, Vec3q posq) {
    q32 cenDistXq = sMarioCamState->posq[0] - c->areaCenXq;
    q32 cenDistZq = sMarioCamState->posq[2] - c->areaCenZq;
    s16 camYaw = atan2sq(cenDistZq, cenDistXq) + sModeOffsetYaw;
    s16 pitch = look_down_slopes(camYaw);
    q32 posYq;
    q32 focusYq;
    q32 yOffq = q(125);
    q32 baseDistq = q(1000);

    sAreaYaw = camYaw - sModeOffsetYaw;
    calc_y_to_curr_floorq(&posYq, QONE, q(200), &focusYq, q(0.9), q(200));
    focus_on_marioq(focusq, posq, posYq + yOffq, focusYq + yOffq, q(sLakituDist) + baseDistq, pitch, camYaw);
    camYaw = find_in_bounds_yaw_wdw_bob_thiq(posq, focusq, camYaw);

    return camYaw;
}

/**
 * Update the camera during 8 directional mode
 */
s32 update_8_directions_cameraq(struct Camera *c, Vec3q focusq, Vec3q posq) {
    s16 camYaw = s8DirModeBaseYaw + s8DirModeYawOffset;
    s16 pitch = look_down_slopes(camYaw);
    q32 posYq;
    q32 focusYq;
    q32 yOffq = q(125);
    q32 baseDistq = q(1000);

    sAreaYaw = camYaw;
    calc_y_to_curr_floorq(&posYq, QONE, q(200), &focusYq, q(0.9), q(200));
    focus_on_marioq(focusq, posq, posYq + yOffq, focusYq + yOffq, q(sLakituDist) + baseDistq, pitch, camYaw);
    pan_ahead_of_player(c);
    if (gCurrLevelArea == AREA_DDD_SUB) {
        camYaw = clamp_positions_and_find_yawq(posq, focusq, q(6839), q(995), q(5994), q(-3945));
    }

    return camYaw;
}

/**
 * Moves the camera for the radial and outward radial camera modes.
 *
 * If sModeOffsetYaw is 0, the camera points directly at the area center point.
 */
void radial_camera_move(struct Camera *c) {
    s16 maxAreaYaw = DEGREES(60);
    s16 minAreaYaw = DEGREES(-60);
    s16 rotateSpeed = 0x1000;
    s16 avoidYaw;
    s32 avoidStatus;
    q32 areaDistXq = sMarioCamState->posq[0] - c->areaCenXq;
    q32 areaDistZq = sMarioCamState->posq[2] - c->areaCenZq;

    // How much the camera's yaw changed
    s16 yawOffset = calculate_yawq(sMarioCamState->posq, c->posq) - atan2sq(areaDistZq, areaDistXq);

    if (yawOffset > maxAreaYaw) {
        yawOffset = maxAreaYaw;
    }
    if (yawOffset < minAreaYaw) {
        yawOffset = minAreaYaw;
    }

    // Check if Mario stepped on a surface that rotates the camera. For example, when Mario enters the
    // gate in BoB, the camera turns right to face up the hill path
    if (!(gCameraMovementFlags & CAM_MOVE_ROTATE)) {
        if (sMarioGeometry.currFloorType == SURFACE_CAMERA_MIDDLE
            && sMarioGeometry.prevFloorType != SURFACE_CAMERA_MIDDLE) {
            gCameraMovementFlags |= (CAM_MOVE_RETURN_TO_MIDDLE | CAM_MOVE_ENTERED_ROTATE_SURFACE);
        }
        if (sMarioGeometry.currFloorType == SURFACE_CAMERA_ROTATE_RIGHT
            && sMarioGeometry.prevFloorType != SURFACE_CAMERA_ROTATE_RIGHT) {
            gCameraMovementFlags |= (CAM_MOVE_ROTATE_RIGHT | CAM_MOVE_ENTERED_ROTATE_SURFACE);
        }
        if (sMarioGeometry.currFloorType == SURFACE_CAMERA_ROTATE_LEFT
            && sMarioGeometry.prevFloorType != SURFACE_CAMERA_ROTATE_LEFT) {
            gCameraMovementFlags |= (CAM_MOVE_ROTATE_LEFT | CAM_MOVE_ENTERED_ROTATE_SURFACE);
        }
    }

    if (gCameraMovementFlags & CAM_MOVE_ENTERED_ROTATE_SURFACE) {
        rotateSpeed = 0x200;
    }

    if (c->mode == CAMERA_MODE_OUTWARD_RADIAL) {
        areaDistXq = -areaDistXq;
        areaDistZq = -areaDistZq;
    }

    // Avoid obstructing walls
    avoidStatus = rotate_camera_around_wallsq(c, c->posq, &avoidYaw, 0x400);
    if (avoidStatus == 3) {
        if (avoidYaw - atan2sq(areaDistZq, areaDistXq) + DEGREES(90) < 0) {
            avoidYaw += DEGREES(180);
        }

        // We want to change sModeOffsetYaw so that the player is no longer obstructed by the wall.
        // So, we make avoidYaw relative to the yaw around the area center
        avoidYaw -= atan2sq(areaDistZq, areaDistXq);

        // Bound avoid yaw to radial mode constraints
        if (avoidYaw > DEGREES(105)) {
            avoidYaw = DEGREES(105);
        }
        if (avoidYaw < DEGREES(-105)) {
            avoidYaw = DEGREES(-105);
        }
    }

    if (gCameraMovementFlags & CAM_MOVE_RETURN_TO_MIDDLE) {
        if (camera_approach_s16_symmetric_bool(&sModeOffsetYaw, 0, rotateSpeed) == 0) {
            gCameraMovementFlags &= ~CAM_MOVE_RETURN_TO_MIDDLE;
        }
    } else {
        // Prevent the player from rotating into obstructing walls
        if ((gCameraMovementFlags & CAM_MOVE_ROTATE_RIGHT) && avoidStatus == 3
            && avoidYaw + 0x10 < sModeOffsetYaw) {
            sModeOffsetYaw = avoidYaw;
            gCameraMovementFlags &= ~(CAM_MOVE_ROTATE_RIGHT | CAM_MOVE_ENTERED_ROTATE_SURFACE);
        }
        if ((gCameraMovementFlags & CAM_MOVE_ROTATE_LEFT) && avoidStatus == 3
            && avoidYaw - 0x10 > sModeOffsetYaw) {
            sModeOffsetYaw = avoidYaw;
            gCameraMovementFlags &= ~(CAM_MOVE_ROTATE_LEFT | CAM_MOVE_ENTERED_ROTATE_SURFACE);
        }

        // If it's the first time rotating, just rotate to +-60 degrees
        if (!(s2ndRotateFlags & CAM_MOVE_ROTATE_RIGHT) && (gCameraMovementFlags & CAM_MOVE_ROTATE_RIGHT)
            && camera_approach_s16_symmetric_bool(&sModeOffsetYaw, maxAreaYaw, rotateSpeed) == 0) {
            gCameraMovementFlags &= ~(CAM_MOVE_ROTATE_RIGHT | CAM_MOVE_ENTERED_ROTATE_SURFACE);
        }
        if (!(s2ndRotateFlags & CAM_MOVE_ROTATE_LEFT) && (gCameraMovementFlags & CAM_MOVE_ROTATE_LEFT)
            && camera_approach_s16_symmetric_bool(&sModeOffsetYaw, minAreaYaw, rotateSpeed) == 0) {
            gCameraMovementFlags &= ~(CAM_MOVE_ROTATE_LEFT | CAM_MOVE_ENTERED_ROTATE_SURFACE);
        }

        // If it's the second time rotating, rotate all the way to +-105 degrees.
        if ((s2ndRotateFlags & CAM_MOVE_ROTATE_RIGHT) && (gCameraMovementFlags & CAM_MOVE_ROTATE_RIGHT)
            && camera_approach_s16_symmetric_bool(&sModeOffsetYaw, DEGREES(105), rotateSpeed) == 0) {
            gCameraMovementFlags &= ~(CAM_MOVE_ROTATE_RIGHT | CAM_MOVE_ENTERED_ROTATE_SURFACE);
            s2ndRotateFlags &= ~CAM_MOVE_ROTATE_RIGHT;
        }
        if ((s2ndRotateFlags & CAM_MOVE_ROTATE_LEFT) && (gCameraMovementFlags & CAM_MOVE_ROTATE_LEFT)
            && camera_approach_s16_symmetric_bool(&sModeOffsetYaw, DEGREES(-105), rotateSpeed) == 0) {
            gCameraMovementFlags &= ~(CAM_MOVE_ROTATE_LEFT | CAM_MOVE_ENTERED_ROTATE_SURFACE);
            s2ndRotateFlags &= ~CAM_MOVE_ROTATE_LEFT;
        }
    }
    if (!(gCameraMovementFlags & CAM_MOVE_ROTATE)) {
        // If not rotating, rotate away from walls obscuring Mario from view
        if (avoidStatus == 3) {
            approach_s16_asymptotic_bool(&sModeOffsetYaw, avoidYaw, 10);
        } else {
            if (c->mode == CAMERA_MODE_RADIAL) {
                // sModeOffsetYaw only updates when Mario is moving
                rotateSpeed = gMarioStates[0].forwardVel * (128 / 32);
                camera_approach_s16_symmetric_bool(&sModeOffsetYaw, yawOffset, rotateSpeed);
            }
            if (c->mode == CAMERA_MODE_OUTWARD_RADIAL) {
                sModeOffsetYaw = offset_yaw_outward_radial(c, atan2sq(areaDistZq, areaDistXq));
            }
        }
    }

    // Bound sModeOffsetYaw within (-120, 120) degrees
    if (sModeOffsetYaw > 0x5554) {
        sModeOffsetYaw = 0x5554;
    }
    if (sModeOffsetYaw < -0x5554) {
        sModeOffsetYaw = -0x5554;
    }
}

/**
 * Moves Lakitu from zoomed in to zoomed out and vice versa.
 * When C-Down mode is not active, sLakituDist and sLakituPitch decrease to 0.
 */
void lakitu_zoomq(q32 rangeDistq, s16 rangePitch) {
    if (sLakituDist < 0) {
        if ((sLakituDist += 30) > 0) {
            sLakituDist = 0;
        }
    } else if (qtrunc(rangeDistq) < sLakituDist) {
        if ((sLakituDist -= 30) < qtrunc(rangeDistq)) {
            sLakituDist = qtrunc(rangeDistq);
        }
    } else if (gCameraMovementFlags & CAM_MOVE_ZOOMED_OUT) {
        if ((sLakituDist += 30) > qtrunc(rangeDistq)) {
            sLakituDist = qtrunc(rangeDistq);
        }
    } else {
        if ((sLakituDist -= 30) < 0) {
            sLakituDist = 0;
        }
    }

    if (gCurrLevelArea == AREA_SSL_PYRAMID && gCamera->mode == CAMERA_MODE_OUTWARD_RADIAL) {
        rangePitch /= 2;
    }

    if (gCameraMovementFlags & CAM_MOVE_ZOOMED_OUT) {
        if ((sLakituPitch += rangePitch / 13) > rangePitch) {
            sLakituPitch = rangePitch;
        }
    } else {
        if ((sLakituPitch -= rangePitch / 13) < 0) {
            sLakituPitch = 0;
        }
    }
}

/**
 * Makes Lakitu cam's yaw match the angle turned towards in C-Up mode, and makes Lakitu slowly fly back
 * to the distance he was at before C-Up
 */
void update_yaw_and_dist_from_c_up(UNUSED struct Camera *c) {
    sModeOffsetYaw = sModeInfo.transitionStart.yaw - sAreaYaw;
    sLakituDist = qtrunc(sModeInfo.transitionStart.distq) - 1000;
    // No longer in C-Up
    gCameraMovementFlags &= ~CAM_MOVING_INTO_MODE;
}

/**
 * Handles input and updates for the radial camera mode
 */
void mode_radial_camera(struct Camera *c) {
    Vec3q posq;
    s16 oldAreaYaw = sAreaYaw;

    if (gCameraMovementFlags & CAM_MOVING_INTO_MODE) {
        update_yaw_and_dist_from_c_up(c);
    }

    radial_camera_input(c);
    radial_camera_move(c);

    if (c->mode == CAMERA_MODE_RADIAL) {
        lakitu_zoomq(q(400), 0x900);
    }
    c->nextYaw = update_radial_cameraq(c, c->focusq, posq);
    c->posq[0] = posq[0];
    c->posq[2] = posq[2];
    sAreaYawChange = sAreaYaw - oldAreaYaw;
    if (sMarioCamState->action == ACT_RIDING_HOOT) {
        posq[1] += q(500);
    }
    set_camera_heightq(c, posq[1]);
    pan_ahead_of_player(c);
}

/**
 * A mode that only has 8 camera angles, 45 degrees apart
 */
void mode_8_directions_camera(struct Camera *c) {
    Vec3q posq;
    s16 oldAreaYaw = sAreaYaw;

    radial_camera_input(c);

    if (gPlayer1Controller->buttonPressed & R_CBUTTONS) {
        s8DirModeYawOffset += DEGREES(45);
        play_sound_cbutton_side();
    }
    if (gPlayer1Controller->buttonPressed & L_CBUTTONS) {
        s8DirModeYawOffset -= DEGREES(45);
        play_sound_cbutton_side();
    }

    lakitu_zoomq(q(400), 0x900);
    c->nextYaw = update_8_directions_cameraq(c, c->focusq, posq);
    c->posq[0] = posq[0];
    c->posq[2] = posq[2];
    sAreaYawChange = sAreaYaw - oldAreaYaw;
    set_camera_heightq(c, posq[1]);
}

/**
 * Updates the camera in outward radial mode.
 * sModeOffsetYaw is calculated in radial_camera_move, which calls offset_yaw_outward_radial
 */
s32 update_outward_radial_cameraq(struct Camera *c, Vec3q focusq, Vec3q posq) {
    q32 xDistFocToMarioq = sMarioCamState->posq[0] - c->areaCenXq;
    q32 zDistFocToMarioq = sMarioCamState->posq[2] - c->areaCenZq;
    s16 camYaw = atan2sq(zDistFocToMarioq, xDistFocToMarioq) + sModeOffsetYaw + DEGREES(180);
    s16 pitch = look_down_slopes(camYaw);
    q32 baseDistq = q(1000);
    // A base offset of 125.f is ~= Mario's eye height
    q32 yOffq = q(125);
    q32 posYq;
    q32 focusYq;

    sAreaYaw = camYaw - sModeOffsetYaw - DEGREES(180);
    calc_y_to_curr_floorq(&posYq, QONE, q(200), &focusYq, q(0.9), q(200));
    focus_on_marioq(focusq, posq, posYq + yOffq, focusYq + yOffq, q(sLakituDist) + baseDistq, pitch, camYaw);

    return camYaw;
}

/**
 * Input and updates for the outward radial mode.
 */
void mode_outward_radial_camera(struct Camera *c) {
    Vec3q posq;
    s16 oldAreaYaw = sAreaYaw;

    if (gCameraMovementFlags & CAM_MOVING_INTO_MODE) {
        update_yaw_and_dist_from_c_up(c);
    }
    radial_camera_input(c);
    radial_camera_move(c);
    lakitu_zoomq(q(400), 0x900);
    c->nextYaw = update_outward_radial_cameraq(c, c->focusq, posq);
    c->posq[0] = posq[0];
    c->posq[2] = posq[2];
    sAreaYawChange = sAreaYaw - oldAreaYaw;
    if (sMarioCamState->action == ACT_RIDING_HOOT) {
        posq[1] += q(500);
    }
    set_camera_heightq(c, posq[1]);
    pan_ahead_of_player(c);
}

/**
 * Move the camera in parallel tracking mode
 *
 * Uses the line between the next two points in sParTrackPath
 * The camera can move forward/back and side to side, but it will face perpendicular to that line
 *
 * Although, annoyingly, it's not truly parallel, the function returns the yaw from the camera to Mario,
 * so Mario will run slightly towards the camera.
 */
s32 update_parallel_tracking_cameraq(struct Camera *c, Vec3q focusq, Vec3q posq) {
    Vec3q pathq[2];
    Vec3q parMidPointq;
    Vec3q marioOffsetq;
    Vec3q camOffsetq;
    /// Adjusts the focus to look where Mario is facing. Unused since marioOffset is copied to focus
    Vec3q focOffsetq;
    s16 pathPitch;
    s16 pathYaw;
    q32 distThreshq;
    q32 zoomq;
    q32 camParDistq;
    q32 pathLengthq;
    q32 parScaleq = QONE / 2;
    q32 marioFloorDistq;
    Vec3q marioPosq;
    Vec3s pathAngle;
    // Variables for changing to the next/prev path in the list
    Vec3q oldPosq;
    Vec3q prevPathPosq;
    Vec3q nextPathPosq;
    q32 distToNextq;
    q32 distToPrevq;
    s16 prevPitch;
    s16 nextPitch;
    s16 prevYaw;
    s16 nextYaw;

    // Store camera pos, for changing between paths
    vec3q_copy(oldPosq, posq);

    vec3q_copy(pathq[0], sParTrackPath[sParTrackIndex].posq);
    vec3q_copy(pathq[1], sParTrackPath[sParTrackIndex + 1].posq);

    distThreshq = sParTrackPath[sParTrackIndex].distThreshq;
    zoomq = sParTrackPath[sParTrackIndex].zoomq;
    calc_y_to_curr_floorq(&marioFloorDistq, QONE, q(200), &marioFloorDistq, q(0.9), q(200));

    marioPosq[0] = sMarioCamState->posq[0];
    // Mario's y pos + ~Mario's height + Mario's height above the floor
    marioPosq[1] = sMarioCamState->posq[1] + q(150) + marioFloorDistq;
    marioPosq[2] = sMarioCamState->posq[2];

    // Calculate middle of the path (parScale is 0.5f)
    parMidPointq[0] = pathq[0][0] + qmul(pathq[1][0] - pathq[0][0], parScaleq);
    parMidPointq[1] = pathq[0][1] + qmul(pathq[1][1] - pathq[0][1], parScaleq);
    parMidPointq[2] = pathq[0][2] + qmul(pathq[1][2] - pathq[0][2], parScaleq);

    // Get direction of path
    vec3q_get_dist_and_angle(pathq[0], pathq[1], &pathLengthq, &pathPitch, &pathYaw);

    marioOffsetq[0] = marioPosq[0] - parMidPointq[0];
    marioOffsetq[1] = marioPosq[1] - parMidPointq[1];
    marioOffsetq[2] = marioPosq[2] - parMidPointq[2];

    // Make marioOffset point from the midpoint -> the start of the path
    // Rotating by -yaw then -pitch moves the hor dist from the midpoint into marioOffset's z coordinate
    // marioOffset[0] = the (perpendicular) horizontal distance from the path
    // marioOffset[1] = the vertical distance from the path
    // marioOffset[2] = the (parallel) horizontal distance from the path's midpoint
    pathYaw = -pathYaw;
    rotate_in_xzq(marioOffsetq, marioOffsetq, pathYaw);
    pathYaw = -pathYaw;
    pathPitch = -pathPitch;
    rotate_in_yzq(marioOffsetq, marioOffsetq, pathPitch);
    pathPitch = -pathPitch;
    vec3q_copy(focOffsetq, marioOffsetq);

    // OK
    focOffsetq[0] = 0;
    focOffsetq[1] = 0;

    // Repeat above calcs with camOffset
    camOffsetq[0] = posq[0] - parMidPointq[0];
    camOffsetq[1] = posq[1] - parMidPointq[1];
    camOffsetq[2] = posq[2] - parMidPointq[2];
    pathYaw = -pathYaw;
    rotate_in_xzq(camOffsetq, camOffsetq, pathYaw);
    pathYaw = -pathYaw;
    pathPitch = -pathPitch;
    rotate_in_yzq(camOffsetq, camOffsetq, pathPitch);
    pathPitch = -pathPitch;

    // If Mario is distThresh units away from the camera along the path, move the camera
    //! When distThresh != 0, it causes Mario to move slightly towards the camera when running sideways
    //! Set each ParallelTrackingPoint's distThresh to 0 to make Mario truly run parallel to the path
    if (marioOffsetq[2] > camOffsetq[2]) {
        if (marioOffsetq[2] - camOffsetq[2] > distThreshq) {
            camOffsetq[2] = marioOffsetq[2] - distThreshq;
        }
    } else {
        if (marioOffsetq[2] - camOffsetq[2] < -distThreshq) {
            camOffsetq[2] = marioOffsetq[2] + distThreshq;
        }
    }

    // If zoom != 0.0, the camera will move zoom% closer to Mario
    marioOffsetq[0] = qmul(-marioOffsetq[0], zoomq);
    marioOffsetq[1] = qmul(marioOffsetq[1], zoomq);
    marioOffsetq[2] = camOffsetq[2];

    //! Does nothing because focOffset[0] is always 0
    //focOffset[0] *= 0.3f;
    //! Does nothing because focOffset[1] is always 0
    //focOffset[1] *= 0.3f;

    pathAngle[0] = pathPitch;
    //pathAngle[1] = pathYaw; //! No effect

    // make marioOffset[2] == distance from the start of the path
    marioOffsetq[2] = pathLengthq / 2 - marioOffsetq[2];

    pathAngle[1] = pathYaw + DEGREES(180);
    pathAngle[2] = 0;

    // Rotate the offset in the direction of the path again
    offset_rotatedq(posq, pathq[0], marioOffsetq, pathAngle);
    vec3q_get_dist_and_angle(pathq[0], c->posq, &camParDistq, &pathPitch, &pathYaw);

    // Adjust the focus. Does nothing, focus is set to Mario at the end
    focOffsetq[2] = pathLengthq / 2 - focOffsetq[2];
    offset_rotatedq(c->focusq, pathq[0], focOffsetq, pathAngle);

    // Changing paths, update the stored position offset
    if (sStatusFlags & CAM_FLAG_CHANGED_PARTRACK_INDEX) {
        sStatusFlags &= ~CAM_FLAG_CHANGED_PARTRACK_INDEX;
        sParTrackTransOff.posq[0] = oldPosq[0] - c->posq[0];
        sParTrackTransOff.posq[1] = oldPosq[1] - c->posq[1];
        sParTrackTransOff.posq[2] = oldPosq[2] - c->posq[2];
    }
    // Slowly transition to the next path
    approach_q32_asymptotic_bool(&sParTrackTransOff.posq[0], 0, q(0.025));
    approach_q32_asymptotic_bool(&sParTrackTransOff.posq[1], 0, q(0.025));
    approach_q32_asymptotic_bool(&sParTrackTransOff.posq[2], 0, q(0.025));
    vec3q_add(c->posq, sParTrackTransOff.posq);

    // Check if the camera should go to the next path
    if (sParTrackPath[sParTrackIndex + 1].startOfPath != 0) {
        // get Mario's distance to the next path
		Vec3q trac_posq[3];
		for(int i = 0; i < 3; i++) {
			vec3q_copy(trac_posq[i], sParTrackPath[sParTrackIndex + i].posq);
		}
        calculate_anglesq(trac_posq[1], trac_posq[2], &nextPitch, &nextYaw);
        vec3q_set_dist_and_angle(trac_posq[1], nextPathPosq, q(400), nextPitch, nextYaw);
        distToPrevq = calc_abs_distq(marioPosq, nextPathPosq);

        // get Mario's distance to the previous path
        calculate_anglesq(trac_posq[1], trac_posq[0], &prevPitch, &prevYaw);
        vec3q_set_dist_and_angle(trac_posq[1], prevPathPosq, q(400), prevPitch, prevYaw);
        distToNextq = calc_abs_distq(marioPosq, prevPathPosq);
        if (distToPrevq < distToNextq) {
            sParTrackIndex++;
            sStatusFlags |= CAM_FLAG_CHANGED_PARTRACK_INDEX;
        }
    }

    // Check if the camera should go to the previous path
    if (sParTrackIndex != 0) {
        // get Mario's distance to the next path
		Vec3q trac_posq[3];
		for(int i = 0; i < 3; i++) {
			vec3q_copy(trac_posq[i], sParTrackPath[sParTrackIndex + i - 1].posq);
		}
        calculate_anglesq(trac_posq[1], trac_posq[2], &nextPitch, &nextYaw);
        vec3q_set_dist_and_angle(trac_posq[1], nextPathPosq, q(700), nextPitch, nextYaw);
        distToPrevq = calc_abs_distq(marioPosq, nextPathPosq);

        // get Mario's distance to the previous path
        calculate_anglesq(trac_posq[1], trac_posq[0], &prevPitch, &prevYaw);
        vec3q_set_dist_and_angle(trac_posq[1], prevPathPosq, q(700), prevPitch, prevYaw);
        distToNextq = calc_abs_distq(marioPosq, prevPathPosq);
        if (distToPrevq > distToNextq) {
            sParTrackIndex--;
            sStatusFlags |= CAM_FLAG_CHANGED_PARTRACK_INDEX;
        }
    }

    // Update the camera focus and return the camera's yaw
    vec3q_copy(focusq, marioPosq);
    vec3q_get_dist_and_angle(focusq, posq, &camParDistq, &pathPitch, &pathYaw);
    return pathYaw;
}

/**
 * Updates the camera during fixed mode.
 */
s32 update_fixed_cameraq(struct Camera *c, Vec3q focusq, UNUSED Vec3q posq) {
    q32 focusFloorOffq;
    q32 goalHeightq;
    q32 ceilHeightq;
    q32 heightOffsetq;
    q32 distCamToFocusq;
    q32 scaleToMarioq = QONE / 2;
    s16 pitch;
    s16 yaw;
    Vec3s faceAngle;
    struct Surface *ceiling;
    Vec3q basePosq;

    play_camera_buzz_if_c_sideways();

    // Don't move closer to Mario in these areas
    switch (gCurrLevelArea) {
        case AREA_RR:
            scaleToMarioq = 0;
            heightOffsetq = 0;
            break;

        case AREA_CASTLE_LOBBY:
            scaleToMarioq = q(0.3f);
            heightOffsetq = 0;
            break;

        case AREA_BBH:
            scaleToMarioq = 0;
            heightOffsetq = 0;
            break;
    }

    handle_c_button_movement(c);
    play_camera_buzz_if_cdown();

    calc_y_to_curr_floorq(&focusFloorOffq, QONE, q(200), &focusFloorOffq, q(0.9), q(200));
    vec3q_copy(focusq, sMarioCamState->posq);
    focusq[1] += focusFloorOffq + q(125);
    vec3q_get_dist_and_angle(focusq, c->posq, &distCamToFocusq, &faceAngle[0], &faceAngle[1]);
    faceAngle[2] = 0;

    vec3q_copy(basePosq, sFixedModeBasePositionq);
    vec3q_add(basePosq, sCastleEntranceOffsetq);

    if (sMarioGeometry.currFloorType != SURFACE_DEATH_PLANE
        && sMarioGeometry.currFloorHeight != FLOOR_LOWER_LIMIT) {
        goalHeightq = q(sMarioGeometry.currFloorHeight) + basePosq[1] + heightOffsetq;
    } else {
        goalHeightq = gLakituState.goalPosq[1];
    }

    if (q(300) > distCamToFocusq) {
        goalHeightq += q(300) - distCamToFocusq;
    }

    ceilHeightq = find_ceilq(c->posq[0], goalHeightq - q(100), c->posq[2], &ceiling);
    if (ceilHeightq != q(CELL_HEIGHT_LIMIT)) {
        if (goalHeightq > (ceilHeightq -= q(125))) {
            goalHeightq = ceilHeightq;
        }
    }

    if (sStatusFlags & CAM_FLAG_SMOOTH_MOVEMENT) {
        camera_approach_q32_symmetric_bool(&c->posq[1], goalHeightq, q(15));
    } else {
        if (goalHeightq < sMarioCamState->posq[1] - q(500)) {
            goalHeightq = sMarioCamState->posq[1] - q(500);
        }
        c->posq[1] = goalHeightq;
    }

    c->posq[0] = basePosq[0] + qmul((sMarioCamState->posq[0] - basePosq[0]), scaleToMarioq);
    c->posq[2] = basePosq[2] + qmul((sMarioCamState->posq[2] - basePosq[2]), scaleToMarioq);

    if (scaleToMarioq != 0) {
        vec3q_get_dist_and_angle(c->focusq, c->posq, &distCamToFocusq, &pitch, &yaw);
        if (distCamToFocusq > q(1000)) {
            distCamToFocusq = q(1000);
            vec3q_set_dist_and_angle(c->focusq, c->posq, distCamToFocusq, pitch, yaw);
        }
    }

    return faceAngle[1];
}

/**
 * Updates the camera during a boss fight
 */
s32 update_boss_fight_cameraq(struct Camera *c, Vec3q focusq, Vec3q posq) {
    struct Object *o;
    q32 focusDistanceq;
    // Floor normal values
    q32 nxq;
    q32 nyq;
    q32 nzq;
    /// Floor originOffset
    q32 ooq;
    s16 yaw;
    s16 heldState;
    struct Surface *floor;
    Vec3q secondFocusq;
    Vec3q holdFocOffsetq = { 0, q(-150), q(-125) };

    handle_c_button_movement(c);

    // Start camera shakes if bowser jumps or gets thrown.
    if (sMarioCamState->cameraEvent == CAM_EVENT_BOWSER_JUMP) {
        set_environmental_camera_shake(SHAKE_ENV_BOWSER_JUMP);
        sMarioCamState->cameraEvent = 0;
    }
    if (sMarioCamState->cameraEvent == CAM_EVENT_BOWSER_THROW_BOUNCE) {
        set_environmental_camera_shake(SHAKE_ENV_BOWSER_THROW_BOUNCE);
        sMarioCamState->cameraEvent = 0;
    }

    yaw = sModeOffsetYaw + DEGREES(45);
    // Get boss's position and whether Mario is holding it.
    if ((o = gSecondCameraFocus) != NULL) {
        object_pos_to_vec3q(secondFocusq, o);
        heldState = o->oHeldState;
    } else {
    // If no boss is there, just rotate around the area's center point.
        secondFocusq[0] = c->areaCenXq;
        secondFocusq[1] = sMarioCamState->posq[1];
        secondFocusq[2] = c->areaCenZq;
        heldState = 0;
    }

    focusDistanceq = qmul(calc_abs_distq(sMarioCamState->posq, secondFocusq), q(1.6));
    if (focusDistanceq < q(800)) {
        focusDistanceq = q(800);
    }
    if (focusDistanceq > q(5000)) {
        focusDistanceq = q(5000);
    }

    // If holding the boss, add a slight offset to secondFocus so that the spinning is more pronounced.
    if (heldState == 1) {
        offset_rotatedq(secondFocusq, sMarioCamState->posq, holdFocOffsetq, sMarioCamState->faceAngle);
    }

    // Set the camera focus to the average of Mario and secondFocus
    focusq[0] = (sMarioCamState->posq[0] + secondFocusq[0]) / 2;
    focusq[1] = (sMarioCamState->posq[1] + secondFocusq[1]) / 2 + q(125);
    focusq[2] = (sMarioCamState->posq[2] + secondFocusq[2]) / 2;

    // Calculate the camera's position as an offset from the focus
    // When C-Down is not active, this
    vec3q_set_dist_and_angle(focusq, posq, focusDistanceq, 0x1000, yaw);
    // Find the floor of the arena
    posq[1] = find_floorq(c->areaCenXq, q(CELL_HEIGHT_LIMIT), c->areaCenZq, &floor);
    if (floor != NULL) {
        nxq = (q32) floor->compressed_normal.x * QONE / COMPRESSED_NORMAL_ONE;
        nyq = (q32) floor->compressed_normal.y * QONE / COMPRESSED_NORMAL_ONE;
        nzq = (q32) floor->compressed_normal.z * QONE / COMPRESSED_NORMAL_ONE;
        ooq = floor->originOffsetq;
        posq[1] = q(300) - qdiv((qmul(nxq, posq[0]) + qmul(nzq, posq[2]) + ooq), nyq);
        switch (gCurrLevelArea) {
            case AREA_BOB:
                posq[1] += q(125);
                //! fall through, makes the BoB boss fight camera move up twice as high as it should
				break; // fix
            case AREA_WF:
                posq[1] += q(125);
        }
    }

    //! Must be same line to match on -O2
    // Prevent the camera from going to the ground in the outside boss fight
    if (gCurrLevelNum == LEVEL_BBH) { posq[1] = q(2047); }

    // Rotate from C-Button input
    if (sCSideButtonYaw < 0) {
        sModeOffsetYaw += 0x200;
        if ((sCSideButtonYaw += 0x100) > 0) {
            sCSideButtonYaw = 0;
        }
    }
    if (sCSideButtonYaw > 0) {
        sModeOffsetYaw -= 0x200;
        if ((sCSideButtonYaw -= 0x100) < 0) {
            sCSideButtonYaw = 0;
        }
    }

    focusq[1] = (sMarioCamState->posq[1] + secondFocusq[1]) / 2 + q(100);
    if (heldState == 1) {
        focusq[1] += 300 * sinqs((gMarioStates[0].angleVel[1] > 0) ?  gMarioStates[0].angleVel[1]
                                                                     : -gMarioStates[0].angleVel[1]);
    }

    //! Unnecessary conditional, focusDistance is already bounded to 800
    if (focusDistanceq < q(400)) {
        focusDistanceq = q(400);
    }

    // Set C-Down distance and pitch.
    // C-Down will essentially double the distance from the center.
    // sLakituPitch approaches 33.75 degrees.
    lakitu_zoomq(focusDistanceq, 0x1800);

    // Move the camera position back as sLakituDist and sLakituPitch increase.
    // This doesn't zoom out of bounds because pos is set above each frame.
    // The constant 0x1000 doubles the pitch from the center when sLakituPitch is 0
    // When Lakitu is fully zoomed out, the pitch comes to 0x3800, or 78.75 degrees, up from the focus.
    vec3q_set_dist_and_angle(posq, posq, q(sLakituDist), sLakituPitch + 0x1000, yaw);

    return yaw;
}

// 2nd iteration of data
//s16 unused8032D0A8[] = { 14, 1, 2, 4 };
//s16 unused8032D0B0[] = { 16, 9, 17, 0 };

/**
 * Maps cutscene to numbers in [0,4]. Used in determine_dance_cutscene() with sDanceCutsceneIndexTable.
 *
 * Only the first 5 entries are used. Perhaps the last 5 were bools used to indicate whether the star
 * type exits the course or not.
 */
u8 sDanceCutsceneTable[] = {
    CUTSCENE_DANCE_FLY_AWAY, CUTSCENE_DANCE_ROTATE, CUTSCENE_DANCE_CLOSEUP, CUTSCENE_KEY_DANCE, CUTSCENE_DANCE_DEFAULT,
    FALSE,                   FALSE,                 FALSE,                  FALSE,              TRUE,
};

/**
 * Perhaps used by different dance cutscenes.
 */
//struct UnusedDanceInfo {
//    Vec3f point;
//    f32 distTarget;
//    f32 distMultiplier;
//};
//
//struct UnusedDanceInfo unusedDanceInfo1 = {
//  {-3026.0f, 912.0f, -2148.0f},
//  600.0f,
//  0.3f
//};
//
//u32 unusedDanceType = 0;
//struct UnusedDanceInfo unusedDanceInfo2 = {
//  {-4676.0f, 917.0f, -3802.0f},
//  600.0f,
//  0.3f
//};


/**
 * Table that dictates camera movement in bookend room.
 * Due to only the X being varied in the table, this only moves along the X axis linearly.
 * Third entry is seemingly unused.
 */
struct ParallelTrackingPoint sBBHLibraryParTrackPath[] = {
    { 1, { q(-929), q(1619), q(-1490) }, q(50), 0 },
    { 0, { q(-2118), q(1619), q(-1490) }, q(50), 0 },
    { 0, { 0, 0, 0 }, 0, 0 },
};

s32 unused_update_mode_5_camera(UNUSED struct Camera *c, UNUSED Vec3q focusq, UNUSED Vec3q posq) {
#ifdef AVOID_UB
   return 0;
#endif
}

void mode_boss_fight_camera(struct Camera *c) {
    c->nextYaw = update_boss_fight_cameraq(c, c->focusq, c->posq);
}

/**
 * Parallel tracking mode, the camera faces perpendicular to a line defined by sParTrackPath
 *
 * @see update_parallel_tracking_camera
 */
void mode_parallel_tracking_camera(struct Camera *c) {
    s16 dummy;

    radial_camera_input(c);
    set_fov_function(CAM_FOV_DEFAULT);
    c->nextYaw = update_parallel_tracking_cameraq(c, c->focusq, c->posq);
    camera_approach_s16_symmetric_bool(&dummy, 0, 0x0400);
}

/**
 * Fixed camera mode, the camera rotates around a point and looks and zooms toward Mario.
 */
void mode_fixed_camera(struct Camera *c) {
    if (gCurrLevelNum == LEVEL_BBH) {
        set_fov_function(CAM_FOV_BBH);
    } else {
        set_fov_function(CAM_FOV_APP_45);
    }
    c->nextYaw = update_fixed_cameraq(c, c->focusq, c->posq);
    c->yaw = c->nextYaw;
    pan_ahead_of_player(c);
    vec3q_set(sCastleEntranceOffsetq, 0, 0, 0);
}

/**
 * Updates the camera in BEHIND_MARIO mode.
 *
 * The C-Buttons rotate the camera 90 degrees left/right and 67.5 degrees up/down.
 */
s32 update_behind_mario_cameraq(struct Camera *c, Vec3q focusq, Vec3q posq) {
    q32 distq;
    s16 absPitch;
    s16 pitch;
    s16 yaw;
    s16 goalPitch = -sMarioCamState->faceAngle[0];
    s16 marioYaw = sMarioCamState->faceAngle[1] + DEGREES(180);
    s16 goalYawOff = 0;
    s16 yawSpeed;
    s16 pitchInc = 32;
    q32 maxDistq = q(800);
    q32 focYOffq = q(125);

    // Zoom in when Mario R_TRIG mode is active
    if (sSelectionFlags & CAM_MODE_MARIO_ACTIVE) {
        maxDistq = q(350);
        focYOffq = q(120);
    }
    if (!(sMarioCamState->action & (ACT_FLAG_SWIMMING | ACT_FLAG_METAL_WATER))) {
        pitchInc = 128;
    }

    // Focus on Mario
    vec3q_copy(focusq, sMarioCamState->posq);
    c->focusq[1] += focYOffq;
    //! @bug unnecessary
    //distq = calc_abs_distq(focusq, posq);
    //! @bug unnecessary
    //pitch = calculate_pitch(focus, pos);
    vec3q_get_dist_and_angle(focusq, posq, &distq, &pitch, &yaw);
    if (distq > maxDistq) {
        distq = maxDistq;
    }
    if ((absPitch = pitch) < 0) {
        absPitch = -absPitch;
    }

    // Determine the yaw speed based on absPitch. A higher absPitch (further away from looking straight)
    // translates to a slower speed
    // Note: Pitch is always within +- 90 degrees or +-0x4000, and 0x4000 / 0x200 = 32
    yawSpeed = 32 - absPitch / 0x200;
    if (yawSpeed < 1) {
        yawSpeed = 1;
    }
    if (yawSpeed > 32) {
        yawSpeed = 32;
    }

    if (sCSideButtonYaw != 0) {
        camera_approach_s16_symmetric_bool(&sCSideButtonYaw, 0, 1);
        yawSpeed = 8;
    }
    if (sBehindMarioSoundTimer != 0) {
        goalPitch = 0;
        camera_approach_s16_symmetric_bool(&sBehindMarioSoundTimer, 0, 1);
        pitchInc = 0x800;
    }

    if (sBehindMarioSoundTimer == 28) {
        if (sCSideButtonYaw < 5 || sCSideButtonYaw > 28) {
            play_sound_cbutton_up();
        }
    }
    if (sCSideButtonYaw == 28) {
        if (sBehindMarioSoundTimer < 5 || sBehindMarioSoundTimer > 28) {
            play_sound_cbutton_up();
        }
    }

    // C-Button input. Note: Camera rotates in the opposite direction of the button (airplane controls)
    //! @bug C-Right and C-Up take precedence due to the way input is handled here

    // Rotate right
    if (sCButtonsPressed & L_CBUTTONS) {
        if (gPlayer1Controller->buttonPressed & L_CBUTTONS) {
            play_sound_cbutton_side();
        }
        if (distq < maxDistq) {
            camera_approach_q32_symmetric_bool(&distq, maxDistq, q(5));
        }
        goalYawOff = -0x3FF8;
        sCSideButtonYaw = 30;
        yawSpeed = 2;
    }
    // Rotate left
    if (sCButtonsPressed & R_CBUTTONS) {
        if (gPlayer1Controller->buttonPressed & R_CBUTTONS) {
            play_sound_cbutton_side();
        }
        if (distq < maxDistq) {
            camera_approach_q32_symmetric_bool(&distq, maxDistq, q(5));
        }
        goalYawOff = 0x3FF8;
        sCSideButtonYaw = 30;
        yawSpeed = 2;
    }
    // Rotate up
    if (sCButtonsPressed & D_CBUTTONS) {
        if (gPlayer1Controller->buttonPressed & (U_CBUTTONS | D_CBUTTONS)) {
            play_sound_cbutton_side();
        }
        if (distq < maxDistq) {
            camera_approach_q32_symmetric_bool(&distq, maxDistq, q(5));
        }
        goalPitch = -0x3000;
        sBehindMarioSoundTimer = 30;
        pitchInc = 0x800;
    }
    // Rotate down
    if (sCButtonsPressed & U_CBUTTONS) {
        if (gPlayer1Controller->buttonPressed & (U_CBUTTONS | D_CBUTTONS)) {
            play_sound_cbutton_side();
        }
        if (distq < maxDistq) {
            camera_approach_q32_symmetric_bool(&distq, maxDistq, q(5));
        }
        goalPitch = 0x3000;
        sBehindMarioSoundTimer = 30;
        pitchInc = 0x800;
    }

    approach_s16_asymptotic_bool(&yaw, marioYaw + goalYawOff, yawSpeed);
    camera_approach_s16_symmetric_bool(&pitch, goalPitch, pitchInc);
    if (distq < q(300)) {
        distq = q(300);
    }
    vec3q_set_dist_and_angle(focusq, posq, distq, pitch, yaw);
    if (gCurrLevelArea == AREA_WDW_MAIN) {
        yaw = clamp_positions_and_find_yawq(posq, focusq, q(4508), q(-3739), q(4508), q(-3739));
    }
    if (gCurrLevelArea == AREA_THI_HUGE) {
        yaw = clamp_positions_and_find_yawq(posq, focusq, q(8192), q(-8192), q(8192), q(-8192));
    }
    if (gCurrLevelArea == AREA_THI_TINY) {
        yaw = clamp_positions_and_find_yawq(posq, focusq, q(2458), q(-2458), q(2458), q(-2458));
    }

    return yaw;
}

/**
 * "Behind Mario" mode: used when Mario is flying, on the water's surface, or shot from a cannon
 */
s32 mode_behind_mario(struct Camera *c) {
    struct MarioState *marioState = &gMarioStates[0];
    struct Surface *floor;
    Vec3q newPosq;
    //! @bug oldPos is unused, see resolve_geometry_collisions
    //Vec3f oldPos;
    q32 waterHeightq;
    q32 floorHeightq;
    q32 distCamToFocusq;
    s16 camPitch;
    s16 camYaw;
    s16 yaw;

    //vec3q_copy(oldPosq, c->posq);
    gCameraMovementFlags &= ~CAM_MOVING_INTO_MODE;
    vec3q_copy(newPosq, c->posq);
    yaw = update_behind_mario_cameraq(c, c->focusq, newPosq);
    c->posq[0] = newPosq[0];
    c->posq[2] = newPosq[2];

    // Keep the camera above the water surface if swimming
    if (c->mode == CAMERA_MODE_WATER_SURFACE) {
        floorHeightq = find_floorq(c->posq[0], c->posq[1], c->posq[2], &floor);
        newPosq[1] = q(marioState->waterLevel + 120);
        if (newPosq[1] < (floorHeightq += q(120))) {
            newPosq[1] = floorHeightq;
        }
    }
    approach_camera_heightq(c, newPosq[1], q(50));
    waterHeightq = find_water_levelq(c->posq[0], c->posq[2]) + q(100);
    if (c->posq[1] <= waterHeightq) {
        gCameraMovementFlags |= CAM_MOVE_SUBMERGED;
    } else {
        gCameraMovementFlags &= ~CAM_MOVE_SUBMERGED;
    }

    resolve_geometry_collisionsq(c->posq);
    // Prevent camera getting too far away
    vec3q_get_dist_and_angle(c->focusq, c->posq, &distCamToFocusq, &camPitch, &camYaw);
    if (distCamToFocusq > q(800)) {
        distCamToFocusq = q(800);
        vec3q_set_dist_and_angle(c->focusq, c->posq, distCamToFocusq, camPitch, camYaw);
    }
    pan_ahead_of_player(c);

    return yaw;
}

/**
 * Update the camera in slide and hoot mode.
 *
 * In slide mode, keep the camera 800 units from Mario
 */
s16 update_slide_camera(struct Camera *c) {
    struct Surface *floor;
    q32 floorHeightq;
    Vec3q posq;
    q32 distCamToFocusq;
    q32 maxCamDistq;
    q32 pitchScaleq;
    s16 camPitch;
    s16 camYaw;
    UNUSED struct MarioState *marioState = &gMarioStates[0];
    s16 goalPitch = 0x1555;
    s16 goalYaw = sMarioCamState->faceAngle[1] + DEGREES(180);

    // Zoom in when inside the CCM shortcut
    if (sStatusFlags & CAM_FLAG_CCM_SLIDE_SHORTCUT) {
        sLakituDist = qtrunc(approach_q32(q(sLakituDist), q(-600), q(20), q(20)));
    } else {
        sLakituDist = qtrunc(approach_q32(q(sLakituDist), q(0), q(20), q(20)));
    }

    // No C-Button input in this mode, notify the player with a buzzer
    play_camera_buzz_if_cbutton();

    // Focus on Mario
    vec3q_copy(c->focusq, sMarioCamState->posq);
    c->focusq[1] += q(50);

    vec3q_get_dist_and_angle(c->focusq, c->posq, &distCamToFocusq, &camPitch, &camYaw);
    maxCamDistq = q(800);

    // In hoot mode, zoom further out and rotate faster
    if (sMarioCamState->action == ACT_RIDING_HOOT) {
        maxCamDistq = q(1000);
        goalPitch = 0x2800;
        camera_approach_s16_symmetric_bool(&camYaw, goalYaw, 0x100);
    } else {
        camera_approach_s16_symmetric_bool(&camYaw, goalYaw, 0x80);
    }
    camera_approach_s16_symmetric_bool(&camPitch, goalPitch, 0x100);

    // Hoot mode
    if (sMarioCamState->action != ACT_RIDING_HOOT && sMarioGeometry.currFloorType == SURFACE_DEATH_PLANE) {
        vec3q_set_dist_and_angle(c->focusq, posq, maxCamDistq + q(sLakituDist), camPitch, camYaw);
        c->posq[0] = posq[0];
        c->posq[2] = posq[2];
        camera_approach_q32_symmetric_bool(&c->posq[1], c->focusq[1], q(30));
        vec3q_get_dist_and_angle(c->posq, c->focusq, &distCamToFocusq, &camPitch, &camYaw);
        pitchScaleq = (distCamToFocusq - maxCamDistq + q(sLakituDist)) / 10000;
        if (pitchScaleq > QONE) {
            pitchScaleq = QONE;
        }
        camPitch += qtrunc((q32) (0x1000 * pitchScaleq));
        vec3q_set_dist_and_angle(c->posq, c->focusq, distCamToFocusq, camPitch, camYaw);

    // Slide mode
    } else {
        vec3q_set_dist_and_angle(c->focusq, c->posq, maxCamDistq + q(sLakituDist), camPitch, camYaw);
        sStatusFlags |= CAM_FLAG_BLOCK_SMOOTH_MOVEMENT;

        // Stay above the slide floor
        floorHeightq = find_floorq(c->posq[0], c->posq[1] + q(200), c->posq[2], &floor) + q(125);
        if (c->posq[1] < floorHeightq) {
            c->posq[1] = floorHeightq;
        }
        // Stay closer than maxCamDist
        vec3q_get_dist_and_angle(c->focusq, c->posq, &distCamToFocusq, &camPitch, &camYaw);
        if (distCamToFocusq > maxCamDistq + q(sLakituDist)) {
            distCamToFocusq = maxCamDistq + q(sLakituDist);
            vec3q_set_dist_and_angle(c->focusq, c->posq, distCamToFocusq, camPitch, camYaw);
        }
    }

    camYaw = calculate_yawq(c->focusq, c->posq);
    return camYaw;
}

void mode_behind_mario_camera(struct Camera *c) {
    c->nextYaw = mode_behind_mario(c);
}

s32 nop_update_water_cameraq(UNUSED struct Camera *c, UNUSED Vec3q focusq, UNUSED Vec3q posq) {
#ifdef AVOID_UB
   return 0;
#endif
}

/**
 * Exactly the same as BEHIND_MARIO
 */
void mode_water_surface_camera(struct Camera *c) {
    c->nextYaw = mode_behind_mario(c);
}

/**
 * Used in sModeTransitions for CLOSE and FREE_ROAM mode
 */
s32 update_mario_cameraq(UNUSED struct Camera *c, Vec3q focusq, Vec3q posq) {
    s16 yaw = sMarioCamState->faceAngle[1] + sModeOffsetYaw + DEGREES(180);
    focus_on_marioq(focusq, posq, q(125), q(125), gCameraZoomDistq, 0x05B0, yaw);

    return sMarioCamState->faceAngle[1];
}

/**
 * Update the camera in default, close, and free roam mode
 *
 * The camera moves behind Mario, and can rotate all the way around
 */
s16 update_default_camera(struct Camera *c) {
    Vec3q tempPosq;
    Vec3q cPosq;
    struct Surface *marioFloor;
    struct Surface *cFloor;
    struct Surface *tempFloor;
    struct Surface *ceil;
    q32 camFloorHeightq;
    q32 tempFloorHeightq;
    q32 marioFloorHeightq;
    q32 distq;
    q32 zoomDistq;
    q32 waterHeightq;
    q32 gasHeightq;
    s16 avoidYaw;
    s16 pitch;
    s16 yaw;
    s16 yawGoal = sMarioCamState->faceAngle[1] + DEGREES(180);
    q32 posHeightq;
    q32 focHeightq;
    q32 distFromWaterq;
    s16 tempPitch;
    s16 tempYaw;
    volatile q32 xzDistq;
    s16 nextYawVel;
    s16 yawVel = 0;
    q32 scaleq;
    s32 avoidStatus = 0;
    s32 closeToMario = 0;
    q32 ceilHeightq = find_ceilq(gLakituState.goalPosq[0],
                                 gLakituState.goalPosq[1],
                                 gLakituState.goalPosq[2], &ceil);
    s16 yawDir;

    handle_c_button_movement(c);
    vec3q_get_dist_and_angle(sMarioCamState->posq, c->posq, &distq, &pitch, &yaw);

    // If C-Down is active, determine what distance the camera should be from Mario
    if (gCameraMovementFlags & CAM_MOVE_ZOOMED_OUT) {
        //! In Mario mode, the camera is zoomed out further than in Lakitu mode (1400 vs 1200)
        if (set_cam_angle(0) == CAM_ANGLE_MARIO) {
            zoomDistq = gCameraZoomDistq + q(1050);
        } else {
            zoomDistq = gCameraZoomDistq + q(400);
        }
    } else {
        zoomDistq = gCameraZoomDistq;
    }

    if (sMarioCamState->action & ACT_FLAG_HANGING ||
        sMarioCamState->action == ACT_RIDING_HOOT) {
        zoomDistq = qmul(zoomDistq, q(0.8));
        set_handheld_shake(HAND_CAM_SHAKE_HANG_OWL);
    }

    // If not zooming out, only allow dist to decrease
    if (sZoomAmountq == 0) {
        if (distq > zoomDistq) {
            if ((distq -= q(50)) < zoomDistq) {
                distq = zoomDistq;
            }
        }
    } else {
        if ((sZoomAmountq -= q(30)) < 0) {
            sZoomAmountq = 0;
        }
        if (distq > zoomDistq) {
            if ((distq -= q(30)) < zoomDistq) {
                distq = zoomDistq;
            }
        }
        if (distq < zoomDistq) {
            if ((distq += q(30)) > zoomDistq) {
                distq = zoomDistq;
            }
        }
    }

    // Determine how fast to rotate the camera
    if (gPlayer1Controller->rightStickMag > 0.f) {
        yaw += gPlayer1Controller->rightStickX * (0x400 / -80.f);
        stick_pitch += gPlayer1Controller->rightStickY * (0x400 / -80.f);
        nextYawVel = 0;
    } else if (sCSideButtonYaw == 0) {
        if (c->mode == CAMERA_MODE_FREE_ROAM) {
            nextYawVel = 0xC0;
        } else {
            nextYawVel = 0x100;
        }
        if ((gPlayer1Controller->stickX != 0.f || gPlayer1Controller->stickY != 0.f) != 0) {
            nextYawVel = 0x20;
        }
    } else {
        if (sCSideButtonYaw < 0) {
            yaw += 0x200;
        }
        if (sCSideButtonYaw > 0) {
            yaw -= 0x200;
        }
        camera_approach_s16_symmetric_bool(&sCSideButtonYaw, 0, 0x100);
        camera_approach_s16_symmetric_bool(&stick_pitch, 0, 0x100);
        nextYawVel = 0;
    }
    sYawSpeed = 0x400;
    xzDistq = calc_hor_distq(sMarioCamState->posq, c->posq);

    if (sStatusFlags & CAM_FLAG_BEHIND_MARIO_POST_DOOR) {
        if (xzDistq >= q(250)) {
            sStatusFlags &= ~CAM_FLAG_BEHIND_MARIO_POST_DOOR;
        }
        if (ABS((sMarioCamState->faceAngle[1] - yaw) / 2) < 0x1800) {
            sStatusFlags &= ~CAM_FLAG_BEHIND_MARIO_POST_DOOR;
            yaw = sCameraYawAfterDoorCutscene + DEGREES(180);
            distq = q(800);
            sStatusFlags |= CAM_FLAG_BLOCK_SMOOTH_MOVEMENT;
        }
    } else if (xzDistq < q(250)) {
        // Turn rapidly if very close to Mario
        c->posq[0] += (q32) (250 - qtrunc(xzDistq)) * sinqs(yaw);
        c->posq[2] += (q32) (250 - qtrunc(xzDistq)) * cosqs(yaw);
        if (sCSideButtonYaw == 0) {
            nextYawVel = 0x1000;
            sYawSpeed = 0;
            vec3q_get_dist_and_angle(sMarioCamState->posq, c->posq, &distq, &pitch, &yaw);
        }
        closeToMario |= 1;
    }

    if (-16 < gPlayer1Controller->stickY) {
        c->yaw = yaw;
    }

    calc_y_to_curr_floorq(&posHeightq, q(1), q(200), &focHeightq, q(0.9), q(200));
    vec3q_copy(cPosq, c->posq);
    avoidStatus = rotate_camera_around_wallsq(c, cPosq, &avoidYaw, 0x600);
    // If a wall is blocking the view of Mario, then rotate in the calculated direction
    if (avoidStatus == 3) {
        unusedFreeRoamWallYaw = avoidYaw;
        sAvoidYawVel = yaw;
        sStatusFlags |= CAM_FLAG_COLLIDED_WITH_WALL;
        //! Does nothing
        //vec3q_get_dist_and_angle(sMarioCamState->pos, cPos, &xzDist, &tempPitch, &tempYaw);
        // Rotate to avoid the wall
        approach_s16_asymptotic_bool(&yaw, avoidYaw, 10);
        //! Does nothing
        //vec3f_set_dist_and_angle(sMarioCamState->pos, cPos, xzDist, tempPitch, tempYaw);
        sAvoidYawVel = (sAvoidYawVel - yaw) / 0x100;
    } else {
        if (gMarioStates[0].forwardVel == 0.f) {
            if (sStatusFlags & CAM_FLAG_COLLIDED_WITH_WALL) {
                if ((yawGoal - yaw) / 0x100 >= 0) {
                    yawDir = -1;
                } else {
                    yawDir = 1;
                }
                if ((sAvoidYawVel > 0 && yawDir > 0) || (sAvoidYawVel < 0 && yawDir < 0)) {
                    yawVel = nextYawVel;
                }
            } else {
                yawVel = nextYawVel;
            }
        } else {
            if (nextYawVel == 0x1000) {
                yawVel = nextYawVel;
            }
            sStatusFlags &= ~CAM_FLAG_COLLIDED_WITH_WALL;
        }

        // If a wall is near the camera, turn twice as fast
        if (avoidStatus != 0) {
            yawVel += yawVel;
        }
        // ...Unless the camera already rotated from being close to Mario
        if ((closeToMario & 1) && avoidStatus != 0) {
            yawVel = 0;
        }
        if (yawVel != 0 && get_dialog_id() == DIALOG_NONE) {
            camera_approach_s16_symmetric_bool(&yaw, yawGoal, yawVel);
        }
    }

    // Only zoom out if not obstructed by walls and Lakitu hasn't collided with any
    if (avoidStatus == 0 && !(sStatusFlags & CAM_FLAG_COLLIDED_WITH_WALL)) {
        approach_q32_asymptotic_bool(&distq, zoomDistq - q(100), q(0.05));
    }
    vec3q_set_dist_and_angle(sMarioCamState->posq, cPosq, distq, pitch, yaw);
    cPosq[1] += posHeightq + q(125);

    // Move the camera away from walls and set the collision flag
    if (collide_with_wallsq(cPosq, q(10), q(80)) != 0) {
        sStatusFlags |= CAM_FLAG_COLLIDED_WITH_WALL;
    }

    c->focusq[0] = sMarioCamState->posq[0];
    c->focusq[1] = sMarioCamState->posq[1] + q(125) + focHeightq;
    c->focusq[2] = sMarioCamState->posq[2];

    marioFloorHeightq = q(125) + q(sMarioGeometry.currFloorHeight);
    marioFloor = sMarioGeometry.currFloor;
    camFloorHeightq = find_floorq(cPosq[0], cPosq[1] + q(50), cPosq[2], &cFloor) + q(125);
    for (scaleq = q(0.1); scaleq < QONE; scaleq += q(0.2)) {
        scale_along_lineq(tempPosq, cPosq, sMarioCamState->posq, scaleq);
        tempFloorHeightq = find_floorq(tempPosq[0], tempPosq[1], tempPosq[2], &tempFloor) + q(125);
        if (tempFloor != NULL && tempFloorHeightq > marioFloorHeightq) {
            marioFloorHeightq = tempFloorHeightq;
            marioFloor = tempFloor;
        }
    }

    // Lower the camera in Mario mode
    if (sSelectionFlags & CAM_MODE_MARIO_ACTIVE) {
        marioFloorHeightq -= q(35);
        camFloorHeightq -= q(35);
        c->focusq[1] -= q(25);
    }

    // If there's water below the camera, decide whether to keep the camera above the water surface
    waterHeightq = find_water_levelq(cPosq[0], cPosq[2]);
    if (waterHeightq != q(FLOOR_LOWER_LIMIT)) {
        waterHeightq += q(125);
        distFromWaterq = waterHeightq - marioFloorHeightq;
        if (!(gCameraMovementFlags & CAM_MOVE_METAL_BELOW_WATER)) {
            if (distFromWaterq > q(800) && (sMarioCamState->action & ACT_FLAG_METAL_WATER)) {
                gCameraMovementFlags |= CAM_MOVE_METAL_BELOW_WATER;
            }
        } else {
            if (distFromWaterq < q(400) || !(sMarioCamState->action & ACT_FLAG_METAL_WATER)) {
                gCameraMovementFlags &= ~CAM_MOVE_METAL_BELOW_WATER;
            }
        }
        // If not wearing the metal cap, always stay above
        if (!(gCameraMovementFlags & CAM_MOVE_METAL_BELOW_WATER) && camFloorHeightq < waterHeightq) {
            camFloorHeightq = waterHeightq;
        }
    } else {
        gCameraMovementFlags &= ~CAM_MOVE_METAL_BELOW_WATER;
    }

    cPosq[1] = camFloorHeightq;
    vec3q_copy(tempPosq, cPosq);
    tempPosq[1] -= q(125);
    if (marioFloor != NULL && camFloorHeightq <= marioFloorHeightq) {
        avoidStatus = is_range_behind_surfaceq(c->focusq, tempPosq, marioFloor, 0, -1);
        if (avoidStatus != 1 && ceilHeightq > marioFloorHeightq) {
            camFloorHeightq = marioFloorHeightq;
        }
    }

    posHeightq = 0;
    if (c->mode == CAMERA_MODE_FREE_ROAM) {
        if (gCameraMovementFlags & CAM_MOVE_ZOOMED_OUT) {
            posHeightq = q(375);
            if (gCurrLevelArea == AREA_SSL_PYRAMID) {
                posHeightq /= 2;
            }
        } else {
            posHeightq = q(100);
        }
    }
    if ((gCameraMovementFlags & CAM_MOVE_ZOOMED_OUT) && (sSelectionFlags & CAM_MODE_MARIO_ACTIVE)) {
        posHeightq = q(610);
        if (gCurrLevelArea == AREA_SSL_PYRAMID || gCurrLevelNum == LEVEL_CASTLE) {
            posHeightq /= 2;
        }
    }

    // Make Lakitu fly above the gas
    gasHeightq = find_poison_gas_levelq(cPosq[0], cPosq[2]);
    if (gasHeightq != q(FLOOR_LOWER_LIMIT)) {
        if ((gasHeightq += q(130)) > c->posq[1]) {
            c->posq[1] = gasHeightq;
        }
    }

    if (sMarioCamState->action & ACT_FLAG_HANGING || sMarioCamState->action == ACT_RIDING_HOOT) {
        camFloorHeightq = sMarioCamState->posq[1] + q(400);
        if (c->mode == CAMERA_MODE_FREE_ROAM) {
            camFloorHeightq -= q(100);
        }
        ceilHeightq = q(CELL_HEIGHT_LIMIT);
        vec3q_copy(c->focusq, sMarioCamState->posq);
    }

    if (sMarioCamState->action & ACT_FLAG_ON_POLE) {
        camFloorHeightq = QFIELD(gMarioStates[0].usedObj, oPosY) + q(125);
        if (sMarioCamState->posq[1] - q(100) > camFloorHeightq) {
            camFloorHeightq = sMarioCamState->posq[1] - q(100);
        }
        ceilHeightq = q(CELL_HEIGHT_LIMIT);
        vec3q_copy(c->focusq, sMarioCamState->posq);
    }
    if (camFloorHeightq != q(FLOOR_LOWER_LIMIT)) {
        camFloorHeightq += posHeightq;
        approach_camera_heightq(c, camFloorHeightq, q(20));
    }
    c->posq[0] = cPosq[0];
    c->posq[2] = cPosq[2];
    cPosq[0] = gLakituState.goalPosq[0];
    cPosq[1] = c->posq[1];
    cPosq[2] = gLakituState.goalPosq[2];
    vec3q_get_dist_and_angle(cPosq, c->posq, &distq, &tempPitch, &tempYaw);
    // Prevent the camera from lagging behind too much
    if (distq > q(50)) {
        distq = q(50);
        vec3q_set_dist_and_angle(cPosq, c->posq, distq, tempPitch, tempYaw);
    }
    if (sMarioGeometry.currFloorType != SURFACE_DEATH_PLANE) {
        vec3q_get_dist_and_angle(c->focusq, c->posq, &distq, &tempPitch, &tempYaw);
        if (distq > zoomDistq) {
            distq = zoomDistq;
            vec3q_set_dist_and_angle(c->focusq, c->posq, distq, tempPitch, tempYaw);
        }
    }
    if (ceilHeightq != q(CELL_HEIGHT_LIMIT)) {
        if (c->posq[1] > (ceilHeightq -= q(150))
            && (avoidStatus = is_range_behind_surfaceq(c->posq, sMarioCamState->posq, ceil, 0, -1)) == 1) {
            c->posq[1] = ceilHeightq;
        }
    }
    if (gCurrLevelArea == AREA_WDW_TOWN) {
        yaw = clamp_positions_and_find_yawq(c->posq, c->focusq, q(2254), q(-3789), q(3790), q(-2253));
    }
    return yaw;
}

/**
 * The default camera mode
 * Used by close and free roam modes
 */
void mode_default_camera(struct Camera *c) {
    set_fov_function(CAM_FOV_DEFAULT);
    c->nextYaw = update_default_camera(c);
    pan_ahead_of_player(c);
}

/**
 * The mode used by close and free roam
 */
void mode_lakitu_camera(struct Camera *c) {
    gCameraZoomDistq = q(800);
    mode_default_camera(c);
}

/**
 * When no other mode is active and the current R button mode is Mario
 */
void mode_mario_camera(struct Camera *c) {
    gCameraZoomDistq = q(350);
    mode_default_camera(c);
}

/**
 * Rotates the camera around the spiral staircase.
 */
s32 update_spiral_stairs_cameraq(struct Camera *c, Vec3q focusq, Vec3q posq) {
    /// The returned yaw
    s16 camYaw;
    // unused
    s16 focPitch;
    /// The focus (Mario)'s yaw around the stairs
    s16 focYaw;
    // unused
    s16 posPitch;
    /// The camera's yaw around the stairs
    s16 posYaw;
    Vec3q cPosq;
    Vec3q checkPosq;
    struct Surface *floor;
    // unused
    q32 distq;
    q32 focusHeightq;
    q32 floorHeightq;
    q32 focYq;

    handle_c_button_movement(c);
    // Set base pos to the center of the staircase
    vec3q_set(sFixedModeBasePositionq, q(-1280), q(614), q(1740));

    // Focus on Mario, and move the focus up the staircase with him
    calc_y_to_curr_floorq(&focusHeightq, QONE, q(200), &focusHeightq, q(0.9), q(200));
    focusq[0] = sMarioCamState->posq[0];
    focYq = sMarioCamState->posq[1] + q(125) + focusHeightq;
    focusq[2] = sMarioCamState->posq[2];

    vec3q_copy(cPosq, posq);
    vec3q_get_dist_and_angle(sFixedModeBasePositionq, focusq, &distq, &focPitch, &focYaw);
    vec3q_get_dist_and_angle(sFixedModeBasePositionq, cPosq, &distq, &posPitch, &posYaw);

    sSpiralStairsYawOffset = posYaw - focYaw;
    // posYaw will change if Mario is more than 90 degrees around the stairs, relative to the camera
    if (sSpiralStairsYawOffset < DEGREES(-90)) {
        sSpiralStairsYawOffset = DEGREES(-90);
    }
    if (sSpiralStairsYawOffset > DEGREES(90)) {
        sSpiralStairsYawOffset = DEGREES(90);
    }
    focYaw += sSpiralStairsYawOffset;
    posYaw = focYaw;
    //! @bug unnecessary
    camera_approach_s16_symmetric_bool(&posYaw, focYaw, 0x1000);

    vec3q_set_dist_and_angle(sFixedModeBasePositionq, cPosq, q(300), 0, posYaw);

    // Move the camera's y coord up/down the staircase
    checkPosq[0] = focusq[0] + qmul(cPosq[0] - focusq[0], q(0.7));
    checkPosq[1] = focusq[1] + qmul(cPosq[1] - focusq[1], q(0.7)) + q(300);
    checkPosq[2] = focusq[2] + qmul(cPosq[2] - focusq[2], q(0.7));
    floorHeightq = find_floorq(checkPosq[0], checkPosq[1] + q(50), checkPosq[2], &floor);

    if (floorHeightq != q(FLOOR_LOWER_LIMIT)) {
        if (floorHeightq < sMarioGeometry.currFloorHeight) {
            floorHeightq = sMarioGeometry.currFloorHeight;
        }
        posq[1] = approach_q32(posq[1], (floorHeightq += q(125)), q(30), q(30));
    }

    camera_approach_q32_symmetric_bool(&focusq[1], focYq, q(30));
    posq[0] = cPosq[0];
    posq[2] = cPosq[2];
    camYaw = calculate_yawq(focusq, posq);

    return camYaw;
}

/**
 * The mode used in the spiral staircase in the castle
 */
void mode_spiral_stairs_camera(struct Camera *c) {
    c->nextYaw = update_spiral_stairs_cameraq(c, c->focusq, c->posq);
}

s32 update_slide_or_0f_cameraq(UNUSED struct Camera *c, Vec3q focusq, Vec3q posq) {
    s16 yaw = sMarioCamState->faceAngle[1] + sModeOffsetYaw + DEGREES(180);

    focus_on_marioq(focusq, posq, q(125), q(125), q(800), 5461, yaw);
    return sMarioCamState->faceAngle[1];
}

static UNUSED void unused_mode_0f_camera(struct Camera *c) {
    if (gPlayer1Controller->buttonPressed & U_CBUTTONS) {
        gCameraMovementFlags |= CAM_MOVE_C_UP_MODE;
    }
    c->nextYaw = update_slide_camera(c);
}

/**
 * Slide/hoot mode.
 * In this mode, the camera is always at the back of Mario, because Mario generally only moves forward.
 */
void mode_slide_camera(struct Camera *c) {
    if (sMarioGeometry.currFloorType == SURFACE_CLOSE_CAMERA ||
        sMarioGeometry.currFloorType == SURFACE_NO_CAM_COL_SLIPPERY) {
        mode_lakitu_camera(c);
    } else {
        if (gPlayer1Controller->buttonPressed & U_CBUTTONS) {
            gCameraMovementFlags |= CAM_MOVE_C_UP_MODE;
        }
        c->nextYaw = update_slide_camera(c);
    }
}

void store_lakitu_cam_info_for_c_up(struct Camera *c) {
    vec3q_copy(sCameraStoreCUp.posq, c->posq);
    vec3q_sub(sCameraStoreCUp.posq, sMarioCamState->posq);
    // Only store the y value, and as an offset from Mario, for some reason
    vec3q_set(sCameraStoreCUp.focusq, 0, c->focusq[1] - sMarioCamState->posq[1], 0);
}

/**
 * Start C-Up mode. The actual mode change is handled in update_mario_inputs() in mario.c
 *
 * @see update_mario_inputs
 */
s32 set_mode_c_up(struct Camera *c) {
    if (!(gCameraMovementFlags & CAM_MOVE_C_UP_MODE)) {
        gCameraMovementFlags |= CAM_MOVE_C_UP_MODE;
        store_lakitu_cam_info_for_c_up(c);
        sCameraSoundFlags &= ~CAM_SOUND_C_UP_PLAYED;
        return 1;
    }
    return 0;
}

/**
 * Zoom the camera out of C-Up mode, avoiding moving into a wall, if possible, by searching for an open
 * direction.
 */
s32 exit_c_up(struct Camera *c) {
    struct Surface *surface;
    Vec3q checkFocq;
    Vec3q curPosq;
    // Variables for searching for an open direction
    s32 searching = 0;
    /// The current sector of the circle that we are checking
    s32 sector;
    q32 ceilHeightq;
    q32 floorHeightq;
    q32 curDistq;
    q32 dq;
    s16 curPitch;
    s16 curYaw;
    s16 checkYaw = 0;
    //Vec3q storePosq; // unused
    //Vec3q storeFocq; // unused

    if ((gCameraMovementFlags & CAM_MOVE_C_UP_MODE) && !(gCameraMovementFlags & CAM_MOVE_STARTED_EXITING_C_UP)) {
        // Copy the stored pos and focus. This is unused.
        //vec3q_copy(storePosq, sCameraStoreCUp.posq);
        //vec3q_add(storePosq, sMarioCamState->posq);
        //vec3q_copy(storeFocq, sCameraStoreCUp.focusq);
        //vec3q_add(storeFocq, sMarioCamState->posq);

        checkFocq[0] = sMarioCamState->posq[0];
		checkFocq[1] = c->focusq[1];
        checkFocq[2] = sMarioCamState->posq[2];
        vec3q_get_dist_and_angle(checkFocq, c->posq, &curDistq, &curPitch, &curYaw);
        curPosq[0] = c->posq[0];
		curPosq[1] = c->posq[1];
		curPosq[2] = c->posq[2];
        curDistq = q(80);

        // Search for an open direction to zoom out in, if the camera is changing to close, free roam,
        // or spiral-stairs mode
        if (sModeInfo.lastMode == CAMERA_MODE_SPIRAL_STAIRS || sModeInfo.lastMode == CAMERA_MODE_CLOSE
            || sModeInfo.lastMode == CAMERA_MODE_FREE_ROAM) {
            searching = 1;
            // Check the whole circle around Mario for an open direction to zoom out to
            for (sector = 0; sector < 16 && searching == 1; sector++) {
                vec3q_set_dist_and_angle(checkFocq, curPosq, curDistq, 0, curYaw + checkYaw);

                // If there are no walls this way,
                if (q32_find_wall_collision(&curPosq[0], &curPosq[1], &curPosq[2], q(20), q(50)) == 0) {

                    // Start close to Mario, check for walls, floors, and ceilings all the way to the
                    // zoomed out distance
                    for (dq = curDistq; dq < gCameraZoomDistq; dq += q(20)) {
                        vec3q_set_dist_and_angle(checkFocq, curPosq, dq, 0, curYaw + checkYaw);

                        // Check if we're zooming out into a floor or ceiling
                        ceilHeightq = find_ceilq(curPosq[0], curPosq[1] - q(150), curPosq[2], &surface) + q(-10);
                        if (surface != NULL && ceilHeightq < curPosq[1]) {
                            break;
                        }
                        floorHeightq = find_floorq(curPosq[0], curPosq[1] + q(150), curPosq[2], &surface) + q(10);
                        if (surface != NULL && floorHeightq > curPosq[1]) {
                            break;
                        }

                        // Stop checking this direction if there is a wall blocking the way
                        if (q32_find_wall_collision(&curPosq[0], &curPosq[1], &curPosq[2], q(20), q(50)) == 1) {
                            break;
                        }
                    }

                    // If there was no collision found all the way to the max distance, it's an opening
                    if (dq >= gCameraZoomDistq) {
                        searching = 0;
                    }
                }

                // Alternate left and right, checking each 1/16th (22.5 degrees) of the circle
                if (searching == 1) {
                    checkYaw = -checkYaw;
                    if (checkYaw < 0) {
                        checkYaw -= 0x1000;
                    } else {
                        checkYaw += 0x1000;
                    }
                }
            }

            // Update the stored focus and pos to the direction found in the search
            if (searching == 0) {
                vec3q_set_dist_and_angle(checkFocq, sCameraStoreCUp.posq, gCameraZoomDistq, 0, curYaw + checkYaw);
                vec3q_copy(sCameraStoreCUp.focusq, checkFocq);
                vec3q_sub(sCameraStoreCUp.posq, sMarioCamState->posq);
                vec3q_sub(sCameraStoreCUp.focusq, sMarioCamState->posq);
            }

            gCameraMovementFlags |= CAM_MOVE_STARTED_EXITING_C_UP;
            transition_next_state(c, 15);
        } else {
            // Let the next camera mode handle it
            gCameraMovementFlags &= ~(CAM_MOVE_STARTED_EXITING_C_UP | CAM_MOVE_C_UP_MODE);
            vec3q_set_dist_and_angle(checkFocq, c->posq, curDistq, curPitch, curYaw + checkYaw);
        }
        play_sound_cbutton_down();
    }
    return 0;
}

/**
 * The mode used when C-Up is pressed.
 */
s32 update_c_upq(UNUSED struct Camera *c, Vec3q focusq, Vec3q posq) {
    s16 pitch = sCUpCameraPitch;
    s16 yaw = sMarioCamState->faceAngle[1] + sModeOffsetYaw + DEGREES(180);

    focus_on_marioq(focusq, posq, q(125), q(125), q(250), pitch, yaw);
    return sMarioCamState->faceAngle[1];
}

/**
 * Make Mario's head move in C-Up mode.
 */
void move_mario_head_c_up(UNUSED struct Camera *c) {
    //UNUSED s16 pitch = sCUpCameraPitch;
    //UNUSED s16 yaw = sModeOffsetYaw;

    sCUpCameraPitch += qtrunc(q(gPlayer1Controller->stickY) * (q32) 10);
    sModeOffsetYaw -= qtrunc(q(gPlayer1Controller->stickX) * (q32) 10);

    // Bound looking up to nearly 80 degrees.
    if (sCUpCameraPitch > 0x38E3) {
        sCUpCameraPitch = 0x38E3;
    }
    // Bound looking down to -45 degrees
    if (sCUpCameraPitch < -0x2000) {
        sCUpCameraPitch = -0x2000;
    }

    // Bound the camera yaw to +-120 degrees
    if (sModeOffsetYaw > 0x5555) {
        sModeOffsetYaw = 0x5555;
    }
    if (sModeOffsetYaw < -0x5555) {
        sModeOffsetYaw = -0x5555;
    }

    // Give Mario's neck natural-looking constraints
    sMarioCamState->headRotation[0] = sCUpCameraPitch * 3 / 4;
    sMarioCamState->headRotation[1] = sModeOffsetYaw * 3 / 4;
}

/**
 * Zooms the camera in for C-Up mode
 */
void move_into_c_up(struct Camera *c) {
    struct LinearTransitionPoint *start = &sModeInfo.transitionStart;
    struct LinearTransitionPoint *end = &sModeInfo.transitionEnd;

    q32 distq = end->distq - start->distq;
    s16 pitch = end->pitch - start->pitch;
    s16 yaw   = end->yaw   - start->yaw;

    // Linearly interpolate from start to end position's polar coordinates
    distq = start->distq + distq * sModeInfo.frame / sModeInfo.max;
    pitch = start->pitch + pitch * sModeInfo.frame / sModeInfo.max;
    yaw   = start->yaw   + yaw   * sModeInfo.frame / sModeInfo.max;

    // Linearly interpolate the focus from start to end
    c->focusq[0] = start->focusq[0] + (end->focusq[0] - start->focusq[0]) * sModeInfo.frame / sModeInfo.max;
    c->focusq[1] = start->focusq[1] + (end->focusq[1] - start->focusq[1]) * sModeInfo.frame / sModeInfo.max;
    c->focusq[2] = start->focusq[2] + (end->focusq[2] - start->focusq[2]) * sModeInfo.frame / sModeInfo.max;

    vec3q_add(c->focusq, sMarioCamState->posq);
    vec3q_set_dist_and_angle(c->focusq, c->posq, distq, pitch, yaw);

    sMarioCamState->headRotation[0] = 0;
    sMarioCamState->headRotation[1] = 0;

    // Finished zooming in
    if (++sModeInfo.frame == sModeInfo.max) {
        gCameraMovementFlags &= ~CAM_MOVING_INTO_MODE;
    }
}

/**
 * The main update function for C-Up mode
 */
s32 mode_c_up_camera(struct Camera *c) {
    //UNUSED u8 unused[12];

    // Play a sound when entering C-Up mode
    if (!(sCameraSoundFlags & CAM_SOUND_C_UP_PLAYED)) {
        play_sound_cbutton_up();
        sCameraSoundFlags |= CAM_SOUND_C_UP_PLAYED;
    }

    // Zoom in first
    if (gCameraMovementFlags & CAM_MOVING_INTO_MODE) {
        gCameraMovementFlags |= CAM_MOVE_C_UP_MODE;
        move_into_c_up(c);
        return 1;
    }

    if (!(gCameraMovementFlags & CAM_MOVE_STARTED_EXITING_C_UP)) {
        // Normal update
        move_mario_head_c_up(c);
        update_c_upq(c, c->focusq, c->posq);
    } else {
        // Exiting C-Up
        if (sStatusFlags & CAM_FLAG_TRANSITION_OUT_OF_C_UP) {
            // Retrieve the previous position and focus
            vec3q_copy(c->posq, sCameraStoreCUp.posq);
            vec3q_add(c->posq, sMarioCamState->posq);
            vec3q_copy(c->focusq, sCameraStoreCUp.focusq);
            vec3q_add(c->focusq, sMarioCamState->posq);
            // Make Mario look forward
            camera_approach_s16_symmetric_bool(&sMarioCamState->headRotation[0], 0, 1024);
            camera_approach_s16_symmetric_bool(&sMarioCamState->headRotation[1], 0, 1024);
        } else {
            // Finished exiting C-Up
            gCameraMovementFlags &= ~(CAM_MOVE_STARTED_EXITING_C_UP | CAM_MOVE_C_UP_MODE);
        }
    }
    sPanDistanceq = 0;

    // Exit C-Up mode
    if (gPlayer1Controller->buttonPressed & (A_BUTTON | B_BUTTON | D_CBUTTONS | L_CBUTTONS | R_CBUTTONS)) {
        exit_c_up(c);
    }
    return 0;
}

/**
 * Used when Mario is in a cannon.
 */
s32 update_in_cannonq(UNUSED struct Camera *c, Vec3q focusq, Vec3q posq) {
    focus_on_marioq(posq, focusq, q(125) + sCannonYOffsetq, q(125), q(800),
                                    sMarioCamState->faceAngle[0], sMarioCamState->faceAngle[1]);
    return sMarioCamState->faceAngle[1];
}

/**
 * Updates the camera when Mario is in a cannon.
 * sCannonYOffset is used to make the camera rotate down when Mario has just entered the cannon
 */
void mode_cannon_camera(struct Camera *c) {
    sLakituPitch = 0;
    gCameraMovementFlags &= ~CAM_MOVING_INTO_MODE;
    c->nextYaw = update_in_cannonq(c, c->focusq, c->posq);
    if (gPlayer1Controller->buttonPressed & A_BUTTON) {
        set_camera_mode(c, CAMERA_MODE_BEHIND_MARIO, 1);
        sPanDistanceq = 0;
        sCannonYOffsetq = 0;
        sStatusFlags &= ~CAM_FLAG_BLOCK_SMOOTH_MOVEMENT;
    } else {
        sCannonYOffsetq = approach_q32(sCannonYOffsetq, 0, q(100), q(100));
    }
}

/**
 * Cause Lakitu to fly to the next Camera position and focus over a number of frames.
 *
 * At the end of each frame, Lakitu's position and focus ("state") are stored.
 * Calling this function makes next_lakitu_state() fly from the last frame's state to the
 * current frame's calculated state.
 *
 * @see next_lakitu_state()
 */
void transition_next_state(UNUSED struct Camera *c, s16 frames) {
    if (!(sStatusFlags & CAM_FLAG_FRAME_AFTER_CAM_INIT)) {
        sStatusFlags |= (CAM_FLAG_START_TRANSITION | CAM_FLAG_TRANSITION_OUT_OF_C_UP);
        sModeTransition.framesLeft = frames;
    }
}

/**
 * Sets the camera mode to `newMode` and initializes sModeTransition with `numFrames` frames
 *
 * Used to change the camera mode to 'level-oriented' modes
 *      namely: RADIAL/OUTWARD_RADIAL, 8_DIRECTIONS, FREE_ROAM, CLOSE, SPIRAL_STAIRS, and SLIDE_HOOT
 */
void transition_to_camera_mode(struct Camera *c, s16 newMode, s16 numFrames) {
    if (c->mode != newMode) {
        sModeInfo.newMode = (newMode != -1) ? newMode : sModeInfo.lastMode;
        sModeInfo.lastMode = c->mode;
        c->mode = sModeInfo.newMode;

        // Clear movement flags that would affect the transition
        gCameraMovementFlags &= (u16)~(CAM_MOVE_RESTRICT | CAM_MOVE_ROTATE);
        if (!(sStatusFlags & CAM_FLAG_FRAME_AFTER_CAM_INIT)) {
            transition_next_state(c, numFrames);
            sCUpCameraPitch = 0;
            sModeOffsetYaw = 0;
            sLakituDist = 0;
            sLakituPitch = 0;
            sAreaYawChange = 0;
            sPanDistanceq = 0;
            sCannonYOffsetq = 0;
        }
    }
}

/**
 * Used to change the camera mode between its default/previous and certain Mario-oriented modes,
 *      namely: C_UP, WATER_SURFACE, CLOSE, and BEHIND_MARIO
 *
 * Stores the current pos and focus in sModeInfo->transitionStart, and
 * stores the next pos and focus into sModeInfo->transitionEnd. These two fields are used in
 * move_into_c_up().
 *
 * @param mode the mode to change to, or -1 to switch to the previous mode
 * @param frames number of frames the transition should last, only used when entering C_UP
 */
void set_camera_mode(struct Camera *c, s16 mode, s16 frames) {
    struct LinearTransitionPoint *start = &sModeInfo.transitionStart;
    struct LinearTransitionPoint *end = &sModeInfo.transitionEnd;

    if (mode == CAMERA_MODE_WATER_SURFACE && gCurrLevelArea == AREA_TTM_OUTSIDE) {
    } else {
        // Clear movement flags that would affect the transition
        gCameraMovementFlags &= (u16)~(CAM_MOVE_RESTRICT | CAM_MOVE_ROTATE);
        gCameraMovementFlags |= CAM_MOVING_INTO_MODE;
        if (mode == CAMERA_MODE_NONE) {
            mode = CAMERA_MODE_CLOSE;
        }
        sCUpCameraPitch = 0;
        sModeOffsetYaw = 0;
        sLakituDist = 0;
        sLakituPitch = 0;
        sAreaYawChange = 0;

        sModeInfo.newMode = (mode != -1) ? mode : sModeInfo.lastMode;
        sModeInfo.lastMode = c->mode;
        sModeInfo.max = frames;
        sModeInfo.frame = 1;

        c->mode = sModeInfo.newMode;
        gLakituState.mode = c->mode;

        vec3q_copy(end->focusq, c->focusq);
        vec3q_sub(end->focusq, sMarioCamState->posq);

        vec3q_copy(end->posq, c->posq);
        vec3q_sub(end->posq, sMarioCamState->posq);

        sAreaYaw = sModeTransitions[sModeInfo.newMode](c, end->focusq, end->posq);

        // End was updated by sModeTransitions
        vec3q_sub(end->focusq, sMarioCamState->posq);
        vec3q_sub(end->posq, sMarioCamState->posq);

        vec3q_copy(start->focusq, gLakituState.curFocusq);
        vec3q_sub(start->focusq, sMarioCamState->posq);

        vec3q_copy(start->posq, gLakituState.curPosq);
        vec3q_sub(start->posq, sMarioCamState->posq);

        vec3q_get_dist_and_angle(start->focusq, start->posq, &start->distq, &start->pitch, &start->yaw);
        vec3q_get_dist_and_angle(end->focusq, end->posq, &end->distq, &end->pitch, &end->yaw);
    }
}

/**
 * Updates Lakitu's position/focus and applies camera shakes.
 */
void update_lakitu(struct Camera *c) {
    struct Surface *floor = NULL;
    Vec3q newPosq;
    Vec3q newFocq;
    q32 distToFloorq;
    s16 newYaw;

    if (!(gCameraMovementFlags & CAM_MOVE_PAUSE_SCREEN)) {
        newYaw = next_lakitu_stateq(newPosq, newFocq, c->posq, c->focusq, sOldPositionq, sOldFocusq,
                                    c->nextYaw);
        set_or_approach_s16_symmetric(&c->yaw, newYaw, sYawSpeed);
        sStatusFlags &= ~CAM_FLAG_UNUSED_CUTSCENE_ACTIVE;

        // Update old state
        vec3q_copy(sOldPositionq, newPosq);
        vec3q_copy(sOldFocusq, newFocq);

        gLakituState.yaw = c->yaw;
        gLakituState.nextYaw = c->nextYaw;
        vec3q_copy(gLakituState.goalPosq, c->posq);
        vec3q_copy(gLakituState.goalFocusq, c->focusq);

        // Simulate Lakitu flying to the new position and turning towards the new focus
        set_or_approach_vec3q_asymptotic(gLakituState.curPosq, newPosq,
                                         gLakituState.posHSpeedq, gLakituState.posVSpeedq,
                                         gLakituState.posHSpeedq);
        set_or_approach_vec3q_asymptotic(gLakituState.curFocusq, newFocq,
                                         gLakituState.focHSpeedq, gLakituState.focVSpeedq,
                                         gLakituState.focHSpeedq);
        // Adjust Lakitu's speed back to normal
        set_or_approach_q32_asymptotic(&gLakituState.focHSpeedq, q(0.8), q(0.05));
        set_or_approach_q32_asymptotic(&gLakituState.focVSpeedq, q(0.3), q(0.05));
        set_or_approach_q32_asymptotic(&gLakituState.posHSpeedq, q(0.3), q(0.05));
        set_or_approach_q32_asymptotic(&gLakituState.posVSpeedq, q(0.3), q(0.05));

        // Turn on smooth movement when it hasn't been blocked for 2 frames
        if (sStatusFlags & CAM_FLAG_BLOCK_SMOOTH_MOVEMENT) {
            sStatusFlags &= ~CAM_FLAG_BLOCK_SMOOTH_MOVEMENT;
        } else {
            sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;
        }

        vec3q_copy(gLakituState.posq, gLakituState.curPosq);
        vec3q_copy(gLakituState.focusq, gLakituState.curFocusq);

        if (c->cutscene) {
            vec3q_add(gLakituState.focusq, sPlayer2FocusOffsetq);
            vec3q_set(sPlayer2FocusOffsetq, 0, 0, 0);
        }

		// seems unused
        //vec3q_get_dist_and_angle(gLakituState.posq, gLakituState.focusq, gLakituState.focusDistanceq,
        //                         &gLakituState.oldPitch, &gLakituState.oldYaw);

        gLakituState.roll = 0;

        // Apply camera shakes
        shake_camera_pitchq(gLakituState.posq, gLakituState.focusq);
        shake_camera_yawq(gLakituState.posq, gLakituState.focusq);
        shake_camera_roll(&gLakituState.roll);
        shake_camera_handheldq(gLakituState.posq, gLakituState.focusq);

        if (sMarioCamState->action == ACT_DIVE && gLakituState.lastFrameAction != ACT_DIVE) {
            set_camera_shake_from_hit(SHAKE_HIT_FROM_BELOW);
        }

        gLakituState.roll += sHandheldShakeRoll;
        gLakituState.roll += gLakituState.keyDanceRoll;

        if (c->mode != CAMERA_MODE_C_UP && c->cutscene == 0) {
            gCheckingSurfaceCollisionsForCamera = TRUE;
            distToFloorq = find_floorq(gLakituState.posq[0],
                                     gLakituState.posq[1] + q(20),
                                     gLakituState.posq[2], &floor);
            if (distToFloorq != q(FLOOR_LOWER_LIMIT)) {
                if (gLakituState.posq[1] < (distToFloorq += q(100))) {
                    gLakituState.posq[1] = distToFloorq;
                } else {
                    gCheckingSurfaceCollisionsForCamera = FALSE;
                }
            }
        }

        vec3q_copy(sModeTransition.marioPosq, sMarioCamState->posq);
    }
    clamp_pitchq(gLakituState.posq, gLakituState.focusq, 0x3E00, -0x3E00);
    gLakituState.mode = c->mode;
    gLakituState.defMode = c->defMode;
}


/**
 * The main camera update function.
 * Gets controller input, checks for cutscenes, handles mode changes, and moves the camera
 */
void update_camera(struct Camera *c) {
    gCamera = c;
    update_camera_hud_status(c);
    if (c->cutscene == 0) {
        // Only process R_TRIG if 'fixed' is not selected in the menu
        if (cam_select_alt_mode(0) == CAM_SELECTION_MARIO) {
            if (gPlayer1Controller->buttonPressed & R_TRIG) {
                if (set_cam_angle(0) == CAM_ANGLE_LAKITU) {
                    set_cam_angle(CAM_ANGLE_MARIO);
                } else {
                    set_cam_angle(CAM_ANGLE_LAKITU);
                }
            }
        }
        play_sound_if_cam_switched_to_lakitu_or_mario();
    }

    // Initialize the camera
    sStatusFlags &= ~CAM_FLAG_FRAME_AFTER_CAM_INIT;
    if (gCameraMovementFlags & CAM_MOVE_INIT_CAMERA) {
        init_camera(c);
        gCameraMovementFlags &= ~CAM_MOVE_INIT_CAMERA;
        sStatusFlags |= CAM_FLAG_FRAME_AFTER_CAM_INIT;
    }

    // Store previous geometry information
    sMarioGeometry.prevFloorHeight = sMarioGeometry.currFloorHeight;
    sMarioGeometry.prevCeilHeight = sMarioGeometry.currCeilHeight;
    sMarioGeometry.prevFloor = sMarioGeometry.currFloor;
    sMarioGeometry.prevCeil = sMarioGeometry.currCeil;
    sMarioGeometry.prevFloorType = sMarioGeometry.currFloorType;
    sMarioGeometry.prevCeilType = sMarioGeometry.currCeilType;

    find_mario_floor_and_ceil(&sMarioGeometry);
    gCheckingSurfaceCollisionsForCamera = TRUE;
    vec3q_copy(c->posq, gLakituState.goalPosq);
    vec3q_copy(c->focusq, gLakituState.goalFocusq);

    c->yaw = gLakituState.yaw;
    c->nextYaw = gLakituState.nextYaw;
    c->mode = gLakituState.mode;
    c->defMode = gLakituState.defMode;

    camera_course_processing(c);
    sCButtonsPressed = find_c_buttons_pressed(sCButtonsPressed, gPlayer1Controller->buttonPressed,
                                              gPlayer1Controller->buttonDown);

    if (c->cutscene != 0) {
        sYawSpeed = 0;
        play_cutscene(c);
        sFramesSinceCutsceneEnded = 0;
    } else {
        // Clear the recent cutscene after 8 frames
        if (gRecentCutscene != 0 && sFramesSinceCutsceneEnded < 8) {
            sFramesSinceCutsceneEnded++;
            if (sFramesSinceCutsceneEnded >= 8) {
                gRecentCutscene = 0;
                sFramesSinceCutsceneEnded = 0;
            }
        }
    }
    // If not in a cutscene, do mode processing
    if (c->cutscene == 0) {
        sYawSpeed = 0x400;

        if (sSelectionFlags & CAM_MODE_MARIO_ACTIVE) {
            switch (c->mode) {
                case CAMERA_MODE_BEHIND_MARIO:
                    mode_behind_mario_camera(c);
                    break;

                case CAMERA_MODE_C_UP:
                    mode_c_up_camera(c);
                    break;

                case CAMERA_MODE_WATER_SURFACE:
                    mode_water_surface_camera(c);
                    break;

                case CAMERA_MODE_INSIDE_CANNON:
                    mode_cannon_camera(c);
                    break;

                default:
                    mode_mario_camera(c);
            }
        } else {
            switch (c->mode) {
                case CAMERA_MODE_BEHIND_MARIO:
                    mode_behind_mario_camera(c);
                    break;

                case CAMERA_MODE_C_UP:
                    mode_c_up_camera(c);
                    break;

                case CAMERA_MODE_WATER_SURFACE:
                    mode_water_surface_camera(c);
                    break;

                case CAMERA_MODE_INSIDE_CANNON:
                    mode_cannon_camera(c);
                    break;

                case CAMERA_MODE_8_DIRECTIONS:
                    mode_8_directions_camera(c);
                    break;

                case CAMERA_MODE_RADIAL:
                    mode_radial_camera(c);
                    break;

                case CAMERA_MODE_OUTWARD_RADIAL:
                    mode_outward_radial_camera(c);
                    break;

                case CAMERA_MODE_CLOSE:
                case CAMERA_MODE_FREE_ROAM:
                    mode_lakitu_camera(c);
                    break;

                case CAMERA_MODE_BOSS_FIGHT:
                    mode_boss_fight_camera(c);
                    break;

                case CAMERA_MODE_PARALLEL_TRACKING:
                    mode_parallel_tracking_camera(c);
                    break;

                case CAMERA_MODE_SLIDE_HOOT:
                    mode_slide_camera(c);
                    break;

                case CAMERA_MODE_FIXED:
                    mode_fixed_camera(c);
                    break;

                case CAMERA_MODE_SPIRAL_STAIRS:
                    mode_spiral_stairs_camera(c);
                    break;
            }
        }
    }
    // Start any Mario-related cutscenes
    start_cutscene(c, get_cutscene_from_mario_status(c));
    //stub_camera_2(c);
    gCheckingSurfaceCollisionsForCamera = FALSE;
    if (gCurrLevelNum != LEVEL_CASTLE) {
        // If fixed camera is selected as the alternate mode, then fix the camera as long as the right
        // trigger is held
        if ((c->cutscene == 0 &&
            (gPlayer1Controller->buttonDown & R_TRIG) && cam_select_alt_mode(0) == CAM_SELECTION_FIXED)
            || (gCameraMovementFlags & CAM_MOVE_FIX_IN_PLACE)
            || (sMarioCamState->action) == ACT_GETTING_BLOWN) {

            // If this is the first frame that R_TRIG is held, play the "click" sound
            if (c->cutscene == 0 && (gPlayer1Controller->buttonPressed & R_TRIG)
                && cam_select_alt_mode(0) == CAM_SELECTION_FIXED) {
                sCameraSoundFlags |= CAM_SOUND_FIXED_ACTIVE;
                play_sound_rbutton_changed();
            }

            // Fixed mode only prevents Lakitu from moving. The camera pos still updates, so
            // Lakitu will fly to his next position as normal whenever R_TRIG is released.
            gLakituState.posHSpeedq = 0;
            gLakituState.posVSpeedq = 0;

            c->nextYaw = calculate_yawq(gLakituState.focusq, gLakituState.posq);
            c->yaw = c->nextYaw;
            gCameraMovementFlags &= ~CAM_MOVE_FIX_IN_PLACE;
        } else {
            // Play the "click" sound when fixed mode is released
            if (sCameraSoundFlags & CAM_SOUND_FIXED_ACTIVE) {
                play_sound_rbutton_changed();
                sCameraSoundFlags &= ~CAM_SOUND_FIXED_ACTIVE;
            }
        }
    } else {
        if ((gPlayer1Controller->buttonPressed & R_TRIG) && cam_select_alt_mode(0) == CAM_SELECTION_FIXED) {
            play_sound_button_change_blocked();
        }
    }

    update_lakitu(c);

    gLakituState.lastFrameAction = sMarioCamState->action;
}

/**
 * Reset all the camera variables to their arcane defaults
 */
void reset_camera(struct Camera *c) {
    //UNUSED s32 unused = 0;
    //UNUSED u8 unused1[16];
    //UNUSED struct LinearTransitionPoint *start = &sModeInfo.transitionStart;
    //UNUSED struct LinearTransitionPoint *end = &sModeInfo.transitionEnd;

    gCamera = c;
    gCameraMovementFlags = 0;
    s2ndRotateFlags = 0;
    sStatusFlags = 0;
    gCutsceneTimer = 0;
    sCutsceneShot = 0;
    gCutsceneObjSpawn = 0;
    gObjCutsceneDone = FALSE;
    gCutsceneFocus = NULL;
    //unused8032CFC8 = 0;
    //unused8032CFCC = 0;
    gSecondCameraFocus = NULL;
    sCButtonsPressed = 0;
    vec3q_copy(sModeTransition.marioPosq, sMarioCamState->posq);
    sModeTransition.framesLeft = 0;
    //unused8032CFCC = -1;
    //unused8032CFC8 = -1;
    gCameraMovementFlags = 0;
    gCameraMovementFlags |= CAM_MOVE_INIT_CAMERA;
    //unused8033B316 = 0;
    sStatusFlags = 0;
    //unused8033B31A = 0;
    sCameraSoundFlags = 0;
    sCUpCameraPitch = 0;
    sModeOffsetYaw = 0;
    sSpiralStairsYawOffset = 0;
    sLakituDist = 0;
    sLakituPitch = 0;
    sAreaYaw = 0;
    sAreaYawChange = 0;
    sPanDistanceq = 0;
    sCannonYOffsetq = 0;
    sZoomAmountq = 0;
    sZeroZoomDistq = 0;
    sBehindMarioSoundTimer = 0;
    sCSideButtonYaw = 0;
    s8DirModeBaseYaw = 0;
    s8DirModeYawOffset = 0;
    c->doorStatus = DOOR_DEFAULT;
    sMarioCamState->headRotation[0] = 0;
    sMarioCamState->headRotation[1] = 0;
    sLuigiCamState->headRotation[0] = 0;
    sLuigiCamState->headRotation[1] = 0;
    sMarioCamState->cameraEvent = 0;
    sMarioCamState->usedObj = NULL;
    gLakituState.shakeMagnitude[0] = 0;
    gLakituState.shakeMagnitude[1] = 0;
    gLakituState.shakeMagnitude[2] = 0;
    //gLakituState.unusedVec2[0] = 0;
    //gLakituState.unusedVec2[1] = 0;
    //gLakituState.unusedVec2[2] = 0;
    //gLakituState.unusedVec1[0] = 0.f;
    //gLakituState.unusedVec1[1] = 0.f;
    //gLakituState.unusedVec1[2] = 0.f;
    gLakituState.lastFrameAction = 0;
    set_fov_function(CAM_FOV_DEFAULT);
    sFOVState.fovq = q(45);
    sFOVState.fovOffsetq = 0;
    sFOVState.unusedIsSleeping = 0;
    sFOVState.shakeAmplitudeq = 0;
    sFOVState.shakePhase = 0;
    sObjectCutscene = 0;
    gRecentCutscene = 0;
    //unused8033B30C = 0;
    //unused8033B310 = 0;
}

void init_camera(struct Camera *c) {
    struct Surface *floor = 0;
    Vec3q marioOffsetq;
    s32 i;

    sCreditsPlayer2Pitch = 0;
    sCreditsPlayer2Yaw = 0;
    gPrevLevel = gCurrLevelArea / 16;
    gCurrLevelArea = gCurrLevelNum * 16 + gCurrentArea->index;
    sSelectionFlags &= CAM_MODE_MARIO_SELECTED;
    sFramesPaused = 0;
    gLakituState.mode = c->mode;
    gLakituState.defMode = c->defMode;
    gLakituState.posHSpeedq = q(0.3);
    gLakituState.posVSpeedq = q(0.3);
    gLakituState.focHSpeedq = q(0.8);
    gLakituState.focHSpeedq = q(0.3); // @bug set focHSpeed back-to-back
    gLakituState.roll = 0;
    gLakituState.keyDanceRoll = 0;
    //gLakituState.unused = 0;
    sStatusFlags &= ~CAM_FLAG_SMOOTH_MOVEMENT;
    vec3q_set(sCastleEntranceOffsetq, 0, 0, 0);
    vec3q_set(sPlayer2FocusOffsetq, 0, 0, 0);
    find_mario_floor_and_ceil(&sMarioGeometry);
    sMarioGeometry.prevFloorHeight = sMarioGeometry.currFloorHeight;
    sMarioGeometry.prevCeilHeight = sMarioGeometry.currCeilHeight;
    sMarioGeometry.prevFloor = sMarioGeometry.currFloor;
    sMarioGeometry.prevCeil = sMarioGeometry.currCeil;
    sMarioGeometry.prevFloorType = sMarioGeometry.currFloorType;
    sMarioGeometry.prevCeilType = sMarioGeometry.currCeilType;
    for (i = 0; i < 32; i++) {
        sCurCreditsSplinePos[i].index = -1;
        sCurCreditsSplineFocus[i].index = -1;
    }
    sCutsceneSplineSegment = 0;
    sCutsceneSplineSegmentProgressq = 0;
    unused8033B6E8 = 0;
    sHandheldShakeIncq = 0;
    sHandheldShakeTimerq = 0;
    sHandheldShakeMag = 0;
    for (i = 0; i < 4; i++) {
        sHandheldShakeSpline[i].index = -1;
    }
    sHandheldShakePitch = 0;
    sHandheldShakeYaw = 0;
    sHandheldShakeRoll = 0;
    c->cutscene = 0;
    marioOffsetq[0] = 0;
    marioOffsetq[1] = q(125);
    marioOffsetq[2] = q(400);

    // Set the camera's starting position or start a cutscene for certain levels
    switch (gCurrLevelNum) {
        // Calls the initial cutscene when you enter Bowser battle levels
        // Note: This replaced an "old" way to call these cutscenes using
        // a camEvent value: CAM_EVENT_BOWSER_INIT
        case LEVEL_BOWSER_1:
#ifndef VERSION_JP
            // Since Bowser 1 has a demo entry, check for it
            // If it is, then set CamAct to the end to directly activate Bowser
            // If it isn't, then start cutscene
            if (gCurrDemoInput == NULL) {
                start_cutscene(c, CUTSCENE_ENTER_BOWSER_ARENA);
            } else if (gSecondCameraFocus != NULL) {
                gSecondCameraFocus->oBowserCamAct = BOWSER_CAM_ACT_END;
            }
#else
            start_cutscene(c, CUTSCENE_ENTER_BOWSER_ARENA);
#endif
            break;
        case LEVEL_BOWSER_2:
            start_cutscene(c, CUTSCENE_ENTER_BOWSER_ARENA);
            break;
        case LEVEL_BOWSER_3:
            start_cutscene(c, CUTSCENE_ENTER_BOWSER_ARENA);
            break;

        //! Hardcoded position checks determine which cutscene to play when Mario enters castle grounds.
        case LEVEL_CASTLE_GROUNDS:
            if (is_within_100_units_of_marioq(q(-1328), q(260), q(4664)) != 1) {
                marioOffsetq[0] = q(-400);
                marioOffsetq[2] = q(-800);
            }
            if (is_within_100_units_of_marioq(q(-6901), q(2376), q(-6509)) == 1) {
                start_cutscene(c, CUTSCENE_EXIT_WATERFALL);
            }
            if (is_within_100_units_of_marioq(q(5408), q(4500), q(3637)) == 1) {
                start_cutscene(c, CUTSCENE_EXIT_FALL_WMOTR);
            }
            gLakituState.mode = CAMERA_MODE_FREE_ROAM;
            break;
        case LEVEL_SA:
            marioOffsetq[2] = q(200);
            break;
        case LEVEL_CASTLE_COURTYARD:
            marioOffsetq[2] = q(-300);
            break;
        case LEVEL_LLL:
            gCameraMovementFlags |= CAM_MOVE_ZOOMED_OUT;
            break;
        case LEVEL_CASTLE:
            marioOffsetq[2] = q(150);
            break;
        case LEVEL_RR:
            vec3q_set(sFixedModeBasePositionq, q(-2985), q(478), q(-5568));
            break;
    }
    if (c->mode == CAMERA_MODE_8_DIRECTIONS) {
        gCameraMovementFlags |= CAM_MOVE_ZOOMED_OUT;
    }
    switch (gCurrLevelArea) {
        case AREA_SSL_EYEROK:
            vec3q_set(marioOffsetq, 0, q(500), q(-100));
            break;
        case AREA_CCM_SLIDE:
        case AREA_THI_WIGGLER:
        case AREA_SL_IGLOO:
            marioOffsetq[2] = q(-300);
            break;
        case AREA_SL_OUTSIDE:
            if (is_within_100_units_of_marioq(q(257), q(2150), q(1399)) == 1) {
                marioOffsetq[2] = q(-300);
            }
            break;
        case AREA_CCM_OUTSIDE:
            gCameraMovementFlags |= CAM_MOVE_ZOOMED_OUT;
            break;
        case AREA_TTM_OUTSIDE:
            gLakituState.mode = CAMERA_MODE_RADIAL;
            break;
    }

    // Set the camera pos to marioOffset (relative to Mario), added to Mario's position
    offset_rotatedq(c->posq, sMarioCamState->posq, marioOffsetq, sMarioCamState->faceAngle);
    if (c->mode != CAMERA_MODE_BEHIND_MARIO) {
        c->posq[1] = find_floorq(sMarioCamState->posq[0], sMarioCamState->posq[1] + q(100),
                               sMarioCamState->posq[2], &floor) + q(125);
    }
    vec3q_copy(c->focusq, sMarioCamState->posq);
    vec3q_copy(gLakituState.curPosq, c->posq);
    vec3q_copy(gLakituState.curFocusq, c->focusq);
    vec3q_copy(gLakituState.goalPosq, c->posq);
    vec3q_copy(gLakituState.goalFocusq, c->focusq);
    vec3q_copy(gLakituState.posq, c->posq);
    vec3q_copy(gLakituState.focusq, c->focusq);
    if (c->mode == CAMERA_MODE_FIXED) {
        set_fixed_cam_axis_sa_lobby(c->mode);
    }
    store_lakitu_cam_info_for_c_up(c);
    gLakituState.yaw = calculate_yawq(c->focusq, c->posq);
    gLakituState.nextYaw = gLakituState.yaw;
    c->yaw = gLakituState.yaw;
    c->nextYaw = gLakituState.yaw;
}

/**
 * Zooms out the camera if paused and the level is 'outside', as determined by sZoomOutAreaMasks.
 *
 * Because gCurrLevelArea is assigned gCurrLevelNum * 16 + gCurrentArea->index,
 * dividing by 32 maps 2 levels to one index.
 *
 * areaBit definition:
 * (gCurrLevelArea & 0x10) / 4):
 *      This adds 4 to the shift if the level is an odd multiple of 16
 *
 * ((gCurrLevelArea & 0xF) - 1) & 3):
 *      This isolates the lower 16 'area' bits, subtracts 1 because areas are 1-indexed, and effectively
 *      modulo-4's the result, because each 8-bit mask only has 4 area bits for each level
 */
void zoom_out_if_paused_and_outside(struct GraphNodeCamera *camera) {
    s16 pitch, yaw;
	q32 distq;
    s32 areaMaskIndex = gCurrLevelArea / 32;
    s32 areaBit = 1 << (((gCurrLevelArea & 0x10) / 4) + (((gCurrLevelArea & 0xF) - 1) & 3));

    if (areaMaskIndex >= LEVEL_MAX / 2) {
        areaMaskIndex = 0;
        areaBit = 0;
    }
    if (gCameraMovementFlags & CAM_MOVE_PAUSE_SCREEN) {
        if (sFramesPaused >= 2) {
            if (sZoomOutAreaMasks[areaMaskIndex] & areaBit) {
                camera->focusq[0] = gCamera->areaCenXq;
                camera->focusq[1] = (sMarioCamState->posq[1] + gCamera->areaCenYq) / 2;
                camera->focusq[2] = gCamera->areaCenZq;
                vec3q_get_dist_and_angle(camera->focusq, sMarioCamState->posq, &distq, &pitch, &yaw);
                vec3q_set_dist_and_angle(sMarioCamState->posq, camera->posq, q(6000), 0x1000, yaw);
                if (gCurrLevelNum != LEVEL_THI) {
                    find_in_bounds_yaw_wdw_bob_thiq(camera->posq, camera->focusq, 0);
                }
            }
        } else {
            sFramesPaused++;
        }
    } else {
        sFramesPaused = 0;
    }
}

void select_mario_cam_mode(void) {
    sSelectionFlags = CAM_MODE_MARIO_SELECTED;
}

/**
 * Allocate the GraphNodeCamera's config.camera, and copy `c`'s focus to the Camera's area center point.
 */
void create_camera(struct GraphNodeCamera *gc, struct AllocOnlyPool *pool) {
    s16 mode = gc->config.mode;
    struct Camera *c = alloc_only_pool_alloc(pool, sizeof(struct Camera));

    gc->config.camera = c;
    c->mode = mode;
    c->defMode = mode;
    c->cutscene = 0;
    c->doorStatus = DOOR_DEFAULT;
    c->areaCenXq = gc->focusq[0];
    c->areaCenYq = gc->focusq[1];
    c->areaCenZq = gc->focusq[2];
    c->yaw = 0;
    vec3q_copy(c->posq, gc->posq);
    vec3q_copy(c->focusq, gc->focusq);
}

/**
 * Copy Lakitu's pos and foc into `gc`
 */
void update_graph_node_camera(struct GraphNodeCamera *gc) {
    gc->rollScreen = gLakituState.roll;
    vec3q_copy(gc->posq, gLakituState.posq);
    vec3q_copy(gc->focusq, gLakituState.focusq);
    zoom_out_if_paused_and_outside(gc);
}

Gfx *geo_camera_main(s32 callContext, struct GraphNode *g, void *context) {
    struct GraphNodeCamera *gc = (struct GraphNodeCamera *) g;

    switch (callContext) {
        case GEO_CONTEXT_CREATE:
            create_camera(gc, context);
            break;
        case GEO_CONTEXT_RENDER:
            update_graph_node_camera(gc);
            break;
    }
    return NULL;
}

void object_pos_to_vec3q(Vec3q dstq, struct Object *o) {
    dstq[0] = QFIELD(o, oPosX);
    dstq[1] = QFIELD(o, oPosY);
    dstq[2] = QFIELD(o, oPosZ);
}

void vec3q_to_object_pos(struct Object *o, Vec3q srcq) {
    QSETFIELD(o, oPosX, srcq[0]);
    QSETFIELD(o, oPosY, srcq[1]);
    QSETFIELD(o, oPosZ, srcq[2]);
}

//void unused_object_angle_to_vec3s(Vec3s dst, struct Object *o) {
//    dst[0] = o->oMoveAnglePitch;
//    dst[1] = o->oMoveAngleYaw;
//    dst[2] = o->oMoveAngleRoll;
//}

/**
 * Produces values using a cubic b-spline curve. Basically Q is the used output,
 * u is a value between 0 and 1 that represents the position along the spline,
 * and a0-a3 are parameters that define the spline.
 *
 * The spline is described at www2.cs.uregina.ca/~anima/408/Notes/Interpolation/UniformBSpline.htm
 */
void evaluate_cubic_splineq(q32 uq, Vec3q Qq, Vec3q a0q, Vec3q a1q, Vec3q a2q, Vec3q a3q) {
    q32 Bq[4];
    //q32 xq;
    //q32 yq;
    //q32 zq;

    if (uq > QONE) {
        uq = QONE;
    }

    Bq[0] = qmul3((QONE - uq), (QONE - uq), (QONE - uq)) / 6;
    Bq[1] = qmul3(uq, uq, uq) / 2 - qmul(uq, uq) + q(0.6666667);
    Bq[2] = qmul3(-uq, uq, uq) / 2 + qmul(uq, uq) / 2 + uq / 2 + q(0.16666667);
    Bq[3] = qmul3(uq, uq, uq) / 6;

    Qq[0] = qmul(Bq[0], a0q[0]) + qmul(Bq[1], a1q[0]) + qmul(Bq[2], a2q[0]) + qmul(Bq[3], a3q[0]);
    Qq[1] = qmul(Bq[0], a0q[1]) + qmul(Bq[1], a1q[1]) + qmul(Bq[2], a2q[1]) + qmul(Bq[3], a3q[1]);
    Qq[2] = qmul(Bq[0], a0q[2]) + qmul(Bq[1], a1q[2]) + qmul(Bq[2], a2q[2]) + qmul(Bq[3], a3q[2]);

    //// Unused code
    //B[0] = -0.5f * u * u + u - 0.33333333f;
    //B[1] = 1.5f * u * u - 2.f * u - 0.5f;
    //B[2] = -1.5f * u * u + u + 1.f;
    //B[3] = 0.5f * u * u - 0.16666667f;

    //x = B[0] * a0[0] + B[1] * a1[0] + B[2] * a2[0] + B[3] * a3[0];
    //y = B[0] * a0[1] + B[1] * a1[1] + B[2] * a2[1] + B[3] * a3[1];
    //z = B[0] * a0[2] + B[1] * a1[2] + B[2] * a2[2] + B[3] * a3[2];

    //unusedSplinePitch = atan2s(sqrtf(x * x + z * z), y);
    //unusedSplineYaw = atan2s(z, x);
}

/**
 * Computes the point that is `progress` percent of the way through segment `splineSegment` of `spline`,
 * and stores the result in `p`. `progress` and `splineSegment` are updated if `progress` becomes >= 1.0.
 *
 * When neither of the next two points' speeds == 0, the number of frames is between 1 and 255. Otherwise
 * it's infinite.
 *
 * To calculate the number of frames it will take to progress through a spline segment:
 * If the next two speeds are the same and nonzero, it's 1.0 / firstSpeed.
 *
 * s1 and s2 are short hand for first/secondSpeed. The progress at any frame n is defined by a recurrency relation:
 *      p(n+1) = (s2 - s1 + 1) * p(n) + s1
 * Which can be written as
 *      p(n) = (s2 * ((s2 - s1 + 1)^(n) - 1)) / (s2 - s1)
 *
 * Solving for the number of frames:
 *      n = log(((s2 - s1) / s1) + 1) / log(s2 - s1 + 1)
 *
 * @return 1 if the point has reached the end of the spline, when `progress` reaches 1.0 or greater, and
 * the 4th CutsceneSplinePoint in the current segment away from spline[splineSegment] has an index of -1.
 */
s32 move_point_along_splineq(Vec3q pq, struct CutsceneSplinePoint spline[], s16 *splineSegment, q32 *progressq) {
    s32 finished = 0;
    Vec3q controlPointsq[4];
    s32 i = 0;
    q32 uq = *progressq;
    q32 progressChangeq;
    q32 firstSpeedq = 0;
    q32 secondSpeedq = 0;
    s32 segment = *splineSegment;

    if (*splineSegment < 0) {
        segment = 0;
        uq = 0;
    }
    if (spline[segment].index == -1 || spline[segment + 1].index == -1 || spline[segment + 2].index == -1) {
        return 1;
    }

    for (i = 0; i < 4; i++) {
        controlPointsq[i][0] = q(spline[segment + i].point[0]);
        controlPointsq[i][1] = q(spline[segment + i].point[1]);
        controlPointsq[i][2] = q(spline[segment + i].point[2]);
    }
    evaluate_cubic_splineq(uq, pq, controlPointsq[0], controlPointsq[1], controlPointsq[2], controlPointsq[3]);

    if (spline[*splineSegment + 1].speed != 0) {
        firstSpeedq = QONE / spline[*splineSegment + 1].speed;
    }
    if (spline[*splineSegment + 2].speed != 0) {
        secondSpeedq = QONE / spline[*splineSegment + 2].speed;
    }
    progressChangeq = qmul(secondSpeedq - firstSpeedq, *progressq) + firstSpeedq;

#ifdef VERSION_EU
    if (gCamera->cutscene == CUTSCENE_INTRO_PEACH) {
        progressChangeq += qmul(progressChangeq, q(0.19));
    }
    if (gCamera->cutscene == CUTSCENE_CREDITS) {
        progressChangeq += qmul(progressChangeq, q(0.15));
    }
    if (gCamera->cutscene == CUTSCENE_ENDING) {
        progressChangeq += qmul(progressChangeq, q(0.1));
    }
#endif

    if (QONE <= (*progressq += progressChangeq)) {
        (*splineSegment)++;
        if (spline[*splineSegment + 3].index == -1) {
            *splineSegment = 0;
            finished = 1;
        }
        *progressq -= QONE;
    }
    return finished;
}

/**
 * If `selection` is 0, just get the current selection
 * If `selection` is 1, select 'Mario' as the alt mode.
 * If `selection` is 2, select 'fixed' as the alt mode.
 *
 * @return the current selection
 */
s32 cam_select_alt_mode(s32 selection) {
    s32 mode = CAM_SELECTION_FIXED;

    if (selection == CAM_SELECTION_MARIO) {
        if (!(sSelectionFlags & CAM_MODE_MARIO_SELECTED)) {
            sSelectionFlags |= CAM_MODE_MARIO_SELECTED;
        }
        sCameraSoundFlags |= CAM_SOUND_UNUSED_SELECT_MARIO;
    }

    // The alternate mode is up-close, but the player just selected fixed in the pause menu
    if (selection == CAM_SELECTION_FIXED && (sSelectionFlags & CAM_MODE_MARIO_SELECTED)) {
        // So change to normal mode in case the user paused in up-close mode
        set_cam_angle(CAM_ANGLE_LAKITU);
        sSelectionFlags &= ~CAM_MODE_MARIO_SELECTED;
        sCameraSoundFlags |= CAM_SOUND_UNUSED_SELECT_FIXED;
    }

    if (sSelectionFlags & CAM_MODE_MARIO_SELECTED) {
        mode = CAM_SELECTION_MARIO;
    }
    return mode;
}

/**
 * Sets the camera angle to either Lakitu or Mario mode. Returns the current mode.
 *
 * If `mode` is 0, just returns the current mode.
 * If `mode` is 1, start Mario mode
 * If `mode` is 2, start Lakitu mode
 */
s32 set_cam_angle(s32 mode) {
    s32 curMode = CAM_ANGLE_LAKITU;

    // Switch to Mario mode
    if (mode == CAM_ANGLE_MARIO && !(sSelectionFlags & CAM_MODE_MARIO_ACTIVE)) {
        sSelectionFlags |= CAM_MODE_MARIO_ACTIVE;
        if (gCameraMovementFlags & CAM_MOVE_ZOOMED_OUT) {
            sSelectionFlags |= CAM_MODE_LAKITU_WAS_ZOOMED_OUT;
            gCameraMovementFlags &= ~CAM_MOVE_ZOOMED_OUT;
        }
        sCameraSoundFlags |= CAM_SOUND_MARIO_ACTIVE;
    }

    // Switch back to normal mode
    if (mode == CAM_ANGLE_LAKITU && (sSelectionFlags & CAM_MODE_MARIO_ACTIVE)) {
        sSelectionFlags &= ~CAM_MODE_MARIO_ACTIVE;
        if (sSelectionFlags & CAM_MODE_LAKITU_WAS_ZOOMED_OUT) {
            sSelectionFlags &= ~CAM_MODE_LAKITU_WAS_ZOOMED_OUT;
            gCameraMovementFlags |= CAM_MOVE_ZOOMED_OUT;
        } else {
            gCameraMovementFlags &= ~CAM_MOVE_ZOOMED_OUT;
        }
        sCameraSoundFlags |= CAM_SOUND_NORMAL_ACTIVE;
    }
    if (sSelectionFlags & CAM_MODE_MARIO_ACTIVE) {
        curMode = CAM_ANGLE_MARIO;
    }
    return curMode;
}

/**
 * Enables the handheld shake effect for this frame.
 *
 * @see shake_camera_handheld()
 */
void set_handheld_shake(u8 mode) {
    switch (mode) {
        // They're not in numerical order because that would be too simple...
        case HAND_CAM_SHAKE_CUTSCENE: // Lowest increment
            sHandheldShakeMag = 0x600;
            sHandheldShakeIncq = q(0.04);
            break;
        case HAND_CAM_SHAKE_LOW: // Lowest magnitude
            sHandheldShakeMag = 0x300;
            sHandheldShakeIncq = q(0.06);
            break;
        case HAND_CAM_SHAKE_HIGH: // Highest mag and inc
            sHandheldShakeMag = 0x1000;
            sHandheldShakeIncq = q(0.1);
            break;
        //case HAND_CAM_SHAKE_UNUSED: // Never used
        case HAND_CAM_SHAKE_HANG_OWL: // exactly the same as UNUSED...
            sHandheldShakeMag = 0x600;
            sHandheldShakeIncq = q(0.07);
            break;
        case HAND_CAM_SHAKE_STAR_DANCE: // Slightly steadier than HANG_OWL and UNUSED
            sHandheldShakeMag = 0x400;
            sHandheldShakeIncq = q(0.07);
            break;
        default:
            sHandheldShakeMag = 0x0;
            sHandheldShakeIncq = 0;
    }
}

/**
 * When sHandheldShakeMag is nonzero, this function adds small random offsets to `focus` every time
 * sHandheldShakeTimer increases above 1.0, simulating the camera shake caused by unsteady hands.
 *
 * This function must be called every frame in order to actually apply the effect, since the effect's
 * mag and inc are set to 0 every frame at the end of this function.
 */
void shake_camera_handheldq(Vec3q posq, Vec3q focusq) {
    s32 i;
    Vec3q shakeOffsetq;
    Vec3q shakeSplineq[4];
    q32 distq;
    s16 pitch;
    s16 yaw;

    if (sHandheldShakeMag == 0) {
        vec3q_set(shakeOffsetq, 0, 0, 0);
    } else {
        for (i = 0; i < 4; i++) {
            shakeSplineq[i][0] = q(sHandheldShakeSpline[i].point[0]);
            shakeSplineq[i][1] = q(sHandheldShakeSpline[i].point[1]);
            shakeSplineq[i][2] = q(sHandheldShakeSpline[i].point[2]);
        }
        evaluate_cubic_splineq(sHandheldShakeTimerq, shakeOffsetq, shakeSplineq[0],
                              shakeSplineq[1], shakeSplineq[2], shakeSplineq[3]);
        if (QONE <= (sHandheldShakeTimerq += sHandheldShakeIncq)) {
            // The first 3 control points are always (0,0,0), so the random spline is always just a
            // straight line
            for (i = 0; i < 3; i++) {
                vec3s_copy(sHandheldShakeSpline[i].point, sHandheldShakeSpline[i + 1].point);
            }
            random_vec3s(sHandheldShakeSpline[3].point, sHandheldShakeMag, sHandheldShakeMag, sHandheldShakeMag / 2);
            sHandheldShakeTimerq -= QONE;

            // Code dead, this is set to be 0 before it is used.
            //sHandheldShakeIncq = random_q32() / 2;
            //if (sHandheldShakeIncq < q(0.02)) {
            //    sHandheldShakeIncq = q(0.02);
            //}
        }
    }

    approach_s16_asymptotic_bool(&sHandheldShakePitch, qtrunc(shakeOffsetq[0]), 0x08);
    approach_s16_asymptotic_bool(&sHandheldShakeYaw, qtrunc(shakeOffsetq[1]), 0x08);
    approach_s16_asymptotic_bool(&sHandheldShakeRoll, qtrunc(shakeOffsetq[2]), 0x08);

    if (sHandheldShakePitch | sHandheldShakeYaw) {
        vec3q_get_dist_and_angle(posq, focusq, &distq, &pitch, &yaw);
        pitch += sHandheldShakePitch;
        yaw += sHandheldShakeYaw;
        vec3q_set_dist_and_angle(posq, focusq, distq, pitch, yaw);
    }

    // Unless called every frame, the effect will stop after the first time.
    sHandheldShakeMag = 0;
    sHandheldShakeIncq = 0;
}

/**
 * Updates C Button input state and stores it in `currentState`
 */
s32 find_c_buttons_pressed(u16 currentState, u16 buttonsPressed, u16 buttonsDown) {
    buttonsPressed &= CBUTTON_MASK;
    buttonsDown &= CBUTTON_MASK;

    if (buttonsPressed & L_CBUTTONS) {
        currentState |= L_CBUTTONS;
        currentState &= ~R_CBUTTONS;
    }
    if (!(buttonsDown & L_CBUTTONS)) {
        currentState &= ~L_CBUTTONS;
    }

    if (buttonsPressed & R_CBUTTONS) {
        currentState |= R_CBUTTONS;
        currentState &= ~L_CBUTTONS;
    }
    if (!(buttonsDown & R_CBUTTONS)) {
        currentState &= ~R_CBUTTONS;
    }

    if (buttonsPressed & U_CBUTTONS) {
        currentState |= U_CBUTTONS;
        currentState &= ~D_CBUTTONS;
    }
    if (!(buttonsDown & U_CBUTTONS)) {
        currentState &= ~U_CBUTTONS;
    }

    if (buttonsPressed & D_CBUTTONS) {
        currentState |= D_CBUTTONS;
        currentState &= ~U_CBUTTONS;
    }
    if (!(buttonsDown & D_CBUTTONS)) {
        currentState &= ~D_CBUTTONS;
    }

    return currentState;
}

/**
 * Determine which icon to show on the HUD
 */
s32 update_camera_hud_status(struct Camera *c) {
    s16 status = CAM_STATUS_NONE;

    if (c->cutscene != 0
        || ((gPlayer1Controller->buttonDown & R_TRIG) && cam_select_alt_mode(0) == CAM_SELECTION_FIXED)) {
        status |= CAM_STATUS_FIXED;
    } else if (set_cam_angle(0) == CAM_ANGLE_MARIO) {
        status |= CAM_STATUS_MARIO;
    } else {
        status |= CAM_STATUS_LAKITU;
    }
    if (gCameraMovementFlags & CAM_MOVE_ZOOMED_OUT) {
        status |= CAM_STATUS_C_DOWN;
    }
    if (gCameraMovementFlags & CAM_MOVE_C_UP_MODE) {
        status |= CAM_STATUS_C_UP;
    }
    set_hud_camera_status(status);
    return status;
}

/**
 * Check `pos` for collisions within `radius`, and update `pos`
 *
 * @return the number of collisions found
 */
s32 collide_with_wallsq(Vec3q posq, q32 offsetYq, q32 radiusq) {
    struct WallCollisionData collisionData;
    struct Surface *wall = NULL;
    q32 normXq;
    q32 normYq;
    q32 normZq;
    q32 originOffsetq;
    q32 offsetq;
    q32 offsetAbsoluteq;
    Vec3q newPosq[4];
    s32 i;
    s32 numCollisions = 0;

    collisionData.xq = posq[0];
    collisionData.yq = posq[1];
    collisionData.zq = posq[2];
    collisionData.radiusq = radiusq;
    collisionData.offsetYq = offsetYq;
    numCollisions = find_wall_collisions(&collisionData);
    if (numCollisions != 0) {
        for (i = 0; i < collisionData.numWalls; i++) {
            wall = collisionData.walls[collisionData.numWalls - 1];
            vec3q_copy(newPosq[i], posq);
            normXq = (q32) wall->compressed_normal.x * QONE / COMPRESSED_NORMAL_ONE;
            normYq = (q32) wall->compressed_normal.y * QONE / COMPRESSED_NORMAL_ONE;
            normZq = (q32) wall->compressed_normal.z * QONE / COMPRESSED_NORMAL_ONE;
            originOffsetq = wall->originOffsetq;
            offsetq = qmul(normXq, newPosq[i][0]) + qmul(normYq, newPosq[i][1]) + qmul(normZq, newPosq[i][2]) + originOffsetq;
            offsetAbsoluteq = ABS2(offsetq);
            if (offsetAbsoluteq < radiusq) {
                newPosq[i][0] += qmul(normXq, radiusq - offsetq);
                newPosq[i][2] += qmul(normZq, radiusq - offsetq);
                vec3q_copy(posq, newPosq[i]);
            }
        }
    }
    return numCollisions;
}

/**
 * Compare a vector to a position, return TRUE if they match.
 */
s32 vec3q_compare(Vec3q posq, q32 posXq, q32 posYq, q32 posZq) {
    s32 equal = FALSE;

    if (posq[0] == posXq && posq[1] == posYq && posq[2] == posZq) {
        equal = TRUE;
    }
    return equal;
}

s32 clamp_pitchq(Vec3q fromq, Vec3q toq, s16 maxPitch, s16 minPitch) {
    s32 outOfRange = 0;
    s16 pitch;
    s16 yaw;
    q32 distq;

    vec3q_get_dist_and_angle(fromq, toq, &distq, &pitch, &yaw);
    if (pitch > maxPitch) {
        pitch = maxPitch;
        outOfRange++;
    }
    if (pitch < minPitch) {
        pitch = minPitch;
        outOfRange++;
    }
    vec3q_set_dist_and_angle(fromq, toq, distq, pitch, yaw);
    return outOfRange;
}

s32 is_within_100_units_of_marioq(q32 posXq, q32 posYq, q32 posZq) {
    s32 isCloseToMario = 0;
    Vec3q posq;

    vec3q_set(posq, posXq, posYq, posZq);
    if (calc_abs_distq(sMarioCamState->posq, posq) < q(100)) {
        isCloseToMario = 1;
    }
    return isCloseToMario;
}

s32 set_or_approach_q32_asymptotic(q32 *dstq, q32 goalq, q32 scaleq) {
    if (sStatusFlags & CAM_FLAG_SMOOTH_MOVEMENT) {
        approach_q32_asymptotic_bool(dstq, goalq, scaleq);
    } else {
        *dstq = goalq;
    }
    if (*dstq == goalq) {
        return FALSE;
    } else {
        return TRUE;
    }
}

/**
 * Approaches an f32 value by taking the difference between the target and current value
 * and adding a fraction of that to the current value.
 * Edits the current value directly, returns TRUE if the target has been reached, FALSE otherwise.
 */
s32 approach_q32_asymptotic_bool(q32 *currentq, q32 targetq, q32 multiplierq) {
    if (multiplierq > QONE) {
        multiplierq = QONE;
    }
    *currentq += (q64) (targetq - *currentq) * multiplierq / QONE;
    return *currentq != targetq;
}

/**
 * Nearly the same as the above function, returns new value instead.
 */
q32 approach_q32_asymptotic(q32 currentq, q32 targetq, q32 multiplierq) {
    return currentq + (q64) (targetq - currentq) * multiplierq / QONE;
}

/**
 * Approaches an s16 value in the same fashion as approach_f32_asymptotic_bool, returns TRUE if target
 * is reached. Note: Since this function takes integers as parameters, the last argument is the
 * reciprocal of what it would be in the previous two functions.
 */
s32 approach_s16_asymptotic_bool(s16 *current, s16 target, s16 divisor) {
    s16 temp = *current;

    if (divisor == 0) {
        *current = target;
    } else {
        temp -= target;
        temp -= temp / divisor;
        temp += target;
        *current = temp;
    }
    if (*current == target) {
        return FALSE;
    } else {
        return TRUE;
    }
}

/**
 * Approaches an s16 value in the same fashion as approach_f32_asymptotic, returns the new value.
 * Note: last parameter is the reciprocal of what it would be in the f32 functions
 */
s32 approach_s16_asymptotic(s16 current, s16 target, s16 divisor) {
    s16 temp = current;

    if (divisor == 0) {
        current = target;
    } else {
        temp -= target;
        temp -= temp / divisor;
        temp += target;
        current = temp;
    }
    return current;
}

/**
 * Applies the approach_f32_asymptotic_bool function to each of the X, Y, & Z components of the given
 * vector.
 */
void approach_vec3q_asymptotic(Vec3q currentq, Vec3q targetq, q32 xMulq, q32 yMulq, q32 zMulq) {
    approach_q32_asymptotic_bool(&currentq[0], targetq[0], xMulq);
    approach_q32_asymptotic_bool(&currentq[1], targetq[1], yMulq);
    approach_q32_asymptotic_bool(&currentq[2], targetq[2], zMulq);
}

/**
 * Applies the set_or_approach_f32_asymptotic_bool function to each of the X, Y, & Z components of the
 * given vector.
 */
void set_or_approach_vec3q_asymptotic(Vec3q dstq, Vec3q goalq, q32 xMulq, q32 yMulq, q32 zMulq) {
    set_or_approach_q32_asymptotic(&dstq[0], goalq[0], xMulq);
    set_or_approach_q32_asymptotic(&dstq[1], goalq[1], yMulq);
    set_or_approach_q32_asymptotic(&dstq[2], goalq[2], zMulq);
}

/**
 * Applies the approach_s32_asymptotic function to each of the X, Y, & Z components of the given
 * vector.
 */
void approach_vec3s_asymptotic(Vec3s current, Vec3s target, s16 xMul, s16 yMul, s16 zMul) {
    approach_s16_asymptotic_bool(&current[0], target[0], xMul);
    approach_s16_asymptotic_bool(&current[1], target[1], yMul);
    approach_s16_asymptotic_bool(&current[2], target[2], zMul);
}

s32 camera_approach_s16_symmetric_bool(s16 *current, s16 target, s16 increment) {
    s16 dist = target - *current;

    if (increment < 0) {
        increment = -1 * increment;
    }
    if (dist > 0) {
        dist -= increment;
        if (dist >= 0) {
            *current = target - dist;
        } else {
            *current = target;
        }
    } else {
        dist += increment;
        if (dist <= 0) {
            *current = target - dist;
        } else {
            *current = target;
        }
    }
    if (*current == target) {
        return FALSE;
    } else {
        return TRUE;
    }
}

s32 camera_approach_s16_symmetric(s16 current, s16 target, s16 increment) {
    s16 dist = target - current;

    if (increment < 0) {
        increment = -1 * increment;
    }
    if (dist > 0) {
        dist -= increment;
        if (dist >= 0) {
            current = target - dist;
        } else {
            current = target;
        }
    } else {
        dist += increment;
        if (dist <= 0) {
            current = target - dist;
        } else {
            current = target;
        }
    }
    return current;
}

s32 set_or_approach_s16_symmetric(s16 *current, s16 target, s16 increment) {
    if (sStatusFlags & CAM_FLAG_SMOOTH_MOVEMENT) {
        camera_approach_s16_symmetric_bool(current, target, increment);
    } else {
        *current = target;
    }
    if (*current == target) {
        return FALSE;
    } else {
        return TRUE;
    }
}

/**
 * Approaches a value by a given increment, returns FALSE if the target is reached.
 * Appears to be a strange way of implementing approach_f32_symmetric from object_helpers.c.
 * It could possibly be an older version of the function
 */
s32 camera_approach_q32_symmetric_bool(q32 *currentq, q32 targetq, q32 incrementq) {
	q32 new = camera_approach_q32_symmetric(*currentq, targetq, incrementq);
	*currentq = new;
	return new != targetq;
}

/**
 * Nearly the same as the above function, this one returns the new value in place of a bool.
 */
q32 camera_approach_q32_symmetric(q32 currentq, q32 targetq, q32 incrementq) {
	q32 curq = currentq;
	if(curq == targetq) {
		return curq;
	}
	if(incrementq < 0) {
		incrementq = -incrementq;
	} else if(incrementq == 0) {
		incrementq = 1;
	}
	if(curq < targetq) {
		curq += incrementq;
		if(curq >= targetq) {
			return currentq = targetq;
		}
	} else {
		curq -= incrementq;
		if(curq <= targetq) {
			return currentq = targetq;
		}
	}
	return currentq = curq;
}

/**
 * Generate a vector with all three values about zero. The
 * three ranges determine how wide the range about zero.
 */
void random_vec3s(Vec3s dst, s16 xRange, s16 yRange, s16 zRange) {
    dst[0] = (random_u16() % xRange) - xRange / 2;
    dst[1] = (random_u16() % yRange) - yRange / 2;
    dst[2] = (random_u16() % zRange) - zRange / 2;
}

/**
 * Decrease value by multiplying it by the distance from (`posX`, `posY`, `posZ`) to
 * the camera divided by `maxDist`
 *
 * @return the reduced value
 */
s16 reduce_by_dist_from_cameraq(s16 value, q32 maxDistq, q32 posXq, q32 posYq, q32 posZq) {
    Vec3q posq;
    q32 distq;
    s16 pitch;
    s16 yaw;
    s16 goalPitch;
    s16 goalYaw;
    s16 result = 0;
    // Direction from pos to (Lakitu's) goalPos
    q32 goalDXq = gLakituState.goalPosq[0] - posXq;
    q32 goalDYq = gLakituState.goalPosq[1] - posYq;
    q32 goalDZq = gLakituState.goalPosq[2] - posZq;

    distq = sqrtq(qmul(goalDXq, goalDXq) + qmul(goalDYq, goalDYq) + qmul(goalDZq, goalDZq));
    if (maxDistq > distq) {
        posq[0] = posXq;
        posq[1] = posYq;
        posq[2] = posZq;
        vec3q_get_dist_and_angle(gLakituState.goalPosq, posq, &distq, &pitch, &yaw);
        if (distq < maxDistq) {
            calculate_anglesq(gLakituState.goalPosq, gLakituState.goalFocusq, &goalPitch, &goalYaw);
            //! Must be same line to match on -O2
            pitch -= goalPitch; yaw -= goalYaw;
            distq -= q(2000);
            if (distq < 0) {
                distq = 0;
            }
            maxDistq -= q(2000);
            if (maxDistq < q(2000)) {
                maxDistq = q(2000);
            }
            result = value * (QONE - qdiv(distq, maxDistq)) / QONE;
            if (pitch < -0x1800 || pitch > 0x400 ||
                yaw   < -0x1800 || yaw >   0x1800) {
                result /= 2;
            }
        }
    }
    return result;
}

s32 clamp_positions_and_find_yawq(Vec3q posq, Vec3q originq, q32 xMaxq, q32 xMinq, q32 zMaxq, q32 zMinq) {
    s16 yaw = gCamera->nextYaw;

    if (posq[0] >= xMaxq) {
        posq[0] = xMaxq;
    }
    if (posq[0] <= xMinq) {
        posq[0] = xMinq;
    }
    if (posq[2] >= zMaxq) {
        posq[2] = zMaxq;
    }
    if (posq[2] <= zMinq) {
        posq[2] = zMinq;
    }
    yaw = calculate_yawq(originq, posq);
    return yaw;
}

/**
 * The yaw passed here is the yaw of the direction FROM Mario TO Lakitu.
 *
 * wallYaw always has 90 degrees added to it before this is called -- it's parallel to the wall.
 *
 * @return the new yaw from Mario to rotate towards.
 *
 * @warning this is jank. It actually returns the yaw that will rotate further INTO the wall. So, the
 *          developers just add 180 degrees to the result.
 */
s32 calc_avoid_yaw(s16 yawFromMario, s16 wallYaw) {
    s16 yawDiff;

    yawDiff = wallYaw - yawFromMario + DEGREES(90);

    if (yawDiff < 0) {
        // Deflect to the right
        yawFromMario = wallYaw;
    } else {
        // Note: this favors the left side if the wall is exactly perpendicular to the camera.
        // Deflect to the left
        yawFromMario = wallYaw + DEGREES(180);
    }
    return yawFromMario;
}

/**
 * Checks if `surf` is within the rect prism defined by xMax, yMax, and zMax
 *
 * @param surf surface to check
 * @param xMax absolute-value max size in x, set to -1 to ignore
 * @param yMax absolute-value max size in y, set to -1 to ignore
 * @param zMax absolute-value max size in z, set to -1 to ignore
 */
s32 is_surf_within_bounding_boxq(struct Surface *surf, q32 xMaxq, q32 yMaxq, q32 zMaxq) {
    // Surface vertex coordinates
    Vec3s sx;
    Vec3s sy;
    Vec3s sz;
    // Max delta between x, y, and z
    s16 dxMax = 0;
    s16 dyMax = 0;
    s16 dzMax = 0;
    // Current deltas between x, y, and z
    s32 dx;
    s32 dy;
    s32 dz;
    s32 i;
    s32 j;

    sx[0] = surf->vertex1[0];
    sx[1] = surf->vertex2[0];
    sx[2] = surf->vertex3[0];
    sy[0] = surf->vertex1[1];
    sy[1] = surf->vertex2[1];
    sy[2] = surf->vertex3[1];
    sz[0] = surf->vertex1[2];
    sz[1] = surf->vertex2[2];
    sz[2] = surf->vertex3[2];

    for (i = 0; i < 3; i++) {
        j = i + 1;
        if (j >= 3) {
            j = 0;
        }
        dx = ABS(sx[i] - sx[j]);
        if (dx > dxMax) {
            dxMax = dx;
        }
        dy = ABS(sy[i] - sy[j]);
        if (dy > dyMax) {
            dyMax = dy;
        }
        dz = ABS(sz[i] - sz[j]);
        if (dz > dzMax) {
            dzMax = dz;
        }
    }
    if (yMaxq != -QONE) {
        if (q(dyMax) < yMaxq) {
            return TRUE;
        }
    }
    if (xMaxq != -QONE && zMaxq != -QONE) {
        if (q(dxMax) < xMaxq && q(dzMax) < zMaxq) {
            return TRUE;
        }
    }
    return FALSE;
}

/**
 * Checks if `pos` is behind the surface, using the dot product.
 *
 * Because the function only uses `surf`s first vertex, some surfaces can shadow others.
 */
s32 is_behind_surfaceq(Vec3q posq, struct Surface *surf) {
    s32 behindSurface = 0;
    // Surface normal
    s32 normX = (surf->vertex2[1] - surf->vertex1[1]) * (surf->vertex3[2] - surf->vertex2[2]) -
                (surf->vertex3[1] - surf->vertex2[1]) * (surf->vertex2[2] - surf->vertex1[2]);
    s32 normY = (surf->vertex2[2] - surf->vertex1[2]) * (surf->vertex3[0] - surf->vertex2[0]) -
                (surf->vertex3[2] - surf->vertex2[2]) * (surf->vertex2[0] - surf->vertex1[0]);
    s32 normZ = (surf->vertex2[0] - surf->vertex1[0]) * (surf->vertex3[1] - surf->vertex2[1]) -
                (surf->vertex3[0] - surf->vertex2[0]) * (surf->vertex2[1] - surf->vertex1[1]);
	normX /= 128; // avoid overflow
	normY /= 128;
	normZ /= 128;
    q32 dirXq = (q(surf->vertex1[0]) - posq[0]) / 128;
    q32 dirYq = (q(surf->vertex1[1]) - posq[1]) / 128;
    q32 dirZq = (q(surf->vertex1[2]) - posq[2]) / 128;

    if (qmul(dirXq, (q32) normX) + qmul(dirYq, (q32) normY) + qmul(dirZq, (q32) normZ) < 0) {
        behindSurface = 1;
    }
    return behindSurface;
}

/**
 * Checks if the whole circular sector is behind the surface.
 */
s32 is_range_behind_surfaceq(Vec3q fromq, Vec3q toq, struct Surface *surf, s16 range, s16 surfType) {
    s32 behindSurface = TRUE;
    s32 leftBehind = 0;
    s32 rightBehind = 0;
    q32 checkDistq;
    s16 checkPitch;
    s16 checkYaw;
    Vec3q checkPosq;

    if (surf != NULL) {
        if (surfType == -1 || surf->type != surfType) {
            if (range == 0) {
                behindSurface = is_behind_surfaceq(toq, surf);
            } else {
                vec3q_get_dist_and_angle(fromq, toq, &checkDistq, &checkPitch, &checkYaw);
                vec3q_set_dist_and_angle(fromq, checkPosq, checkDistq, checkPitch, checkYaw + range);
                leftBehind = is_behind_surfaceq(checkPosq, surf);
                vec3q_set_dist_and_angle(fromq, checkPosq, checkDistq, checkPitch, checkYaw - range);
                rightBehind = is_behind_surfaceq(checkPosq, surf);
                behindSurface = leftBehind * rightBehind;
            }
        }
    }
    return behindSurface;
}

s32 is_mario_behind_surface(UNUSED struct Camera *c, struct Surface *surf) {
    s32 behindSurface = is_behind_surfaceq(sMarioCamState->posq, surf);

    return behindSurface;
}

/**
 * Calculates the distance between two points and sets a vector to a point
 * scaled along a line between them. Typically, somewhere in the middle.
 */
void scale_along_lineq(Vec3q dstq, Vec3q fromq, Vec3q toq, q32 scaleq) {
    Vec3q tempVecq;

    tempVecq[0] = qmul(toq[0] - fromq[0], scaleq) + fromq[0];
    tempVecq[1] = qmul(toq[1] - fromq[1], scaleq) + fromq[1];
    tempVecq[2] = qmul(toq[2] - fromq[2], scaleq) + fromq[2];
    vec3q_copy(dstq, tempVecq);
}
/**
 * Effectively created a rectangular prism defined by a vector starting at the center
 * and extending to the corners. If the position is in this box, the function returns true.
 */
s32 is_pos_in_boundsq(Vec3q posq, Vec3q centerq, Vec3q boundsq, s16 boundsYaw) {
    s32 inBound = FALSE;
    Vec3q relq;

    relq[0] = centerq[0] - posq[0];
    relq[1] = centerq[1] - posq[1];
    relq[2] = centerq[2] - posq[2];

    rotate_in_xzq(relq, relq, boundsYaw);

    if (-boundsq[0] < relq[0] && relq[0] < boundsq[0] &&
        -boundsq[1] < relq[1] && relq[1] < boundsq[1] &&
        -boundsq[2] < relq[2] && relq[2] < boundsq[2]) {
        inBound = TRUE;
    }
    return inBound;
}

s16 calculate_pitchq(Vec3q fromq, Vec3q toq) {
    q32 dxq = toq[0] - fromq[0];
    q32 dyq = toq[1] - fromq[1];
    q32 dzq = toq[2] - fromq[2];
    s16 pitch = atan2sq(sqrtq64((q64) qmul(dxq, dxq) + (q64) qmul(dzq, dzq)), dyq);

    return pitch;
}

s16 calculate_yawq(Vec3q fromq, Vec3q toq) {
    q32 dxq = toq[0] - fromq[0];
    //UNUSED f32 dy = toq[1] - fromq[1];
    q32 dzq = toq[2] - fromq[2];
    s16 yaw = atan2sq(dzq, dxq);

    return yaw;
}

/**
 * Calculates the pitch and yaw between two vectors.
 */
void calculate_anglesq(Vec3q fromq, Vec3q toq, s16 *pitch, s16 *yaw) {
    q32 dxq = toq[0] - fromq[0];
    q32 dyq = toq[1] - fromq[1];
    q32 dzq = toq[2] - fromq[2];

    *pitch = atan2sq(sqrtq(qmul(dxq, dxq) + qmul(dzq, dzq)), dyq);
    *yaw = atan2sq(dzq, dxq);
}

/**
 * Finds the distance between two vectors.
 */
q32 calc_abs_distq(Vec3q aq, Vec3q bq) {
	q32 sumq = 0;
	for(int i = 0; i < 3; i++) {
		q32 distq = (bq[i] - aq[i]) / (q32) 64;
		sumq += qmul(distq, distq);
	}
    return sqrtq(sumq) * (q32) 64;
}

/**
 * Finds the horizontal distance between two vectors.
 */
q32 calc_hor_distq(Vec3q aq, Vec3q bq) {
	q32 distXq = bq[0] - aq[0];
    q32 distZq = bq[2] - aq[2];
	q64 sumq = (q64) qmul(distXq, distXq) + (q64) qmul(distZq, distZq);
    return sqrtq64(sumq);
}

/**
 * Rotates a vector in the horizontal plane and copies it to a new vector.
 */
void rotate_in_xzq(Vec3q dstq, Vec3q srcq, s16 yaw) {
    q32 xq = srcq[0], zq = srcq[2];
	q32 yawsinq = sinqs(yaw);
	q32 yawcosq = cosqs(yaw);
    dstq[1] = srcq[1];
    dstq[0] = qmul(zq, yawsinq) + qmul(xq, yawcosq);
    dstq[2] = qmul(zq, yawcosq) - qmul(xq, yawsinq);
}

/**
 * Rotates a vector in the YZ plane and copies it to a new vector.
 *
 * Note: This function also flips the Z axis, so +Z moves forward, not backward like it would in world
 * space. If possible, use vec3f_set_dist_and_angle()
 */
void rotate_in_yzq(Vec3q dstq, Vec3q srcq, s16 pitch) {
	q32 yq = srcq[1], zq = srcq[2];
	q32 pitchsinq = sinqs(pitch);
	q32 pitchcosq = cosqs(pitch);
    dstq[2] = -(qmul(zq, pitchcosq) - qmul(yq, pitchsinq));
    dstq[1] =   qmul(zq, pitchsinq) + qmul(yq, pitchcosq);
    dstq[0] =   srcq[0];
}

/**
 * Start shaking the camera's pitch (up and down)
 */
void set_camera_pitch_shake(s16 mag, s16 decay, s16 inc) {
    if (gLakituState.shakeMagnitude[0] < mag) {
        gLakituState.shakeMagnitude[0] = mag;
        gLakituState.shakePitchDecay = decay;
        gLakituState.shakePitchVel = inc;
    }
}

/**
 * Start shaking the camera's yaw (side to side)
 */
void set_camera_yaw_shake(s16 mag, s16 decay, s16 inc) {
    if (ABS(mag) > ABS(gLakituState.shakeMagnitude[1])) {
        gLakituState.shakeMagnitude[1] = mag;
        gLakituState.shakeYawDecay = decay;
        gLakituState.shakeYawVel = inc;
    }
}

/**
 * Start shaking the camera's roll (rotate screen clockwise and counterclockwise)
 */
void set_camera_roll_shake(s16 mag, s16 decay, s16 inc) {
    if (gLakituState.shakeMagnitude[2] < mag) {
        gLakituState.shakeMagnitude[2] = mag;
        gLakituState.shakeRollDecay = decay;
        gLakituState.shakeRollVel = inc;
    }
}

/**
 * Start shaking the camera's pitch, but reduce `mag` by it's distance from the camera
 */
void set_pitch_shake_from_pointq(s16 mag, s16 decay, s16 inc, q32 maxDistq, q32 posXq, q32 posYq, q32 posZq) {
    Vec3q posq;
    q32 distq;
    s16 dummyPitch;
    s16 dummyYaw;

    posq[0] = posXq;
    posq[1] = posYq;
    posq[2] = posZq;
    vec3q_get_dist_and_angle(gLakituState.goalPosq, posq, &distq, &dummyPitch, &dummyYaw);
    mag = reduce_by_dist_from_cameraq(mag, maxDistq, posXq, posYq, posZq);
    if (mag != 0) {
        set_camera_pitch_shake(mag, decay, inc);
    }
}

/**
 * Start shaking the camera's yaw, but reduce `mag` by it's distance from the camera
 */
void set_yaw_shake_from_pointq(s16 mag, s16 decay, s16 inc, q32 maxDistq, q32 posXq, q32 posYq, q32 posZq) {
    Vec3q posq;
    q32 distq;
    s16 dummyPitch;
    s16 dummyYaw;

    posq[0] = posXq;
    posq[1] = posYq;
    posq[2] = posZq;
    vec3q_get_dist_and_angle(gLakituState.goalPosq, posq, &distq, &dummyPitch, &dummyYaw);
    mag = reduce_by_dist_from_cameraq(mag, maxDistq, posXq, posYq, posZq);
    if (mag != 0) {
        set_camera_yaw_shake(mag, decay, inc);
    }
}

/**
 * Update the shake offset by `increment`
 */
void increment_shake_offset(s16 *offset, s16 increment) {
    if (increment == -0x8000) {
        *offset = (*offset & 0x8000) + 0xC000;
    } else {
        *offset += increment;
    }
}

/**
 * Apply a vertical shake to the camera by adjusting its pitch
 */
void shake_camera_pitchq(Vec3q posq, Vec3q focusq) {
    q32 distq;
    s16 pitch;
    s16 yaw;

    if (gLakituState.shakeMagnitude[0] | gLakituState.shakeMagnitude[1]) {
        vec3q_get_dist_and_angle(posq, focusq, &distq, &pitch, &yaw);
        pitch += gLakituState.shakeMagnitude[0] * sins(gLakituState.shakePitchPhase);
        vec3q_set_dist_and_angle(posq, focusq, distq, pitch, yaw);
        increment_shake_offset(&gLakituState.shakePitchPhase, gLakituState.shakePitchVel);
        if (camera_approach_s16_symmetric_bool(&gLakituState.shakeMagnitude[0], 0,
                                               gLakituState.shakePitchDecay) == 0) {
            gLakituState.shakePitchPhase = 0;
        }
    }
}

/**
 * Apply a horizontal shake to the camera by adjusting its yaw
 */
void shake_camera_yawq(Vec3q posq, Vec3q focusq) {
    q32 distq;
    s16 pitch;
    s16 yaw;

    if (gLakituState.shakeMagnitude[1] != 0) {
        vec3q_get_dist_and_angle(posq, focusq, &distq, &pitch, &yaw);
        yaw += gLakituState.shakeMagnitude[1] * sins(gLakituState.shakeYawPhase);
        vec3q_set_dist_and_angle(posq, focusq, distq, pitch, yaw);
        increment_shake_offset(&gLakituState.shakeYawPhase, gLakituState.shakeYawVel);
        if (camera_approach_s16_symmetric_bool(&gLakituState.shakeMagnitude[1], 0,
                                               gLakituState.shakeYawDecay) == 0) {
            gLakituState.shakeYawPhase = 0;
        }
    }
}

/**
 * Apply a rotational shake to the camera by adjusting its roll
 */
void shake_camera_roll(s16 *roll) {
    UNUSED u8 unused[8];

    if (gLakituState.shakeMagnitude[2] != 0) {
        increment_shake_offset(&gLakituState.shakeRollPhase, gLakituState.shakeRollVel);
        *roll += gLakituState.shakeMagnitude[2] * sins(gLakituState.shakeRollPhase);
        if (camera_approach_s16_symmetric_bool(&gLakituState.shakeMagnitude[2], 0,
                                               gLakituState.shakeRollDecay) == 0) {
            gLakituState.shakeRollPhase = 0;
        }
    }
}

/**
 * Add an offset to the camera's yaw, used in levels that are inside a rectangular building, like the
 * pyramid or TTC.
 */
s32 offset_yaw_outward_radial(struct Camera *c, s16 areaYaw) {
    s16 yawGoal = DEGREES(60);
    s16 yaw = sModeOffsetYaw;
    q32 distFromAreaCenterq;
    Vec3q areaCenterq;
    s16 dYaw;
    switch (gCurrLevelArea) {
        case AREA_TTC:
            areaCenterq[0] = c->areaCenXq;
            areaCenterq[1] = sMarioCamState->posq[1];
            areaCenterq[2] = c->areaCenZq;
            distFromAreaCenterq = calc_abs_distq(areaCenterq, sMarioCamState->posq);
            if (q(800) > distFromAreaCenterq) {
                yawGoal = 0x3800;
            }
            break;
        case AREA_SSL_PYRAMID:
            // This mask splits the 360 degrees of yaw into 4 corners. It adds 45 degrees so that the yaw
            // offset at the corner will be 0, but the yaw offset near the center will face more towards
            // the direction Mario is running in.
            yawGoal = (areaYaw & 0xC000) - areaYaw + DEGREES(45);
            if (yawGoal < 0) {
                yawGoal = -yawGoal;
            }
            yawGoal = yawGoal / 32 * 48;
            break;
        case AREA_LLL_OUTSIDE:
            yawGoal = 0;
            break;
    }
    dYaw = qtrunc(q(gMarioStates[0].forwardVel) * (q32) (128 / 32));

    if (sAreaYawChange < 0) {
        camera_approach_s16_symmetric_bool(&yaw, -yawGoal, dYaw);
    }
    if (sAreaYawChange > 0) {
        camera_approach_s16_symmetric_bool(&yaw, yawGoal, dYaw);
    }
    // When the final yaw is out of [-60,60] degrees, approach yawGoal faster than dYaw will ever be,
    // making the camera lock in one direction until yawGoal drops below 60 (or Mario presses a C button)
    if (yaw < -DEGREES(60)) {
        //! Maybe they meant to reverse yawGoal's sign?
        camera_approach_s16_symmetric_bool(&yaw, -yawGoal, 0x200);
    }
    if (yaw > DEGREES(60)) {
        //! Maybe they meant to reverse yawGoal's sign?
        camera_approach_s16_symmetric_bool(&yaw, yawGoal, 0x200);
    }
    return yaw;
}

/**
 * Plays the background music that starts while peach reads the intro message.
 */
void cutscene_intro_peach_play_message_music(void) {
    play_music(SEQ_PLAYER_LEVEL, SEQUENCE_ARGS(4, SEQ_EVENT_PEACH_MESSAGE), 0);
}

/**
 * Plays the music that starts after peach fades and Lakitu appears.
 */
void cutscene_intro_peach_play_lakitu_flying_music(void) {
    play_music(SEQ_PLAYER_LEVEL, SEQUENCE_ARGS(15, SEQ_EVENT_CUTSCENE_INTRO), 0);
}

void play_camera_buzz_if_cdown(void) {
    if (gPlayer1Controller->buttonPressed & D_CBUTTONS) {
        play_sound_button_change_blocked();
    }
}

void play_camera_buzz_if_cbutton(void) {
    if (gPlayer1Controller->buttonPressed & CBUTTON_MASK) {
        play_sound_button_change_blocked();
    }
}

void play_camera_buzz_if_c_sideways(void) {
    if ((gPlayer1Controller->buttonPressed & L_CBUTTONS)
        || (gPlayer1Controller->buttonPressed & R_CBUTTONS)) {
        play_sound_button_change_blocked();
    }
}

void play_sound_cbutton_up(void) {
    play_sound(SOUND_MENU_CAMERA_ZOOM_IN, gGlobalSoundSource);
}

void play_sound_cbutton_down(void) {
    play_sound(SOUND_MENU_CAMERA_ZOOM_OUT, gGlobalSoundSource);
}

void play_sound_cbutton_side(void) {
    play_sound(SOUND_MENU_CAMERA_TURN, gGlobalSoundSource);
}

void play_sound_button_change_blocked(void) {
    play_sound(SOUND_MENU_CAMERA_BUZZ, gGlobalSoundSource);
}

void play_sound_rbutton_changed(void) {
    play_sound(SOUND_MENU_CLICK_CHANGE_VIEW, gGlobalSoundSource);
}

void play_sound_if_cam_switched_to_lakitu_or_mario(void) {
    if (sCameraSoundFlags & CAM_SOUND_MARIO_ACTIVE) {
        play_sound_rbutton_changed();
    }
    if (sCameraSoundFlags & CAM_SOUND_NORMAL_ACTIVE) {
        play_sound_rbutton_changed();
    }
    sCameraSoundFlags &= ~(CAM_SOUND_MARIO_ACTIVE | CAM_SOUND_NORMAL_ACTIVE);
}

/**
 * Handles input for radial, outwards radial, parallel tracking, and 8 direction mode.
 */
s32 radial_camera_input(struct Camera *c) {
    if ((gCameraMovementFlags & CAM_MOVE_ENTERED_ROTATE_SURFACE) || !(gCameraMovementFlags & CAM_MOVE_ROTATE)) {

        // If C-L or C-R are pressed, the camera is rotating
        if (gPlayer1Controller->buttonPressed & (L_CBUTTONS | R_CBUTTONS)) {
            gCameraMovementFlags &= ~CAM_MOVE_ENTERED_ROTATE_SURFACE;
            //  @bug this does not clear the rotation flags set by the surface. It's possible to set
            //       both ROTATE_LEFT and ROTATE_RIGHT, locking the camera.
            //       Ex: If a surface set CAM_MOVE_ROTATE_RIGHT and the user presses C-R, it locks the
            //       camera until a different mode is activated
        }

        // Rotate Right and left
        if (gPlayer1Controller->buttonPressed & R_CBUTTONS) {
            if (sModeOffsetYaw > -0x800) {
                // The camera is now rotating right
                if (!(gCameraMovementFlags & CAM_MOVE_ROTATE_RIGHT)) {
                    gCameraMovementFlags |= CAM_MOVE_ROTATE_RIGHT;
                }

                if (c->mode == CAMERA_MODE_RADIAL) {
                    // if > ~48 degrees, we're rotating for the second time.
                    if (sModeOffsetYaw > 0x22AA) {
                        s2ndRotateFlags |= CAM_MOVE_ROTATE_RIGHT;
                    }

                    if (sModeOffsetYaw == DEGREES(105)) {
                        play_sound_button_change_blocked();
                    } else {
                        play_sound_cbutton_side();
                    }
                } else {
                    if (sModeOffsetYaw == DEGREES(60)) {
                        play_sound_button_change_blocked();
                    } else {
                        play_sound_cbutton_side();
                    }
                }
            } else {
                gCameraMovementFlags |= CAM_MOVE_RETURN_TO_MIDDLE;
                play_sound_cbutton_up();
            }
        }
        if (gPlayer1Controller->buttonPressed & L_CBUTTONS) {
            if (sModeOffsetYaw < 0x800) {
                if (!(gCameraMovementFlags & CAM_MOVE_ROTATE_LEFT)) {
                    gCameraMovementFlags |= CAM_MOVE_ROTATE_LEFT;
                }

                if (c->mode == CAMERA_MODE_RADIAL) {
                    // if < ~48 degrees, we're rotating for the second time.
                    if (sModeOffsetYaw < -0x22AA) {
                        s2ndRotateFlags |= CAM_MOVE_ROTATE_LEFT;
                    }

                    if (sModeOffsetYaw == DEGREES(-105)) {
                        play_sound_button_change_blocked();
                    } else {
                        play_sound_cbutton_side();
                    }
                } else {
                    if (sModeOffsetYaw == DEGREES(-60)) {
                        play_sound_button_change_blocked();
                    } else {
                        play_sound_cbutton_side();
                    }
                }
            } else {
                gCameraMovementFlags |= CAM_MOVE_RETURN_TO_MIDDLE;
                play_sound_cbutton_up();
            }
        }
    }

    // Zoom in / enter C-Up
    if (gPlayer1Controller->buttonPressed & U_CBUTTONS) {
        if (gCameraMovementFlags & CAM_MOVE_ZOOMED_OUT) {
            gCameraMovementFlags &= ~CAM_MOVE_ZOOMED_OUT;
            play_sound_cbutton_up();
        } else {
            set_mode_c_up(c);
        }
    }

    // Zoom out
    if (gPlayer1Controller->buttonPressed & D_CBUTTONS) {
        if (gCameraMovementFlags & CAM_MOVE_ZOOMED_OUT) {
            gCameraMovementFlags |= CAM_MOVE_ALREADY_ZOOMED_OUT;
#ifndef VERSION_JP
            play_camera_buzz_if_cdown();
#endif
        } else {
            gCameraMovementFlags |= CAM_MOVE_ZOOMED_OUT;
            play_sound_cbutton_down();
        }
    }

    //! returning uninitialized variable
    return 0;
}

/**
 * Starts a cutscene dialog. Only has an effect when `trigger` is 1
 */
s32 trigger_cutscene_dialog(s32 trigger) {
    s32 result = 0;
    UNUSED struct Camera *c = gCamera;

    if (trigger == 1) {
        start_object_cutscene_without_focus(CUTSCENE_READ_MESSAGE);
    }
    if (trigger == 2) {
    }
    return result;
}

/**
 * Updates the camera based on which C buttons are pressed this frame
 */
void handle_c_button_movement(struct Camera *c) {
    s16 cSideYaw;

    // Zoom in
    if (gPlayer1Controller->buttonPressed & U_CBUTTONS) {
        if (c->mode != CAMERA_MODE_FIXED && (gCameraMovementFlags & CAM_MOVE_ZOOMED_OUT)) {
            gCameraMovementFlags &= ~CAM_MOVE_ZOOMED_OUT;
            play_sound_cbutton_up();
        } else {
            set_mode_c_up(c);
            if (sZeroZoomDistq > gCameraZoomDistq) {
                sZoomAmountq = -gCameraZoomDistq;
            } else {
                sZoomAmountq = gCameraZoomDistq;
            }
        }
    }
    if (c->mode != CAMERA_MODE_FIXED) {
        // Zoom out
        if (gPlayer1Controller->buttonPressed & D_CBUTTONS) {
            if (gCameraMovementFlags & CAM_MOVE_ZOOMED_OUT) {
                gCameraMovementFlags |= CAM_MOVE_ALREADY_ZOOMED_OUT;
                sZoomAmountq = gCameraZoomDistq + q(400);
#ifndef VERSION_JP
                play_camera_buzz_if_cdown();
#endif
            } else {
                gCameraMovementFlags |= CAM_MOVE_ZOOMED_OUT;
                sZoomAmountq = gCameraZoomDistq + q(400);
                play_sound_cbutton_down();
            }
        }

        // Rotate left or right
        cSideYaw = 0x1000;
        if (gPlayer1Controller->buttonPressed & R_CBUTTONS) {
            if (gCameraMovementFlags & CAM_MOVE_ROTATE_LEFT) {
                gCameraMovementFlags &= ~CAM_MOVE_ROTATE_LEFT;
            } else {
                gCameraMovementFlags |= CAM_MOVE_ROTATE_RIGHT;
                if (sCSideButtonYaw == 0) {
                    play_sound_cbutton_side();
                }
                sCSideButtonYaw = -cSideYaw;
            }
        }
        if (gPlayer1Controller->buttonPressed & L_CBUTTONS) {
            if (gCameraMovementFlags & CAM_MOVE_ROTATE_RIGHT) {
                gCameraMovementFlags &= ~CAM_MOVE_ROTATE_RIGHT;
            } else {
                gCameraMovementFlags |= CAM_MOVE_ROTATE_LEFT;
                if (sCSideButtonYaw == 0) {
                    play_sound_cbutton_side();
                }
                sCSideButtonYaw = cSideYaw;
            }
        }
    }
}

/**
 * Zero the 10 cvars.
 */
void clear_cutscene_vars(UNUSED struct Camera *c) {
    s32 i;

    for (i = 0; i < 10; i++) {
        sCutsceneVars[i].unused1 = 0;
        vec3q_set(sCutsceneVars[i].pointq, 0, 0, 0);
        vec3q_set(sCutsceneVars[i].unusedPointq, 0, 0, 0);
        vec3s_set(sCutsceneVars[i].angle, 0, 0, 0);
        sCutsceneVars[i].unused2 = 0;
    }
}

/**
 * Start the cutscene, `cutscene`, if it is not already playing.
 */
void start_cutscene(struct Camera *c, u8 cutscene) {
    if (c->cutscene != cutscene) {
        c->cutscene = cutscene;
        clear_cutscene_vars(c);
    }
}

/**
 * Look up the victory dance cutscene in sDanceCutsceneTable
 *
 * First the index entry is determined based on the course and the star that was just picked up
 * Like the entries in sZoomOutAreaMasks, each entry represents two stars
 * The current courses's 4 bits of the index entry are used as the actual index into sDanceCutsceneTable
 *
 * @return the victory cutscene to use
 */
s32 determine_dance_cutscene(UNUSED struct Camera *c) {
    u8 cutscene = 0;
    u8 cutsceneIndex = 0;
    u8 starIndex = (gLastCompletedStarNum - 1) / 2;
    u8 courseIndex = gCurrCourseNum;

    if (starIndex > 3) {
        starIndex = 0;
    }
    if (courseIndex > COURSE_MAX) {
        courseIndex = COURSE_NONE;
    }
    cutsceneIndex = sDanceCutsceneIndexTable[courseIndex][starIndex];

    if (gLastCompletedStarNum & 1) {
        // Odd stars take the lower four bytes
        cutsceneIndex &= 0xF;
    } else {
        // Even stars use the upper four bytes
        cutsceneIndex = cutsceneIndex >> 4;
    }
    cutscene = sDanceCutsceneTable[cutsceneIndex];
    return cutscene;
}

/**
 * @return `pullResult` or `pushResult` depending on Mario's door action
 */
u8 open_door_cutscene(u8 pullResult, u8 pushResult) {
    s16 result = 0;

    if (sMarioCamState->action == ACT_PULLING_DOOR) {
        result = pullResult;
    }
    if (sMarioCamState->action == ACT_PUSHING_DOOR) {
        result = pushResult;
    }
    return result;
}

/**
 * If no cutscenes are playing, determines if a cutscene should play based on Mario's action and
 * cameraEvent
 *
 * @return the cutscene that should start, 0 if none
 */
u8 get_cutscene_from_mario_status(struct Camera *c) {
    u8 cutscene = c->cutscene;

    if (cutscene == 0) {
        // A cutscene started by an object, if any, will start if nothing else happened
        cutscene = sObjectCutscene;
        sObjectCutscene = 0;
        if (sMarioCamState->cameraEvent == CAM_EVENT_DOOR) {
            switch (gCurrLevelArea) {
                case AREA_CASTLE_LOBBY:
                    //! doorStatus is never DOOR_ENTER_LOBBY when cameraEvent == 6, because
                    //! doorStatus is only used for the star door in the lobby, which uses
                    //! ACT_ENTERING_STAR_DOOR
                    if (c->mode == CAMERA_MODE_SPIRAL_STAIRS || c->mode == CAMERA_MODE_CLOSE
                                                                 || c->doorStatus == DOOR_ENTER_LOBBY) {
                        cutscene = open_door_cutscene(CUTSCENE_DOOR_PULL_MODE, CUTSCENE_DOOR_PUSH_MODE);
                    } else {
                        cutscene = open_door_cutscene(CUTSCENE_DOOR_PULL, CUTSCENE_DOOR_PUSH);
                    }
                    break;
                case AREA_BBH:
                    //! Castle Lobby uses 0 to mean 'no special modes', but BBH uses 1...
                    if (c->doorStatus == DOOR_LEAVING_SPECIAL) {
                        cutscene = open_door_cutscene(CUTSCENE_DOOR_PULL, CUTSCENE_DOOR_PUSH);
                    } else {
                        cutscene = open_door_cutscene(CUTSCENE_DOOR_PULL_MODE, CUTSCENE_DOOR_PUSH_MODE);
                    }
                    break;
                default:
                    cutscene = open_door_cutscene(CUTSCENE_DOOR_PULL, CUTSCENE_DOOR_PUSH);
                    break;
            }
        }
        if (sMarioCamState->cameraEvent == CAM_EVENT_DOOR_WARP) {
            cutscene = CUTSCENE_DOOR_WARP;
        }
        if (sMarioCamState->cameraEvent == CAM_EVENT_CANNON) {
            cutscene = CUTSCENE_ENTER_CANNON;
        }
        if (SURFACE_IS_PAINTING_WARP(sMarioGeometry.currFloorType)) {
            cutscene = CUTSCENE_ENTER_PAINTING;
        }
        switch (sMarioCamState->action) {
            case ACT_DEATH_EXIT:
                cutscene = CUTSCENE_DEATH_EXIT;
                break;
            case ACT_EXIT_AIRBORNE:
                cutscene = CUTSCENE_EXIT_PAINTING_SUCC;
                break;
            case ACT_SPECIAL_EXIT_AIRBORNE:
                if (gPrevLevel == LEVEL_BOWSER_1 || gPrevLevel == LEVEL_BOWSER_2
                    || gPrevLevel == LEVEL_BOWSER_3) {
                    cutscene = CUTSCENE_EXIT_BOWSER_SUCC;
                } else {
                    cutscene = CUTSCENE_EXIT_SPECIAL_SUCC;
                }
                break;
            case ACT_SPECIAL_DEATH_EXIT:
                if (gPrevLevel == LEVEL_BOWSER_1 || gPrevLevel == LEVEL_BOWSER_2
                    || gPrevLevel == LEVEL_BOWSER_3) {
                    cutscene = CUTSCENE_EXIT_BOWSER_DEATH;
                } else {
                    cutscene = CUTSCENE_NONPAINTING_DEATH;
                }
                break;
            case ACT_ENTERING_STAR_DOOR:
                if (c->doorStatus == DOOR_DEFAULT) {
                    cutscene = CUTSCENE_SLIDING_DOORS_OPEN;
                } else {
                    cutscene = CUTSCENE_DOOR_PULL_MODE;
                }
                break;
            case ACT_UNLOCKING_KEY_DOOR:
                cutscene = CUTSCENE_UNLOCK_KEY_DOOR;
                break;
            case ACT_WATER_DEATH:
                cutscene = CUTSCENE_WATER_DEATH;
                break;
            case ACT_DEATH_ON_BACK:
                cutscene = CUTSCENE_DEATH_ON_BACK;
                break;
            case ACT_DEATH_ON_STOMACH:
                cutscene = CUTSCENE_DEATH_ON_STOMACH;
                break;
            case ACT_STANDING_DEATH:
                cutscene = CUTSCENE_STANDING_DEATH;
                break;
            case ACT_SUFFOCATION:
                cutscene = CUTSCENE_SUFFOCATION_DEATH;
                break;
            case ACT_QUICKSAND_DEATH:
                cutscene = CUTSCENE_QUICKSAND_DEATH;
                break;
            case ACT_ELECTROCUTION:
                cutscene = CUTSCENE_STANDING_DEATH;
                break;
            case ACT_STAR_DANCE_EXIT:
                cutscene = determine_dance_cutscene(c);
                break;
            case ACT_STAR_DANCE_WATER:
                cutscene = determine_dance_cutscene(c);
                break;
            case ACT_STAR_DANCE_NO_EXIT:
                cutscene = CUTSCENE_DANCE_DEFAULT;
                break;
        }
        switch (sMarioCamState->cameraEvent) {
            case CAM_EVENT_START_INTRO:
                cutscene = CUTSCENE_INTRO_PEACH;
                break;
            case CAM_EVENT_START_GRAND_STAR:
                cutscene = CUTSCENE_GRAND_STAR;
                break;
            case CAM_EVENT_START_ENDING:
                cutscene = CUTSCENE_ENDING;
                break;
            case CAM_EVENT_START_END_WAVING:
                cutscene = CUTSCENE_END_WAVING;
                break;
            case CAM_EVENT_START_CREDITS:
                cutscene = CUTSCENE_CREDITS;
                break;
        }
    }
    //! doorStatus is reset every frame. CameraTriggers need to constantly set doorStatus
    c->doorStatus = DOOR_DEFAULT;

    return cutscene;
}

/**
 * Moves the camera when Mario has triggered a warp
 */
void warp_cameraq(q32 displacementXq, q32 displacementYq, q32 displacementZq) {
    Vec3q displacementq;
    struct MarioState *marioStates = &gMarioStates[0];
    struct LinearTransitionPoint *start = &sModeInfo.transitionStart;
    struct LinearTransitionPoint *end = &sModeInfo.transitionEnd;

    gCurrLevelArea = gCurrLevelNum * 16 + gCurrentArea->index;
    displacementq[0] = displacementXq;
    displacementq[1] = displacementYq;
    displacementq[2] = displacementZq;
    vec3q_add(gLakituState.curPosq, displacementq);
    vec3q_add(gLakituState.curFocusq, displacementq);
    vec3q_add(gLakituState.goalPosq, displacementq);
    vec3q_add(gLakituState.goalFocusq, displacementq);
    marioStates->waterLevel += qtrunc(displacementYq);

    vec3q_add(start->focusq, displacementq);
    vec3q_add(start->posq, displacementq);
    vec3q_add(end->focusq, displacementq);
    vec3q_add(end->posq, displacementq);
}

/**
 * Make the camera's y coordinate approach `goal`,
 * unless smooth movement is off, in which case the y coordinate is simply set to `goal`
 */
void approach_camera_heightq(struct Camera *c, q32 goalq, q32 incq) {
    if (sStatusFlags & CAM_FLAG_SMOOTH_MOVEMENT) {
        if (c->posq[1] < goalq) {
            if ((c->posq[1] += incq) > goalq) {
                c->posq[1] = goalq;
            }
        } else {
            if ((c->posq[1] -= incq) < goalq) {
                c->posq[1] = goalq;
            }
        }
    } else {
        c->posq[1] = goalq;
    }
}

/**
 * Set the camera's focus to Mario's position, and add several relative offsets.
 *
 * @param leftRight offset to Mario's left/right, relative to his faceAngle
 * @param yOff y offset
 * @param forwBack offset to Mario's front/back, relative to his faceAngle
 * @param yawOff offset to Mario's faceAngle, changes the direction of `leftRight` and `forwBack`
 */
void set_focus_rel_marioq(struct Camera *c, q32 leftRightq, q32 yOffq, q32 forwBackq, s16 yawOff) {
    s16 yaw;
    q32 focFloorYOffq;

    calc_y_to_curr_floorq(&focFloorYOffq, q(1), q(200), &focFloorYOffq, q(0.9), q(200));
    yaw = sMarioCamState->faceAngle[1] + yawOff;
    c->focusq[2] = sMarioCamState->posq[2] + qmul(forwBackq, cosqs(yaw)) - qmul(leftRightq, sinqs(yaw));
    c->focusq[0] = sMarioCamState->posq[0] + qmul(forwBackq, sinqs(yaw)) + qmul(leftRightq, cosqs(yaw));
    c->focusq[1] = sMarioCamState->posq[1] + yOffq + focFloorYOffq;
}

/**
 * Set the camera's position to Mario's position, and add several relative offsets. Unused.
 *
 * @param leftRight offset to Mario's left/right, relative to his faceAngle
 * @param yOff y offset
 * @param forwBack offset to Mario's front/back, relative to his faceAngle
 * @param yawOff offset to Mario's faceAngle, changes the direction of `leftRight` and `forwBack`
 */
UNUSED static void unused_set_pos_rel_marioq(struct Camera *c, q32 leftRightq, q32 yOffq, q32 forwBackq, s16 yawOff) {
    u16 yaw = sMarioCamState->faceAngle[1] + yawOff;

    c->posq[0] = sMarioCamState->posq[0] + qmul(forwBackq, sinqs(yaw)) + qmul(leftRightq, cosqs(yaw));
    c->posq[1] = sMarioCamState->posq[1] + yOffq;
    c->posq[2] = sMarioCamState->posq[2] + qmul(forwBackq, cosqs(yaw)) - qmul(leftRightq, sinqs(yaw));
}

/**
 * Rotates the offset `to` according to the pitch and yaw values in `rotation`.
 * Adds `from` to the rotated offset, and stores the result in `dst`.
 *
 * @warning Flips the Z axis, so that relative to `rotation`, -Z moves forwards and +Z moves backwards.
 */
void offset_rotatedq(Vec3q dstq, Vec3q fromq, Vec3q toq, Vec3s rotation) {
    Vec3q pitchRotatedq;

    // First rotate the direction by rotation's pitch
    //! The Z axis is flipped here.
    pitchRotatedq[2] = -(qmul(toq[2], cosqs(rotation[0])) - qmul(toq[1], sinqs(rotation[0])));
    pitchRotatedq[1] =   qmul(toq[2], sinqs(rotation[0])) + qmul(toq[1], cosqs(rotation[0]));
    pitchRotatedq[0] =   toq[0];

    // Rotate again by rotation's yaw
    dstq[0] = fromq[0] + qmul(pitchRotatedq[2], sinqs(rotation[1])) + qmul(pitchRotatedq[0], cosqs(rotation[1]));
    dstq[1] = fromq[1] + pitchRotatedq[1];
    dstq[2] = fromq[2] + qmul(pitchRotatedq[2], cosqs(rotation[1])) - qmul(pitchRotatedq[0], sinqs(rotation[1]));
}

/**
 * Rotates the offset defined by (`xTo`, `yTo`, `zTo`) according to the pitch and yaw values in `rotation`.
 * Adds `from` to the rotated offset, and stores the result in `dst`.
 *
 * @warning Flips the Z axis, so that relative to `rotation`, -Z moves forwards and +Z moves backwards.
 */
void offset_rotated_coordsq(Vec3q dstq, Vec3q fromq, Vec3s rotation, q32 xToq, q32 yToq, q32 zToq) {
    offset_rotatedq(dstq, fromq, (Vec3q) {xToq, yToq, zToq}, rotation);
}

void determine_pushing_or_pulling_door(s16 *rotation) {
    if (sMarioCamState->action == ACT_PULLING_DOOR) {
        *rotation = 0;
    } else {
        *rotation = DEGREES(-180);
    }
}

/**
 * Calculate Lakitu's next position and focus, according to gCamera's state,
 * and store them in `newPos` and `newFoc`.
 *
 * @param newPos where Lakitu should fly towards this frame
 * @param newFoc where Lakitu should look towards this frame
 *
 * @param curPos gCamera's pos this frame
 * @param curFoc gCamera's foc this frame
 *
 * @param oldPos gCamera's pos last frame
 * @param oldFoc gCamera's foc last frame
 *
 * @return Lakitu's next yaw, which is the same as the yaw passed in if no transition happened
 */
s16 next_lakitu_stateq(Vec3q newPosq, Vec3q newFocq, Vec3q curPosq, Vec3q curFocq,
                       Vec3q oldPosq, Vec3q oldFocq, s16 yaw) {
    s16 yawVelocity;
    s16 pitchVelocity;
    q32 distVelocityq;
    q32 goalDistq;
    s16 goalPitch;
    s16 goalYaw;
    q32 distTimerq = q(sModeTransition.framesLeft);
    s16 angleTimer = sModeTransition.framesLeft;
    Vec3q nextPosq;
    Vec3q nextFocq;
    Vec3q startPosq;
    Vec3q startFocq;
    s32 i;
    q32 floorHeightq;
    struct Surface *floor;

    // If not transitioning, just use gCamera's current pos and foc
    vec3q_copy(newPosq, curPosq);
    vec3q_copy(newFocq, curFocq);

    if (sStatusFlags & CAM_FLAG_START_TRANSITION) {
        for (i = 0; i < 3; i++) {
            // Add Mario's displacement from this frame to the last frame's pos and focus
            // Makes the transition start from where the camera would have moved
            startPosq[i] = oldPosq[i] + sMarioCamState->posq[i] - sModeTransition.marioPosq[i];
            startFocq[i] = oldFocq[i] + sMarioCamState->posq[i] - sModeTransition.marioPosq[i];
        }

        vec3q_get_dist_and_angle(curFocq, startFocq, &sModeTransition.focDistq, &sModeTransition.focPitch,
                                 &sModeTransition.focYaw);
        vec3q_get_dist_and_angle(curFocq, startPosq, &sModeTransition.posDistq, &sModeTransition.posPitch,
                                 &sModeTransition.posYaw);
        sStatusFlags &= ~CAM_FLAG_START_TRANSITION;
    }

    // Transition from the last mode to the current one
    if (sModeTransition.framesLeft > 0) {
        vec3q_get_dist_and_angle(curFocq, curPosq, &goalDistq, &goalPitch, &goalYaw);
        distVelocityq = qdiv(ABS(goalDistq - sModeTransition.posDistq), distTimerq);
        pitchVelocity = ABS(goalPitch - sModeTransition.posPitch) / angleTimer;
        yawVelocity = ABS(goalYaw - sModeTransition.posYaw) / angleTimer;

        camera_approach_q32_symmetric_bool(&sModeTransition.posDistq, goalDistq, distVelocityq);
        camera_approach_s16_symmetric_bool(&sModeTransition.posYaw, goalYaw, yawVelocity);
        camera_approach_s16_symmetric_bool(&sModeTransition.posPitch, goalPitch, pitchVelocity);
        vec3q_set_dist_and_angle(curFocq, nextPosq, sModeTransition.posDistq, sModeTransition.posPitch,
                                 sModeTransition.posYaw);

        vec3q_get_dist_and_angle(curPosq, curFocq, &goalDistq, &goalPitch, &goalYaw);
        pitchVelocity = sModeTransition.focPitch / (s16) sModeTransition.framesLeft;
        yawVelocity = sModeTransition.focYaw / (s16) sModeTransition.framesLeft;
        distVelocityq = sModeTransition.focDistq / sModeTransition.framesLeft;

        camera_approach_s16_symmetric_bool(&sModeTransition.focPitch, goalPitch, pitchVelocity);
        camera_approach_s16_symmetric_bool(&sModeTransition.focYaw, goalYaw, yawVelocity);
        camera_approach_q32_symmetric_bool(&sModeTransition.focDistq, 0, distVelocityq);
        vec3q_set_dist_and_angle(curFocq, nextFocq, sModeTransition.focDistq, sModeTransition.focPitch,
                                 sModeTransition.focYaw);

        vec3q_copy(newFocq, nextFocq);
        vec3q_copy(newPosq, nextPosq);

        if (gCamera->cutscene != 0 || !(gCameraMovementFlags & CAM_MOVE_C_UP_MODE)) {
            floorHeightq = find_floorq(newPosq[0], newPosq[1], newPosq[2], &floor);
            if (floorHeightq != q(FLOOR_LOWER_LIMIT)) {
                if ((floorHeightq += q(125)) > newPosq[1]) {
                    newPosq[1] = floorHeightq;
                }
            }
            q32_find_wall_collision(&newPosq[0], &newPosq[1], &newPosq[2], 0, q(100));
        }
        sModeTransition.framesLeft--;
        yaw = calculate_yawq(newFocq, newPosq);
    } else {
        sModeTransition.posDistq = 0;
        sModeTransition.posPitch = 0;
        sModeTransition.posYaw = 0;
        sStatusFlags &= ~CAM_FLAG_TRANSITION_OUT_OF_C_UP;
    }
    vec3q_copy(sModeTransition.marioPosq, sMarioCamState->posq);
    return yaw;
}

static UNUSED void stop_transitional_movement(void) {
    sStatusFlags &= ~(CAM_FLAG_START_TRANSITION | CAM_FLAG_TRANSITION_OUT_OF_C_UP);
    sModeTransition.framesLeft = 0;
}

/**
 * Start fixed camera mode, setting the base position to (`x`, `y`, `z`)
 *
 * @return TRUE if the base pos was updated
 */
s32 set_camera_mode_fixed(struct Camera *c, s16 x, s16 y, s16 z) {
    s32 basePosSet = FALSE;
    q32 posXq = q(x);
    q32 posYq = q(y);
    q32 posZq = q(z);

    if (sFixedModeBasePositionq[0] != posXq || sFixedModeBasePositionq[1] != posYq
        || sFixedModeBasePositionq[2] != posZq) {
        basePosSet = TRUE;
        sStatusFlags &= ~CAM_FLAG_SMOOTH_MOVEMENT;
    }
    vec3q_set(sFixedModeBasePositionq, posXq, posYq, posZq);
    if (c->mode != CAMERA_MODE_FIXED) {
        sStatusFlags &= ~CAM_FLAG_SMOOTH_MOVEMENT;
        c->mode = CAMERA_MODE_FIXED;
        vec3q_set(c->posq, sFixedModeBasePositionq[0], sMarioCamState->posq[1],
                  sFixedModeBasePositionq[2]);
    }
    return basePosSet;
}

void set_camera_mode_8_directions(struct Camera *c) {
    if (c->mode != CAMERA_MODE_8_DIRECTIONS) {
        c->mode = CAMERA_MODE_8_DIRECTIONS;
        sStatusFlags &= ~CAM_FLAG_SMOOTH_MOVEMENT;
        s8DirModeBaseYaw = 0;
        s8DirModeYawOffset = 0;
    }
}

/**
 * If the camera mode is not already the boss fight camera (camera with two foci)
 * set it to be so.
 */
void set_camera_mode_boss_fight(struct Camera *c) {
    if (c->mode != CAMERA_MODE_BOSS_FIGHT) {
        transition_to_camera_mode(c, CAMERA_MODE_BOSS_FIGHT, 15);
        sModeOffsetYaw = c->nextYaw - DEGREES(45);
    }
}

void set_camera_mode_close_cam(u8 *mode) {
    if (*mode != CAMERA_MODE_CLOSE) {
        sStatusFlags &= ~CAM_FLAG_SMOOTH_MOVEMENT;
        *mode = CAMERA_MODE_CLOSE;
    }
}

/**
 * Change to radial mode.
 * If the difference in yaw between pos -> Mario and pos > focus is < 90 degrees, transition.
 * Otherwise jump to radial mode.
 */
void set_camera_mode_radial(struct Camera *c, s16 transitionTime) {
    Vec3q focusq;
    s16 yaw;

    focusq[0] = c->areaCenXq;
    focusq[1] = sMarioCamState->posq[1];
    focusq[2] = c->areaCenZq;
    if (c->mode != CAMERA_MODE_RADIAL) {
        yaw = calculate_yawq(focusq, sMarioCamState->posq) - calculate_yawq(c->focusq, c->posq) + DEGREES(90);
        if (yaw > 0) {
            transition_to_camera_mode(c, CAMERA_MODE_RADIAL, transitionTime);
        } else {
            c->mode = CAMERA_MODE_RADIAL;
            sStatusFlags &= ~CAM_FLAG_SMOOTH_MOVEMENT;
        }
        sModeOffsetYaw = 0;
    }
}

/**
 * Start parallel tracking mode using the path `path`
 */
void parallel_tracking_init(struct Camera *c, struct ParallelTrackingPoint *path) {
    if (c->mode != CAMERA_MODE_PARALLEL_TRACKING) {
        sParTrackPath = path;
        sParTrackIndex = 0;
        sParTrackTransOff.posq[0] = 0;
        sParTrackTransOff.posq[1] = 0;
        sParTrackTransOff.posq[2] = 0;
        // Place the camera in the middle of the path
        c->posq[0] = (sParTrackPath[0].posq[0] + sParTrackPath[1].posq[0]) / 2;
        c->posq[1] = (sParTrackPath[0].posq[1] + sParTrackPath[1].posq[1]) / 2;
        c->posq[2] = (sParTrackPath[0].posq[2] + sParTrackPath[1].posq[2]) / 2;
        sStatusFlags &= ~CAM_FLAG_SMOOTH_MOVEMENT;
        c->mode = CAMERA_MODE_PARALLEL_TRACKING;
    }
}

/**
 * Set the fixed camera base pos depending on the current level area
 */
void set_fixed_cam_axis_sa_lobby(UNUSED s16 preset) {
    switch (gCurrLevelArea) {
        case AREA_SA:
            vec3q_set(sFixedModeBasePositionq, q(646), q(143), q(-1513));
            break;

        case AREA_CASTLE_LOBBY:
            vec3q_set(sFixedModeBasePositionq, q(-577), q(143), q(1443));
            break;
    }
}

/**
 * Block area-specific CameraTrigger and special surface modes.
 * Generally, block area mode changes if:
 *      Mario is wearing the metal cap, or at the water's surface, or the camera is in Mario mode
 *
 * However, if the level is WDW, DDD, or COTMC (levels that have metal cap and water):
 *      Only block area mode changes if Mario is in a cannon,
 *      or if the camera is in Mario mode and Mario is not swimming or in water with the metal cap
 */
void check_blocking_area_processing(const u8 *mode) {
    if (sMarioCamState->action & ACT_FLAG_METAL_WATER ||
                        *mode == CAMERA_MODE_BEHIND_MARIO || *mode == CAMERA_MODE_WATER_SURFACE) {
        sStatusFlags |= CAM_FLAG_BLOCK_AREA_PROCESSING;
    }

    if (gCurrLevelNum == LEVEL_DDD || gCurrLevelNum == LEVEL_WDW || gCurrLevelNum == LEVEL_COTMC) {
        sStatusFlags &= ~CAM_FLAG_BLOCK_AREA_PROCESSING;
    }

    if ((*mode == CAMERA_MODE_BEHIND_MARIO &&
            !(sMarioCamState->action & (ACT_FLAG_SWIMMING | ACT_FLAG_METAL_WATER))) ||
         *mode == CAMERA_MODE_INSIDE_CANNON) {
        sStatusFlags |= CAM_FLAG_BLOCK_AREA_PROCESSING;
    }
}

BAD_RETURN(s32) cam_rr_exit_building_side(struct Camera *c) {
    set_camera_mode_8_directions(c);
    s8DirModeBaseYaw = DEGREES(90);
}

BAD_RETURN(s32) cam_rr_exit_building_top(struct Camera *c) {
    set_camera_mode_8_directions(c);
    if (c->posq[1] < q(6343)) {
        c->posq[1] = q(7543);
        gLakituState.goalPosq[1] = c->posq[1];
        gLakituState.curPosq[1] = c->posq[1];
        sStatusFlags &= ~CAM_FLAG_SMOOTH_MOVEMENT;
    }
}

BAD_RETURN(s32) cam_rr_enter_building_window(struct Camera *c) {
    if (c->mode != CAMERA_MODE_FIXED) {
        set_camera_mode_fixed(c, -2974, 478, -3975);
    }
}

BAD_RETURN(s32) cam_rr_enter_building(struct Camera *c) {
    if (c->mode != CAMERA_MODE_FIXED) {
        set_camera_mode_fixed(c, -2953, 798, -3943);
    }
    // Prevent the camera from being above the roof
    if (c->posq[1] > q(6043)) {
        c->posq[1] = q(6043);
    }
}

BAD_RETURN(s32) cam_rr_enter_building_side(struct Camera *c) {
    if (c->mode != CAMERA_MODE_FIXED) {
        sStatusFlags &= ~CAM_FLAG_SMOOTH_MOVEMENT;
        c->mode = CAMERA_MODE_FIXED;
    }
}

/**
 * Fix the camera in place as Mario gets exits out the MC cave into the waterfall.
 */
BAD_RETURN(s32) cam_cotmc_exit_waterfall(UNUSED struct Camera *c) {
    gCameraMovementFlags |= CAM_MOVE_FIX_IN_PLACE;
}

/**
 * Sets 8 directional mode and blocks the next trigger from processing.
 * Activated when Mario is walking in front of the snowman's head.
 */
BAD_RETURN(s32) cam_sl_snowman_head_8dir(struct Camera *c) {
    sStatusFlags |= CAM_FLAG_BLOCK_AREA_PROCESSING;
    transition_to_camera_mode(c, CAMERA_MODE_8_DIRECTIONS, 60);
    s8DirModeBaseYaw = 0x1D27;
}

/**
 * Sets free roam mode in SL, called by a trigger that covers a large area and surrounds the 8 direction
 * trigger.
 */
BAD_RETURN(s32) cam_sl_free_roam(struct Camera *c) {
    transition_to_camera_mode(c, CAMERA_MODE_FREE_ROAM, 60);
}

/**
 * Warps the camera underneath the floor, used in HMC to move under the elevator platforms
 */
void move_camera_through_floor_while_descendingq(struct Camera *c, q32 heightq) {
    if ((q(sMarioGeometry.currFloorHeight) < heightq - q(100))
        && (sMarioGeometry.prevFloorHeight > sMarioGeometry.currFloorHeight)) {
        c->posq[1] = heightq - q(400);
        gLakituState.curPosq[1] = heightq - q(400);
        gLakituState.goalPosq[1] = heightq - q(400);
    }
}

BAD_RETURN(s32) cam_hmc_enter_maze(struct Camera *c) {
    s16 pitch, yaw;
    q32 distq;

    if (c->posq[1] > q(-102)) {
        vec3q_get_dist_and_angle(c->focusq, gLakituState.goalPosq, &distq, &pitch, &yaw);
        vec3q_set_dist_and_angle(c->focusq, gLakituState.goalPosq, q(300), pitch, yaw);
        gLakituState.goalPosq[1] = q(-800);
#ifndef VERSION_JP
        c->posq[1] = gLakituState.goalPosq[1];
        gLakituState.curPosq[1] = gLakituState.goalPosq[1];
#endif
        sStatusFlags &= ~CAM_FLAG_SMOOTH_MOVEMENT;
    }
}

BAD_RETURN(s32) cam_hmc_elevator_black_hole(struct Camera *c) {
    move_camera_through_floor_while_descendingq(c, q(1536));
}

BAD_RETURN(s32) cam_hmc_elevator_maze_emergency_exit(struct Camera *c) {
    move_camera_through_floor_while_descendingq(c, q(2355));
}

BAD_RETURN(s32) cam_hmc_elevator_lake(struct Camera *c) {
    move_camera_through_floor_while_descendingq(c, q(1843));
}

BAD_RETURN(s32) cam_hmc_elevator_maze(struct Camera *c) {
    move_camera_through_floor_while_descendingq(c, q(1843));
}

/**
 * Starts the "Enter Pyramid Top" cutscene.
 */
BAD_RETURN(s32) cam_ssl_enter_pyramid_top(UNUSED struct Camera *c) {
    start_object_cutscene_without_focus(CUTSCENE_ENTER_PYRAMID_TOP);
}

/**
 * Change to close mode in the center of the pyramid. Outside this trigger, the default mode is outwards
 * radial.
 */
BAD_RETURN(s32) cam_ssl_pyramid_center(struct Camera *c) {
    sStatusFlags |= CAM_FLAG_BLOCK_AREA_PROCESSING;
    transition_to_camera_mode(c, CAMERA_MODE_CLOSE, 90);
}

/**
 * Changes the mode back to outward radial in the boss room inside the pyramid.
 */
BAD_RETURN(s32) cam_ssl_boss_room(struct Camera *c) {
    sStatusFlags |= CAM_FLAG_BLOCK_AREA_PROCESSING;
    transition_to_camera_mode(c, CAMERA_MODE_OUTWARD_RADIAL, 90);
}

/**
 * Moves the camera to through the tunnel by forcing sModeOffsetYaw
 */
BAD_RETURN(s32) cam_thi_move_cam_through_tunnel(UNUSED struct Camera *c) {
    if (sModeOffsetYaw < DEGREES(60)) {
        sModeOffsetYaw = DEGREES(60);
    }
}

/**
 * Aligns the camera to look through the tunnel
 */
BAD_RETURN(s32) cam_thi_look_through_tunnel(UNUSED struct Camera *c) {
    // ~82.5 degrees
    if (sModeOffsetYaw > 0x3AAA) {
        sModeOffsetYaw = 0x3AAA;
    }
}

/**
 * Unused. Changes the camera to radial mode when Mario is on the tower.
 *
 * @see sCamBOB for bounds.
 */
BAD_RETURN(s32) cam_bob_tower(struct Camera *c) {
    sStatusFlags |= CAM_FLAG_BLOCK_AREA_PROCESSING;
    transition_to_camera_mode(c, CAMERA_MODE_RADIAL, 90);
}

/**
 * Unused. Changes the camera to free roam mode when Mario is not climbing the tower.
 *
 * This is the only CameraTrigger event that uses the area == -1 feature:
 * If this was used, it would be called by default in BoB.
 *
 * @see sCamBOB
 */
BAD_RETURN(s32) cam_bob_default_free_roam(struct Camera *c) {
    transition_to_camera_mode(c, CAMERA_MODE_FREE_ROAM, 90);
}

/**
 * Starts the pool entrance cutscene if Mario is not exiting the pool.
 * Used in both the castle and HMC.
 */
BAD_RETURN(s32) cam_castle_hmc_start_pool_cutscene(struct Camera *c) {
    if ((sMarioCamState->action != ACT_SPECIAL_DEATH_EXIT)
        && (sMarioCamState->action != ACT_SPECIAL_EXIT_AIRBORNE)) {
        start_cutscene(c, CUTSCENE_ENTER_POOL);
    }
}

/**
 * Sets the fixed mode pos offset so that the camera faces the doorway when Mario is near the entrance
 * to the castle lobby
 */
BAD_RETURN(s32) cam_castle_lobby_entrance(UNUSED struct Camera *c) {
    vec3q_set(sCastleEntranceOffsetq, q(-813) - sFixedModeBasePositionq[0],
              q(378) - sFixedModeBasePositionq[1], q(1103) - sFixedModeBasePositionq[2]);
}

/**
 * Make the camera look up the stairs from the 2nd to 3rd floor of the castle
 */
BAD_RETURN(s32) cam_castle_look_upstairs(struct Camera *c) {
    struct Surface *floor;
    q32 floorHeightq = find_floorq(c->posq[0], c->posq[1], c->posq[2], &floor);

    // If Mario is on the first few steps, fix the camera pos, making it look up
    if ((q(sMarioGeometry.currFloorHeight) > q(1229)) && (floorHeightq < q(1229))
        && (sCSideButtonYaw == 0)) {
        vec3q_set(c->posq, q(-227), q(1425), q(1533));
    }
}

/**
 * Make the camera look down the stairs towards the basement star door
 */
BAD_RETURN(s32) cam_castle_basement_look_downstairs(struct Camera *c) {
    struct Surface *floor;
    q32 floorHeightq = find_floorq(c->posq[0], c->posq[1], c->posq[2], &floor);

    // Fix the camera pos, making it look downwards. Only active on the top few steps
    if ((floorHeightq > q(-110)) && (sCSideButtonYaw == 0)) {
        vec3q_set(c->posq, q(-980), q(249), q(-1398));
    }
}

/**
 * Enter the fixed-mode castle lobby. A trigger for this is placed in every entrance so that the camera
 * changes to fixed mode.
 */
BAD_RETURN(s32) cam_castle_enter_lobby(struct Camera *c) {
    if (c->mode != CAMERA_MODE_FIXED) {
        sStatusFlags &= ~CAM_FLAG_SMOOTH_MOVEMENT;
        set_fixed_cam_axis_sa_lobby(c->mode);
        c->mode = CAMERA_MODE_FIXED;
    }
}

/**
 * Starts spiral stairs mode.
 */
BAD_RETURN(s32) cam_castle_enter_spiral_stairs(struct Camera *c) {
    transition_to_camera_mode(c, CAMERA_MODE_SPIRAL_STAIRS, 20);
}

/**
 * unused, starts close mode if the camera is in spiral stairs mode.
 * This was replaced with cam_castle_close_mode
 */
static UNUSED BAD_RETURN(s32) cam_castle_leave_spiral_stairs(struct Camera *c) {
    if (c->mode == CAMERA_MODE_SPIRAL_STAIRS) {
        transition_to_camera_mode(c, CAMERA_MODE_CLOSE, 30);
    } else {
        set_camera_mode_close_cam(&c->mode);
    }
}

/**
 * The default mode when outside of the lobby and spiral staircase. A trigger for this is placed at
 * every door leaving the lobby and spiral staircase.
 */
BAD_RETURN(s32) cam_castle_close_mode(struct Camera *c) {
    set_camera_mode_close_cam(&c->mode);
}

/**
 * Functions the same as cam_castle_close_mode, but sets doorStatus so that the camera will enter
 * fixed-mode when Mario leaves the room.
 */
BAD_RETURN(s32) cam_castle_leave_lobby_sliding_door(struct Camera *c) {
    cam_castle_close_mode(c);
    c->doorStatus = DOOR_ENTER_LOBBY;
}

/**
 * Just calls cam_castle_enter_lobby
 */
BAD_RETURN(s32) cam_castle_enter_lobby_sliding_door(struct Camera *c) {
    cam_castle_enter_lobby(c);
}

BAD_RETURN(s32) cam_bbh_room_6(struct Camera *c) {
    parallel_tracking_init(c, sBBHLibraryParTrackPath);
}

BAD_RETURN(s32) cam_bbh_fall_off_roof(struct Camera *c) {
    set_camera_mode_close_cam(&c->mode);
}

BAD_RETURN(s32) cam_bbh_fall_into_pool(struct Camera *c) {
    Vec3q dirq;
    set_camera_mode_close_cam(&c->mode);
    vec3q_set(dirq, 0, 0, q(300));
    offset_rotatedq(gLakituState.goalPosq, sMarioCamState->posq, dirq, sMarioCamState->faceAngle);
    gLakituState.goalPosq[1] = q(-2300);
    vec3q_copy(c->posq, gLakituState.goalPosq);
    sStatusFlags &= ~CAM_FLAG_SMOOTH_MOVEMENT;
}

BAD_RETURN(s32) cam_bbh_room_1(struct Camera *c) {
    set_camera_mode_fixed(c, 956, 440, 1994);
}

BAD_RETURN(s32) cam_bbh_leave_front_door(struct Camera *c) {
    c->doorStatus = DOOR_LEAVING_SPECIAL;
    cam_bbh_room_1(c);
}

BAD_RETURN(s32) cam_bbh_room_2_lower(struct Camera *c) {
    set_camera_mode_fixed(c, 2591, 400, 1284);
}

BAD_RETURN(s32) cam_bbh_room_4(struct Camera *c) {
    set_camera_mode_fixed(c, 3529, 340, -1384);
}

BAD_RETURN(s32) cam_bbh_room_8(struct Camera *c) {
    set_camera_mode_fixed(c, -500, 740, -1306);
}

/**
 * In BBH's room 5's library (the first floor room with the vanish cap/boo painting)
 * set the camera mode to fixed and position to (-2172, 200, 675)
 */
BAD_RETURN(s32) cam_bbh_room_5_library(struct Camera *c) {
    set_camera_mode_fixed(c, -2172, 200, 675);
}

/**
 * In BBH's room 5 (the first floor room with the vanish cap/boo painting)
 * set the camera mode to to the hidden room's position
 * if coming from the library.
 */
BAD_RETURN(s32) cam_bbh_room_5_library_to_hidden_transition(struct Camera *c) {
    if (set_camera_mode_fixed(c, -2172, 200, 675) == 1) {
        transition_next_state(c, 20);
    }
}

BAD_RETURN(s32) cam_bbh_room_5_hidden_to_library_transition(struct Camera *c) {
    if (set_camera_mode_fixed(c, -1542, 320, -307) == 1) {
        transition_next_state(c, 20);
    }
}

BAD_RETURN(s32) cam_bbh_room_5_hidden(struct Camera *c) {
    c->doorStatus = DOOR_LEAVING_SPECIAL;
    set_camera_mode_fixed(c, -1542, 320, -307);
}

BAD_RETURN(s32) cam_bbh_room_3(struct Camera *c) {
    set_camera_mode_fixed(c, -1893, 320, 2327);
}

BAD_RETURN(s32) cam_bbh_room_7_mr_i(struct Camera *c) {
    set_camera_mode_fixed(c, 1371, 360, -1302);
}

BAD_RETURN(s32) cam_bbh_room_7_mr_i_to_coffins_transition(struct Camera *c) {
    if (set_camera_mode_fixed(c, 1371, 360, -1302) == 1) {
        transition_next_state(c, 20);
    }
}

BAD_RETURN(s32) cam_bbh_room_7_coffins_to_mr_i_transition(struct Camera *c) {
    if (set_camera_mode_fixed(c, 2115, 260, -772) == 1) {
        transition_next_state(c, 20);
    }
}

BAD_RETURN(s32) cam_bbh_elevator_room_lower(struct Camera *c) {
    c->doorStatus = DOOR_LEAVING_SPECIAL;
    set_camera_mode_close_cam(&c->mode);
}

BAD_RETURN(s32) cam_bbh_room_0_back_entrance(struct Camera *c) {
    set_camera_mode_close_cam(&c->mode);
}

BAD_RETURN(s32) cam_bbh_elevator(struct Camera *c) {
    if (c->mode == CAMERA_MODE_FIXED) {
        set_camera_mode_close_cam(&c->mode);
        c->posq[1] = q(-405);
        gLakituState.goalPosq[1] = q(-405);
    }
}

BAD_RETURN(s32) cam_bbh_room_12_upper(struct Camera *c) {
    c->doorStatus = DOOR_LEAVING_SPECIAL;
    set_camera_mode_fixed(c, -2932, 296, 4429);
}

BAD_RETURN(s32) cam_bbh_enter_front_door(struct Camera *c) {
    set_camera_mode_close_cam(&c->mode);
}

BAD_RETURN(s32) cam_bbh_room_2_library(struct Camera *c) {
    set_camera_mode_fixed(c, 3493, 440, 617);
}

BAD_RETURN(s32) cam_bbh_room_2_library_to_trapdoor_transition(struct Camera *c) {
    if (set_camera_mode_fixed(c, 3493, 440, 617) == 1) {
        transition_next_state(c, 20);
    }
}

BAD_RETURN(s32) cam_bbh_room_2_trapdoor(struct Camera *c) {
    set_camera_mode_fixed(c, 3502, 440, 1217);
}

BAD_RETURN(s32) cam_bbh_room_2_trapdoor_transition(struct Camera *c) {
    if (set_camera_mode_fixed(c, 3502, 440, 1217) == 1) {
        transition_next_state(c, 20);
    }
}

BAD_RETURN(s32) cam_bbh_room_9_attic(struct Camera *c) {
    set_camera_mode_fixed(c, -670, 460, 372);
}

BAD_RETURN(s32) cam_bbh_room_9_attic_transition(struct Camera *c) {
    if (set_camera_mode_fixed(c, -670, 460, 372) == 1) {
        transition_next_state(c, 20);
    }
}

BAD_RETURN(s32) cam_bbh_room_9_mr_i_transition(struct Camera *c) {
    if (set_camera_mode_fixed(c, 131, 380, -263) == 1) {
        transition_next_state(c, 20);
    }
}

BAD_RETURN(s32) cam_bbh_room_13_balcony(struct Camera *c) {
    set_camera_mode_fixed(c, 210, 420, 3109);
}

BAD_RETURN(s32) cam_bbh_room_0(struct Camera *c) {
    c->doorStatus = DOOR_LEAVING_SPECIAL;
    set_camera_mode_fixed(c, -204, 807, 204);
}

BAD_RETURN(s32) cam_ccm_enter_slide_shortcut(UNUSED struct Camera *c) {
    sStatusFlags |= CAM_FLAG_CCM_SLIDE_SHORTCUT;
}

BAD_RETURN(s32) cam_ccm_leave_slide_shortcut(UNUSED struct Camera *c) {
    sStatusFlags &= ~CAM_FLAG_CCM_SLIDE_SHORTCUT;
}

/**
 * Apply any modes that are triggered by special floor surface types
 */
u32 surface_type_modes(struct Camera *c) {
    u32 modeChanged = 0;

    switch (sMarioGeometry.currFloorType) {
        case SURFACE_CLOSE_CAMERA:
            transition_to_camera_mode(c, CAMERA_MODE_CLOSE, 90);
            modeChanged += 1;
            break;

        case SURFACE_CAMERA_FREE_ROAM:
            transition_to_camera_mode(c, CAMERA_MODE_FREE_ROAM, 90);
            modeChanged += 1;
            break;

        case SURFACE_NO_CAM_COL_SLIPPERY:
            transition_to_camera_mode(c, CAMERA_MODE_CLOSE, 90);
            modeChanged += 1;
            break;
    }
    return modeChanged;
}

/**
 * Set the camera mode to `mode` if Mario is not standing on a special surface
 */
u32 set_mode_if_not_set_by_surface(struct Camera *c, u8 mode) {
    u32 modeChanged = 0;
    modeChanged = surface_type_modes(c);

    if ((modeChanged == 0) && (mode != 0)) {
        transition_to_camera_mode(c, mode, 90);
    }

    return modeChanged;
}

/**
 * Used in THI, check if Mario is standing on any of the special surfaces in that area
 */
void surface_type_modes_thi(struct Camera *c) {
    switch (sMarioGeometry.currFloorType) {
        case SURFACE_CLOSE_CAMERA:
            if (c->mode != CAMERA_MODE_CLOSE) {
                transition_to_camera_mode(c, CAMERA_MODE_FREE_ROAM, 90);
            }
            break;

        case SURFACE_CAMERA_FREE_ROAM:
            if (c->mode != CAMERA_MODE_CLOSE) {
                transition_to_camera_mode(c, CAMERA_MODE_FREE_ROAM, 90);
            }
            break;

        case SURFACE_NO_CAM_COL_SLIPPERY:
            if (c->mode != CAMERA_MODE_CLOSE) {
                transition_to_camera_mode(c, CAMERA_MODE_FREE_ROAM, 90);
            }
            break;

        case SURFACE_CAMERA_8_DIR:
            transition_to_camera_mode(c, CAMERA_MODE_8_DIRECTIONS, 90);
            break;

        default:
            transition_to_camera_mode(c, CAMERA_MODE_RADIAL, 90);
    }
}

/**
 * Terminates a list of CameraTriggers.
 */
#define NULL_TRIGGER                                                                                    \
    { 0, NULL, 0, 0, 0, 0, 0, 0, 0 }

/**
 * The SL triggers operate camera behavior in front of the snowman who blows air.
 * The first sets a 8 direction mode, while the latter (which encompasses the former)
 * sets free roam mode.
 *
 * This behavior is exploitable, since the ranges assume that Mario must pass through the latter on
 * exit. Using hyperspeed, the earlier area can be directly exited from, keeping the changes it applies.
 */
struct CameraTrigger sCamSL[] = {
    { 1, cam_sl_snowman_head_8dir, 1119, 3584, 1125, 1177, 358, 358, -0x1D27 },
    // This trigger surrounds the previous one
    { 1, cam_sl_free_roam, 1119, 3584, 1125, 4096, 4096, 4096, -0x1D27 },
    NULL_TRIGGER
};

/**
 * The THI triggers are specifically for the tunnel near the start of the Huge Island.
 * The first helps the camera from getting stuck on the starting side, the latter aligns with the
 * tunnel. Both sides achieve their effect by editing the camera yaw.
 */
struct CameraTrigger sCamTHI[] = {
    { 1, cam_thi_move_cam_through_tunnel, -4609, -2969, 6448, 100, 300, 300, 0 },
    { 1, cam_thi_look_through_tunnel,     -4809, -2969, 6448, 100, 300, 300, 0 },
    NULL_TRIGGER
};

/**
 * The HMC triggers are mostly for warping the camera below platforms, but the second trigger is used to
 * start the cutscene for entering the CotMC pool.
 */
struct CameraTrigger sCamHMC[] = {
    { 1, cam_hmc_enter_maze, 1996, 102, 0, 205, 100, 205, 0 },
    { 1, cam_castle_hmc_start_pool_cutscene, 3350, -4689, 4800, 600, 50, 600, 0 },
    { 1, cam_hmc_elevator_black_hole, -3278, 1236, 1379, 358, 200, 358, 0 },
    { 1, cam_hmc_elevator_maze_emergency_exit, -2816, 2055, -2560, 358, 200, 358, 0 },
    { 1, cam_hmc_elevator_lake, -3532, 1543, -7040, 358, 200, 358, 0 },
    { 1, cam_hmc_elevator_maze, -972, 1543, -7347, 358, 200, 358, 0 },
    NULL_TRIGGER
};

/**
 * The SSL triggers are for starting the enter pyramid top cutscene,
 * setting close mode in the middle of the pyramid, and setting the boss fight camera mode to outward
 * radial.
 */
struct CameraTrigger sCamSSL[] = {
    { 1, cam_ssl_enter_pyramid_top, -2048, 1080, -1024, 150, 150, 150, 0 },
    { 2, cam_ssl_pyramid_center, 0, -104, -104, 1248, 1536, 2950, 0 },
    { 2, cam_ssl_pyramid_center, 0, 2500, 256, 515, 5000, 515, 0 },
    { 3, cam_ssl_boss_room, 0, -1534, -2040, 1000, 800, 1000, 0 },
    NULL_TRIGGER
};

/**
 * The RR triggers are for changing between fixed and 8 direction mode when entering / leaving the building at
 * the end of the ride.
 */
struct CameraTrigger sCamRR[] = {
    { 1, cam_rr_exit_building_side, -4197, 3819, -3087, 1769, 1490, 342, 0 },
    { 1, cam_rr_enter_building_side, -4197, 3819, -3771, 769, 490, 342, 0 },
    { 1, cam_rr_enter_building_window, -5603, 4834, -5209, 300, 600, 591, 0 },
    { 1, cam_rr_enter_building, -2609, 3730, -5463, 300, 650, 577, 0 },
    { 1, cam_rr_exit_building_top, -4196, 7343, -5155, 4500, 1000, 4500, 0 },
    { 1, cam_rr_enter_building, -4196, 6043, -5155, 500, 300, 500, 0 },
    NULL_TRIGGER,
};

/**
 * These triggers are unused, but because the first trigger surrounds the BoB tower and activates radial
 * mode (which is called "tower mode" in the patent), it's speculated they belonged to BoB.
 *
 * This table contains the only instance of a CameraTrigger with an area set to -1, and it sets the mode
 * to free_roam when Mario is not walking up the tower.
 */
struct CameraTrigger sCamBOB[] = {
    {  1, cam_bob_tower, 2468, 2720, -4608, 3263, 1696, 3072, 0 },
    { -1, cam_bob_default_free_roam, 0, 0, 0, 0, 0, 0, 0 },
    NULL_TRIGGER
};

/**
 * The CotMC trigger is only used to prevent fix Lakitu in place when Mario exits through the waterfall.
 */
struct CameraTrigger sCamCotMC[] = {
    { 1, cam_cotmc_exit_waterfall, 0, 1500, 3500, 550, 10000, 1500, 0 },
    NULL_TRIGGER
};

/**
 * The CCM triggers are used to set the flag that says when Mario is in the slide shortcut.
 */
struct CameraTrigger sCamCCM[] = {
    { 2, cam_ccm_enter_slide_shortcut, -4846, 2061, 27, 1229, 1342, 396, 0 },
    { 2, cam_ccm_leave_slide_shortcut, -6412, -3917, -6246, 307, 185, 132, 0 },
    NULL_TRIGGER
};

/**
 * The Castle triggers are used to set the camera to fixed mode when entering the lobby, and to set it
 * to close mode when leaving it. They also set the mode to spiral staircase.
 *
 * There are two triggers for looking up and down straight staircases when Mario is at the start,
 * and one trigger that starts the enter pool cutscene when Mario enters HMC.
 */
struct CameraTrigger sCamCastle[] = {
    { 1, cam_castle_close_mode, -1100, 657, -1346, 300, 150, 300, 0 },
    { 1, cam_castle_enter_lobby, -1099, 657, -803, 300, 150, 300, 0 },
    { 1, cam_castle_close_mode, -2304, -264, -4072, 140, 150, 140, 0 },
    { 1, cam_castle_close_mode, -2304, 145, -1344, 140, 150, 140, 0 },
    { 1, cam_castle_enter_lobby, -2304, 145, -802, 140, 150, 140, 0 },
    //! Sets the camera mode when leaving secret aquarium
    { 1, cam_castle_close_mode, 2816, 1200, -256, 100, 100, 100, 0 },
    { 1, cam_castle_close_mode, 256, -161, -4226, 140, 150, 140, 0 },
    { 1, cam_castle_close_mode, 256, 145, -1344, 140, 150, 140, 0 },
    { 1, cam_castle_enter_lobby, 256, 145, -802, 140, 150, 140, 0 },
    { 1, cam_castle_close_mode, -1023, 44, -4870, 140, 150, 140, 0 },
    { 1, cam_castle_close_mode, -459, 145, -1020, 140, 150, 140, 0x6000 },
    { 1, cam_castle_enter_lobby, -85, 145, -627, 140, 150, 140, 0 },
    { 1, cam_castle_close_mode, -1589, 145, -1020, 140, 150, 140, -0x6000 },
    { 1, cam_castle_enter_lobby, -1963, 145, -627, 140, 150, 140, 0 },
    { 1, cam_castle_leave_lobby_sliding_door, -2838, 657, -1659, 200, 150, 150, 0x2000 },
    { 1, cam_castle_enter_lobby_sliding_door, -2319, 512, -1266, 300, 150, 300, 0x2000 },
    { 1, cam_castle_close_mode, 844, 759, -1657, 40, 150, 40, -0x2000 },
    { 1, cam_castle_enter_lobby, 442, 759, -1292, 140, 150, 140, -0x2000 },
    { 2, cam_castle_enter_spiral_stairs, -1000, 657, 1740, 200, 300, 200, 0 },
    { 2, cam_castle_enter_spiral_stairs, -996, 1348, 1814, 200, 300, 200, 0 },
    { 2, cam_castle_close_mode, -946, 657, 2721, 50, 150, 50, 0 },
    { 2, cam_castle_close_mode, -996, 1348, 907, 50, 150, 50, 0 },
    { 2, cam_castle_close_mode, -997, 1348, 1450, 140, 150, 140, 0 },
    { 1, cam_castle_close_mode, -4942, 452, -461, 140, 150, 140, 0x4000 },
    { 1, cam_castle_close_mode, -3393, 350, -793, 140, 150, 140, 0x4000 },
    { 1, cam_castle_enter_lobby, -2851, 350, -792, 140, 150, 140, 0x4000 },
    { 1, cam_castle_enter_lobby, 803, 350, -228, 140, 150, 140, -0x4000 },
    //! Duplicate camera trigger outside JRB door
    { 1, cam_castle_enter_lobby, 803, 350, -228, 140, 150, 140, -0x4000 },
    { 1, cam_castle_close_mode, 1345, 350, -229, 140, 150, 140, 0x4000 },
    { 1, cam_castle_close_mode, -946, -929, 622, 300, 150, 300, 0 },
    { 2, cam_castle_look_upstairs, -205, 1456, 2508, 210, 928, 718, 0 },
    { 1, cam_castle_basement_look_downstairs, -1027, -587, -718, 318, 486, 577, 0 },
    { 1, cam_castle_lobby_entrance, -1023, 376, 1830, 300, 400, 300, 0 },
    { 3, cam_castle_hmc_start_pool_cutscene, 2485, -1689, -2659, 600, 50, 600, 0 },
    NULL_TRIGGER
};

/**
 * The BBH triggers are the most complex, they cause the camera to enter fixed mode for each room,
 * transition between rooms, and enter free roam when outside.
 *
 * The triggers are also responsible for warping the camera below platforms.
 */
struct CameraTrigger sCamBBH[] = {
    { 1, cam_bbh_enter_front_door, 742, 0, 2369, 200, 200, 200, 0 },
    { 1, cam_bbh_leave_front_door, 741, 0, 1827, 200, 200, 200, 0 },
    { 1, cam_bbh_room_1, 222, 0, 1458, 200, 200, 200, 0 },
    { 1, cam_bbh_room_1, 222, 0, 639, 200, 200, 200, 0 },
    { 1, cam_bbh_room_1, 435, 0, 222, 200, 200, 200, 0 },
    { 1, cam_bbh_room_1, 1613, 0, 222, 200, 200, 200, 0 },
    { 1, cam_bbh_room_1, 1827, 0, 1459, 200, 200, 200, 0 },
    { 1, cam_bbh_room_1, -495, 819, 1407, 200, 200, 200, 0 },
    { 1, cam_bbh_room_1, -495, 819, 640, 250, 200, 200, 0 },
    { 1, cam_bbh_room_1, 179, 819, 222, 200, 200, 200, 0 },
    { 1, cam_bbh_room_1, 1613, 819, 222, 200, 200, 200, 0 },
    { 1, cam_bbh_room_1, 1827, 819, 486, 200, 200, 200, 0 },
    { 1, cam_bbh_room_1, 1827, 819, 1818, 200, 200, 200, 0 },
    { 1, cam_bbh_room_2_lower, 2369, 0, 1459, 200, 200, 200, 0 },
    { 1, cam_bbh_room_2_lower, 3354, 0, 1347, 200, 200, 200, 0 },
    { 1, cam_bbh_room_2_lower, 2867, 514, 1843, 512, 102, 409, 0 },
    { 1, cam_bbh_room_4, 3354, 0, 804, 200, 200, 200, 0 },
    { 1, cam_bbh_room_4, 1613, 0, -320, 200, 200, 200, 0 },
    { 1, cam_bbh_room_8, 435, 0, -320, 200, 200, 200, 0 },
    { 1, cam_bbh_room_5_library, -2021, 0, 803, 200, 200, 200, 0 },
    { 1, cam_bbh_room_5_library, -320, 0, 640, 200, 200, 200, 0 },
    { 1, cam_bbh_room_5_library_to_hidden_transition, -1536, 358, -254, 716, 363, 102, 0 },
    { 1, cam_bbh_room_5_hidden_to_library_transition, -1536, 358, -459, 716, 363, 102, 0 },
    { 1, cam_bbh_room_5_hidden, -1560, 0, -1314, 200, 200, 200, 0 },
    { 1, cam_bbh_room_3, -320, 0, 1459, 200, 200, 200, 0 },
    { 1, cam_bbh_room_3, -2021, 0, 1345, 200, 200, 200, 0 },
    { 1, cam_bbh_room_2_library, 2369, 819, 486, 200, 200, 200, 0 },
    { 1, cam_bbh_room_2_library, 2369, 1741, 486, 200, 200, 200, 0 },
    { 1, cam_bbh_room_2_library_to_trapdoor_transition, 2867, 1228, 1174, 716, 414, 102, 0 },
    { 1, cam_bbh_room_2_trapdoor_transition, 2867, 1228, 1378, 716, 414, 102, 0 },
    { 1, cam_bbh_room_2_trapdoor, 2369, 819, 1818, 200, 200, 200, 0 },
    { 1, cam_bbh_room_9_attic, 1829, 1741, 486, 200, 200, 200, 0 },
    { 1, cam_bbh_room_9_attic, 741, 1741, 1587, 200, 200, 200, 0 },
    { 1, cam_bbh_room_9_attic_transition, 102, 2048, -191, 100, 310, 307, 0 },
    { 1, cam_bbh_room_9_mr_i_transition, 409, 2048, -191, 100, 310, 307, 0 },
    { 1, cam_bbh_room_13_balcony, 742, 1922, 2164, 200, 200, 200, 0 },
    { 1, cam_bbh_fall_off_roof, 587, 1322, 2677, 1000, 400, 600, 0 },
    { 1, cam_bbh_room_3, -1037, 819, 1408, 200, 200, 200, 0 },
    { 1, cam_bbh_room_3, -1970, 1024, 1345, 200, 200, 200, 0 },
    { 1, cam_bbh_room_8, 179, 819, -320, 200, 200, 200, 0 },
    { 1, cam_bbh_room_7_mr_i, 1613, 819, -320, 200, 200, 200, 0 },
    { 1, cam_bbh_room_7_mr_i_to_coffins_transition, 2099, 1228, -819, 102, 414, 716, 0 },
    { 1, cam_bbh_room_7_coffins_to_mr_i_transition, 2304, 1228, -819, 102, 414, 716, 0 },
    { 1, cam_bbh_room_6, -1037, 819, 640, 200, 200, 200, 0 },
    { 1, cam_bbh_room_6, -1970, 1024, 803, 200, 200, 200, 0 },
    { 1, cam_bbh_room_1, 1827, 819, 1818, 200, 200, 200, 0 },
    { 1, cam_bbh_fall_into_pool, 2355, -1112, -193, 1228, 500, 1343, 0 },
    { 1, cam_bbh_fall_into_pool, 2355, -1727, 1410, 1228, 500, 705, 0 },
    { 1, cam_bbh_elevator_room_lower, 0, -2457, 1827, 250, 200, 250, 0 },
    { 1, cam_bbh_elevator_room_lower, 0, -2457, 2369, 250, 200, 250, 0 },
    { 1, cam_bbh_elevator_room_lower, 0, -2457, 4929, 250, 200, 250, 0 },
    { 1, cam_bbh_elevator_room_lower, 0, -2457, 4387, 250, 200, 250, 0 },
    { 1, cam_bbh_room_0_back_entrance, 1887, -2457, 204, 250, 200, 250, 0 },
    { 1, cam_bbh_room_0, 1272, -2457, 204, 250, 200, 250, 0 },
    { 1, cam_bbh_room_0, -1681, -2457, 204, 250, 200, 250, 0 },
    { 1, cam_bbh_room_0_back_entrance, -2296, -2457, 204, 250, 200, 250, 0 },
    { 1, cam_bbh_elevator, -2939, -605, 5367, 800, 100, 800, 0 },
    { 1, cam_bbh_room_12_upper, -2939, -205, 5367, 300, 100, 300, 0 },
    { 1, cam_bbh_room_12_upper, -2332, -204, 4714, 250, 200, 250, 0x6000 },
    { 1, cam_bbh_room_0_back_entrance, -1939, -204, 4340, 250, 200, 250, 0x6000 },
    NULL_TRIGGER
};

#define _ NULL
#define STUB_LEVEL(_0, _1, _2, _3, _4, _5, _6, _7, cameratable) cameratable,
#define DEFINE_LEVEL(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, cameratable) cameratable,
#define DEFINE_LEVEL_REMOVED(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, cameratable) _,

/*
 * This table has an extra 2 levels after the last unknown_38 stub level. What I think
 * the programmer was thinking was that the table is null terminated and so used the
 * level count as a correspondence to the ID of the final level, but the enum represents
 * an ID *after* the last stub level, not before or during it.
 *
 * Each table is terminated with NULL_TRIGGER
 */
struct CameraTrigger *sCameraTriggers[LEVEL_COUNT + 1] = {
    NULL,
    #include "levels/level_defines.h"
};
#undef _
#undef STUB_LEVEL
#undef DEFINE_LEVEL
#undef DEFINE_LEVEL_REMOVED

struct CutsceneSplinePoint sIntroStartToPipePosition[] = {
    { 0, 0, { 2122, 8762, 9114 } },  { 0, 0, { 2122, 8762, 9114 } },  { 1, 0, { 2122, 7916, 9114 } },
    { 1, 0, { 2122, 7916, 9114 } },  { 2, 0, { 957, 5166, 8613 } },   { 3, 0, { 589, 4338, 7727 } },
    { 4, 0, { 690, 3366, 6267 } },   { 5, 0, { -1600, 2151, 4955 } }, { 6, 0, { -1557, 232, 1283 } },
    { 7, 0, { -6962, -295, 2729 } }, { 8, 0, { -6979, 131, 3246 } },  { 9, 0, { -6360, -283, 4044 } },
    { 0, 0, { -5695, -334, 5264 } }, { 1, 0, { -5568, -319, 7933 } }, { 2, 0, { -3848, -200, 6278 } },
    { 3, 0, { -965, -263, 6092 } },  { 4, 0, { 1607, 2465, 6329 } },  { 5, 0, { 2824, 180, 3548 } },
    { 6, 0, { 1236, 136, 945 } },    { 0, 0, { 448, 136, 564 } },     { 0, 0, { 448, 136, 564 } },
    { 0, 0, { 448, 136, 564 } },     { -1, 0, { 448, 136, 564 } }
};

struct CutsceneSplinePoint sIntroStartToPipeFocus[] = {
    { 0, 50, { 1753, 29800, 8999 } }, { 0, 50, { 1753, 29800, 8999 } },
    { 1, 50, { 1753, 8580, 8999 } },  { 1, 100, { 1753, 8580, 8999 } },
    { 2, 50, { 520, 5400, 8674 } },   { 3, 50, { 122, 4437, 7875 } },
    { 4, 50, { 316, 3333, 6538 } },   { 5, 36, { -1526, 2189, 5448 } },
    { 6, 50, { -1517, 452, 1731 } },  { 7, 50, { -6659, -181, 3109 } },
    { 8, 17, { -6649, 183, 3618 } },  { 9, 20, { -6009, -214, 4395 } },
    { 0, 50, { -5258, -175, 5449 } }, { 1, 36, { -5158, -266, 7651 } },
    { 2, 26, { -3351, -192, 6222 } }, { 3, 25, { -483, -137, 6060 } },
    { 4, 100, { 1833, 2211, 5962 } }, { 5, 26, { 3022, 207, 3090 } },
    { 6, 20, { 1250, 197, 449 } },    { 7, 50, { 248, 191, 227 } },
    { 7, 0, { 48, 191, 227 } },       { 7, 0, { 48, 191, 227 } },
    { -1, 0, { 48, 191, 227 } }
};

/**
 * Describes the spline the camera follows, starting when the camera jumps to Lakitu and ending after
 * Mario jumps out of the pipe when the first dialog opens.  This table specifically updates the
 * camera's position.
 */
struct CutsceneSplinePoint sIntroPipeToDialogPosition[] = {
    { 0, 0, { -785, 625, 4527 } },  { 1, 0, { -785, 625, 4527 } },  { 2, 0, { -1286, 644, 4376 } },
    { 3, 0, { -1286, 623, 4387 } }, { 4, 0, { -1286, 388, 3963 } }, { 5, 0, { -1286, 358, 4093 } },
    { 6, 0, { -1386, 354, 4159 } }, { 7, 0, { -1477, 306, 4223 } }, { 8, 0, { -1540, 299, 4378 } },
    { 9, 0, { -1473, 316, 4574 } }, { 0, 0, { -1328, 485, 5017 } }, { 0, 0, { -1328, 485, 5017 } },
    { 0, 0, { -1328, 485, 5017 } }, { -1, 0, { -1328, 485, 5017 } }
};

/**
 * Describes the spline that the camera's focus follows, during the same part of the intro as the above.
 */
#ifdef VERSION_EU
struct CutsceneSplinePoint sIntroPipeToDialogFocus[] = {
    { 0, 25, { -1248, 450, 4596 } }, { 1, 71, { -1258, 485, 4606 } }, { 2, 71, { -1379, 344, 4769 } },
    { 3, 22, { -1335, 366, 4815 } }, { 4, 23, { -1315, 370, 4450 } }, { 5, 40, { -1322, 333, 4591 } },
    { 6, 25, { -1185, 329, 4616 } }, { 7, 21, { -1059, 380, 4487 } }, { 8, 14, { -1086, 421, 4206 } },
    { 9, 21, { -1321, 346, 4098 } }, { 0, 0, { -1328, 385, 4354 } },  { 0, 0, { -1328, 385, 4354 } },
    { 0, 0, { -1328, 385, 4354 } },  { -1, 0, { -1328, 385, 4354 } }
};
#else
struct CutsceneSplinePoint sIntroPipeToDialogFocus[] = {
    { 0, 20, { -1248, 450, 4596 } }, { 1, 59, { -1258, 485, 4606 } }, { 2, 59, { -1379, 344, 4769 } },
    { 3, 20, { -1335, 366, 4815 } }, { 4, 23, { -1315, 370, 4450 } }, { 5, 40, { -1322, 333, 4591 } },
    { 6, 25, { -1185, 329, 4616 } }, { 7, 21, { -1059, 380, 4487 } }, { 8, 14, { -1086, 421, 4206 } },
    { 9, 21, { -1321, 346, 4098 } }, { 0, 0, { -1328, 385, 4354 } },  { 0, 0, { -1328, 385, 4354 } },
    { 0, 0, { -1328, 385, 4354 } },  { -1, 0, { -1328, 385, 4354 } }
};
#endif

struct CutsceneSplinePoint sEndingFlyToWindowPos[] = {
    { 0, 0, { -86, 876, 640 } },   { 1, 0, { -86, 876, 610 } },   { 2, 0, { -66, 945, 393 } },
    { 3, 0, { -80, 976, 272 } },   { 4, 0, { -66, 1306, -36 } },  { 5, 0, { -70, 1869, -149 } },
    { 6, 0, { -10, 2093, -146 } }, { 7, 0, { -10, 2530, -248 } }, { 8, 0, { -10, 2530, -263 } },
    { 9, 0, { -10, 2530, -273 } }
};

struct CutsceneSplinePoint sEndingFlyToWindowFocus[] = {
    { 0, 50, { -33, 889, -7 } },    { 1, 35, { -33, 889, -7 } },    { 2, 31, { -17, 1070, -193 } },
    { 3, 25, { -65, 1182, -272 } }, { 4, 20, { -64, 1559, -542 } }, { 5, 25, { -68, 2029, -677 } },
    { 6, 25, { -9, 2204, -673 } },  { 7, 25, { -8, 2529, -772 } },  { 8, 0, { -8, 2529, -772 } },
    { 9, 0, { -8, 2529, -772 } },   { -1, 0, { -8, 2529, -772 } }
};

struct CutsceneSplinePoint sEndingPeachDescentCamPos[] = {
    { 0, 50, { 1, 120, -1150 } },    { 1, 50, { 1, 120, -1150 } },    { 2, 40, { 118, 121, -1199 } },
    { 3, 40, { 147, 74, -1306 } },   { 4, 40, { 162, 95, -1416 } },   { 5, 40, { 25, 111, -1555 } },
    { 6, 40, { -188, 154, -1439 } }, { 7, 40, { -203, 181, -1242 } }, { 8, 40, { 7, 191, -1057 } },
    { 9, 40, { 262, 273, -1326 } },  { 0, 40, { -4, 272, -1627 } },   { 1, 35, { -331, 206, -1287 } },
    { 2, 30, { -65, 219, -877 } },   { 3, 25, { 6, 216, -569 } },     { 4, 25, { -8, 157, 40 } },
    { 5, 25, { -4, 106, 200 } },     { 6, 25, { -6, 72, 574 } },      { 7, 0, { -6, 72, 574 } },
    { 8, 0, { -6, 72, 574 } },       { -1, 0, { -6, 72, 574 } }
};

struct CutsceneSplinePoint sEndingMarioToPeachPos[] = {
    { 0, 0, { -130, 1111, -1815 } }, { 1, 0, { -131, 1052, -1820 } }, { 2, 0, { -271, 1008, -1651 } },
    { 3, 0, { -439, 1043, -1398 } }, { 4, 0, { -433, 1040, -1120 } }, { 5, 0, { -417, 1040, -1076 } },
    { 6, 0, { -417, 1040, -1076 } }, { 7, 0, { -417, 1040, -1076 } }, { -1, 0, { -417, 1040, -1076 } }
};

struct CutsceneSplinePoint sEndingMarioToPeachFocus[] = {
    { 0, 50, { -37, 1020, -1332 } }, { 1, 20, { -36, 1012, -1330 } }, { 2, 20, { -24, 1006, -1215 } },
    { 3, 20, { 28, 1002, -1224 } },  { 4, 24, { 45, 1013, -1262 } },  { 5, 35, { 34, 1000, -1287 } },
    { 6, 0, { 34, 1000, -1287 } },   { 7, 0, { 34, 1000, -1287 } },   { -1, 0, { 34, 1000, -1287 } }
};

struct CutsceneSplinePoint sEndingLookUpAtCastle[] = {
    { 0, 50, { 200, 1066, -1414 } }, { 0, 50, { 200, 1066, -1414 } }, { 0, 30, { 198, 1078, -1412 } },
    { 0, 33, { 15, 1231, -1474 } },  { 0, 39, { -94, 1381, -1368 } }, { 0, 0, { -92, 1374, -1379 } },
    { 0, 0, { -92, 1374, -1379 } },  { -1, 0, { -92, 1374, -1379 } }
};

struct CutsceneSplinePoint sEndingLookAtSkyFocus[] = {
#ifdef VERSION_EU
    { 0, 50, { 484, 1368, -868 } }, { 0, 72, { 479, 1372, -872 } }, { 0, 50, { 351, 1817, -918 } },
#else
    { 0, 50, { 484, 1368, -888 } }, { 0, 72, { 479, 1372, -892 } }, { 0, 50, { 351, 1817, -918 } },
#endif
    { 0, 50, { 351, 1922, -598 } }, { 0, 0, { 636, 2027, -415 } },  { 0, 0, { 636, 2027, -415 } },
    { -1, 0, { 636, 2027, -415 } }
};

/**
 * Activates any CameraTriggers that Mario is inside.
 * Then, applies area-specific processing to the camera, such as setting the default mode, or changing
 * the mode based on the terrain type Mario is standing on.
 *
 * @return the camera's mode after processing, although this is unused in the code
 */
s16 camera_course_processing(struct Camera *c) {
    s16 level = gCurrLevelNum;
    s16 mode;
    s8 area = gCurrentArea->index;
    // Bounds iterator
    u32 b;
    // Camera trigger's bounding box
    Vec3q centerq, boundsq;
    u32 insideBounds = FALSE;
    u8 oldMode = c->mode;

    if (c->mode == CAMERA_MODE_C_UP) {
        c->mode = sModeInfo.lastMode;
    }
    check_blocking_area_processing(&c->mode);
    if (level > LEVEL_COUNT + 1) {
        level = LEVEL_COUNT + 1;
    }

    if (sCameraTriggers[level]) {
        b = 0;

        // Process positional triggers.
        // All triggered events are called, not just the first one.
        while (sCameraTriggers[level][b].event) {

            // Check only the current area's triggers
            if (sCameraTriggers[level][b].area == area) {
                // Copy the bounding box into center and bounds
                vec3q_set(centerq, q(sCameraTriggers[level][b].centerX),
                                  q(sCameraTriggers[level][b].centerY),
                                  q(sCameraTriggers[level][b].centerZ));
                vec3q_set(boundsq, q(sCameraTriggers[level][b].boundsX),
                                  q(sCameraTriggers[level][b].boundsY),
                                  q(sCameraTriggers[level][b].boundsZ));

                // Check if Mario is inside the bounds
                if (is_pos_in_boundsq(sMarioCamState->posq, centerq, boundsq,
                                                   sCameraTriggers[level][b].boundsYaw)) {
                    //! This should be checked before calling is_pos_in_bounds. (It doesn't belong
                    //! outside the while loop because some events disable area processing)
                    if (!(sStatusFlags & CAM_FLAG_BLOCK_AREA_PROCESSING)) {
                        sCameraTriggers[level][b].event(c);
                        insideBounds = TRUE;
                    }
                }
            }

            if ((sCameraTriggers[level])[b].area == -1) {
                // Default triggers are only active if Mario is not already inside another trigger
                if (!insideBounds) {
                    if (!(sStatusFlags & CAM_FLAG_BLOCK_AREA_PROCESSING)) {
                        sCameraTriggers[level][b].event(c);
                    }
                }
            }

            b += 1;
        }
    }

    // Area-specific camera processing
    if (!(sStatusFlags & CAM_FLAG_BLOCK_AREA_PROCESSING)) {
        switch (gCurrLevelArea) {
            case AREA_WF:
                if (sMarioCamState->action == ACT_RIDING_HOOT) {
                    transition_to_camera_mode(c, CAMERA_MODE_SLIDE_HOOT, 60);
                } else {
                    switch (sMarioGeometry.currFloorType) {
                        case SURFACE_CAMERA_8_DIR:
                            transition_to_camera_mode(c, CAMERA_MODE_8_DIRECTIONS, 90);
                            s8DirModeBaseYaw = DEGREES(90);
                            break;

                        case SURFACE_BOSS_FIGHT_CAMERA:
                            if (gCurrActNum == 1) {
                                set_camera_mode_boss_fight(c);
                            } else {
                                set_camera_mode_radial(c, 60);
                            }
                            break;
                        default:
                            set_camera_mode_radial(c, 60);
                    }
                }
                break;

            case AREA_BBH:
                // if camera is fixed at bbh_room_13_balcony_camera (but as floats)
                if (vec3q_compare(sFixedModeBasePositionq, q(210), q(420), q(3109)) == 1)
                {
                    if (sMarioCamState->posq[1] < q(1800)) {
                        transition_to_camera_mode(c, CAMERA_MODE_CLOSE, 30);
                    }
                }
                break;

            case AREA_SSL_PYRAMID:
                set_mode_if_not_set_by_surface(c, CAMERA_MODE_OUTWARD_RADIAL);
                break;

            case AREA_SSL_OUTSIDE:
                set_mode_if_not_set_by_surface(c, CAMERA_MODE_RADIAL);
                break;

            case AREA_THI_HUGE:
                break;

            case AREA_THI_TINY:
                surface_type_modes_thi(c);
                break;

            case AREA_TTC:
                set_mode_if_not_set_by_surface(c, CAMERA_MODE_OUTWARD_RADIAL);
                break;

            case AREA_BOB:
                if (set_mode_if_not_set_by_surface(c, CAMERA_MODE_NONE) == 0) {
                    if (sMarioGeometry.currFloorType == SURFACE_BOSS_FIGHT_CAMERA) {
                        set_camera_mode_boss_fight(c);
                    } else {
                        if (c->mode == CAMERA_MODE_CLOSE) {
                            transition_to_camera_mode(c, CAMERA_MODE_RADIAL, 60);
                        } else {
                            set_camera_mode_radial(c, 60);
                        }
                    }
                }
                break;

            case AREA_WDW_MAIN:
                switch (sMarioGeometry.currFloorType) {
                    case SURFACE_INSTANT_WARP_1B:
                        c->defMode = CAMERA_MODE_RADIAL;
                        break;
                }
                break;

            case AREA_WDW_TOWN:
                switch (sMarioGeometry.currFloorType) {
                    case SURFACE_INSTANT_WARP_1C:
                        c->defMode = CAMERA_MODE_CLOSE;
                        break;
                }
                break;

            case AREA_DDD_WHIRLPOOL:
                //! @bug this does nothing
                gLakituState.defMode = CAMERA_MODE_OUTWARD_RADIAL;
                break;

            case AREA_DDD_SUB:
                if ((c->mode != CAMERA_MODE_BEHIND_MARIO)
                    && (c->mode != CAMERA_MODE_WATER_SURFACE)) {
                    if (((sMarioCamState->action & ACT_FLAG_ON_POLE) != 0)
                        || (sMarioGeometry.currFloorHeight > 800.f)) {
                        transition_to_camera_mode(c, CAMERA_MODE_8_DIRECTIONS, 60);

                    } else {
                        if (sMarioCamState->posq[1] < q(800)) {
                            transition_to_camera_mode(c, CAMERA_MODE_FREE_ROAM, 60);
                        }
                    }
                }
                //! @bug this does nothing
                gLakituState.defMode = CAMERA_MODE_FREE_ROAM;
                break;
        }
    }

    sStatusFlags &= ~CAM_FLAG_BLOCK_AREA_PROCESSING;
    if (oldMode == CAMERA_MODE_C_UP) {
        sModeInfo.lastMode = c->mode;
        c->mode = oldMode;
    }
    mode = c->mode;
    return mode;
}

/**
 * Move `pos` between the nearest floor and ceiling
 * @param lastGood unused, passed as the last position the camera was in -- removed
 */
void resolve_geometry_collisionsq(Vec3q posq) {
    q32 ceilYq, floorYq;
    struct Surface *surf;

    q32_find_wall_collision(&posq[0], &posq[1], &posq[2], 0, q(100));
    floorYq = find_floorq(posq[0], posq[1] + q(50), posq[2], &surf);
    ceilYq = find_ceilq(posq[0], posq[1] - q(50), posq[2], &surf);

    if ((q(FLOOR_LOWER_LIMIT) != floorYq) && (q(CELL_HEIGHT_LIMIT) == ceilYq)) {
        if (posq[1] < (floorYq += q(125))) {
            posq[1] = floorYq;
        }
    }

    if ((q(FLOOR_LOWER_LIMIT) == floorYq) && (q(CELL_HEIGHT_LIMIT) != ceilYq)) {
        if (posq[1] > (ceilYq -= q(125))) {
            posq[1] = ceilYq;
        }
    }

    if ((q(FLOOR_LOWER_LIMIT) != floorYq) && (q(CELL_HEIGHT_LIMIT) != ceilYq)) {
        floorYq += q(125);
        ceilYq -= q(125);

        if ((posq[1] <= floorYq) && (posq[1] < ceilYq)) {
            posq[1] = floorYq;
        }
        if ((posq[1] > floorYq) && (posq[1] >= ceilYq)) {
            posq[1] = ceilYq;
        }
        if ((posq[1] <= floorYq) && (posq[1] >= ceilYq)) {
            posq[1] = (floorYq + ceilYq) / 2;
        }
    }
}

/**
 * Checks for any walls obstructing Mario from view, and calculates a new yaw that the camera should
 * rotate towards.
 *
 * @param[out] avoidYaw the angle (from Mario) that the camera should rotate towards to avoid the wall.
 *                      The camera then approaches avoidYaw until Mario is no longer obstructed.
 *                      avoidYaw is always parallel to the wall.
 * @param yawRange      how wide of an arc to check for walls obscuring Mario.
 *
 * @return 3 if a wall is covering Mario, 1 if a wall is only near the camera.
 */
s32 rotate_camera_around_wallsq(struct Camera *c, Vec3q cPosq, s16 *avoidYaw, s16 yawRange) {
    struct WallCollisionData colData;
    struct Surface *wall;
    q32 dummyDistq, checkDistq;
    q32 coarseRadiusq;
    q32 fineRadiusq;
    s16 wallYaw, horWallNorm;
    s16 dummyPitch;
    // The yaw of the vector from Mario to the camera.
    s16 yawFromMario;
    s32 status = 0;
    /// The current iteration. The algorithm takes 8 equal steps from Mario back to the camera.
    s32 step = 0;

    vec3q_get_dist_and_angle(sMarioCamState->posq, cPosq, &dummyDistq, &dummyPitch, &yawFromMario);
    sStatusFlags &= ~CAM_FLAG_CAM_NEAR_WALL;
    colData.offsetYq = q(100);
    // The distance from Mario to Lakitu
    checkDistq = 0;
    /// The radius used to find potential walls to avoid.
    /// @bug Increases to 250.f, but the max collision radius is 200.f
    coarseRadiusq = q(150);
    /// This only increases when there is a wall collision found in the coarse pass
    fineRadiusq = q(100);

    for (step = 0; step < 8; step++) {
        // Start at Mario, move backwards to Lakitu's position
        colData.xq = sMarioCamState->posq[0] + qmul(cPosq[0] - sMarioCamState->posq[0], checkDistq);
        colData.yq = sMarioCamState->posq[1] + qmul(cPosq[1] - sMarioCamState->posq[1], checkDistq);
        colData.zq = sMarioCamState->posq[2] + qmul(cPosq[2] - sMarioCamState->posq[2], checkDistq);
        colData.radiusq = coarseRadiusq;
        // Increase the coarse check radius
        camera_approach_q32_symmetric_bool(&coarseRadiusq, q(250), q(30));

        if (find_wall_collisions(&colData) != 0) {
            wall = colData.walls[colData.numWalls - 1];

            // If we're over halfway from Mario to Lakitu, then there's a wall near the camera, but
            // not necessarily obstructing Mario
            if (step >= 5) {
                sStatusFlags |= CAM_FLAG_CAM_NEAR_WALL;
                if (status <= 0) {
                    status = 1;
                    wall = colData.walls[colData.numWalls - 1];
                    // wallYaw is parallel to the wall, not perpendicular
                    wallYaw = atan2sq((q32) wall->compressed_normal.z * QONE / COMPRESSED_NORMAL_ONE, (q32) wall->compressed_normal.x * QONE / COMPRESSED_NORMAL_ONE) + DEGREES(90);
                    // Calculate the avoid direction. The function returns the opposite direction so add 180
                    // degrees.
                    *avoidYaw = calc_avoid_yaw(yawFromMario, wallYaw) + DEGREES(180);
                }
            }

            colData.xq = sMarioCamState->posq[0] + qmul(cPosq[0] - sMarioCamState->posq[0], checkDistq);
            colData.yq = sMarioCamState->posq[1] + qmul(cPosq[1] - sMarioCamState->posq[1], checkDistq);
            colData.zq = sMarioCamState->posq[2] + qmul(cPosq[2] - sMarioCamState->posq[2], checkDistq);
            colData.radiusq = fineRadiusq;
            // Increase the fine check radius
            camera_approach_q32_symmetric_bool(&fineRadiusq, q(200), q(20));

            if (find_wall_collisions(&colData) != 0) {
                wall = colData.walls[colData.numWalls - 1];
                horWallNorm = atan2sq((q32) wall->compressed_normal.z * QONE / COMPRESSED_NORMAL_ONE, (q32) wall->compressed_normal.x * QONE / COMPRESSED_NORMAL_ONE);
                wallYaw = horWallNorm + DEGREES(90);
                // If Mario would be blocked by the surface, then avoid it
                if ((is_range_behind_surfaceq(sMarioCamState->posq, cPosq, wall, yawRange, SURFACE_WALL_MISC) == 0)
                    && (is_mario_behind_surface(c, wall) == TRUE)
                    // Also check if the wall is tall enough to cover Mario
                    && (is_surf_within_bounding_boxq(wall, -QONE, q(150), -QONE) == FALSE)) {
                    // Calculate the avoid direction. The function returns the opposite direction so add 180
                    // degrees.
                    *avoidYaw = calc_avoid_yaw(yawFromMario, wallYaw) + DEGREES(180);
                    camera_approach_s16_symmetric_bool(avoidYaw, horWallNorm, yawRange);
                    status = 3;
                    step = 8;
                }
            }
        }
        checkDistq += q(0.125);
    }

    return status;
}

/**
 * Stores type and height of the nearest floor and ceiling to Mario in `pg`
 *
 * Note: Also finds the water level, but waterHeight is unused
 */
void find_mario_floor_and_ceil(struct PlayerGeometry *pg) {
    struct Surface *surf;
    s16 tempCheckingSurfaceCollisionsForCamera = gCheckingSurfaceCollisionsForCamera;
    gCheckingSurfaceCollisionsForCamera = TRUE;

    if (find_floorq(sMarioCamState->posq[0], sMarioCamState->posq[1] + q(10),
                   sMarioCamState->posq[2], &surf) != q(FLOOR_LOWER_LIMIT)) {
        pg->currFloorType = surf->type;
    } else {
        pg->currFloorType = 0;
    }

    if (find_ceilq(sMarioCamState->posq[0], sMarioCamState->posq[1] - q(10),
                  sMarioCamState->posq[2], &surf) != q(CELL_HEIGHT_LIMIT)) {
        pg->currCeilType = surf->type;
    } else {
        pg->currCeilType = 0;
    }

    gCheckingSurfaceCollisionsForCamera = FALSE;
    pg->currFloorHeight = qtof(find_floorq(sMarioCamState->posq[0],
                                     sMarioCamState->posq[1] + q(10),
                                     sMarioCamState->posq[2], &pg->currFloor));
    pg->currCeilHeight = qtof(find_ceilq(sMarioCamState->posq[0],
                                   sMarioCamState->posq[1] - q(10),
                                   sMarioCamState->posq[2], &pg->currCeil));
    //pg->waterHeight = find_water_level(sMarioCamState->pos[0], sMarioCamState->pos[2]);
    gCheckingSurfaceCollisionsForCamera = tempCheckingSurfaceCollisionsForCamera;
}

/**
 * Start a cutscene focusing on an object
 * This will play if nothing else happened in the same frame, like exiting or warping.
 */
void start_object_cutscene(u8 cutscene, struct Object *o) {
    sObjectCutscene = cutscene;
    gRecentCutscene = 0;
    gCutsceneFocus = o;
    gObjCutsceneDone = FALSE;
}

/**
 * Start a low-priority cutscene without focusing on an object
 * This will play if nothing else happened in the same frame, like exiting or warping.
 */
u8 start_object_cutscene_without_focus(u8 cutscene) {
    sObjectCutscene = cutscene;
    sCutsceneDialogResponse = DIALOG_RESPONSE_NONE;
    return 0;
}

s16 unused_dialog_cutscene_response(u8 cutscene) {
    // if not in a cutscene, start this one
    if ((gCamera->cutscene == 0) && (sObjectCutscene == 0)) {
        sObjectCutscene = cutscene;
    }

    // if playing this cutscene and Mario responded, return the response
    if ((gCamera->cutscene == cutscene) && (sCutsceneDialogResponse)) {
        return sCutsceneDialogResponse;
    } else {
        return 0;
    }
}

s16 cutscene_object_with_dialog(u8 cutscene, struct Object *o, s16 dialogID) {
    s16 response = DIALOG_RESPONSE_NONE;

    if ((gCamera->cutscene == 0) && (sObjectCutscene == 0)) {
        if (gRecentCutscene != cutscene) {
            start_object_cutscene(cutscene, o);
            if (dialogID != DIALOG_NONE) {
                sCutsceneDialogID = dialogID;
            } else {
                sCutsceneDialogID = DIALOG_001;
            }
        } else {
            response = sCutsceneDialogResponse;
        }

        gRecentCutscene = 0;
    }
    return response;
}

s16 cutscene_object_without_dialog(u8 cutscene, struct Object *o) {
    s16 response = cutscene_object_with_dialog(cutscene, o, DIALOG_NONE);
    return response;
}

/**
 * @return 0 if not started, 1 if started, and -1 if finished
 */
s16 cutscene_object(u8 cutscene, struct Object *o) {
    s16 status = 0;

    if ((gCamera->cutscene == 0) && (sObjectCutscene == 0)) {
        if (gRecentCutscene != cutscene) {
            start_object_cutscene(cutscene, o);
            status = 1;
        } else {
            status = -1;
        }
    }
    return status;
}

/**
 * Update the camera's yaw and nextYaw. This is called from cutscenes to ignore the camera mode's yaw.
 */
void update_camera_yaw(struct Camera *c) {
    c->nextYaw = calculate_yawq(c->focusq, c->posq);
    c->yaw = c->nextYaw;
}

void cutscene_reset_spline(void) {
    sCutsceneSplineSegment = 0;
    sCutsceneSplineSegmentProgressq = 0;
}

void stop_cutscene_and_retrieve_stored_info(struct Camera *c) {
    gCutsceneTimer = CUTSCENE_STOP;
    c->cutscene = 0;
    vec3q_copy(c->focusq, sCameraStoreCutscene.focusq);
    vec3q_copy(c->posq, sCameraStoreCutscene.posq);
}

void cap_switch_save(UNUSED s16 dummy) {
    save_file_do_save(gCurrSaveFileNum - 1);
}

void init_spline_point(struct CutsceneSplinePoint *splinePoint, s8 index, u8 speed, Vec3s point) {
    splinePoint->index = index;
    splinePoint->speed = speed;
    vec3s_copy(splinePoint->point, point);
}

// TODO: (Scrub C)
void copy_spline_segment(struct CutsceneSplinePoint dst[], struct CutsceneSplinePoint src[]) {
    s32 j = 0;
    s32 i = 0;

    init_spline_point(&dst[i], src[j].index, src[j].speed, src[j].point);
    i += 1;
    do {
        do {
            init_spline_point(&dst[i], src[j].index, src[j].speed, src[j].point);
            i += 1;
            j += 1;
        } while ((src[j].index != -1) && (src[j].index != -1)); //! same comparison performed twice
    } while (j > 16);

    // Create the end of the spline by duplicating the last point
    do { init_spline_point(&dst[i], 0, src[j].speed, src[j].point); } while (0);
    do { init_spline_point(&dst[i + 1], 0, 0, src[j].point); } while (0);
    do { init_spline_point(&dst[i + 2], 0, 0, src[j].point); } while (0);
    do { init_spline_point(&dst[i + 3], -1, 0, src[j].point); } while (0);
}

/**
 * Triggers Mario to enter a dialog state. This is used to make Mario look at the focus of a cutscene,
 * for example, bowser.
 * @param state 0 = stop, 1 = start, 2 = start and look up, and 3 = start and look down
 *
 * @return if Mario left the dialog state, return CUTSCENE_LOOP, else return gCutsceneTimer
 */
s16 cutscene_common_set_dialog_state(s32 state) {
    s16 timer = gCutsceneTimer;
    // If the dialog ended, return CUTSCENE_LOOP, which would end the cutscene shot
    if (set_mario_npc_dialog(state) == MARIO_DIALOG_STATUS_SPEAK) {
        timer = CUTSCENE_LOOP;
    }
    return timer;
}

/// Unused SSL cutscene?
static UNUSED void unused_cutscene_mario_dialog_looking_down(UNUSED struct Camera *c) {
    gCutsceneTimer = cutscene_common_set_dialog_state(MARIO_DIALOG_LOOK_DOWN);
}

/**
 * Cause Mario to enter the normal dialog state.
 */
static BAD_RETURN(s32) cutscene_mario_dialog(UNUSED struct Camera *c) {
    gCutsceneTimer = cutscene_common_set_dialog_state(MARIO_DIALOG_LOOK_FRONT);
}

/// Unused SSL cutscene?
static UNUSED void unused_cutscene_mario_dialog_looking_up(UNUSED struct Camera *c) {
    gCutsceneTimer = cutscene_common_set_dialog_state(MARIO_DIALOG_LOOK_UP);
}

/**
 * Lower the volume (US only) and start the peach letter background music
 */
BAD_RETURN(s32) cutscene_intro_peach_start_letter_music(UNUSED struct Camera *c) {
#if defined(VERSION_US) || defined(VERSION_SH)
    seq_player_lower_volume(SEQ_PLAYER_LEVEL, 60, 40);
#endif
    cutscene_intro_peach_play_message_music();
}

/**
 * Raise the volume (not in JP) and start the flying music.
 */
BAD_RETURN(s32) cutscene_intro_peach_start_flying_music(UNUSED struct Camera *c) {
#ifndef VERSION_JP
    seq_player_unlower_volume(SEQ_PLAYER_LEVEL, 60);
#endif
    cutscene_intro_peach_play_lakitu_flying_music();
}

#ifdef VERSION_EU
/**
 * Lower the volume for the letter background music. In US, this happens on the same frame as the music
 * starts.
 */
BAD_RETURN(s32) cutscene_intro_peach_eu_lower_volume(UNUSED struct Camera *c) {
    seq_player_lower_volume(SEQ_PLAYER_LEVEL, 60, 40);
}
#endif

void reset_pan_distance(UNUSED struct Camera *c) {
    sPanDistanceq = 0;
}

/**
 * Easter egg: the player 2 controller can move the camera's focus in the ending and credits.
 */
void player2_rotate_cam(struct Camera *c, s16 minPitch, s16 maxPitch, s16 minYaw, s16 maxYaw) {
    q32 distCamToFocusq;
    s16 pitch, yaw, pitchCap;

    // Change the camera rotation to match the 2nd player's stick
    approach_s16_asymptotic_bool(&sCreditsPlayer2Yaw, -(s16)(gPlayer2Controller->stickX * 250.f), 4);
    approach_s16_asymptotic_bool(&sCreditsPlayer2Pitch, -(s16)(gPlayer2Controller->stickY * 265.f), 4);
    vec3q_get_dist_and_angle(c->posq, c->focusq, &distCamToFocusq, &pitch, &yaw);

    pitchCap = 0x3800 - pitch; if (pitchCap < 0) {
        pitchCap = 0;
    }
    if (maxPitch > pitchCap) {
        maxPitch = pitchCap;
    }

    pitchCap = -0x3800 - pitch;
    if (pitchCap > 0) {
        pitchCap = 0;
    }
    if (minPitch < pitchCap) {
        minPitch = pitchCap;
    }

    if (sCreditsPlayer2Pitch > maxPitch) {
        sCreditsPlayer2Pitch = maxPitch;
    }
    if (sCreditsPlayer2Pitch < minPitch) {
        sCreditsPlayer2Pitch = minPitch;
    }

    if (sCreditsPlayer2Yaw > maxYaw) {
        sCreditsPlayer2Yaw = maxYaw;
    }
    if (sCreditsPlayer2Yaw < minYaw) {
        sCreditsPlayer2Yaw = minYaw;
    }

    pitch += sCreditsPlayer2Pitch;
    yaw += sCreditsPlayer2Yaw;
    vec3q_set_dist_and_angle(c->posq, sPlayer2FocusOffsetq, distCamToFocusq, pitch, yaw);
    vec3q_sub(sPlayer2FocusOffsetq, c->focusq);
}

/**
 * Store camera info for the cannon opening cutscene
 */
void store_info_cannon(struct Camera *c) {
    vec3q_copy(sCameraStoreCutscene.posq, c->posq);
    vec3q_copy(sCameraStoreCutscene.focusq, c->focusq);
    sCameraStoreCutscene.panDistq = sPanDistanceq;
    sCameraStoreCutscene.cannonYOffsetq = sCannonYOffsetq;
}

/**
 * Retrieve camera info for the cannon opening cutscene
 */
void retrieve_info_cannon(struct Camera *c) {
    vec3q_copy(c->posq, sCameraStoreCutscene.posq);
    vec3q_copy(c->focusq, sCameraStoreCutscene.focusq);
    sPanDistanceq = sCameraStoreCutscene.panDistq;
    sCannonYOffsetq = sCameraStoreCutscene.cannonYOffsetq;
}

/**
 * Store camera info for the star spawn cutscene
 */
void store_info_star(struct Camera *c) {
    reset_pan_distance(c);
    vec3q_copy(sCameraStoreCutscene.posq, c->posq);
    sCameraStoreCutscene.focusq[0] = sMarioCamState->posq[0];
    sCameraStoreCutscene.focusq[1] = c->focusq[1];
    sCameraStoreCutscene.focusq[2] = sMarioCamState->posq[2];
}

/**
 * Retrieve camera info for the star spawn cutscene
 */
void retrieve_info_star(struct Camera *c) {
    vec3q_copy(c->posq, sCameraStoreCutscene.posq);
    vec3q_copy(c->focusq, sCameraStoreCutscene.focusq);
}

/**
 * Rotate the camera's focus around the camera's position by incYaw and incPitch
 */
void pan_camera(struct Camera *c, s16 incPitch, s16 incYaw) {
    q32 distCamToFocusq;
    s16 pitch, yaw;

    vec3q_get_dist_and_angle(c->posq, c->focusq, &distCamToFocusq, &pitch, &yaw);
    pitch += incPitch;     yaw += incYaw;
    vec3q_set_dist_and_angle(c->posq, c->focusq, distCamToFocusq, pitch, yaw);
}

BAD_RETURN(s32) cutscene_shake_explosion(UNUSED struct Camera *c) {
    set_environmental_camera_shake(SHAKE_ENV_EXPLOSION);
    cutscene_set_fov_shake_preset(1);
}

static UNUSED void unused_start_bowser_bounce_shake(UNUSED struct Camera *c) {
    set_environmental_camera_shake(SHAKE_ENV_BOWSER_THROW_BOUNCE);
}

/**
 * Change the spherical coordinates of `to` relative to `from` by `incDist`, `incPitch`, and `incYaw`
 *
 * @param from    the base position
 * @param[out] to the destination position
 */
void rotate_and_move_vec3q(Vec3q toq, Vec3q fromq, q32 incDistq, s16 incPitch, s16 incYaw) {
    q32 distq;
    s16 pitch, yaw;

    vec3q_get_dist_and_angle(fromq, toq, &distq, &pitch, &yaw);
    pitch += incPitch;
    yaw += incYaw;
    distq += incDistq;
    vec3q_set_dist_and_angle(fromq, toq, distq, pitch, yaw);
}

void set_flag_post_door(struct Camera *c) {
    sStatusFlags |= CAM_FLAG_BEHIND_MARIO_POST_DOOR;
    sCameraYawAfterDoorCutscene = calculate_yawq(c->focusq, c->posq);
}

void cutscene_soften_music(UNUSED struct Camera *c) {
    seq_player_lower_volume(SEQ_PLAYER_LEVEL, 60, 40);
}

void cutscene_unsoften_music(UNUSED struct Camera *c) {
    seq_player_unlower_volume(SEQ_PLAYER_LEVEL, 60);
}

UNUSED static void stub_camera_5(UNUSED struct Camera *c) {
}

BAD_RETURN(s32) cutscene_unused_start(UNUSED struct Camera *c) {
}

BAD_RETURN(s32) cutscene_unused_loop(UNUSED struct Camera *c) {
}

/**
 * Set the camera position and focus for when Mario falls from the sky.
 */
BAD_RETURN(s32) cutscene_ending_mario_fall_start(struct Camera *c) {
    vec3q_set(c->focusq, q(-26), q(0), q(-137));
    vec3q_set(c->posq, q(165), q(4725), q(324));
}

/**
 * Focus on Mario when he's falling from the sky.
 */
BAD_RETURN(s32) cutscene_ending_mario_fall_focus_mario(struct Camera *c) {
    Vec3q offsetq;
    vec3q_set(offsetq, 0, q(80), 0);

    offsetq[2] = qmul(ABSi(sMarioCamState->posq[1] - c->posq[1]), q(-0.1));
    if (offsetq[2] > q(-100)) {
        offsetq[2] = q(-100);
    }

    offset_rotatedq(c->focusq, sMarioCamState->posq, offsetq, sMarioCamState->faceAngle);
}

/**
 * Mario falls from the sky after the grand star cutscene.
 */
BAD_RETURN(s32) cutscene_ending_mario_fall(struct Camera *c) {
    cutscene_event(cutscene_ending_mario_fall_start, c, 0, 0);
    cutscene_event(cutscene_ending_mario_fall_focus_mario, c, 0, -1);
    player2_rotate_cam(c, -0x2000, 0x2000, -0x2000, 0x2000);
}

/**
 * Closeup of Mario as the wing cap fades and Mario looks up.
 */
BAD_RETURN(s32) cutscene_ending_mario_land_closeup(struct Camera *c) {
    vec3q_set(c->focusq, q(85), q(826), q(250));
    vec3q_set(c->posq, q(-51), q(988), q(-202));
    player2_rotate_cam(c, -0x2000, 0x2000, -0x2000, 0x2000);
}

/**
 * Reset the spline progress and cvar9.
 */
BAD_RETURN(s32) cutscene_ending_reset_spline(UNUSED struct Camera *c) {
    sCutsceneVars[9].pointq[0] = 0;
    cutscene_reset_spline();
}

/**
 * Follow sEndingFlyToWindowPos/Focus up to the window.
 */
BAD_RETURN(s32) cutscene_ending_fly_up_to_window(struct Camera *c) {
    move_point_along_splineq(c->posq, sEndingFlyToWindowPos, &sCutsceneSplineSegment, &sCutsceneSplineSegmentProgressq);
    move_point_along_splineq(c->focusq, sEndingFlyToWindowFocus, &sCutsceneSplineSegment, &sCutsceneSplineSegmentProgressq);
}

/**
 * Move the camera up to the window as the star power frees peach.
 */
BAD_RETURN(s32) cutscene_ending_stars_free_peach(struct Camera *c) {
    cutscene_event(cutscene_ending_reset_spline, c, 0, 0);
    cutscene_event(cutscene_ending_fly_up_to_window, c, 0, -1);
    player2_rotate_cam(c, -0x2000, 0x2000, -0x2000, 0x2000);
}

/**
 * Move the camera to the ground as Mario lands.
 */
BAD_RETURN(s32) cutscene_ending_mario_land(struct Camera *c) {
    vec3q_set(c->focusq, q(sEndingFlyToWindowFocus[0].point[0]), q(sEndingFlyToWindowFocus[0].point[1]) + q(80), q(sEndingFlyToWindowFocus[0].point[2]));
    vec3q_set(c->posq, q(sEndingFlyToWindowPos[0].point[0]), q(sEndingFlyToWindowPos[0].point[1]), q(sEndingFlyToWindowPos[0].point[2]) + q(150));
    player2_rotate_cam(c, -0x800, 0x2000, -0x2000, 0x2000);
}

/**
 * Move the camera closer to peach appearing.
 */
BAD_RETURN(s32) cutscene_ending_peach_appear_closeup(struct Camera *c) {
    vec3q_set(c->posq, q(179), q(2463), q(-1216));
    c->posq[1] = QFIELD(gCutsceneFocus, oPosY) + q(35);
    vec3q_set(c->focusq, QFIELD(gCutsceneFocus, oPosX), QFIELD(gCutsceneFocus, oPosY) + q(125), QFIELD(gCutsceneFocus, oPosZ));
}

/**
 * Peach fades in, the camera focuses on her.
 */
BAD_RETURN(s32) cutscene_ending_peach_appears(struct Camera *c) {
    cutscene_event(cutscene_ending_peach_appear_closeup, c, 0, 0);
    approach_q32_asymptotic_bool(&c->posq[1], QFIELD(gCutsceneFocus, oPosY) + q(35), q(0.02));
    approach_q32_asymptotic_bool(&c->focusq[1], QFIELD(gCutsceneFocus, oPosY) + q(125), q(0.15));
    player2_rotate_cam(c, -0x2000, 0x2000, -0x2000, 0x2000);
}

/**
 * Reset spline progress, set cvar2 y offset.
 */
BAD_RETURN(s32) cutscene_ending_peach_descends_start(UNUSED struct Camera *c) {
    cutscene_reset_spline();
    sCutsceneVars[2].pointq[1] = q(150);
}

/**
 * Follow the sEndingPeachDescentCamPos spline, which rotates around peach.
 */
BAD_RETURN(s32) cutscene_ending_follow_peach_descent(struct Camera *c) {
    move_point_along_splineq(c->posq, sEndingPeachDescentCamPos, &sCutsceneSplineSegment, &sCutsceneSplineSegmentProgressq);
    c->posq[1] += QFIELD(gCutsceneFocus, oPosY) + sCutsceneVars[3].pointq[1];
}

/**
 * Decrease cvar2's y offset while the camera flies backwards to Mario.
 */
BAD_RETURN(s32) cutscene_ending_peach_descent_lower_focus(UNUSED struct Camera *c) {
    camera_approach_q32_symmetric_bool(&(sCutsceneVars[2].pointq[1]), q(90), q(0.5));
}

/**
 * Keep following the sEndingPeachDescentCamPos spline, which leads back to Mario.
 */
BAD_RETURN(s32) cutscene_ending_peach_descent_back_to_mario(struct Camera *c) {
    Vec3q posq;

    move_point_along_splineq(posq, sEndingPeachDescentCamPos, &sCutsceneSplineSegment, &sCutsceneSplineSegmentProgressq);
    c->posq[0] = posq[0];
    c->posq[2] = posq[2];
    approach_q32_asymptotic_bool(&c->posq[1], (posq[1] += QFIELD(gCutsceneFocus, oPosY)), q(0.07));
}

/**
 * Peach starts floating to the ground. Rotate the camera around her, then fly backwards to Mario when
 * she lands.
 */
BAD_RETURN(s32) cutscene_ending_peach_descends(struct Camera *c) {
    cutscene_event(cutscene_ending_peach_descends_start, c, 0, 0);
    cutscene_event(cutscene_ending_follow_peach_descent, c, 0, 299);
    cutscene_event(cutscene_ending_peach_descent_back_to_mario, c, 300, -1);
    cutscene_event(cutscene_ending_peach_descent_lower_focus, c, 300, -1);
    vec3q_set(c->focusq, QFIELD(gCutsceneFocus, oPosX), sCutsceneVars[2].pointq[1] + QFIELD(gCutsceneFocus, oPosY),
              QFIELD(gCutsceneFocus, oPosZ));
    player2_rotate_cam(c, -0x2000, 0x2000, -0x2000, 0x2000);
}

/**
 * Mario runs across the bridge to peach, and takes off his cap.
 * Follow the sEndingMarioToPeach* splines while Mario runs across.
 */
BAD_RETURN(s32) cutscene_ending_mario_to_peach(struct Camera *c) {
    cutscene_event(cutscene_ending_reset_spline, c, 0, 0);
    move_point_along_splineq(c->posq, sEndingMarioToPeachPos, &sCutsceneSplineSegment, &sCutsceneSplineSegmentProgressq);
    move_point_along_splineq(c->focusq, sEndingMarioToPeachFocus, &sCutsceneSplineSegment, &sCutsceneSplineSegmentProgressq);
    player2_rotate_cam(c, -0x2000, 0x2000, -0x2000, 0x2000);
}

/**
 * Make the focus follow the sEndingLookUpAtCastle spline.
 */
BAD_RETURN(s32) cutscene_ending_look_up_at_castle(UNUSED struct Camera *c) {
    move_point_along_splineq(c->focusq, sEndingLookUpAtCastle, &sCutsceneSplineSegment, &sCutsceneSplineSegmentProgressq);
}

/**
 * Peach opens her eyes and the camera looks at the castle window again.
 */
BAD_RETURN(s32) cutscene_ending_peach_wakeup(struct Camera *c) {
    cutscene_event(cutscene_ending_reset_spline, c, 0, 0);
    cutscene_event(cutscene_ending_look_up_at_castle, c, 0, 0);
#ifdef VERSION_EU
    cutscene_event(cutscene_ending_look_up_at_castle, c, 265, -1);
    cutscene_spawn_obj(7, 315);
    cutscene_spawn_obj(9, 355);
#else
    cutscene_event(cutscene_ending_look_up_at_castle, c, 250, -1);
    cutscene_spawn_obj(7, 300);
    cutscene_spawn_obj(9, 340);
#endif
    vec3q_set(c->posq, q(-163), q(978), q(-1082));
    player2_rotate_cam(c, -0x800, 0x2000, -0x2000, 0x2000);
}

/**
 * Side view of peach and Mario. Peach thanks Mario for saving her.
 */
BAD_RETURN(s32) cutscene_ending_dialog(struct Camera *c) {
    vec3q_set(c->focusq, q(11), q(983), q(-1273));
    vec3q_set(c->posq, q(-473), q(970), q(-1152));
    player2_rotate_cam(c, -0x800, 0x2000, -0x2000, 0x2000);
}

/**
 * Zoom in and move the camera close to Mario and peach.
 */
BAD_RETURN(s32) cutscene_ending_kiss_closeup(struct Camera *c) {
    set_fov_function(CAM_FOV_SET_29);
    vec3q_set(c->focusq, q(350), q(1034), q(-1216));
    vec3q_set(c->posq, q(-149), q(1021), q(-1216));
}

/**
 * Fly back and zoom out for Mario's spin after the kiss.
 */
BAD_RETURN(s32) cutscene_ending_kiss_here_we_go(struct Camera *c) {
    Vec3q posq, focq;

    set_fov_function(CAM_FOV_DEFAULT);
    vec3q_set(focq, q(233), q(1068), q(-1298));
    vec3q_set(posq, q(-250), q(966), q(-1111));
    //! another double typo
    approach_vec3q_asymptotic(c->posq, posq, q(0.2), q(0.1), q(0.2));
    approach_vec3q_asymptotic(c->focusq, focq, q(0.2), q(0.1), q(0.2));
}

/**
 * Peach kisses Mario on the nose.
 */
BAD_RETURN(s32) cutscene_ending_kiss(struct Camera *c) {
    cutscene_event(cutscene_ending_kiss_closeup, c, 0, 0);
#ifdef VERSION_EU
    cutscene_event(cutscene_ending_kiss_here_we_go, c, 185, -1);
#else
    cutscene_event(cutscene_ending_kiss_here_we_go, c, 155, -1);
#endif
    player2_rotate_cam(c, -0x800, 0x2000, -0x2000, 0x2000);
}

/**
 * Make the focus follow sEndingLookAtSkyFocus.
 */
BAD_RETURN(s32) cutscene_ending_look_at_sky(struct Camera *c) {
    move_point_along_splineq(c->focusq, sEndingLookAtSkyFocus, &sCutsceneSplineSegment, &sCutsceneSplineSegmentProgressq);
    vec3q_set(c->posq, q(699), q(1680), q(-703));
}

/**
 * Zoom in the fov. The fovFunc was just set to default, so it wants to approach 45. But while this is
 * called, it will stay at about 37.26f
 */
BAD_RETURN(s32) cutscene_ending_zoom_fov(UNUSED struct Camera *c) {
    sFOVState.fovq = q(37);
}

/**
 * Peach suggests baking a cake for Mario. Mario looks back at the camera before going inside the castle.
 */
BAD_RETURN(s32) cutscene_ending_cake_for_mario(struct Camera *c) {
    cutscene_event(cutscene_ending_reset_spline, c, 0, 0);
    cutscene_event(cutscene_ending_look_at_sky, c, 0, 0);
    cutscene_event(cutscene_ending_zoom_fov, c, 0, 499);
    cutscene_event(cutscene_ending_look_at_sky, c, 500, -1);
    cutscene_spawn_obj(8, 600);
    cutscene_spawn_obj(8, 608);
    cutscene_spawn_obj(8, 624);
    cutscene_spawn_obj(8, 710);
}

/**
 * Stop the ending cutscene, reset the fov.
 */
BAD_RETURN(s32) cutscene_ending_stop(struct Camera *c) {
    set_fov_function(CAM_FOV_SET_45);
    c->cutscene = 0;
    gCutsceneTimer = CUTSCENE_STOP;
}

/**
 * Start the grand star cutscene.
 * cvar0 is a relative offset from Mario.
 * cvar1 is the is the camera's goal position.
 */
BAD_RETURN(s32) cutscene_grand_star_start(UNUSED struct Camera *c) {
    vec3q_set(sCutsceneVars[0].pointq, 0, q(150), q(-600));
    offset_rotatedq(sCutsceneVars[1].pointq, sMarioCamState->posq, sCutsceneVars[0].pointq, sMarioCamState->faceAngle);
    sCutsceneVars[1].pointq[1] = q(457);
}

/**
 * Make the camera fly to the front of Mario.
 */
BAD_RETURN(s32) cutscene_grand_star_front_of_mario(struct Camera *c) {
    q32 goalDistq;
    s16 goalPitch, goalYaw;
    q32 distq;
    s16 pitch, yaw;

    vec3q_get_dist_and_angle(sMarioCamState->posq, sCutsceneVars[1].pointq, &goalDistq, &goalPitch, &goalYaw);
    vec3q_get_dist_and_angle(sMarioCamState->posq, c->posq, &distq, &pitch, &yaw);
    approach_q32_asymptotic_bool(&distq, goalDistq, q(0.1));
    approach_s16_asymptotic_bool(&pitch, goalPitch, 32);
    approach_s16_asymptotic_bool(&yaw, goalYaw + 0x1200, 20);
    vec3q_set_dist_and_angle(sMarioCamState->posq, c->posq, distq, pitch, yaw);
}

/**
 * Started shortly after Mario starts the triple jump. Stores Mario's face angle and zeros cvar2.
 */
BAD_RETURN(s32) cutscene_grand_star_mario_jump(UNUSED struct Camera *c) {
    vec3s_set(sCutsceneVars[0].angle, 0, sMarioCamState->faceAngle[1], 0);
    vec3q_set(sCutsceneVars[2].pointq, 0, 0, 0);
}

/**
 * Accelerate cvar2 to point back and to the left (relative to the camera).
 */
BAD_RETURN(s32) cutscene_grand_star_accel_cvar2(UNUSED struct Camera *c) {
    camera_approach_q32_symmetric_bool(&sCutsceneVars[2].pointq[2], q(-40), q(2));
    sCutsceneVars[2].pointq[0] = q(5);
}

/**
 * Decrease cvar2 offset, follow Mario by directly updating the camera's pos.
 */
BAD_RETURN(s32) cutscene_grand_star_approach_mario(struct Camera *c) {
    camera_approach_q32_symmetric_bool(&sCutsceneVars[2].pointq[2], 0, q(2));
    sCutsceneVars[2].pointq[0] = 0;
    approach_q32_asymptotic_bool(&c->posq[0], sMarioCamState->posq[0], q(0.01));
    approach_q32_asymptotic_bool(&c->posq[2], sMarioCamState->posq[2], q(0.01));
}

/**
 * Offset the camera's position by cvar2. Before Mario triple jumps, this moves back and to the left.
 * After the triple jump, cvar2 decelerates to 0.
 */
BAD_RETURN(s32) cutscene_grand_star_move_cvar2(struct Camera *c) {
    offset_rotatedq(c->posq, c->posq, sCutsceneVars[2].pointq, sCutsceneVars[0].angle);
}

BAD_RETURN(s32) cutscene_grand_star_focus_mario(struct Camera *c) {
    Vec3q focq;

    vec3q_set(focq, sMarioCamState->posq[0], (sMarioCamState->posq[1] - q(307)) / 2 + q(407), sMarioCamState->posq[2]);
    approach_vec3q_asymptotic(c->focusq, focq, q(0.5), q(0.8), q(0.5));
}

/**
 * The first part of the grand star cutscene, after Mario has collected the grand star.
 */
BAD_RETURN(s32) cutscene_grand_star(struct Camera *c) {
    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;
    cutscene_event(cutscene_grand_star_start, c, 0, 0);
    cutscene_event(cutscene_grand_star_front_of_mario, c, 0, 109);
    cutscene_event(cutscene_grand_star_focus_mario, c, 0, -1);
    cutscene_event(cutscene_grand_star_mario_jump, c, 110, 110);
    cutscene_event(cutscene_grand_star_accel_cvar2, c, 110, 159);
    cutscene_event(cutscene_grand_star_approach_mario, c, 160, -1);
    cutscene_event(cutscene_grand_star_move_cvar2, c, 110, -1);
}

/**
 * Zero the cvars that are used when Mario is flying.
 */
BAD_RETURN(s32) cutscene_grand_star_fly_start(struct Camera *c) {
    //! cvar7 is unused in grand star
    vec3q_set(sCutsceneVars[7].pointq, q(0.5), q(0.5), q(0.5));
    //! cvar6 is unused in grand star
    vec3q_set(sCutsceneVars[6].pointq, q(0.01), q(0.01), q(0.01));
    vec3q_set(sCutsceneVars[4].pointq, 0, 0, 0);
    vec3q_set(sCutsceneVars[5].pointq, 0, c->focusq[1] - sMarioCamState->posq[1], 0);
    sCutsceneVars[8].pointq[2] = 0;
    sCutsceneVars[8].pointq[0] = 0;
}

/**
 * Decrease the cvar offsets so that Lakitu flies closer to Mario.
 */
BAD_RETURN(s32) cutscene_grand_star_fly_move_to_mario(UNUSED struct Camera *c) {
    Vec3q posOffq;

    vec3q_set(posOffq, q(-600), 0, q(-400));
    approach_vec3q_asymptotic(sCutsceneVars[4].pointq, posOffq, q(0.05), q(0.05), q(0.05));
    camera_approach_q32_symmetric_bool(&sCutsceneVars[5].pointq[1], q(0), q(2));
    camera_approach_q32_symmetric_bool(&sCutsceneVars[5].pointq[2], q(-200), q(6));
}

/**
 * Gradually increase the cvar offsets so Lakitu flies away. Mario flies offscreen to the right.
 *
 * cvar4 is the position offset from Mario.
 * cvar5 is the focus offset from Mario.
 * cvar8.point[0] is the approach velocity.
 */
BAD_RETURN(s32) cutscene_grand_star_fly_mario_offscreen(UNUSED struct Camera *c) {
    camera_approach_q32_symmetric_bool(&sCutsceneVars[8].pointq[0], q(15), q(0.1));

    camera_approach_q32_symmetric_bool(&sCutsceneVars[4].pointq[0], q(-2000), sCutsceneVars[8].pointq[0]);
    camera_approach_q32_symmetric_bool(&sCutsceneVars[4].pointq[1], q(1200), sCutsceneVars[8].pointq[0] / 10);
    camera_approach_q32_symmetric_bool(&sCutsceneVars[4].pointq[2], q(1000), sCutsceneVars[8].pointq[0] / 10);

    camera_approach_q32_symmetric_bool(&sCutsceneVars[5].pointq[0], q(0), sCutsceneVars[8].pointq[0]);
    camera_approach_q32_symmetric_bool(&sCutsceneVars[5].pointq[1], q(1200), sCutsceneVars[8].pointq[0] / 2);
    camera_approach_q32_symmetric_bool(&sCutsceneVars[5].pointq[2], q(1000), qdiv(sCutsceneVars[8].pointq[0], q(1.5)));
}

/**
 * Make Lakitu approach the cvars.
 * cvar4 is the position offset.
 * cvar5 is the focus offset.
 */
BAD_RETURN(s32) cutscene_grand_star_fly_app_cvars(struct Camera *c) {
    Vec3q goalPosq, goalFocq;
	q32 distq;
    s16 pitch, yaw;

    camera_approach_q32_symmetric_bool(&sCutsceneVars[8].pointq[2], q(90), q(2.5));
    offset_rotatedq(goalPosq, sMarioCamState->posq, sCutsceneVars[4].pointq, sMarioCamState->faceAngle);
    offset_rotatedq(goalFocq, sMarioCamState->posq, sCutsceneVars[5].pointq, sMarioCamState->faceAngle);

    // Move towards goalPos by cvar8's Z speed
    vec3q_get_dist_and_angle(goalPosq, c->posq, &distq, &pitch, &yaw);
    camera_approach_q32_symmetric_bool(&distq, 0, sCutsceneVars[8].pointq[2]);
    vec3q_set_dist_and_angle(goalPosq, c->posq, distq, pitch, yaw);

    approach_vec3q_asymptotic(c->posq, goalPosq, q(0.01), q(0.01), q(0.01));
    approach_vec3q_asymptotic(c->focusq, goalFocq, q(0.5), q(0.8), q(0.5));
}

/**
 * Part of the grand star cutscene, starts after Mario is flying.
 *
 * cvar4 and cvar5 are directions, relative to Mario:
 * cvar4 is used as the camera position's offset from Mario.
 * cvar5 is used as the camera focus's offset from Mario.
 *
 * cvar8.point[2] is Lakitu's speed.
 */
BAD_RETURN(s32) cutscene_grand_star_fly(struct Camera *c) {
    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;
    cutscene_event(cutscene_grand_star_fly_start, c, 0, 0);
    cutscene_event(cutscene_grand_star_fly_move_to_mario, c, 0, 140);
    cutscene_event(cutscene_grand_star_fly_mario_offscreen, c, 141, -1);
    cutscene_event(cutscene_grand_star_fly_app_cvars, c, 0, -1);
}

/**
 * Adjust the camera focus towards a point `dist` units in front of Mario.
 * @param dist distance in Mario's forward direction. Note that this is relative to Mario, so a negative
 *        distance will focus in front of Mario, and a positive distance will focus behind him.
 */
void focus_in_front_of_marioq(struct Camera *c, q32 distq, q32 speedq) {
    Vec3q goalFocusq, offsetq;

    offsetq[0] = 0;
    offsetq[2] = distq;
    offsetq[1] = q(100);

    offset_rotatedq(goalFocusq, sMarioCamState->posq, offsetq, sMarioCamState->faceAngle);
    approach_vec3q_asymptotic(c->focusq, goalFocusq, speedq, speedq, speedq);
}

/**
 * Approach Mario and look up. Since Mario faces the camera when he collects the star, there's no need
 * to worry about the camera's yaw.
 */
BAD_RETURN(s32) cutscene_dance_move_to_mario(struct Camera *c) {
    s16 pitch, yaw;
    q32 distq;

    vec3q_get_dist_and_angle(sMarioCamState->posq, c->posq, &distq, &pitch, &yaw);
    approach_q32_asymptotic_bool(&distq, q(600), q(0.3));
    approach_s16_asymptotic_bool(&pitch, 0x1000, 0x10);
    vec3q_set_dist_and_angle(sMarioCamState->posq, c->posq, distq, pitch, yaw);
}

BAD_RETURN(s32) cutscene_dance_rotate(struct Camera *c) {
    rotate_and_move_vec3q(c->posq, sMarioCamState->posq, 0, 0, 0x200);
}

BAD_RETURN(s32) cutscene_dance_rotate_move_back(struct Camera *c) {
    rotate_and_move_vec3q(c->posq, sMarioCamState->posq, q(-15), 0, 0);
}

BAD_RETURN(s32) cutscene_dance_rotate_move_towards_mario(struct Camera *c) {
    rotate_and_move_vec3q(c->posq, sMarioCamState->posq, q(20), 0, 0);
}

/**
 * Speculated to be dance-related due to its proximity to the other dance functions
 */
//UNUSED static BAD_RETURN(s32) cutscene_dance_unused(UNUSED struct Camera *c) {
//}

/**
 * Slowly turn to the point 100 units in front of Mario
 */
BAD_RETURN(s32) cutscene_dance_default_focus_mario(struct Camera *c) {
    focus_in_front_of_marioq(c, q(-100), q(0.2));
}

/**
 * Focus twice as far away as default dance, and move faster.
 */
BAD_RETURN(s32) cutscene_dance_rotate_focus_mario(struct Camera *c) {
    focus_in_front_of_marioq(c, q(-200), q(0.03));
}

BAD_RETURN(s32) cutscene_dance_shake_fov(UNUSED struct Camera *c) {
    set_fov_shake(0x200, 0x28, 0x8000);
}

/**
 * Handles both the default and rotate dance cutscenes.
 * In the default dance: the camera moves closer to Mario, then stays in place.
 * In the rotate dance: the camera moves closer and rotates clockwise around Mario.
 */
BAD_RETURN(s32) cutscene_dance_default_rotate(struct Camera *c) {
    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;
    sYawSpeed = 0;
    set_fov_function(CAM_FOV_DEFAULT);
    cutscene_event(cutscene_dance_default_focus_mario, c, 0, 20);
    cutscene_event(cutscene_dance_move_to_mario, c, 0, 39);
    // Shake the camera on the 4th beat of the music, when Mario gives the peace sign.
    cutscene_event(cutscene_dance_shake_fov, c, 40, 40);

    if (c->cutscene != CUTSCENE_DANCE_DEFAULT) { // CUTSCENE_DANCE_ROTATE
        cutscene_event(cutscene_dance_rotate_focus_mario, c, 75, 102);
        cutscene_event(cutscene_dance_rotate, c, 50, -1);
        // These two functions move the camera away and then towards Mario.
        cutscene_event(cutscene_dance_rotate_move_back, c, 50, 80);
        cutscene_event(cutscene_dance_rotate_move_towards_mario, c, 70, 90);
    } else {
        // secret star, 100 coin star, or bowser red coin star.
        if ((sMarioCamState->action != ACT_STAR_DANCE_NO_EXIT)
            && (sMarioCamState->action != ACT_STAR_DANCE_WATER)
            && (sMarioCamState->action != ACT_STAR_DANCE_EXIT)) {
            gCutsceneTimer = CUTSCENE_STOP;
            c->cutscene = 0;
            transition_next_state(c, 20);
            sStatusFlags |= CAM_FLAG_UNUSED_CUTSCENE_ACTIVE;
        }
    }
}

/**
 * If the camera's yaw is out of the range of `absYaw` +- `yawMax`, then set the yaw to `absYaw`
 */
BAD_RETURN(s32) star_dance_bound_yaw(struct Camera *c, s16 absYaw, s16 yawMax) {
    s16 dummyPitch, yaw;
    q32 distCamToMarioq;
    s16 yawFromAbs;

    vec3q_get_dist_and_angle(sMarioCamState->posq, c->posq, &distCamToMarioq, &dummyPitch, &yaw);
    yawFromAbs = yaw - absYaw;

    // Because angles are s16, this checks if yaw is negative
    if ((yawFromAbs & 0x8000) != 0) {
        yawFromAbs = -yawFromAbs;
    }
    if (yawFromAbs > yawMax) {
        yaw = absYaw;
        c->nextYaw = yaw;
        c->yaw = yaw;
    }
}

/**
 * Start the closeup dance cutscene by restricting the camera's yaw in certain areas.
 * Store the camera's focus in cvar9.
 */
BAD_RETURN(s32) cutscene_dance_closeup_start(struct Camera *c) {
    if ((gLastCompletedStarNum == 4) && (gCurrCourseNum == COURSE_JRB)) {
        star_dance_bound_yaw(c, 0x0, 0x4000);
    }
    if ((gLastCompletedStarNum == 1) && (gCurrCourseNum == COURSE_DDD)) {
        star_dance_bound_yaw(c, 0x8000, 0x5000);
    }
    if ((gLastCompletedStarNum == 5) && (gCurrCourseNum == COURSE_WDW)) {
        star_dance_bound_yaw(c, 0x8000, 0x800);
    }

    vec3q_copy(sCutsceneVars[9].pointq, c->focusq);
    //! cvar8 is unused in the closeup cutscene
    sCutsceneVars[8].angle[0] = 0x2000;
}

/**
 * Focus the camera on Mario eye height.
 */
BAD_RETURN(s32) cutscene_dance_closeup_focus_mario(struct Camera *c) {
    Vec3q marioPosq;

    vec3q_set(marioPosq, sMarioCamState->posq[0], sMarioCamState->posq[1] + q(125), sMarioCamState->posq[2]);
    approach_vec3q_asymptotic(sCutsceneVars[9].pointq, marioPosq, q(0.2), q(0.2), q(0.2));
    vec3q_copy(c->focusq, sCutsceneVars[9].pointq);
}

/**
 * Fly above Mario, looking down.
 */
BAD_RETURN(s32) cutscene_dance_closeup_fly_above(struct Camera *c) {
    s16 pitch, yaw;
    q32 distq;
    s16 goalPitch = 0x1800;

    if ((gLastCompletedStarNum == 6 && gCurrCourseNum == COURSE_SL) ||
        (gLastCompletedStarNum == 4 && gCurrCourseNum == COURSE_TTC)) {
        goalPitch = 0x800;
    }

    vec3q_get_dist_and_angle(sMarioCamState->posq, c->posq, &distq, &pitch, &yaw);
    approach_q32_asymptotic_bool(&distq, q(800), q(0.05));
    approach_s16_asymptotic_bool(&pitch, goalPitch, 16);
    approach_s16_asymptotic_bool(&yaw, c->yaw, 8);
    vec3q_set_dist_and_angle(sMarioCamState->posq, c->posq, distq, pitch, yaw);
}

/**
 * Fly closer right when Mario gives the peace sign.
 */
BAD_RETURN(s32) cutscene_dance_closeup_fly_closer(struct Camera *c) {
    s16 pitch, yaw;
    q32 distq;

    vec3q_get_dist_and_angle(sMarioCamState->posq, c->posq, &distq, &pitch, &yaw);
    approach_q32_asymptotic_bool(&distq, q(240), q(0.4));
    approach_s16_asymptotic_bool(&yaw, c->yaw, 8);
    approach_s16_asymptotic_bool(&pitch, 0x1000, 5);
    vec3q_set_dist_and_angle(sMarioCamState->posq, c->posq, distq, pitch, yaw);
}

/**
 * Zoom in by increasing fov to 80 degrees. Most dramatic zoom in the game.
 */
BAD_RETURN(s32) cutscene_dance_closeup_zoom(UNUSED struct Camera *c) {
    set_fov_function(CAM_FOV_APP_80);
}

/**
 * Shake fov, starts on the first frame Mario has the peace sign up.
 */
BAD_RETURN(s32) cutscene_dance_closeup_shake_fov(UNUSED struct Camera *c) {
    set_fov_shake(0x300, 0x30, 0x8000);
}

/**
 * The camera moves in for a closeup on Mario. Used for stars that are underwater or in tight places.
 */
BAD_RETURN(s32) cutscene_dance_closeup(struct Camera *c) {
    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;

    if (sMarioCamState->action == ACT_STAR_DANCE_WATER) {
        cutscene_event(cutscene_dance_closeup_start, c, 0, 0);
        cutscene_event(cutscene_dance_closeup_focus_mario, c, 0, -1);
        cutscene_event(cutscene_dance_closeup_fly_above, c, 0, 62);
        cutscene_event(cutscene_dance_closeup_fly_closer, c, 63, -1);
        cutscene_event(cutscene_dance_closeup_zoom, c, 63, 63);
        cutscene_event(cutscene_dance_closeup_shake_fov, c, 70, 70);
    } else {
        cutscene_event(cutscene_dance_closeup_start, c, 0, 0);
        cutscene_event(cutscene_dance_closeup_focus_mario, c, 0, -1);
        // Almost twice as fast as under water
        cutscene_event(cutscene_dance_closeup_fly_above, c, 0, 32);
        cutscene_event(cutscene_dance_closeup_fly_closer, c, 33, -1);
        cutscene_event(cutscene_dance_closeup_zoom, c, 33, 33);
        cutscene_event(cutscene_dance_closeup_shake_fov, c, 40, 40);
    }
    set_handheld_shake(HAND_CAM_SHAKE_CUTSCENE);
}

/**
 * cvar8.point[2] is the amount to increase distance from Mario
 */
BAD_RETURN(s32) cutscene_dance_fly_away_start(struct Camera *c) {
    Vec3q areaCenterq;

    vec3q_copy(sCutsceneVars[9].pointq, c->focusq);
    sCutsceneVars[8].pointq[2] = q(65);

    if (c->mode == CAMERA_MODE_RADIAL) {
        vec3q_set(areaCenterq, c->areaCenXq, c->areaCenYq, c->areaCenZq);
        c->yaw = calculate_yawq(areaCenterq, c->posq);
        c->nextYaw = c->yaw;
    }

    // Restrict the camera yaw in tight spaces
    if ((gLastCompletedStarNum == 6) && (gCurrCourseNum == COURSE_CCM)) {
        star_dance_bound_yaw(c, 0x5600, 0x800);
    }
    if ((gLastCompletedStarNum == 2) && (gCurrCourseNum == COURSE_TTM)) {
        star_dance_bound_yaw(c, 0x0,    0x800);
    }
    if ((gLastCompletedStarNum == 1) && (gCurrCourseNum == COURSE_SL)) {
        star_dance_bound_yaw(c, 0x2000, 0x800);
    }
    if ((gLastCompletedStarNum == 3) && (gCurrCourseNum == COURSE_RR)) {
        star_dance_bound_yaw(c, 0x0,    0x800);
    }
}

BAD_RETURN(s32) cutscene_dance_fly_away_approach_mario(struct Camera *c) {
    s16 pitch, yaw;
    q32 distq;

    vec3q_get_dist_and_angle(sMarioCamState->posq, c->posq, &distq, &pitch, &yaw);
    approach_q32_asymptotic_bool(&distq, q(600), q(0.3));
    approach_s16_asymptotic_bool(&pitch, 0x1000, 16);
    approach_s16_asymptotic_bool(&yaw, c->yaw, 8);
    vec3q_set_dist_and_angle(sMarioCamState->posq, c->posq, distq, pitch, yaw);
}

BAD_RETURN(s32) cutscene_dance_fly_away_focus_mario(struct Camera *c) {
    Vec3q marioPosq;

    vec3q_set(marioPosq, sMarioCamState->posq[0], sMarioCamState->posq[1] + q(125), sMarioCamState->posq[2]);
    approach_vec3q_asymptotic(sCutsceneVars[9].pointq, marioPosq, q(0.2), q(0.2), q(0.2));
    vec3q_copy(c->focusq, sCutsceneVars[9].pointq);
}

/**
 * Slowly pan the camera downwards and to the camera's right, using cvar9's angle.
 */
void cutscene_pan_cvar9(struct Camera *c) {
    vec3q_copy(c->focusq, sCutsceneVars[9].pointq);
    sCutsceneVars[9].angle[0] -= 29;
    sCutsceneVars[9].angle[1] += 29;
    pan_camera(c, sCutsceneVars[9].angle[0], sCutsceneVars[9].angle[1]);
}

/**
 * Move backwards and rotate slowly around Mario.
 */
BAD_RETURN(s32) cutscene_dance_fly_rotate_around_mario(struct Camera *c) {
    cutscene_pan_cvar9(c);
    rotate_and_move_vec3q(c->posq, sMarioCamState->posq, sCutsceneVars[8].pointq[2], 0, 0);
}

/**
 * Rotate quickly while Lakitu flies up.
 */
BAD_RETURN(s32) cutscene_dance_fly_away_rotate_while_flying(struct Camera *c) {
    rotate_and_move_vec3q(c->posq, sMarioCamState->posq, 0, 0, 0x80);
}

BAD_RETURN(s32) cutscene_dance_fly_away_shake_fov(UNUSED struct Camera *c) {
    set_fov_shake(0x400, 0x30, 0x8000);
}

/**
 * After collecting the star, Lakitu flies upwards out of the course.
 */
BAD_RETURN(s32) cutscene_dance_fly_away(struct Camera *c) {
    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;
    cutscene_event(cutscene_dance_fly_away_start, c, 0, 0);
    cutscene_event(cutscene_dance_fly_away_focus_mario, c, 0, 30);
    cutscene_event(cutscene_dance_fly_away_approach_mario, c, 0, 30);
    cutscene_event(cutscene_dance_fly_rotate_around_mario, c, 55, 124);
    cutscene_event(cutscene_dance_fly_away_rotate_while_flying, c, 55, 124);
    cutscene_event(cutscene_dance_fly_away_shake_fov, c, 40, 40);
    set_fov_function(CAM_FOV_DEFAULT);
    set_handheld_shake(HAND_CAM_SHAKE_STAR_DANCE);
}

/**
 * Jump the camera pos and focus to cvar 8 and 7.
 * Called every frame, starting after 10, so when these cvars are updated, the camera will jump.
 */
BAD_RETURN(s32) cutscene_key_dance_jump_cvar(struct Camera *c) {
    offset_rotatedq(c->posq, sMarioCamState->posq, sCutsceneVars[8].pointq, sMarioCamState->faceAngle);
    offset_rotatedq(c->focusq, sMarioCamState->posq, sCutsceneVars[7].pointq, sMarioCamState->faceAngle);
}

/**
 * Jump to a closeup view of Mario and the key.
 */
BAD_RETURN(s32) cutscene_key_dance_jump_closeup(UNUSED struct Camera *c) {
    vec3q_set(sCutsceneVars[8].pointq, q(38), q(171), q(-248));
    vec3q_set(sCutsceneVars[7].pointq, q(-57), q(51), q(187));
}

/**
 * Jump to a view from the lower left (Mario's right).
 */
BAD_RETURN(s32) cutscene_key_dance_jump_lower_left(UNUSED struct Camera *c) {
    vec3q_set(sCutsceneVars[8].pointq, q(-178), q(62), q(-132));
    vec3q_set(sCutsceneVars[7].pointq, q(299), q(91), q(58));
}

/**
 * Jump to a rotated view from above.
 */
BAD_RETURN(s32) cutscene_key_dance_jump_above(UNUSED struct Camera *c) {
    gLakituState.keyDanceRoll = 0x2800;
    vec3q_set(sCutsceneVars[8].pointq, q(89), q(373), q(-304));
    vec3q_set(sCutsceneVars[7].pointq, 0, q(127), 0);
}

/**
 * Finally, jump to a further view, slightly to Mario's left.
 */
BAD_RETURN(s32) cutscene_key_dance_jump_last(UNUSED struct Camera *c) {
    gLakituState.keyDanceRoll = 0;
    vec3q_set(sCutsceneVars[8].pointq, q(135), q(158), q(-673));
    vec3q_set(sCutsceneVars[7].pointq, q(-20), q(135), q(-198));
}

BAD_RETURN(s32) cutscene_key_dance_shake_fov(UNUSED struct Camera *c) {
    set_fov_shake(0x180, 0x30, 0x8000);
}

BAD_RETURN(s32) cutscene_key_dance_handheld_shake(UNUSED struct Camera *c) {
    set_handheld_shake(HAND_CAM_SHAKE_CUTSCENE);
}

BAD_RETURN(s32) cutscene_key_dance_focus_mario(struct Camera *c) {
    focus_in_front_of_marioq(c, 0, q(0.2));
}

/**
 * Cutscene that plays when Mario collects a key from bowser. It's basically a sequence of four jump
 * cuts.
 */
BAD_RETURN(s32) cutscene_key_dance(struct Camera *c) {
    cutscene_event(cutscene_dance_move_to_mario, c, 0, 10);
    cutscene_event(cutscene_key_dance_focus_mario, c, 0, 10);
    cutscene_event(cutscene_key_dance_jump_closeup, c, 0, 0);
    cutscene_event(cutscene_key_dance_jump_lower_left, c, 20, 20);
    cutscene_event(cutscene_key_dance_jump_above, c, 35, 35);
    cutscene_event(cutscene_key_dance_jump_last, c, 52, 52);
    cutscene_event(cutscene_key_dance_jump_cvar, c, 11, -1);
    cutscene_event(cutscene_key_dance_shake_fov, c, 54, 54);
    cutscene_event(cutscene_key_dance_handheld_shake, c, 52, -1);
}

BAD_RETURN(s32) cutscene_bowser_area_shake_fov(UNUSED struct Camera *c) {
    cutscene_set_fov_shake_preset(2);
}

/**
 * Set oBowserCamAct to 1, which causes bowser to start walking.
 */
BAD_RETURN(s32) cutscene_bowser_area_start_bowser_walking(UNUSED struct Camera *c) {
    gSecondCameraFocus->oBowserCamAct = BOWSER_CAM_ACT_WALK;
}

/**
 * Offset the camera from bowser using cvar2 and cvar3
 * @bug cvar2.point is (0,0,0) on the first frame, but because of the warp transition, this behavior
 *      isn't seen. After the first frame, cvar2.point is bowser's position.
 */
BAD_RETURN(s32) cutscene_bowser_arena_set_pos(struct Camera *c) {
    vec3q_set_dist_and_angle(sCutsceneVars[2].pointq, c->posq, sCutsceneVars[3].pointq[2],
                                  sCutsceneVars[3].angle[0], sCutsceneVars[3].angle[1]);
    vec3q_set(sCutsceneVars[2].pointq, QFIELD(gSecondCameraFocus, oPosX), QFIELD(gSecondCameraFocus, oPosY),
              QFIELD(gSecondCameraFocus, oPosZ));
}

/**
 * Apply a sine wave to the focus's y coordinate.
 * The y offset starts at 120, then decreases to 0 before reaching ~240 on the last frame.
 */
BAD_RETURN(s32) cutscene_bowser_arena_focus_sine(UNUSED struct Camera *c) {
    //! unused initialization
    //q32 yOffq = q(150);

    // cvar4 was zeroed when the cutscene started.
    q32 yOffq = sinqs(sCutsceneVars[4].angle[1]) * 120 + q(120);
    sCutsceneVars[4].angle[1] -= 0x200;
    approach_q32_asymptotic_bool(&sCutsceneVars[0].pointq[1], yOffq, q(0.5));
}

/**
 * Set the camera focus according to cvar0 and cvar2.
 */
BAD_RETURN(s32) cutscene_bowser_arena_set_focus(struct Camera *c) {
    offset_rotatedq(c->focusq, sCutsceneVars[2].pointq, sCutsceneVars[0].pointq, sCutsceneVars[2].angle);
}

/**
 * Adjust the cvar offsets, making the camera look up, move slightly further back, and focus a little
 * further in front of bowser.
 */
BAD_RETURN(s32) cutscene_bowser_arena_adjust_offsets(UNUSED struct Camera *c) {
    approach_s16_asymptotic_bool(&sCutsceneVars[3].angle[0], 0x6C8, 30);
    approach_q32_asymptotic_bool(&sCutsceneVars[0].pointq[2], q(-200), q(0.02));
    approach_q32_asymptotic_bool(&sCutsceneVars[3].pointq[2], q(550), q(0.02));
}

/**
 * Decrease cvar0's z offset, making the camera focus pan left towards bowser.
 */
BAD_RETURN(s32) cutscene_bowser_arena_pan_left(UNUSED struct Camera *c) {
    approach_q32_asymptotic_bool(&sCutsceneVars[0].pointq[2], 0, q(0.05));
}

/**
 * Duplicate of cutscene_mario_dialog().
 */
BAD_RETURN(s32) cutscene_bowser_arena_mario_dialog(UNUSED struct Camera *c) {
    cutscene_common_set_dialog_state(MARIO_DIALOG_LOOK_FRONT);
}

void cutscene_stop_dialog(UNUSED struct Camera *c) {
    cutscene_common_set_dialog_state(MARIO_DIALOG_STOP);
}

/**
 * Active for the first 5 frames of the cutscene.
 * cvar3 is the camera's polar offset from bowser
 * cvar2.angle is bowser's move angle
 *
 * cvar0 is the focus offset from bowser
 */
BAD_RETURN(s32) cutscene_bowser_arena_start(struct Camera *c) {
    sCutsceneVars[3].pointq[2] = q(430);
    sCutsceneVars[3].angle[1] = gSecondCameraFocus->oMoveAngleYaw - DEGREES(45);
    sCutsceneVars[3].angle[0] = 0xD90;

    //! Tricky math: Bowser starts at (0, 307, -1000), with a moveAngle of (0,0,0). A sane person would
    //! expect this offset to move the focus to (0, 427, -1800).
    //! BUT because offset_rotated() flips the Z direction (to match sm64's coordinate system), this
    //! offset actually moves the focus to (0, 427, -200)
    vec3q_set(sCutsceneVars[0].pointq, q(0), q(120), q(-800));
    vec3s_set(sCutsceneVars[2].angle, gSecondCameraFocus->oMoveAnglePitch,
              gSecondCameraFocus->oMoveAngleYaw, gSecondCameraFocus->oMoveAngleRoll);

    // Set the camera's position and focus.
    cutscene_bowser_arena_set_pos(c);
    cutscene_bowser_arena_set_focus(c);
}

/**
 * Create the dialog box depending on which bowser fight Mario is in.
 */
BAD_RETURN(s32) bowser_fight_intro_dialog(UNUSED struct Camera *c) {
    s16 dialog;

    switch (gCurrLevelNum) {
        case LEVEL_BOWSER_1:
            dialog = DIALOG_067;
            break;
        case LEVEL_BOWSER_2:
            dialog = DIALOG_092;
            break;
        default: // LEVEL_BOWSER_3
            dialog = DIALOG_093;
    }

    create_dialog_box(dialog);
}

/**
 * Create the dialog box and wait until it's gone.
 */
BAD_RETURN(s32) cutscene_bowser_arena_dialog(struct Camera *c) {
    cutscene_event(bowser_fight_intro_dialog, c, 0, 0);

    if (get_dialog_id() == DIALOG_NONE) {
        gCutsceneTimer = CUTSCENE_LOOP;
    }
}

/**
 * End the bowser arena cutscene.
 */
BAD_RETURN(s32) cutscene_bowser_arena_end(struct Camera *c) {
    cutscene_stop_dialog(c);
    c->cutscene = 0;
    transition_next_state(c, 20);
    sStatusFlags |= CAM_FLAG_UNUSED_CUTSCENE_ACTIVE;
    sModeOffsetYaw = sMarioCamState->faceAngle[1] + DEGREES(90);
    gSecondCameraFocus->oBowserCamAct = BOWSER_CAM_ACT_END;
}

/**
 * Cutscene that plays when Mario enters a bowser fight.
 */
BAD_RETURN(s32) cutscene_bowser_arena(struct Camera *c) {
    //! This does nothing, but may have been used in development
    cutscene_spawn_obj(2, 0);

    if (gSecondCameraFocus != NULL) {
        cutscene_event(cutscene_bowser_arena_mario_dialog, c, 0, -1);
        cutscene_event(cutscene_bowser_arena_start, c, 0, 5);
        cutscene_event(cutscene_bowser_area_start_bowser_walking, c, 40, 40);
        cutscene_event(cutscene_bowser_area_shake_fov, c, 145, 145);
        cutscene_event(cutscene_bowser_arena_set_pos, c, 40, -1);
        cutscene_event(cutscene_bowser_arena_pan_left, c, 40, 99);
        cutscene_event(cutscene_bowser_arena_adjust_offsets, c, 100, -1);
        cutscene_event(cutscene_bowser_arena_focus_sine, c, 40, 140);
        cutscene_event(cutscene_bowser_arena_set_focus, c, 40, -1);
        cutscene_event(cutscene_shake_explosion, c, 60, 60);
        cutscene_event(cutscene_shake_explosion, c, 82, 82);
        cutscene_event(cutscene_shake_explosion, c, 109, 109);
        cutscene_event(cutscene_shake_explosion, c, 127, 127);
    }
}

BAD_RETURN(s32) cutscene_star_spawn_store_info(struct Camera *c) {
    store_info_star(c);
}

/**
 * Focus on the top of the star.
 */
BAD_RETURN(s32) cutscene_star_spawn_focus_star(struct Camera *c) {
    Vec3q starPosq;

    if (gCutsceneFocus != NULL) {
        object_pos_to_vec3q(starPosq, gCutsceneFocus);
        starPosq[1] += q(gCutsceneFocus->hitboxHeight_s16);
        approach_vec3q_asymptotic(c->focusq, starPosq, q(0.1), q(0.1), q(0.1));
    }
}

/**
 * Use boss fight mode's update function to move the focus back.
 */
BAD_RETURN(s32) cutscene_star_spawn_update_boss_fight(struct Camera *c) {
    Vec3q posq, focusq;

    update_boss_fight_cameraq(c, focusq, posq);
    approach_vec3q_asymptotic(c->focusq, focusq, q(0.2), q(0.2), q(0.2));
    approach_vec3q_asymptotic(c->posq, posq, q(0.2), q(0.2), q(0.2));
}

/**
 * Fly back to the camera's previous pos and focus.
 */
BAD_RETURN(s32) cutscene_star_spawn_fly_back(struct Camera *c) {
    retrieve_info_star(c);
    transition_next_state(c, 15);
}

/**
 * Plays when a star spawns (ie from a box).
 */
BAD_RETURN(s32) cutscene_star_spawn(struct Camera *c) {
    cutscene_event(cutscene_star_spawn_store_info, c, 0, 0);
    cutscene_event(cutscene_star_spawn_focus_star, c, 0, -1);
    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;

    if (gObjCutsceneDone) {
        // Set the timer to CUTSCENE_LOOP, which start the next shot.
        gCutsceneTimer = CUTSCENE_LOOP;
    }
}

/**
 * Move the camera back to Mario.
 */
BAD_RETURN(s32) cutscene_star_spawn_back(struct Camera *c) {
    if ((c->mode == CAMERA_MODE_BOSS_FIGHT) && (set_cam_angle(0) == CAM_ANGLE_LAKITU)) {
        cutscene_event(cutscene_star_spawn_update_boss_fight, c, 0, -1);
    } else {
        cutscene_event(cutscene_star_spawn_fly_back, c, 0, 0);
    }

    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;
    sStatusFlags |= CAM_FLAG_UNUSED_CUTSCENE_ACTIVE;
}

BAD_RETURN(s32) cutscene_star_spawn_end(struct Camera *c) {
    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;
    gCutsceneTimer = CUTSCENE_STOP;
    c->cutscene = 0;
}

BAD_RETURN(s32) cutscene_exit_waterfall_warp(struct Camera *c) {
    //! hardcoded position
    vec3q_set(c->posq, q(-3899), q(39), q(-5671));
}

/**
 * Look at Mario, used by cutscenes that play when Mario exits a course to castle grounds.
 */
BAD_RETURN(s32) cutscene_exit_to_castle_grounds_focus_mario(struct Camera *c) {
    vec3q_copy(c->focusq, sMarioCamState->posq);
    c->focusq[1] = c->posq[1] + (sMarioCamState->posq[1] + q(125) - c->posq[1]) / 2;
    approach_vec3q_asymptotic(c->focusq, sMarioCamState->posq, q(0.05), q(0.4), q(0.05));
}

/**
 * Cutscene that plays when Mario leaves CotMC through the waterfall.
 */
BAD_RETURN(s32) cutscene_exit_waterfall(struct Camera *c) {
    cutscene_event(cutscene_exit_waterfall_warp, c, 0, 0);
    cutscene_event(cutscene_exit_to_castle_grounds_focus_mario, c, 0, -1);
    update_camera_yaw(c);
}

/**
 * End the cutscene, used by cutscenes that play when Mario exits a course to castle grounds.
 */
BAD_RETURN(s32) cutscene_exit_to_castle_grounds_end(struct Camera *c) {
    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;
    gCutsceneTimer = CUTSCENE_STOP;
    c->cutscene = 0;
    update_camera_yaw(c);
}

BAD_RETURN(s32) cutscene_exit_fall_to_castle_grounds_warp(struct Camera *c) {
    //! hardcoded position
    vec3q_set(c->posq, q(5830), q(32), q(3985));
}

/**
 * Cutscene that plays when Mario falls from WMOTR.
 */
BAD_RETURN(s32) cutscene_exit_fall_to_castle_grounds(struct Camera *c) {
    cutscene_event(cutscene_exit_fall_to_castle_grounds_warp, c, 0, 0);
    cutscene_event(cutscene_exit_to_castle_grounds_focus_mario, c, 0, -1);
    update_camera_yaw(c);
}

/**
 * Start the red coin star spawning cutscene.
 */
BAD_RETURN(s32) cutscene_red_coin_star_start(struct Camera *c) {
    object_pos_to_vec3q(sCutsceneVars[1].pointq, gCutsceneFocus);
    store_info_star(c);
    // Store the default fov for after the cutscene
    sCutsceneVars[2].pointq[2] = sFOVState.fovq;
}

/**
 * Look towards the star's x and z position
 */
BAD_RETURN(s32) cutscene_red_coin_star_focus_xz(struct Camera *c) {
    approach_q32_asymptotic_bool(&c->focusq[0], QFIELD(gCutsceneFocus, oPosX), q(0.15));
    approach_q32_asymptotic_bool(&c->focusq[2], QFIELD(gCutsceneFocus, oPosZ), q(0.15));
}

/**
 * Look towards the star's y position. Only active before the camera warp.
 */
BAD_RETURN(s32) cutscene_red_coin_star_focus_y(struct Camera *c) {
    approach_q32_asymptotic_bool(&c->focusq[1], QFIELD(gCutsceneFocus, oPosY), q(0.1));
}

/**
 * Look 80% up towards the star. Only active after the camera warp.
 */
BAD_RETURN(s32) cutscene_red_coin_star_look_up_at_star(struct Camera *c) {
    c->focusq[1] = sCutsceneVars[1].pointq[1] + qmul(QFIELD(gCutsceneFocus, oPosY) - sCutsceneVars[1].pointq[1], q(0.8));
}

/**
 * Warp the camera near the star's spawn point
 */
BAD_RETURN(s32) cutscene_red_coin_star_warp(struct Camera *c) {
    q32 distq;
    s16 pitch, yaw, posYaw;
    struct Object *o = gCutsceneFocus;

    vec3q_set(sCutsceneVars[1].pointq, QFIELD(o, oHomeX), QFIELD(o, oHomeY), QFIELD(o, oHomeZ));
    vec3q_get_dist_and_angle(sCutsceneVars[1].pointq, c->posq, &distq, &pitch, &yaw);
    posYaw = calculate_yawq(sCutsceneVars[1].pointq, c->posq);
    yaw = calculate_yawq(sCutsceneVars[1].pointq, sMarioCamState->posq);

    if (ABS(yaw - posYaw + DEGREES(90)) < ABS(yaw - posYaw - DEGREES(90))) {
        yaw += DEGREES(90);
    } else {
        yaw -= DEGREES(90);
    }

    vec3q_set_dist_and_angle(sCutsceneVars[1].pointq, c->posq, q(400), 0x1000, yaw);
    sStatusFlags &= ~CAM_FLAG_SMOOTH_MOVEMENT;
}

/**
 * Zoom out while looking at the star.
 */
BAD_RETURN(s32) cutscene_red_coin_star_set_fov(UNUSED struct Camera *c) {
    sFOVState.fovq = q(60);
}

BAD_RETURN(s32) cutscene_red_coin_star(struct Camera *c) {
    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;
    cutscene_event(cutscene_red_coin_star_start, c, 0, 0);
    cutscene_event(cutscene_red_coin_star_warp, c, 30, 30);
    cutscene_event(cutscene_red_coin_star_focus_xz, c, 0, -1);
    cutscene_event(cutscene_red_coin_star_focus_y, c, 0, 29);
    cutscene_event(cutscene_red_coin_star_look_up_at_star, c, 30, -1);
    cutscene_event(cutscene_red_coin_star_set_fov, c, 30, -1);

    if (gObjCutsceneDone) {
        // Set the timer to CUTSCENE_LOOP, which start the next shot.
        gCutsceneTimer = CUTSCENE_LOOP;
    }
}

/**
 * End the red coin star spawning cutscene
 */
BAD_RETURN(s32) cutscene_red_coin_star_end(struct Camera *c) {
    retrieve_info_star(c);
    gCutsceneTimer = CUTSCENE_STOP;
    c->cutscene = 0;
    // Restore the default fov
    sFOVState.fovq = sCutsceneVars[2].pointq[2];
}

/**
 * Moves the camera towards the cutscene's focus, stored in sCutsceneVars[3].point
 *
 * sCutsceneVars[3].point is used as the target point
 * sCutsceneVars[0].point is used as the current camera focus during the transition
 *
 * @param rotPitch constant pitch offset to add to the camera's focus
 * @param rotYaw constant yaw offset to add to the camera's focus
 */
void cutscene_goto_cvar_posq(struct Camera *c, q32 goalDistq, s16 goalPitch, s16 rotPitch, s16 rotYaw) {
    q32 nextDistq;
    s16 nextPitch, nextYaw;
    // The next 2 polar coord points are only used in CUTSCENE_PREPARE_CANNON
    q32 cannonDistq;
    s16 cannonPitch, cannonYaw;
    q32 curDistq;
    s16 curPitch, curYaw;
    vec3q_get_dist_and_angle(sCutsceneVars[3].pointq, c->posq, &nextDistq, &nextPitch, &nextYaw);
    // If over 8000 units away from the cannon, just teleport there
    if ((nextDistq > q(8000)) && (c->cutscene == CUTSCENE_PREPARE_CANNON)) {
        nextDistq = goalDistq * 4;
        nextPitch = goalPitch;
        vec3q_copy(sCutsceneVars[0].pointq, sCutsceneVars[3].pointq);
        sStatusFlags &= ~CAM_FLAG_SMOOTH_MOVEMENT;

        if (gCurrLevelNum == LEVEL_TTM) {
            nextYaw = atan2sq(sCutsceneVars[3].pointq[2] - c->areaCenZq,
                             sCutsceneVars[3].pointq[0] - c->areaCenXq);
        }
    } else {
        if (c->cutscene == CUTSCENE_PREPARE_CANNON) {
            vec3q_get_dist_and_angle(c->posq, sCutsceneVars[0].pointq, &curDistq, &curPitch, &curYaw);
            vec3q_get_dist_and_angle(c->posq, sCutsceneVars[3].pointq, &cannonDistq, &cannonPitch, &cannonYaw);
            approach_q32_asymptotic_bool(&curDistq, cannonDistq, q(0.1));
            approach_s16_asymptotic_bool(&curPitch, cannonPitch, 15);
            approach_s16_asymptotic_bool(&curYaw, cannonYaw, 15);
            // Move the current focus, sCutsceneVars[0].point, in the direction towards the cannon
            vec3q_set_dist_and_angle(c->posq, sCutsceneVars[0].pointq, curDistq, curPitch, curYaw);
        } else {
            approach_vec3q_asymptotic(sCutsceneVars[0].pointq, sCutsceneVars[3].pointq, q(0.1), q(0.1), q(0.1));
        }
    }

    approach_q32_asymptotic_bool(&nextDistq, goalDistq, q(0.05));
    approach_s16_asymptotic_bool(&nextPitch, goalPitch, 0x20);

    vec3q_set_dist_and_angle(sCutsceneVars[3].pointq, c->posq, nextDistq, nextPitch, nextYaw);
    vec3q_copy(c->focusq, sCutsceneVars[0].pointq);

    // Apply the constant rotation given
    pan_camera(c, rotPitch, rotYaw);
    vec3q_get_dist_and_angle(c->posq, c->focusq, &nextDistq, &nextPitch, &nextYaw);

    if (nextPitch < -0x3000) {
        nextPitch = -0x3000;
    }
    if (nextPitch > 0x3000) {
        nextPitch = 0x3000;
    }

    vec3q_set_dist_and_angle(c->posq, c->focusq, nextDistq, nextPitch, nextYaw);
}

/**
 * Store the camera's pos and focus, and copy the cannon's position to cvars.
 */
BAD_RETURN(s32) cutscene_prepare_cannon_start(struct Camera *c) {
    store_info_cannon(c);
    vec3q_copy(sCutsceneVars[0].pointq, c->focusq);
    sCutsceneVars[2].pointq[0] = q(30);
    // Store the cannon door's position in sCutsceneVars[3]'s point
    object_pos_to_vec3q(sCutsceneVars[3].pointq, gCutsceneFocus);
    vec3s_set(sCutsceneVars[5].angle, 0, 0, 0);
}

/**
 * Fly towards the cannon door.
 */
BAD_RETURN(s32) cutscene_prepare_cannon_fly_to_cannon(struct Camera *c) {
    cutscene_goto_cvar_posq(c, q(300), 0x2000, 0, sCutsceneVars[5].angle[1]);
    camera_approach_s16_symmetric_bool(&sCutsceneVars[5].angle[1], 0x400, 17);
    set_handheld_shake(HAND_CAM_SHAKE_CUTSCENE);
}

/**
 * Used in the cannon opening cutscene to fly back to the camera's last position and focus
 */
void cannon_approach_prevq(q32 *valueq, q32 targetq) {
    q32 incq = qdiv(ABS(targetq - *valueq), sCutsceneVars[2].pointq[0]);
    camera_approach_q32_symmetric_bool(valueq, targetq, incq);
}

/**
 * Fly or warp back to the previous pos and focus, stored in sCameraStoreCutscene.
 */
BAD_RETURN(s32) cutscene_prepare_cannon_fly_back(struct Camera *c) {
    q32 distToPrevPosq = calc_abs_distq(c->posq, sCameraStoreCutscene.posq);

    if (distToPrevPosq < q(8000)) {
        cannon_approach_prevq(&c->posq[0], sCameraStoreCutscene.posq[0]);
        cannon_approach_prevq(&c->posq[1], sCameraStoreCutscene.posq[1]);
        cannon_approach_prevq(&c->posq[2], sCameraStoreCutscene.posq[2]);
        cannon_approach_prevq(&c->focusq[0], sCameraStoreCutscene.focusq[0]);
        cannon_approach_prevq(&c->focusq[1], sCameraStoreCutscene.focusq[1]);
        cannon_approach_prevq(&c->focusq[2], sCameraStoreCutscene.focusq[2]);
    } else {
        // If too far away, just warp back
        vec3q_copy(c->focusq, sCameraStoreCutscene.focusq);
        vec3q_copy(c->posq, sCameraStoreCutscene.posq);
        sStatusFlags &= ~CAM_FLAG_SMOOTH_MOVEMENT;
    }
    if (sCutsceneVars[2].pointq[0] > QONE) {
        sCutsceneVars[2].pointq[0] -= QONE;
    }
}

/**
 * Cutscene that plays when the cannon is opened.
 */
BAD_RETURN(s32) cutscene_prepare_cannon(struct Camera *c) {
    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;
    cutscene_event(cutscene_prepare_cannon_start, c, 0, 0);
    cutscene_event(cutscene_prepare_cannon_fly_to_cannon, c, 0, 140);
    cutscene_event(cutscene_prepare_cannon_fly_back, c, 141, -1);
}

/**
 * Stop the cannon opening cutscene.
 */
BAD_RETURN(s32) cutscene_prepare_cannon_end(struct Camera *c) {
    gCutsceneTimer = CUTSCENE_STOP;
    c->cutscene = 0;
    retrieve_info_cannon(c);
    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;
}

/**
 * Moves the camera to Mario's side when Mario starts ACT_WATER_DEATH
 * Note that ACT_WATER_DEATH only starts when Mario gets hit by an enemy under water. It does not start
 * when Mario drowns.
 */
void water_death_move_to_mario_side(struct Camera *c) {
    q32 distq;
    s16 pitch, yaw;

    vec3q_get_dist_and_angle(sMarioCamState->posq, c->posq, &distq, &pitch, &yaw);
    approach_s16_asymptotic_bool(&yaw, (sMarioCamState->faceAngle[1] - 0x3000), 8);
    vec3q_set_dist_and_angle(sMarioCamState->posq, c->posq, distq, pitch, yaw);
}

/**
 * Unnecessary, only used in cutscene_death_standing_goto_mario()
 */
void death_goto_mario(struct Camera *c) {
    cutscene_goto_cvar_posq(c, q(400), 0x1000, 0x300, 0);
}

BAD_RETURN(s32) cutscene_death_standing_start(struct Camera *c) {
    vec3q_copy(sCutsceneVars[0].pointq, c->focusq);
    vec3q_copy(sCutsceneVars[3].pointq, sMarioCamState->posq);
    sCutsceneVars[3].pointq[1] += q(70);
}

/**
 * Fly to Mario and turn on handheld shake.
 */
BAD_RETURN(s32) cutscene_death_standing_goto_mario(struct Camera *c) {
    death_goto_mario(c);
    set_handheld_shake(HAND_CAM_SHAKE_HIGH);
}

/**
 * Cutscene that plays when Mario dies while standing.
 */
BAD_RETURN(s32) cutscene_death_standing(struct Camera *c) {
    cutscene_event(cutscene_death_standing_start, c, 0, 0);
    cutscene_event(cutscene_death_standing_goto_mario, c, 0, -1);
    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;
}

BAD_RETURN(s32) cutscene_death_stomach_start(struct Camera *c) {
    Vec3q offsetq = { 0, q(40), q(-60) };

    offset_rotatedq(sCutsceneVars[3].pointq, sMarioCamState->posq, offsetq, sMarioCamState->faceAngle);
    vec3q_copy(sCutsceneVars[0].pointq, c->focusq);
}

BAD_RETURN(s32) cutscene_death_stomach_goto_mario(struct Camera *c) {
    cutscene_goto_cvar_posq(c, q(400), 0x1800, 0, -0x400);
}

/**
 * Ah, yes
 */
UNUSED static void unused_water_death_move_to_side_of_mario(struct Camera *c) {
    water_death_move_to_mario_side(c);
}

/**
 * Cutscene that plays when Mario dies on his stomach.
 */
BAD_RETURN(s32) cutscene_death_stomach(struct Camera *c) {
    cutscene_event(cutscene_death_stomach_start, c, 0, 0);
    cutscene_event(cutscene_death_stomach_goto_mario, c, 0, -1);
    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;
    set_handheld_shake(HAND_CAM_SHAKE_CUTSCENE);
}

BAD_RETURN(s32) cutscene_bbh_death_start(struct Camera *c) {
    Vec3q dirq = { 0, q(40), q(60) };

    offset_rotatedq(sCutsceneVars[3].pointq, sMarioCamState->posq, dirq, sMarioCamState->faceAngle);
    vec3q_copy(sCutsceneVars[0].pointq, c->focusq);
}

BAD_RETURN(s32) cutscene_bbh_death_goto_mario(struct Camera *c) {
    cutscene_goto_cvar_posq(c, q(400), 0x1800, 0, 0x400);
}

/**
 * Cutscene that plays when Mario dies in BBH.
 */
BAD_RETURN(s32) cutscene_bbh_death(struct Camera *c) {
    cutscene_event(cutscene_bbh_death_start, c, 0, 0);
    cutscene_event(cutscene_bbh_death_goto_mario, c, 0, -1);
    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;
    set_handheld_shake(HAND_CAM_SHAKE_CUTSCENE);
}

/**
 * Copy the camera's focus to cvar0
 */
BAD_RETURN(s32) cutscene_quicksand_death_start(struct Camera *c) {
    vec3q_copy(sCutsceneVars[0].pointq, c->focusq);
}

/**
 * Fly closer to Mario. In WATER_DEATH, move to Mario's side.
 */
BAD_RETURN(s32) cutscene_quicksand_death_goto_mario(struct Camera *c) {
    cutscene_goto_cvar_posq(c, q(400), 0x2800, 0x200, 0);

    if (c->cutscene == CUTSCENE_WATER_DEATH) {
        water_death_move_to_mario_side(c);
    }
}

/**
 * Cutscene that plays when Mario dies in quicksand.
 */
BAD_RETURN(s32) cutscene_quicksand_death(struct Camera *c) {
    sCutsceneVars[3].pointq[0] = sMarioCamState->posq[0];
    sCutsceneVars[3].pointq[1] = sMarioCamState->posq[1] + q(20);
    sCutsceneVars[3].pointq[2] = sMarioCamState->posq[2];

    cutscene_event(cutscene_quicksand_death_start, c, 0, 0);
    cutscene_event(cutscene_quicksand_death_goto_mario, c, 0, -1);
    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;
    set_handheld_shake(HAND_CAM_SHAKE_HIGH);
}

/**
 * Fly away from Mario near the end of the cutscene.
 */
BAD_RETURN(s32) cutscene_suffocation_fly_away(UNUSED struct Camera *c) {
    Vec3q targetq;
    Vec3q offsetq = { 0, q(20), q(120) };

    offset_rotatedq(targetq, sMarioCamState->posq, offsetq, sMarioCamState->faceAngle);
    approach_vec3q_asymptotic(sCutsceneVars[3].pointq, targetq, q(0.1), q(0.1), q(0.1));
}

/**
 * Keep Lakitu above the gas level.
 */
BAD_RETURN(s32) cutscene_suffocation_stay_above_gas(struct Camera *c) {
    q32 gasLevelq;

    cutscene_goto_cvar_posq(c, q(400), 0x2800, 0x200, 0);
    gasLevelq = find_poison_gas_levelq(sMarioCamState->posq[0], sMarioCamState->posq[2]);

    if (gasLevelq != q(FLOOR_LOWER_LIMIT)) {
        if ((gasLevelq += q(130)) > c->posq[1]) {
            c->posq[1] = gasLevelq;
        }
    }
}

/**
 * Quickly rotate around Mario.
 */
BAD_RETURN(s32) cutscene_suffocation_rotate(struct Camera *c) {
    q32 distq;
    s16 pitch, yaw;

    vec3q_get_dist_and_angle(sMarioCamState->posq, c->posq, &distq, &pitch, &yaw);
    yaw += 0x100;
    vec3q_set_dist_and_angle(sMarioCamState->posq, c->posq, distq, pitch, yaw);
}

/**
 * Cutscene that plays when Mario dies from suffocation (ie due to HMC gas).
 */
BAD_RETURN(s32) cutscene_suffocation(struct Camera *c) {
    cutscene_event(cutscene_death_stomach_start, c, 0, 0);
    cutscene_event(cutscene_suffocation_rotate, c, 0, -1);
    cutscene_event(cutscene_suffocation_stay_above_gas, c, 0, -1);
    cutscene_event(cutscene_suffocation_fly_away, c, 50, -1);
    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;
    set_handheld_shake(HAND_CAM_SHAKE_HIGH);
}

BAD_RETURN(s32) cutscene_enter_pool_start(struct Camera *c) {
    vec3q_copy(sCutsceneVars[3].pointq, sMarioCamState->posq);

    if (gCurrLevelNum == LEVEL_CASTLE) { // entering HMC
        vec3q_set(sCutsceneVars[3].pointq, q(2485), q(-1589), q(-2659));
    }
    if (gCurrLevelNum == LEVEL_HMC) { // entering CotMC
        vec3q_set(sCutsceneVars[3].pointq, q(3350), q(-4589), q(4800));
    }

    vec3q_copy(sCutsceneVars[0].pointq, c->focusq);
}

BAD_RETURN(s32) cutscene_enter_pool_loop(struct Camera *c) {
    cutscene_goto_cvar_posq(c, q(1200), 0x2000, 0x200, 0);
}

BAD_RETURN(s32) cutscene_enter_pool(struct Camera *c) {
    cutscene_event(cutscene_enter_pool_start, c, 0, 0);
    cutscene_event(cutscene_enter_pool_loop, c, 0, -1);
    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;
}

/**
 * Store the camera focus in cvar1.
 * Store the area's center position (which happens to be the pyramid, in SSL) in cvar3.
 */
BAD_RETURN(s32) cutscene_pyramid_top_explode_start(struct Camera *c) {
    reset_pan_distance(c);
    store_info_cannon(c);

    vec3q_copy(sCutsceneVars[1].pointq, c->focusq);
    vec3q_set(sCutsceneVars[3].pointq, c->areaCenXq, q(1280), c->areaCenZq);
}

/**
 * Zoom in on the pyramid.
 */
BAD_RETURN(s32) cutscene_pyramid_top_explode_zoom_in(UNUSED struct Camera *c) {
    set_fov_function(CAM_FOV_APP_30);
}

/**
 * Look at the pyramid top.
 */
BAD_RETURN(s32) cutscene_pyramid_top_explode_focus(struct Camera *c) {
    approach_vec3q_asymptotic(c->focusq, sCutsceneVars[3].pointq, q(0.02), q(0.02), q(0.02));
    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;
}

/**
 * Store the old pos and focus, then warp to the pyramid top.
 */
BAD_RETURN(s32) cutscene_pyramid_top_explode_warp(struct Camera *c) {
    s16 pitch, yaw;
    q32 distq;

    set_fov_function(CAM_FOV_DEFAULT);
    sFOVState.fovq = q(45);

    vec3q_copy(sCutsceneVars[4].pointq, c->posq);
    vec3q_copy(sCutsceneVars[5].pointq, c->focusq);
    vec3q_copy(c->focusq, sCutsceneVars[3].pointq);

    vec3q_get_dist_and_angle(sCutsceneVars[3].pointq, sMarioCamState[0].posq, &distq, &pitch, &yaw);
    vec3q_set_dist_and_angle(sCutsceneVars[3].pointq, c->posq, q(2000), 0, yaw);
    c->posq[1] += q(500);
}

/**
 * Close up view of the spinning pyramid top as it rises.
 */
BAD_RETURN(s32) cutscene_pyramid_top_explode_closeup(struct Camera *c) {
    s16 pitch, yaw;
    q32 distq;

    vec3q_get_dist_and_angle(sCutsceneVars[3].pointq, c->posq, &distq, &pitch, &yaw);
    approach_q32_asymptotic_bool(&distq, q(2000), q(0.1));
    vec3q_set_dist_and_angle(sCutsceneVars[3].pointq, c->posq, distq, pitch, yaw);

    c->focusq[1] += q(4);
    c->posq[1] -= q(5);
    sFOVState.fovq = q(45);
    set_handheld_shake(HAND_CAM_SHAKE_CUTSCENE);
}

/**
 * Shake the camera during the closeup.
 */
BAD_RETURN(s32) cutscene_pyramid_top_explode_cam_shake(UNUSED struct Camera *c) {
    set_environmental_camera_shake(SHAKE_ENV_PYRAMID_EXPLODE);
}

/**
 * Warp back to the old position, and start a heavy camera shake.
 */
BAD_RETURN(s32) cutscene_pyramid_top_explode_warp_back(struct Camera *c) {
    UNUSED u32 pad[2];

    vec3q_copy(c->posq, sCutsceneVars[4].pointq);
    vec3q_copy(c->focusq, sCutsceneVars[5].pointq);
    set_environmental_camera_shake(SHAKE_ENV_BOWSER_JUMP);
}

/**
 * An unused cutscene for when the pyramid explodes.
 */
BAD_RETURN(s32) cutscene_pyramid_top_explode(struct Camera *c) {
    cutscene_event(cutscene_pyramid_top_explode_start, c, 0, 0);
    cutscene_event(cutscene_pyramid_top_explode_focus, c, 0, 30);
    cutscene_event(cutscene_pyramid_top_explode_warp, c, 31, 31);
    cutscene_event(cutscene_pyramid_top_explode_closeup, c, 31, 139);
    cutscene_event(cutscene_pyramid_top_explode_zoom_in, c, 23, 23);
    cutscene_event(cutscene_pyramid_top_explode_warp_back, c, 140, 140);
    cutscene_event(cutscene_pyramid_top_explode_cam_shake, c, 31, 139);
}

/**
 * End the pyramid top explosion cutscene.
 */
BAD_RETURN(s32) cutscene_pyramid_top_explode_end(struct Camera *c) {
    cutscene_stop_dialog(c);
    stop_cutscene_and_retrieve_stored_info(c);
    // Move the camera back to Mario
    transition_next_state(c, 30);
}

/**
 * Store the camera focus in cvar0, and store the top of the pyramid in cvar3.
 */
BAD_RETURN(s32) cutscene_enter_pyramid_top_start(struct Camera *c) {
    vec3q_copy(sCutsceneVars[0].pointq, c->focusq);
    vec3q_set(sCutsceneVars[3].pointq, c->areaCenXq, q(1280), c->areaCenZq);
}

/**
 * Cutscene that plays when Mario enters the top of the pyramid.
 */
BAD_RETURN(s32) cutscene_enter_pyramid_top(struct Camera *c) {
    cutscene_event(cutscene_enter_pyramid_top_start, c, 0, 0);
    // Move to cvar3
    cutscene_goto_cvar_posq(c, q(200), 0x3000, 0, 0);
    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;
    set_handheld_shake(HAND_CAM_SHAKE_CUTSCENE);

    if (sMarioCamState->posq[1] > q(1250)) {
        // End the cutscene early if Mario ledge-grabbed.
        // This only works because of the janky way that ledge-grabbing is implemented.
        cutscene_exit_to_castle_grounds_end(c);
    }
}

UNUSED static void unused_cutscene_goto_cvar(struct Camera *c) {
    q32 distq;
    distq = calc_abs_distq(sCutsceneVars[3].pointq, sMarioCamState->posq);
    distq = calc_abs_distq(sCutsceneVars[9].pointq, sMarioCamState->posq) + q(200);
    cutscene_goto_cvar_posq(c, distq, 0x1000, 0x300, 0);
}

/**
 * cvar8 is Mario's position and faceAngle
 *
 * cvar9.point is gCutsceneFocus's position
 * cvar9.angle[1] is the yaw between Mario and the gCutsceneFocus
 */
BAD_RETURN(s32) cutscene_dialog_start(struct Camera *c) {
    s16 yaw;

    cutscene_soften_music(c);
    set_time_stop_flags(TIME_STOP_ENABLED | TIME_STOP_DIALOG);

#ifndef VERSION_JP
    if (c->mode == CAMERA_MODE_BOSS_FIGHT) {
        vec3q_copy(sCameraStoreCutscene.focusq, c->focusq);
        vec3q_copy(sCameraStoreCutscene.posq, c->posq);
    } else {
#endif
        store_info_star(c);
#ifndef VERSION_JP
    }
#endif

    // Store Mario's position and faceAngle
    sCutsceneVars[8].angle[0] = 0;
    vec3q_copy(sCutsceneVars[8].pointq, sMarioCamState->posq);
    sCutsceneVars[8].pointq[1] += q(125);

    // Store gCutsceneFocus's position and yaw
    object_pos_to_vec3q(sCutsceneVars[9].pointq, gCutsceneFocus);
    sCutsceneVars[9].pointq[1] += q(gCutsceneFocus->hitboxHeight_s16 + 200);
    sCutsceneVars[9].angle[1] = calculate_yawq(sCutsceneVars[8].pointq, sCutsceneVars[9].pointq);

    yaw = calculate_yawq(sMarioCamState->posq, gLakituState.curPosq);
    if ((yaw - sCutsceneVars[9].angle[1]) & 0x8000) {
        sCutsceneVars[9].angle[1] -= 0x6000;
    } else {
        sCutsceneVars[9].angle[1] += 0x6000;
    }
}

/**
 * Move closer to Mario and the object, adjusting to their difference in height.
 * The camera's generally ends up looking over Mario's shoulder.
 */
BAD_RETURN(s32) cutscene_dialog_move_mario_shoulder(struct Camera *c) {
    q32 distq;
    s16 pitch, yaw;
    Vec3q focusq, posq;

    scale_along_lineq(focusq, sCutsceneVars[9].pointq, sMarioCamState->posq, q(0.7));
    vec3q_get_dist_and_angle(c->posq, focusq, &distq, &pitch, &yaw);
    pitch = calculate_pitchq(c->posq, sCutsceneVars[9].pointq);
    vec3q_set_dist_and_angle(c->posq, posq, distq, pitch, yaw);

    focusq[1] += (sCutsceneVars[9].pointq[1] - focusq[1]) / 10;
    approach_vec3q_asymptotic(c->focusq, focusq, q(0.2), q(0.2), q(0.2));

    vec3q_copy(posq, c->posq);

    // Set y pos to cvar8's y (top of focus object)
    posq[1] = sCutsceneVars[8].pointq[1];
    vec3q_get_dist_and_angle(sCutsceneVars[8].pointq, posq, &distq, &pitch, &yaw);
    approach_s16_asymptotic_bool(&yaw, sCutsceneVars[9].angle[1], 0x10);
    approach_q32_asymptotic_bool(&distq, q(180), q(0.05));
    vec3q_set_dist_and_angle(sCutsceneVars[8].pointq, posq, distq, pitch, yaw);

    // Move up if Mario is below the focus object, down is Mario is above
    posq[1] = sCutsceneVars[8].pointq[1]
              + sinqs(calculate_pitchq(sCutsceneVars[9].pointq, sCutsceneVars[8].pointq)) * 100;

    approach_q32_asymptotic_bool(&c->posq[1], posq[1], q(0.05));
    c->posq[0] = posq[0];
    c->posq[2] = posq[2];
}

/**
 * Create the dialog with sCutsceneDialogID
 */
BAD_RETURN(s32) cutscene_dialog_create_dialog_box(struct Camera *c) {
    if (c->cutscene == CUTSCENE_RACE_DIALOG) {
        create_dialog_box_with_response(sCutsceneDialogID);
    } else {
        create_dialog_box(sCutsceneDialogID);
    }

    //! Unused. This may have been used before sCutsceneDialogResponse was implemented.
    sCutsceneVars[8].angle[0] = DIALOG_RESPONSE_NOT_DEFINED;
}

/**
 * Cutscene that plays when Mario talks to an object.
 */
BAD_RETURN(s32) cutscene_dialog(struct Camera *c) {
    cutscene_event(cutscene_dialog_start, c, 0, 0);
    cutscene_event(cutscene_dialog_move_mario_shoulder, c, 0, -1);
    cutscene_event(cutscene_dialog_create_dialog_box, c, 10, 10);
    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;

    if (gDialogResponse != DIALOG_RESPONSE_NONE) {
        sCutsceneDialogResponse = gDialogResponse;
    }

    if ((get_dialog_id() == DIALOG_NONE) && (sCutsceneVars[8].angle[0] != 0)) {
        if (c->cutscene != CUTSCENE_RACE_DIALOG) {
            sCutsceneDialogResponse = DIALOG_RESPONSE_NOT_DEFINED;
        }

        gCutsceneTimer = CUTSCENE_LOOP;
        retrieve_info_star(c);
        transition_next_state(c, 15);
        sStatusFlags |= CAM_FLAG_UNUSED_CUTSCENE_ACTIVE;
        cutscene_unsoften_music(c);
    }
}

/**
 * Sets the CAM_FLAG_UNUSED_CUTSCENE_ACTIVE flag, which does nothing.
 */
BAD_RETURN(s32) cutscene_dialog_set_flag(UNUSED struct Camera *c) {
    sStatusFlags |= CAM_FLAG_UNUSED_CUTSCENE_ACTIVE;
}

/**
 * Ends the dialog cutscene.
 */
BAD_RETURN(s32) cutscene_dialog_end(struct Camera *c) {
    sStatusFlags |= CAM_FLAG_UNUSED_CUTSCENE_ACTIVE;
    c->cutscene = 0;
    clear_time_stop_flags(TIME_STOP_ENABLED | TIME_STOP_DIALOG);
}

/**
 * Soften the music, clear cvar0
 *
 * In this cutscene, cvar0.angle[0] is used as a state variable.
 */
BAD_RETURN(s32) cutscene_read_message_start(struct Camera *c) {
    cutscene_soften_music(c);
    transition_next_state(c, 30);
    reset_pan_distance(c);
    store_info_star(c);

    sCutsceneVars[1].angle[0] = sCUpCameraPitch;
    sCutsceneVars[1].angle[1] = sModeOffsetYaw;
    sCUpCameraPitch = -0x830;
    sModeOffsetYaw = 0;
    sCutsceneVars[0].angle[0] = 0;
}

UNUSED static void unused_cam_to_mario(struct Camera *c) {
    Vec3s dir;

    vec3s_set(dir, 0, sMarioCamState->faceAngle[1], 0);
    offset_rotated_coordsq(c->posq, sMarioCamState->posq, dir, 0, q(100), q(190));
    offset_rotated_coordsq(c->focusq, sMarioCamState->posq, dir, 0, q(70), q(-20));
}

/**
 * Cutscene that plays when Mario is reading a message (a sign or message on the wall)
 */
BAD_RETURN(s32) cutscene_read_message(struct Camera *c) {
    UNUSED u32 pad[2];

    cutscene_event(cutscene_read_message_start, c, 0, 0);
    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;

    switch (sCutsceneVars[0].angle[0]) {
        // Do nothing until message is gone.
        case 0:
            if (get_dialog_id() != DIALOG_NONE) {
                sCutsceneVars[0].angle[0] += 1;
                set_time_stop_flags(TIME_STOP_ENABLED | TIME_STOP_DIALOG);
            }
            break;
        // Leave the dialog.
        case 1:
            move_mario_head_c_up(c);
            update_c_upq(c, c->focusq, c->posq);

            // This could cause softlocks. If a message starts one frame after another one closes, the
            // cutscene will never end.
            if (get_dialog_id() == DIALOG_NONE) {
                gCutsceneTimer = CUTSCENE_LOOP;
                retrieve_info_star(c);
                transition_next_state(c, 15);
                sStatusFlags |= CAM_FLAG_UNUSED_CUTSCENE_ACTIVE;
                clear_time_stop_flags(TIME_STOP_ENABLED | TIME_STOP_DIALOG);
                // Retrieve previous state
                sCUpCameraPitch = sCutsceneVars[1].angle[0];
                sModeOffsetYaw = sCutsceneVars[1].angle[1];
                cutscene_unsoften_music(c);
            }
    }
    sStatusFlags |= CAM_FLAG_UNUSED_CUTSCENE_ACTIVE;
}

/**
 * Set CAM_FLAG_UNUSED_CUTSCENE_ACTIVE, which does nothing.
 */
BAD_RETURN(s32) cutscene_read_message_set_flag(UNUSED struct Camera *c) {
    sStatusFlags |= CAM_FLAG_UNUSED_CUTSCENE_ACTIVE;
}

/**
 * End the message cutscene.
 */
BAD_RETURN(s32) cutscene_read_message_end(struct Camera *c) {
    sStatusFlags |= CAM_FLAG_UNUSED_CUTSCENE_ACTIVE;
    c->cutscene = 0;
}

/**
 * Set cvars:
 * cvar7 is Mario's pos and angle
 * cvar6 is the focus offset
 * cvar5 is the position offset
 */
BAD_RETURN(s32) cutscene_exit_succ_start(UNUSED struct Camera *c) {
    vec3q_copy(sCutsceneVars[7].pointq, sMarioCamState->posq);
    vec3s_copy(sCutsceneVars[7].angle, sMarioCamState->faceAngle);
    vec3q_set(sCutsceneVars[6].pointq, q(6), q(363), q(543));
    vec3q_set(sCutsceneVars[5].pointq, q(137), q(226), q(995));
}

/**
 * Set the camera pos depending on which level Mario exited.
 */
BAD_RETURN(s32) cutscene_non_painting_set_cam_pos(struct Camera *c) {
    struct Surface *floor;

    switch (gPrevLevel) {
        case LEVEL_HMC:
            vec3q_set(c->posq, q(3465), q(-1008), q(-2961));
            break;

        case LEVEL_COTMC:
            vec3q_set(c->posq, q(3465), q(-1008), q(-2961));
            break;

        case LEVEL_RR:
            vec3q_set(c->posq, q(-3741), q(3151), q(6065));
            break;

        case LEVEL_WMOTR:
            vec3q_set(c->posq, q(1972), q(3230), q(5891));
            break;

        default:
            offset_rotatedq(c->posq, sCutsceneVars[7].pointq, sCutsceneVars[5].pointq, sCutsceneVars[7].angle);
            c->posq[1] = find_floorq(c->posq[0], c->posq[1] + q(1000), c->posq[2], &floor) + q(125);
            break;
    }
}

/**
 * Update the camera focus depending on which level Mario exited.
 */
BAD_RETURN(s32) cutscene_non_painting_set_cam_focus(struct Camera *c) {
    offset_rotatedq(c->focusq, sCutsceneVars[7].pointq, sCutsceneVars[6].pointq, sCutsceneVars[7].angle);

    if ((gPrevLevel == LEVEL_COTMC) || (gPrevLevel == LEVEL_HMC) || (gPrevLevel == LEVEL_RR)
        || (gPrevLevel == LEVEL_WMOTR)) {
        c->focusq[0] = c->posq[0] + qmul(sMarioCamState->posq[0] - c->posq[0], q(0.7));
        c->focusq[1] = c->posq[1] + qmul(sMarioCamState->posq[1] - c->posq[1], q(0.4));
        c->focusq[2] = c->posq[2] + qmul(sMarioCamState->posq[2] - c->posq[2], q(0.7));
    } else {
        c->focusq[1] = c->posq[1] + qmul(sMarioCamState->posq[1] - c->posq[1], q(0.2));
    }
}

/**
 * Focus slightly left of Mario. Perhaps to keep the bowser painting in view?
 */
BAD_RETURN(s32) cutscene_exit_bowser_succ_focus_left(UNUSED struct Camera *c) {
    approach_q32_asymptotic_bool(&sCutsceneVars[6].pointq[0], q(-24), q(0.05));
}

/**
 * Instead of focusing on the key, just start a pitch shake. Clever!
 * The shake lasts 32 frames.
 */
BAD_RETURN(s32) cutscene_exit_bowser_key_toss_shake(struct Camera *c) {
    //! Unnecessary check.
    if (c->cutscene == CUTSCENE_EXIT_BOWSER_SUCC) {
        set_camera_pitch_shake(0x800, 0x40, 0x800);
    }
}

/**
 * Start a camera shake when Mario lands on the ground.
 */
BAD_RETURN(s32) cutscene_exit_succ_shake_landing(UNUSED struct Camera *c) {
    set_environmental_camera_shake(SHAKE_ENV_EXPLOSION);
}

/**
 * Cutscene that plays when Mario beats bowser and exits the level.
 */
BAD_RETURN(s32) cutscene_exit_bowser_succ(struct Camera *c) {
    cutscene_event(cutscene_exit_succ_start, c, 0, 0);
    cutscene_event(cutscene_non_painting_set_cam_pos, c, 0, -1);
    cutscene_event(cutscene_exit_bowser_succ_focus_left, c, 18, -1);
    cutscene_event(cutscene_non_painting_set_cam_focus, c, 0, -1);
    cutscene_event(cutscene_exit_bowser_key_toss_shake, c, 125, 125);
    cutscene_event(cutscene_exit_succ_shake_landing, c, 41, 41);
}

/**
 * End a non-painting exit cutscene. Used by BBH and bowser courses.
 */
BAD_RETURN(s32) cutscene_non_painting_end(struct Camera *c) {
    c->cutscene = 0;

    if (c->defMode == CAMERA_MODE_CLOSE) {
        c->mode = CAMERA_MODE_CLOSE;
    } else {
        c->mode = CAMERA_MODE_FREE_ROAM;
    }

    sStatusFlags |= CAM_FLAG_UNUSED_CUTSCENE_ACTIVE;
    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;
    transition_next_state(c, 60);
    update_camera_yaw(c);
}

/**
 * Override the position offset.
 */
BAD_RETURN(s32) cutscene_exit_non_painting_succ_override_cvar(UNUSED struct Camera *c) {
    vec3q_set(sCutsceneVars[5].pointq, q(137), q(246), q(1115));
}

/**
 * Cutscene that plays when Mario collects a star and leaves a non-painting course, like HMC or BBH.
 */
BAD_RETURN(s32) cutscene_exit_non_painting_succ(struct Camera *c) {
    cutscene_event(cutscene_exit_succ_start, c, 0, 0);
    cutscene_event(cutscene_exit_non_painting_succ_override_cvar, c, 0, 0);
    cutscene_event(cutscene_non_painting_set_cam_pos, c, 0, -1);
    cutscene_event(cutscene_exit_bowser_succ_focus_left, c, 18, -1);
    cutscene_event(cutscene_non_painting_set_cam_focus, c, 0, -1);
    cutscene_event(cutscene_exit_succ_shake_landing, c, 41, 41);
    update_camera_yaw(c);
}

/**
 * Set cvar7 to Mario's pos and faceAngle
 * Set cvar6 to the focus offset from Mario.
 * set cvar5 to the pos offset from Mario. (This is always overwritten)
 */
BAD_RETURN(s32) cutscene_non_painting_death_start(UNUSED struct Camera *c) {
    vec3q_copy(sCutsceneVars[7].pointq, sMarioCamState->posq);
    vec3s_copy(sCutsceneVars[7].angle, sMarioCamState->faceAngle);
    vec3q_set(sCutsceneVars[6].pointq, q(-42), q(350), q(727));
    // This is always overwritten, except in the unused cutscene_exit_bowser_death()
    vec3q_set(sCutsceneVars[5].pointq, q(107), q(226), q(1187));
}

/**
 * This cutscene is the same as non_painting_death, but the camera is closer to Mario and lower.
 * Because it it doesn't call cutscene_non_painting_death_override_offset, the value from
 * cutscene_non_painting_death_start is used.
 *
 * This cutscene is unused, dying in bowser's arena spawns Mario near the warp pipe, not back in the
 * hub.
 */
BAD_RETURN(s32) cutscene_exit_bowser_death(struct Camera *c) {
    cutscene_event(cutscene_non_painting_death_start, c, 0, 0);
    cutscene_event(cutscene_non_painting_set_cam_pos, c, 0, -1);
    cutscene_event(cutscene_non_painting_set_cam_focus, c, 0, -1);
}

/**
 * Set the offset from Mario depending on the course Mario exited.
 * This overrides cutscene_non_painting_death_start()
 */
BAD_RETURN(s32) cutscene_non_painting_death_override_offset(UNUSED struct Camera *c) {
    switch (gPrevLevel) {
        case LEVEL_HMC:
            vec3q_set(sCutsceneVars[5].pointq, q(187), q(369), q(-197));
            break;
        case LEVEL_COTMC:
            vec3q_set(sCutsceneVars[5].pointq, q(187), q(369), q(-197));
            break;
        default:
            vec3q_set(sCutsceneVars[5].pointq, q(107), q(246), q(1307));
            break;
    }
}

/**
 * Cutscene played when Mario dies in a non-painting course, like HMC or BBH.
 */
BAD_RETURN(s32) cutscene_non_painting_death(struct Camera *c) {
    cutscene_event(cutscene_non_painting_death_start, c, 0, 0);
    cutscene_event(cutscene_non_painting_death_override_offset, c, 0, 0);
    cutscene_event(cutscene_non_painting_set_cam_pos, c, 0, -1);
    cutscene_event(cutscene_non_painting_set_cam_focus, c, 0, -1);
    sStatusFlags |= CAM_FLAG_UNUSED_CUTSCENE_ACTIVE;
}

/**
 * Set cvars:
 * cvar3 is an offset applied to the camera's rotation around Mario. It starts at 0x1200
 * cvar 1 is more complicated:
 *      First the yaw from Mario to the camera is calculated. cvar1 is the high byte of the difference
 *      between that yaw and Mario's faceAngle plus 0x1200. The reason for taking the high byte is
 *      because cvar1 rotates until is reaches 0, so it's important that it's a multiple of 0x100.
 */
BAD_RETURN(s32) cutscene_cap_switch_press_start(struct Camera *c) {
    s16 yaw;

    store_info_star(c);
    yaw = calculate_yawq(sMarioCamState->posq, c->posq);
    sCutsceneVars[3].angle[1] = 0x1200;
    // Basically the amount of rotation to get from behind Mario to in front of Mario
    sCutsceneVars[1].angle[1] = (yaw - (sMarioCamState->faceAngle[1] + sCutsceneVars[3].angle[1])) & 0xFF00;
}

/**
 * Rotate around Mario. As each cvar stops updating, the rotation slows until the camera ends up in
 * front of Mario.
 */
BAD_RETURN(s32) cutscene_cap_switch_press_rotate_around_mario(struct Camera *c) {
    q32 distq;
    s16 pitch, yaw;

    vec3q_get_dist_and_angle(sMarioCamState->posq, c->posq, &distq, &pitch, &yaw);

    // cvar3 wraps around until it reaches 0x1000
    if (sCutsceneVars[3].angle[1] != 0x1000) {
        sCutsceneVars[3].angle[1] += 0x100;
    }

    // cvar1 wraps until 0
    if (sCutsceneVars[1].angle[1] != 0) {
        sCutsceneVars[1].angle[1] += 0x100;
    }

    yaw = sMarioCamState->faceAngle[1] + sCutsceneVars[3].angle[1] + sCutsceneVars[1].angle[1];
    vec3q_set_dist_and_angle(sMarioCamState->posq, c->posq, distq, pitch, yaw);
}

/**
 * Move the camera slightly downwards.
 */
BAD_RETURN(s32) cutscene_cap_switch_press_lower_cam(struct Camera *c) {
    rotate_and_move_vec3q(c->posq, sMarioCamState->posq, 0, -0x20, 0);
}

/**
 * Move the camera closer to Mario.
 */
BAD_RETURN(s32) cutscene_cap_switch_press_approach_mario(struct Camera *c) {
    s16 pitch, yaw;
    q32 distq;

    vec3q_get_dist_and_angle(sMarioCamState->posq, c->posq, &distq, &pitch, &yaw);
    approach_q32_asymptotic_bool(&distq, q(195), q(0.2));
    approach_s16_asymptotic_bool(&pitch, 0, 0x10);
    vec3q_set_dist_and_angle(sMarioCamState->posq, c->posq, distq, pitch, yaw);

    approach_q32_asymptotic_bool(&c->focusq[0], sMarioCamState->posq[0], q(0.1));
    approach_q32_asymptotic_bool(&c->focusq[1], sMarioCamState->posq[1] + q(110), q(0.1));
    approach_q32_asymptotic_bool(&c->focusq[2], sMarioCamState->posq[2], q(0.1));
}

/**
 * Pan the camera left so that Mario is on the right side of the screen when the camera stops spinning.
 */
BAD_RETURN(s32) cutscene_cap_switch_press_pan_left(struct Camera *c) {
    vec3q_copy(c->focusq, sMarioCamState->posq);
    c->focusq[1] += q(110);
    camera_approach_s16_symmetric_bool(&sCutsceneVars[0].angle[1], 0x800, 0x20);
    pan_camera(c, sCutsceneVars[0].angle[0], sCutsceneVars[0].angle[1]);
}

/**
 * Create a dialog box with the cap switch's text.
 */
BAD_RETURN(s32) cutscene_cap_switch_press_create_dialog(UNUSED struct Camera *c) {
    create_dialog_box_with_response(gCutsceneFocus->oBehParams2ndByte + DIALOG_010);
}

static UNUSED BAD_RETURN(s32) unused_cap_switch_retrieve_info(struct Camera *c) {
    retrieve_info_star(c);
    transition_next_state(c, 30);
}

/**
 * Cutscene that plays when Mario presses a cap switch.
 */
BAD_RETURN(s32) cutscene_cap_switch_press(struct Camera *c) {
    q32 distq;
    s16 pitch, yaw;

    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;
    sStatusFlags |= CAM_FLAG_UNUSED_CUTSCENE_ACTIVE;

    cutscene_event(cutscene_cap_switch_press_start, c, 0, 0);
    cutscene_event(cutscene_cap_switch_press_approach_mario, c, 0, 30);
    cutscene_event(cutscene_cap_switch_press_pan_left, c, 0, -1);
    cutscene_event(cutscene_cap_switch_press_rotate_around_mario, c, 30, -1);
    cutscene_event(cutscene_cap_switch_press_lower_cam, c, 10, 70);
    cutscene_event(cutscene_cap_switch_press_create_dialog, c, 10, 10);
    vec3q_get_dist_and_angle(sMarioCamState->posq, c->posq, &distq, &pitch, &yaw);

    if (gDialogResponse != DIALOG_RESPONSE_NONE) {
        sCutsceneVars[4].angle[0] = gDialogResponse;
    }

    if ((get_dialog_id() == DIALOG_NONE) && (sCutsceneVars[4].angle[0] != 0)) {
        sCutsceneDialogResponse = sCutsceneVars[4].angle[0];
        if (sCutsceneVars[4].angle[0] == 1) {
            cap_switch_save(gCutsceneFocus->oBehParams2ndByte);
        }
        stop_cutscene_and_retrieve_stored_info(c);
        transition_next_state(c, 30);
    }
}

/**
 * Sets cvars:
 * cvar0 is the camera's position
 * cvar1 is the camera's focus
 * cvar2 is the goal position
 * cvar3 is the goal focus
 */
BAD_RETURN(s32) cutscene_unlock_key_door_start(struct Camera *c) {
    Vec3q posOffq, focusOffq;

    vec3q_copy(sCutsceneVars[0].pointq, c->posq);
    vec3q_copy(sCutsceneVars[1].pointq, c->focusq);
    vec3q_set(posOffq, q(-206), q(108), q(234));
    vec3q_set(focusOffq, q(48), q(104), q(-193));
    offset_rotatedq(sCutsceneVars[2].pointq, sMarioCamState->posq, posOffq, sMarioCamState->faceAngle);
    offset_rotatedq(sCutsceneVars[3].pointq, sMarioCamState->posq, focusOffq, sMarioCamState->faceAngle);
}

/**
 * Move the camera to the cvars position and focus, closer to Mario.
 * Gives a better view of the key.
 */
BAD_RETURN(s32) cutscene_unlock_key_door_approach_mario(struct Camera *c) {
    approach_vec3q_asymptotic(c->posq, sCutsceneVars[2].pointq, q(0.1), q(0.1), q(0.1));
    approach_vec3q_asymptotic(c->focusq, sCutsceneVars[3].pointq, q(0.1), q(0.1), q(0.1));
}

/**
 * Move the camera focus up a bit, focusing on the key in the lock.
 */
BAD_RETURN(s32) cutscene_unlock_key_door_focus_lock(UNUSED struct Camera *c) {
    approach_q32_asymptotic_bool(&sCutsceneVars[3].pointq[1], sMarioCamState->posq[1] + q(140), q(0.07));
}

BAD_RETURN(s32) cutscene_unlock_key_door_stub(UNUSED struct Camera *c) {
}

/**
 * Move back to the previous pos and focus, stored in cvar0 and cvar1.
 */
BAD_RETURN(s32) cutscene_unlock_key_door_fly_back(struct Camera *c) {
    approach_vec3q_asymptotic(c->posq, sCutsceneVars[0].pointq, q(0.1), q(0.1), q(0.1));
    approach_vec3q_asymptotic(c->focusq, sCutsceneVars[1].pointq, q(0.1), q(0.1), q(0.1));
}

/**
 * Shake the camera's fov when the key is put in the lock.
 */
BAD_RETURN(s32) cutscene_unlock_key_door_fov_shake(UNUSED struct Camera *c) {
    cutscene_set_fov_shake_preset(1);
}

/**
 * Cutscene that plays when Mario unlocks a key door.
 */
BAD_RETURN(s32) cutscene_unlock_key_door(UNUSED struct Camera *c) {
    cutscene_event(cutscene_unlock_key_door_start, c, 0, 0);
    cutscene_event(cutscene_unlock_key_door_approach_mario, c, 0, 123);
    cutscene_event(cutscene_unlock_key_door_fly_back, c, 124, -1);
    cutscene_event(cutscene_unlock_key_door_fov_shake, c, 79, 79);
    cutscene_event(cutscene_unlock_key_door_focus_lock, c, 70, 110);
    cutscene_event(cutscene_unlock_key_door_stub, c, 112, 112);
}

/**
 * Move the camera along `positionSpline` and point its focus at the corresponding point along
 * `focusSpline`. sCutsceneSplineSegmentProgress is updated after pos and focus are calculated.
 */
s32 intro_peach_move_camera_start_to_pipe(struct Camera *c, struct CutsceneSplinePoint positionSpline[],
                  struct CutsceneSplinePoint focusSpline[]) {
    Vec3q offsetq;
    UNUSED s32 posReturn = 0;
    s32 focusReturn = 0;

    /**
     * The position spline's speed parameters are all 0, so sCutsceneSplineSegmentProgress doesn't get
     * updated. Otherwise position would move two frames ahead, and c->focus would always be one frame
     * further along the spline than c->pos.
     */
    posReturn = move_point_along_splineq(c->posq, positionSpline, &sCutsceneSplineSegment, &sCutsceneSplineSegmentProgressq);
    focusReturn = move_point_along_splineq(c->focusq, focusSpline, &sCutsceneSplineSegment, &sCutsceneSplineSegmentProgressq);

    // The two splines used by this function are reflected in the horizontal plane for some reason,
    // so they are rotated every frame. Why do this, Nintendo?
    rotate_in_xzq(c->focusq, c->focusq, DEGREES(-180));
    rotate_in_xzq(c->posq, c->posq, DEGREES(-180));

    vec3q_set(offsetq, q(-1328), q(260), q(4664));
    vec3q_add(c->focusq, offsetq);
    vec3q_add(c->posq, offsetq);

    //posReturn += focusReturn; // Unused
    return focusReturn;
}

/**
 * Create a dialog box with the letter text
 */
BAD_RETURN(s32) peach_letter_text(UNUSED struct Camera *c) {
    create_dialog_box(DIALOG_020);
}

#ifndef VERSION_JP
BAD_RETURN(s32) play_sound_peach_reading_letter(UNUSED struct Camera *c) {
    play_sound(SOUND_PEACH_DEAR_MARIO, gGlobalSoundSource);
}
#endif

/**
 * Move the camera from peach reading the letter all the way to Mario's warp pipe. Follow the
 * sIntroStartToPipe splines.
 */
BAD_RETURN(s32) cutscene_intro_peach_start_to_pipe_spline(struct Camera *c) {
    if (intro_peach_move_camera_start_to_pipe(c, sIntroStartToPipePosition, sIntroStartToPipeFocus) != 0) {
        gCameraMovementFlags &= ~CAM_MOVE_C_UP_MODE;
        gCutsceneTimer = CUTSCENE_LOOP;
    }
}

/**
 * Loop the cutscene until Mario exits the dialog.
 */
BAD_RETURN(s32) cutscene_intro_peach_dialog(struct Camera *c) {
    if (get_dialog_id() == DIALOG_NONE) {
        vec3q_copy(gLakituState.goalPosq, c->posq);
        vec3q_copy(gLakituState.goalFocusq, c->focusq);
        sStatusFlags |= (CAM_FLAG_SMOOTH_MOVEMENT | CAM_FLAG_UNUSED_CUTSCENE_ACTIVE);
        gCutsceneTimer = CUTSCENE_STOP;
        c->cutscene = 0;
    }
}

BAD_RETURN(s32) cutscene_intro_peach_follow_pipe_spline(struct Camera *c) {
    move_point_along_splineq(c->posq, sIntroPipeToDialogPosition, &sCutsceneSplineSegment, &sCutsceneSplineSegmentProgressq);
    move_point_along_splineq(c->focusq, sIntroPipeToDialogFocus, &sCutsceneSplineSegment, &sCutsceneSplineSegmentProgressq);
}

BAD_RETURN(s32) cutscene_intro_peach_clear_cutscene_status(UNUSED struct Camera *c) {
    sMarioCamState->cameraEvent = 0;
}

/**
 * Set fov to 8 degrees, then zoom out to 30.
 */
BAD_RETURN(s32) cutscene_intro_peach_zoom_fov(UNUSED struct Camera *c) {
    sFOVState.fovq = q(8);
    set_fov_function(CAM_FOV_ZOOM_30);
}

/**
 * Reset the spline progress, turn on handheld shake.
 */
BAD_RETURN(s32) cutscene_intro_peach_reset_spline(UNUSED struct Camera *c) {
    sCutsceneSplineSegment = 0;
    sCutsceneSplineSegmentProgressq = q(0.1);
    //! @bug since this event is only called for one frame, this handheld shake is turned off on the
    //! next frame.
    set_handheld_shake(HAND_CAM_SHAKE_HIGH);
}

/**
 * Turn off handheld shake. This was likely written before handheld shake was changed to turn off every
 * frame, as it's the only instance of HAND_CAM_SHAKE_OFF.
 */
BAD_RETURN(s32) cutscene_intro_peach_handheld_shake_off(UNUSED struct Camera *c) {
    set_handheld_shake(HAND_CAM_SHAKE_OFF);
}

BAD_RETURN(s32) intro_pipe_exit_text(UNUSED struct Camera *c) {
    create_dialog_box(DIALOG_033);
}

#ifndef VERSION_JP
BAD_RETURN(s32) play_sound_intro_turn_on_hud(UNUSED struct Camera *c) {
    play_sound_rbutton_changed();
}
#endif

/**
 * Fly to the pipe. Near the end, the camera jumps to Lakitu's position and the hud turns on.
 */
BAD_RETURN(s32) cutscene_intro_peach_fly_to_pipe(struct Camera *c) {
#if defined(VERSION_US) || defined(VERSION_SH)
    cutscene_event(play_sound_intro_turn_on_hud, c, 818, 818);
#elif defined(VERSION_EU)
    cutscene_event(play_sound_intro_turn_on_hud, c, 673, 673);
#endif
    cutscene_spawn_obj(6, 1);
    cutscene_event(cutscene_intro_peach_start_flying_music, c, 0, 0);
    cutscene_event(cutscene_intro_peach_start_to_pipe_spline, c, 0, -1);
#ifdef VERSION_EU
    cutscene_event(cutscene_intro_peach_clear_cutscene_status, c, 572, 572);
#else
    cutscene_event(cutscene_intro_peach_clear_cutscene_status, c, 717, 717);
#endif
    clamp_pitchq(c->posq, c->focusq, 0x3B00, -0x3B00);
    sCutsceneVars[1].pointq[1] = q(400);
}

/**
 * Lakitu flies around the warp pipe, then Mario jumps out.
 */
BAD_RETURN(s32) cutscene_intro_peach_mario_appears(struct Camera *c) {
    sMarioCamState->cameraEvent = 0;
    cutscene_event(cutscene_intro_peach_reset_spline, c, 0, 0);
    cutscene_event(cutscene_intro_peach_follow_pipe_spline, c, 0, -1);
    cutscene_event(cutscene_intro_peach_handheld_shake_off, c, 70, 70);
    cutscene_event(intro_pipe_exit_text, c, 250, 250);

    approach_q32_asymptotic_bool(&sCutsceneVars[1].pointq[1], q(80) + q(sMarioGeometry.currFloorHeight) +
                                 qmul(sMarioCamState->posq[1] - q(sMarioGeometry.currFloorHeight), q(1.1)), q(0.4));

    // Make the camera look up as Mario jumps out of the pipe
    if (c->focusq[1] < sCutsceneVars[1].pointq[1]) {
        c->focusq[1] = sCutsceneVars[1].pointq[1];
    }

    sStatusFlags |= CAM_FLAG_UNUSED_CUTSCENE_ACTIVE;
}

/**
 * Reset the fov. This gives the effect of peach zooming out as she fades.
 */
BAD_RETURN(s32) cutscene_intro_peach_reset_fov(UNUSED struct Camera *c) {
    set_fov_function(CAM_FOV_DEFAULT);
}

/**
 * Peach reads the letter to Mario.
 */
BAD_RETURN(s32) cutscene_intro_peach_letter(struct Camera *c) {
    cutscene_spawn_obj(5, 0);
    cutscene_event(cutscene_intro_peach_zoom_fov, c, 0, 0);
    cutscene_event(cutscene_intro_peach_start_letter_music, c, 65, 65);
#ifdef VERSION_EU
    cutscene_event(cutscene_intro_peach_eu_lower_volume, c, 68, 68);
#endif
    cutscene_event(cutscene_intro_peach_start_to_pipe_spline, c, 0, 0);
    cutscene_event(peach_letter_text, c, 65, 65);
#ifndef VERSION_JP
    cutscene_event(play_sound_peach_reading_letter, c, 83, 83);
#endif

    if ((gCutsceneTimer > 120) && (get_dialog_id() == DIALOG_NONE)) {
        // Start the next scene
        gCutsceneTimer = CUTSCENE_LOOP;
    }

    clamp_pitchq(c->posq, c->focusq, 0x3B00, -0x3B00);
}

/**
 * Reset the spline progress.
 */
BAD_RETURN(s32) cutscene_end_waving_start(UNUSED struct Camera *c) {
    cutscene_reset_spline();
}

// 3rd part of data
struct CutsceneSplinePoint gIntroLakituStartToPipeFocus[] = {
    { 0, 32, { 58, -250, 346 } },    { 1, 50, { -159, -382, 224 } }, { 2, 37, { 0, -277, 237 } },
    { 3, 15, { 1, -44, 245 } },      { 4, 35, { 0, -89, 228 } },     { 5, 15, { 28, 3, 259 } },
    { 6, 25, { -38, -201, 371 } },   { 7, 20, { -642, 118, 652 } },  { 8, 25, { 103, -90, 861 } },
    { 9, 25, { 294, 145, 579 } },    { 10, 30, { 220, -42, 500 } },  { 11, 20, { 10, -134, 200 } },
    { 12, 20, { -143, -145, 351 } }, { 13, 14, { -256, -65, 528 } }, { 14, 20, { -251, -52, 459 } },
    { 15, 25, { -382, 520, 395 } },  { 16, 25, { -341, 240, 653 } }, { 17, 5, { -262, 700, 143 } },
    { 18, 15, { -760, 32, 27 } },    { 19, 20, { -756, -6, -26 } },  { 20, 20, { -613, 5, 424 } },
    { 21, 20, { -22, -100, 312 } },  { 22, 25, { 212, 80, 61 } },    { 23, 20, { 230, -28, 230 } },
    { 24, 35, { -83, -51, 303 } },   { 25, 17, { 126, 90, 640 } },   { 26, 9, { 158, 95, 763 } },
    { 27, 8, { 113, -25, 1033 } },   { 28, 20, { 57, -53, 1291 } },  { 29, 15, { 73, -34, 1350 } },
    { 30, 7, { 0, 96, 1400 } },      { 31, 8, { -59, 269, 1450 } },  { 32, 15, { 57, 1705, 1500 } },
    { 0, 15, { -227, 511, 1550 } },  { -1, 15, { -227, 511, 1600 } }
};

struct CutsceneSplinePoint gIntroLakituStartToPipeOffsetFromCamera[] = {
    { 0, 0, { -46, 87, -15 } },   { 1, 0, { -38, 91, -11 } },  { 2, 0, { -31, 93, -13 } },
    { 3, 0, { -50, 84, -16 } },   { 4, 0, { -52, 83, -17 } },  { 5, 0, { -10, 99, 3 } },
    { 6, 0, { -54, 83, -10 } },   { 7, 0, { -31, 85, -40 } },  { 8, 0, { -34, 91, 19 } },
    { 9, 0, { -9, 95, 28 } },     { 10, 0, { 17, 72, 66 } },   { 11, 0, { 88, -7, 45 } },
    { 12, 0, { 96, -6, -26 } },   { 13, 0, { 56, -1, -82 } },  { 14, 0, { 40, 65, -63 } },
    { 15, 0, { -26, -3, -96 } },  { 16, 0, { 92, 82, 19 } },   { 17, 0, { 92, 32, 19 } },
    { 18, 0, { 92, 32, 19 } },    { 19, 0, { 92, 102, 19 } },  { 20, 0, { -69, 59, -70 } },
    { 21, 0, { -77, 109, -61 } }, { 22, 0, { -87, 59, -46 } }, { 23, 0, { -99, -3, 11 } },
    { 24, 0, { -99, -11, 5 } },   { 25, 0, { -97, -6, 19 } },  { 26, 0, { -97, 22, -7 } },
    { 27, 0, { -98, -11, -13 } }, { 28, 0, { -97, -11, 19 } }, { 29, 0, { -91, -11, 38 } },
    { 30, 0, { -76, -11, 63 } },  { 31, 0, { -13, 33, 93 } },  { 32, 0, { 51, -11, 84 } },
    { 33, 0, { 51, -11, 84 } },   { -1, 0, { 51, -11, 84 } }
};

struct CutsceneSplinePoint gEndWavingPos[] = {
    { 0, 0, { -5, 975, -917 } },    { 0, 0, { -5, 975, -917 } },    { 0, 0, { -5, 975, -917 } },
    { 0, 0, { -76, 1067, 742 } },   { 0, 0, { -105, 1576, 3240 } }, { 0, 0, { -177, 1709, 5586 } },
    { 0, 0, { -177, 1709, 5586 } }, { 0, 0, { -177, 1709, 5586 } }, { 0, 0, { -177, 1709, 5586 } }
};

struct CutsceneSplinePoint gEndWavingFocus[] = {
    { 0, 50, { 18, 1013, -1415 } }, { 0, 100, { 17, 1037, -1412 } }, { 0, 100, { 16, 1061, -1408 } },
    { 0, 100, { -54, 1053, 243 } }, { 0, 100, { -84, 1575, 2740 } }, { 0, 50, { -156, 1718, 5086 } },
    { 0, 0, { -156, 1718, 5086 } }, { 0, 0, { -156, 1718, 5086 } },  { 0, 0, { -156, 1718, 5086 } }
};

BAD_RETURN(s32) cutscene_end_waving(struct Camera *c) {
    cutscene_event(cutscene_end_waving_start, c, 0, 0);
    move_point_along_splineq(c->posq, gEndWavingPos, &sCutsceneSplineSegment, &sCutsceneSplineSegmentProgressq);
    move_point_along_splineq(c->focusq, gEndWavingFocus, &sCutsceneSplineSegment, &sCutsceneSplineSegmentProgressq);
    cutscene_spawn_obj(6, 120);
}

/**
 * Called on the first frame of the credits. Resets the spline progress.
 */
BAD_RETURN(s32) cutscene_credits_reset_spline(UNUSED struct Camera *c) {
    cutscene_reset_spline();
}

extern struct CutsceneSplinePoint sBobCreditsSplinePositions[];
extern struct CutsceneSplinePoint sBobCreditsSplineFocus[];
extern struct CutsceneSplinePoint sWfCreditsSplinePositions[];
extern struct CutsceneSplinePoint sWfCreditsSplineFocus[];
extern struct CutsceneSplinePoint sJrbCreditsSplinePositions[];
extern struct CutsceneSplinePoint sJrbCreditsSplineFocus[];
extern struct CutsceneSplinePoint sCcmSlideCreditsSplinePositions[];
extern struct CutsceneSplinePoint sCcmSlideCreditsSplineFocus[];
extern struct CutsceneSplinePoint sBbhCreditsSplinePositions[];
extern struct CutsceneSplinePoint sBbhCreditsSplineFocus[];
extern struct CutsceneSplinePoint sHmcCreditsSplinePositions[];
extern struct CutsceneSplinePoint sHmcCreditsSplineFocus[];
extern struct CutsceneSplinePoint sThiWigglerCreditsSplinePositions[];
extern struct CutsceneSplinePoint sThiWigglerCreditsSplineFocus[];
extern struct CutsceneSplinePoint sVolcanoCreditsSplinePositions[];
extern struct CutsceneSplinePoint sVolcanoCreditsSplineFocus[];
extern struct CutsceneSplinePoint sSslCreditsSplinePositions[];
extern struct CutsceneSplinePoint sSslCreditsSplineFocus[];
extern struct CutsceneSplinePoint sDddCreditsSplinePositions[];
extern struct CutsceneSplinePoint sDddCreditsSplineFocus[];
extern struct CutsceneSplinePoint sSlCreditsSplinePositions[];
extern struct CutsceneSplinePoint sSlCreditsSplineFocus[];
extern struct CutsceneSplinePoint sWdwCreditsSplinePositions[];
extern struct CutsceneSplinePoint sWdwCreditsSplineFocus[];
extern struct CutsceneSplinePoint sTtmCreditsSplinePositions[];
extern struct CutsceneSplinePoint sTtmCreditsSplineFocus[];
extern struct CutsceneSplinePoint sThiHugeCreditsSplinePositions[];
extern struct CutsceneSplinePoint sThiHugeCreditsSplineFocus[];
extern struct CutsceneSplinePoint sTtcCreditsSplinePositions[];
extern struct CutsceneSplinePoint sTtcCreditsSplineFocus[];
extern struct CutsceneSplinePoint sRrCreditsSplinePositions[];
extern struct CutsceneSplinePoint sRrCreditsSplineFocus[];
extern struct CutsceneSplinePoint sSaCreditsSplinePositions[];
extern struct CutsceneSplinePoint sSaCreditsSplineFocus[];
extern struct CutsceneSplinePoint sCotmcCreditsSplinePositions[];
extern struct CutsceneSplinePoint sCotmcCreditsSplineFocus[];
extern struct CutsceneSplinePoint sDddSubCreditsSplinePositions[];
extern struct CutsceneSplinePoint sDddSubCreditsSplineFocus[];
extern struct CutsceneSplinePoint sCcmOutsideCreditsSplinePositions[];
extern struct CutsceneSplinePoint sCcmOutsideCreditsSplineFocus[];

/**
 * Follow splines through the courses of the game.
 */
BAD_RETURN(s32) cutscene_credits(struct Camera *c) {
    struct CutsceneSplinePoint *focus, *pos;

    cutscene_event(cutscene_credits_reset_spline, c, 0, 0);

    switch (gCurrLevelArea) {
        case AREA_BOB:
            pos = sBobCreditsSplinePositions;
            focus = sBobCreditsSplineFocus;
            break;
        case AREA_WF:
            pos = sWfCreditsSplinePositions;
            focus = sWfCreditsSplineFocus;
            break;
        case AREA_JRB_MAIN:
            pos = sJrbCreditsSplinePositions;
            focus = sJrbCreditsSplineFocus;
            break;
        case AREA_CCM_SLIDE:
            pos = sCcmSlideCreditsSplinePositions;
            focus = sCcmSlideCreditsSplineFocus;
            break;
        case AREA_BBH:
            pos = sBbhCreditsSplinePositions;
            focus = sBbhCreditsSplineFocus;
            break;
        case AREA_HMC:
            pos = sHmcCreditsSplinePositions;
            focus = sHmcCreditsSplineFocus;
            break;
        case AREA_THI_WIGGLER:
            pos = sThiWigglerCreditsSplinePositions;
            focus = sThiWigglerCreditsSplineFocus;
            break;
        case AREA_LLL_VOLCANO:
            pos = sVolcanoCreditsSplinePositions;
            focus = sVolcanoCreditsSplineFocus;
            break;
        case AREA_SSL_OUTSIDE:
            pos = sSslCreditsSplinePositions;
            focus = sSslCreditsSplineFocus;
            break;
        case AREA_DDD_WHIRLPOOL:
            pos = sDddCreditsSplinePositions;
            focus = sDddCreditsSplineFocus;
            break;
        case AREA_SL_OUTSIDE:
            pos = sSlCreditsSplinePositions;
            focus = sSlCreditsSplineFocus;
            break;
        case AREA_WDW_MAIN:
            pos = sWdwCreditsSplinePositions;
            focus = sWdwCreditsSplineFocus;
            break;
        case AREA_TTM_OUTSIDE:
            pos = sTtmCreditsSplinePositions;
            focus = sTtmCreditsSplineFocus;
            break;
        case AREA_THI_HUGE:
            pos = sThiHugeCreditsSplinePositions;
            focus = sThiHugeCreditsSplineFocus;
            break;
        case AREA_TTC:
            pos = sTtcCreditsSplinePositions;
            focus = sTtcCreditsSplineFocus;
            break;
        case AREA_RR:
            pos = sRrCreditsSplinePositions;
            focus = sRrCreditsSplineFocus;
            break;
        case AREA_SA:
            pos = sSaCreditsSplinePositions;
            focus = sSaCreditsSplineFocus;
            break;
        case AREA_COTMC:
            pos = sCotmcCreditsSplinePositions;
            focus = sCotmcCreditsSplineFocus;
            break;
        case AREA_DDD_SUB:
            pos = sDddSubCreditsSplinePositions;
            focus = sDddSubCreditsSplineFocus;
            break;
        case AREA_CCM_OUTSIDE:
            //! Checks if the "Snowman's Lost His Head" star was collected. The credits likely would
            //! have avoided the snowman if the player didn't collect that star, but in the end the
            //! developers decided against it.
            if (save_file_get_star_flags(gCurrSaveFileNum - 1, gCurrCourseNum - 1) & 0x10) {
                pos = sCcmOutsideCreditsSplinePositions;
                focus = sCcmOutsideCreditsSplineFocus;
            } else {
                pos = sCcmOutsideCreditsSplinePositions;
                focus = sCcmOutsideCreditsSplineFocus;
            }
            break;
        default:
            pos = sCcmOutsideCreditsSplinePositions;
            focus = sCcmOutsideCreditsSplineFocus;
    }

    copy_spline_segment(sCurCreditsSplinePos, pos);
    copy_spline_segment(sCurCreditsSplineFocus, focus);
    move_point_along_splineq(c->posq, sCurCreditsSplinePos, &sCutsceneSplineSegment, &sCutsceneSplineSegmentProgressq);
    move_point_along_splineq(c->focusq, sCurCreditsSplineFocus, &sCutsceneSplineSegment, &sCutsceneSplineSegmentProgressq);
    player2_rotate_cam(c, -0x2000, 0x2000, -0x4000, 0x4000);
}

/**
 * Set the camera pos relative to Mario.
 */
BAD_RETURN(s32) cutscene_sliding_doors_open_start(struct Camera *c) {
    q32 distq;
    s16 pitch, yaw;

    vec3q_get_dist_and_angle(sMarioCamState->posq, c->posq, &distq, &pitch, &yaw);

    // If the camera is too close, warp it backwards set it to a better angle.
    if (distq < q(500)) {
        distq = q(500);
        yaw = sMarioCamState->faceAngle[1] + 0x8800;
        pitch = 0x800;
    }

    vec3q_set_dist_and_angle(sMarioCamState->posq, c->posq, distq, pitch, yaw);
}

/**
 * cvar1: Mario's position
 * cvar0.angle: Mario's angle
 * cvar0.point: offset from Mario
 */
BAD_RETURN(s32) cutscene_sliding_doors_open_set_cvars(UNUSED struct Camera *c) {
    vec3q_copy(sCutsceneVars[1].pointq, sMarioCamState->posq);
    vec3s_copy(sCutsceneVars[0].angle, sMarioCamState->faceAngle);
    vec3q_set(sCutsceneVars[0].pointq, q(80), q(325), q(200));
}

/**
 * Decrease the cvar0 y offset to 75, which would simulate Lakitu flying under the doorway.
 * However, the initial y offset is too high for Lakitu to reach 75 in time.
 */
BAD_RETURN(s32) cutscene_sliding_doors_go_under_doorway(UNUSED struct Camera *c) {
    camera_approach_q32_symmetric_bool(&sCutsceneVars[0].pointq[1], q(75), q(10));
}

/**
 * Approach a y offset of 125 again.
 */
BAD_RETURN(s32) cutscene_sliding_doors_fly_back_up(UNUSED struct Camera *c) {
    camera_approach_q32_symmetric_bool(&sCutsceneVars[0].pointq[1], q(125), q(10));
}

/**
 * Follow Mario through the door, by approaching cvar1.point.
 */
BAD_RETURN(s32) cutscene_sliding_doors_follow_mario(struct Camera *c) {
    Vec3q posq;

    vec3q_copy(posq, c->posq);
    // Update cvar1 with Mario's position (the y value doesn't change)
    sCutsceneVars[1].pointq[0] = sMarioCamState->posq[0];
    sCutsceneVars[1].pointq[2] = sMarioCamState->posq[2];

    // Decrease cvar0's offsets, moving the camera behind Mario at his eye height.
    approach_q32_asymptotic_bool(&sCutsceneVars[0].pointq[0], 0, q(0.1));
    camera_approach_q32_symmetric_bool(&sCutsceneVars[0].pointq[2], q(125), q(50));
    // Update cvar0's angle
    approach_vec3s_asymptotic(sCutsceneVars[0].angle, sMarioCamState->faceAngle, 16, 16, 16);

    // Apply the offset to the camera's position
    offset_rotatedq(posq, sCutsceneVars[1].pointq, sCutsceneVars[0].pointq, sCutsceneVars[0].angle);
    approach_vec3q_asymptotic(c->posq, posq, q(0.15), q(0.05), q(0.15));

    // Focus on Mario's eye height
    set_focus_rel_marioq(c, 0, q(125), 0, 0);
}

/**
 * Plays when Mario opens the sliding doors.
 * Note: the star door unlocking event is not a cutscene, it's handled by Mario separately.
 */
BAD_RETURN(s32) cutscene_sliding_doors_open(struct Camera *c) {
    reset_pan_distance(c);
    cutscene_event(cutscene_sliding_doors_open_start, c, 0, 8);
    cutscene_event(cutscene_sliding_doors_open_set_cvars, c, 8, 8);
    cutscene_event(cutscene_sliding_doors_go_under_doorway, c, 8, 28);
    cutscene_event(cutscene_sliding_doors_fly_back_up, c, 29, -1);
    cutscene_event(cutscene_sliding_doors_follow_mario, c, 8, -1);
}

/**
 * Ends the double door cutscene.
 */
BAD_RETURN(s32) cutscene_double_doors_end(struct Camera *c) {
    set_flag_post_door(c);
    c->cutscene = 0;
    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;
}

BAD_RETURN(s32) cutscene_enter_painting_stub(UNUSED struct Camera *c) {
}

/**
 * Plays when Mario enters a painting. The camera flies up to the painting's center, then it slowly
 * zooms in until the star select screen appears.
 */
BAD_RETURN(s32) cutscene_enter_painting(struct Camera *c) {
    struct Surface *floor, *highFloor;
    Vec3q paintingPosq, focusq, focusOffsetq;
    Vec3s paintingAngle;
    q32 floorHeightq;

    cutscene_event(cutscene_enter_painting_stub, c, 0, 0);
    // Zoom in
    set_fov_function(CAM_FOV_APP_20);
    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;

    if (gRipplingPainting != NULL) {
        paintingAngle[0] = 0;
        paintingAngle[1] = (s32)((gRipplingPainting->yaw / 360.f) * 65536.f); // convert degrees to IAU
        paintingAngle[2] = 0;

        focusOffsetq[0] = q(gRipplingPainting->size) / 2;
        focusOffsetq[1] = focusOffsetq[0];
        focusOffsetq[2] = 0;

        paintingPosq[0] = q(gRipplingPainting->posX);
        paintingPosq[1] = q(gRipplingPainting->posY);
        paintingPosq[2] = q(gRipplingPainting->posZ);

        offset_rotatedq(focusq, paintingPosq, focusOffsetq, paintingAngle);
        approach_vec3q_asymptotic(c->focusq, focusq, q(0.1), q(0.1), q(0.1));
        focusOffsetq[2] = q(gRipplingPainting->size) * -500 / 307;
        offset_rotatedq(focusq, paintingPosq, focusOffsetq, paintingAngle);
        floorHeightq = find_floorq(focusq[0], focusq[1] + q(500), focusq[2], &highFloor) + q(125);

        if (focusq[1] < floorHeightq) {
            focusq[1] = floorHeightq;
        }

        if (c->cutscene == CUTSCENE_ENTER_PAINTING) {
            approach_vec3q_asymptotic(c->posq, focusq, q(0.2), q(0.1), q(0.2));
        } else {
            approach_vec3q_asymptotic(c->posq, focusq, q(0.9), q(0.9), q(0.9));
        }

        find_floorq(sMarioCamState->posq[0], sMarioCamState->posq[1] + q(50), sMarioCamState->posq[2], &floor);

        if ((floor->type < SURFACE_PAINTING_WOBBLE_A6) || (floor->type > SURFACE_PAINTING_WARP_F9)) {
            c->cutscene = 0;
            gCutsceneTimer = CUTSCENE_STOP;
            sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;
        }
    }
    c->mode = CAMERA_MODE_CLOSE;
}

/**
 * Warp the camera to Mario, then use his faceAngle to calculate the right relative position.
 *
 * cvar0.point is Mario's position
 * cvar0.angle is Mario's faceAngle
 *
 * cvar1 is the camera's position relative to Mario
 * cvar2 is the camera's focus relative to Mario
 */
BAD_RETURN(s32) cutscene_exit_painting_start(struct Camera *c) {
    struct Surface *floor;
    q32 floorHeightq;

    vec3q_set(sCutsceneVars[2].pointq, q(258), q(-352), q(1189));
    vec3q_set(sCutsceneVars[1].pointq, q(65), q(-155), q(444));

    if (gPrevLevel == LEVEL_TTM) {
        sCutsceneVars[1].pointq[1] = 0;
        sCutsceneVars[1].pointq[2] = 0;
    }
    vec3q_copy(sCutsceneVars[0].pointq, sMarioCamState->posq);
    sCutsceneVars[0].angle[0] = 0;
    sCutsceneVars[0].angle[1] = sMarioCamState->faceAngle[1];
    sCutsceneVars[0].angle[2] = 0;
    offset_rotatedq(c->focusq, sCutsceneVars[0].pointq, sCutsceneVars[1].pointq, sCutsceneVars[0].angle);
    offset_rotatedq(c->posq, sCutsceneVars[0].pointq, sCutsceneVars[2].pointq, sCutsceneVars[0].angle);
    floorHeightq = find_floorq(c->posq[0], c->posq[1] + q(10), c->posq[2], &floor);

    if (floorHeightq != q(FLOOR_LOWER_LIMIT)) {
        if (c->posq[1] < (floorHeightq += q(60))) {
            c->posq[1] = floorHeightq;
        }
    }
}

/**
 * Decrease cvar2's x and z offset, moving closer to Mario.
 */
BAD_RETURN(s32) cutscene_exit_painting_move_to_mario(struct Camera *c) {
    Vec3q posq;

    //! Tricky math: Since offset_rotated() flips Z offsets, you'd expect a positive Z offset to move
    //! the camera into the wall. However, Mario's faceAngle always points into the painting, so a
    //! positive Z offset moves the camera "behind" Mario, away from the painting.
    //!
    //! In the success cutscene, when Mario jumps out face-first, only his gfx angle is updated. His
    //! actual face angle isn't updated until after the cutscene.
    approach_q32_asymptotic_bool(&sCutsceneVars[2].pointq[0], q(178), q(0.05));
    approach_q32_asymptotic_bool(&sCutsceneVars[2].pointq[2], q(889), q(0.05));
    offset_rotatedq(posq, sCutsceneVars[0].pointq, sCutsceneVars[2].pointq, sCutsceneVars[0].angle);
    c->posq[0] = posq[0];
    c->posq[2] = posq[2];
}

/**
 * Move the camera down to the floor Mario lands on.
 */
BAD_RETURN(s32) cutscene_exit_painting_move_to_floor(struct Camera *c) {
    struct Surface *floor;
    Vec3q floorHeightq;

    vec3q_copy(floorHeightq, sMarioCamState->posq);
    floorHeightq[1] = find_floorq(sMarioCamState->posq[0], sMarioCamState->posq[1] + q(10), sMarioCamState->posq[2], &floor);

    if (floor != NULL) {
        floorHeightq[1] = floorHeightq[1] + qmul(sMarioCamState->posq[1] - floorHeightq[1], q(0.7)) + q(125);
        approach_vec3q_asymptotic(c->focusq, floorHeightq, q(0.2), q(0.2), q(0.2));

        if (floorHeightq[1] < c->posq[1]) {
            approach_q32_asymptotic_bool(&c->posq[1], floorHeightq[1], q(0.05));
        }
    }
}

/**
 * Cutscene played when Mario leaves a painting, either due to death or collecting a star.
 */
BAD_RETURN(s32) cutscene_exit_painting(struct Camera *c) {
    cutscene_event(cutscene_exit_painting_start, c, 0, 0);
    cutscene_event(cutscene_exit_painting_move_to_mario, c, 5, -1);
    cutscene_event(cutscene_exit_painting_move_to_floor, c, 5, -1);

    //! Hardcoded position. TTM's painting is close to an opposite wall, so just fix the pos.
    if (gPrevLevel == LEVEL_TTM) {
        vec3q_set(c->posq, q(-296), q(1261), q(3521));
    }

    update_camera_yaw(c);
}

/**
 * Unused. Warp the camera to Mario.
 */
BAD_RETURN(s32) cutscene_unused_exit_start(struct Camera *c) {
    Vec3q offsetq;
    Vec3s marioAngle;

    vec3q_set(offsetq, q(200), q(300), q(200));
    vec3s_set(marioAngle, 0, sMarioCamState->faceAngle[1], 0);
    offset_rotatedq(c->posq, sMarioCamState->posq, offsetq, marioAngle);
    set_focus_rel_marioq(c, 0, q(125), 0, 0);
}

/**
 * Unused. Focus on Mario as he exits.
 */
BAD_RETURN(s32) cutscene_unused_exit_focus_mario(struct Camera *c) {
    Vec3q focusq;

    vec3q_set(focusq, sMarioCamState->posq[0], sMarioCamState->posq[1] + q(125), sMarioCamState->posq[2]);
    set_focus_rel_marioq(c, 0, q(125), 0, 0);
    approach_vec3q_asymptotic(c->focusq, focusq, q(0.02), q(0.001), q(0.02));
    update_camera_yaw(c);
}

/**
 * Give control back to the player.
 */
BAD_RETURN(s32) cutscene_exit_painting_end(struct Camera *c) {
    c->mode = CAMERA_MODE_CLOSE;
    c->cutscene = 0;
    gCutsceneTimer = CUTSCENE_STOP;
    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;
    sStatusFlags &= ~CAM_FLAG_BLOCK_SMOOTH_MOVEMENT;
    update_camera_yaw(c);
}

/**
 * End the cutscene, starting cannon mode.
 */
BAD_RETURN(s32) cutscene_enter_cannon_end(struct Camera *c) {
    sStatusFlags &= ~CAM_FLAG_SMOOTH_MOVEMENT;
    sStatusFlags |= CAM_FLAG_BLOCK_SMOOTH_MOVEMENT;
    c->mode = CAMERA_MODE_INSIDE_CANNON;
    c->cutscene = 0;
    sCannonYOffsetq = q(800);
}

/**
 * Rotate around the cannon as it rises out of the hole.
 */
BAD_RETURN(s32) cutscene_enter_cannon_raise(struct Camera *c) {
    struct Object *o;
    q32 floorHeightq;
    struct Surface *floor;
    Vec3q cannonFocusq;
    Vec3s cannonAngle;

    // Shake the camera when the cannon is fully raised
    cutscene_event(cutscene_shake_explosion, c, 70, 70);
    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;
    camera_approach_s16_symmetric_bool(&sCutsceneVars[1].angle[0], 0, 0x80);
    camera_approach_s16_symmetric_bool(&sCutsceneVars[2].angle[0], 0, 0x80);
    // Move the camera around the cannon, gradually rotating and moving closer
    vec3q_set_dist_and_angle(sCutsceneVars[0].pointq, c->posq, sCutsceneVars[1].pointq[2], sCutsceneVars[1].angle[0],
                             sCutsceneVars[1].angle[1]);
    sCutsceneVars[1].pointq[2] = approach_q32(sCutsceneVars[1].pointq[2], q(400), q(5), q(5));
    sCutsceneVars[1].angle[1] += 0x40;
    sCutsceneVars[3].pointq[1] += q(2);
    c->posq[1] += sCutsceneVars[3].pointq[1];

    if ((o = sMarioCamState->usedObj) != NULL) {
        sCutsceneVars[0].pointq[1] = QFIELD(o, oPosY);
        cannonAngle[0] = o->oMoveAnglePitch;
        cannonAngle[1] = o->oMoveAngleYaw;
        cannonAngle[2] = o->oMoveAngleRoll;
        c->focusq[0] = QFIELD(o, oPosX);
        c->focusq[1] = QFIELD(o, oPosY);
        c->focusq[2] = QFIELD(o, oPosZ);
        cannonFocusq[0] = 0;
        cannonFocusq[1] = q(100);
        cannonFocusq[2] = 0;
        offset_rotatedq(c->focusq, c->focusq, cannonFocusq, cannonAngle);
    }

    floorHeightq = find_floorq(c->posq[0], c->posq[1] + q(500), c->posq[2], &floor) + q(100);

    if (c->posq[1] < floorHeightq) {
        c->posq[1] = floorHeightq;
    }
}

/**
 * Start the cannon entering cutscene
 */
BAD_RETURN(s32) cutscene_enter_cannon_start(struct Camera *c) {
    struct Object *o;

    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;
    sMarioCamState->cameraEvent = 0;

    // Store the cannon's position and angle in cvar0
    if ((o = sMarioCamState->usedObj) != NULL) {
        sCutsceneVars[0].pointq[0] = QFIELD(o, oPosX);
        sCutsceneVars[0].pointq[1] = QFIELD(o, oPosY);
        sCutsceneVars[0].pointq[2] = QFIELD(o, oPosZ);
        sCutsceneVars[0].angle[0] = o->oMoveAnglePitch;
        sCutsceneVars[0].angle[1] = o->oMoveAngleYaw;
        sCutsceneVars[0].angle[2] = o->oMoveAngleRoll;
    }

    // Store the camera's polar offset from the cannon in cvar1
    vec3q_get_dist_and_angle(sCutsceneVars[0].pointq, c->posq, &sCutsceneVars[1].pointq[2],
                             &sCutsceneVars[1].angle[0], &sCutsceneVars[1].angle[1]);
    sCutsceneVars[3].pointq[1] = 0;
    //! cvar4 is unused in this cutscene
    sCutsceneVars[4].pointq[1] = 0;
}

/**
 * Store the camera's pos and focus for the door cutscene
 */
BAD_RETURN(s32) cutscene_door_start(struct Camera *c) {
    vec3q_copy(sCutsceneVars[0].pointq, c->posq);
    vec3q_copy(sCutsceneVars[1].pointq, c->focusq);
}

/**
 * Fix the camera in place while the door opens.
 */
BAD_RETURN(s32) cutscene_door_fix_cam(struct Camera *c) {
    vec3q_copy(c->posq, sCutsceneVars[0].pointq);
    vec3q_copy(c->focusq, sCutsceneVars[1].pointq);
}

/**
 * Loop until Mario is no longer using the door.
 */
BAD_RETURN(s32) cutscene_door_loop(struct Camera *c) {
    //! bitwise AND instead of boolean
    if ((sMarioCamState->action != ACT_PULLING_DOOR) & (sMarioCamState->action != ACT_PUSHING_DOOR)) {
        gCutsceneTimer = CUTSCENE_STOP;
        c->cutscene = 0;
    }
}

/**
 * Warp the camera behind Mario.
 */
BAD_RETURN(s32) cutscene_door_move_behind_mario(struct Camera *c) {
    Vec3q camOffsetq;
    s16 doorRotation;

    reset_pan_distance(c);
    determine_pushing_or_pulling_door(&doorRotation);
    set_focus_rel_marioq(c, 0, q(125), 0, 0);
    vec3s_set(sCutsceneVars[0].angle, 0, sMarioCamState->faceAngle[1] + doorRotation, 0);
    vec3q_set(camOffsetq, 0, q(125), q(250));

    if (doorRotation == 0) { //! useless code
        camOffsetq[0] = 0;
    } else {
        camOffsetq[0] = 0;
    }

    offset_rotatedq(c->posq, sMarioCamState->posq, camOffsetq, sCutsceneVars[0].angle);
}

/**
 * Follow Mario through the door.
 */
BAD_RETURN(s32) cutscene_door_follow_mario(struct Camera *c) {
    s16 pitch, yaw;
    q32 distq;

    set_focus_rel_marioq(c, 0, q(125), 0, 0);
    vec3q_get_dist_and_angle(c->focusq, c->posq, &distq, &pitch, &yaw);
    camera_approach_q32_symmetric_bool(&distq, q(150), q(7));
    vec3q_set_dist_and_angle(c->focusq, c->posq, distq, pitch, yaw);
    update_camera_yaw(c);
}

/**
 * Ends the door cutscene. Sets the camera mode to close mode unless the default is free roam.
 */
BAD_RETURN(s32) cutscene_door_end(struct Camera *c) {
    if (c->defMode == CAMERA_MODE_FREE_ROAM) {
        c->mode = CAMERA_MODE_FREE_ROAM;
    } else {
        c->mode = CAMERA_MODE_CLOSE;
    }

    c->cutscene = 0;
    gCutsceneTimer = CUTSCENE_STOP;
    sStatusFlags |= CAM_FLAG_SMOOTH_MOVEMENT;
    sStatusFlags &= ~CAM_FLAG_BLOCK_SMOOTH_MOVEMENT;
    set_flag_post_door(c);
    update_camera_yaw(c);
}

/**
 * Used for entering a room that uses a specific camera mode, like the castle lobby or BBH
 */
BAD_RETURN(s32) cutscene_door_mode(struct Camera *c) {
    reset_pan_distance(c);
    camera_course_processing(c);

    if (c->mode == CAMERA_MODE_FIXED) {
        c->nextYaw = update_fixed_cameraq(c, c->focusq, c->posq);
    }
    if (c->mode == CAMERA_MODE_PARALLEL_TRACKING) {
        c->nextYaw = update_parallel_tracking_cameraq(c, c->focusq, c->posq);
    }

    c->yaw = c->nextYaw;

    // Loop until Mario is no longer using the door
    if (sMarioCamState->action != ACT_ENTERING_STAR_DOOR &&
        sMarioCamState->action != ACT_PULLING_DOOR &&
        sMarioCamState->action != ACT_PUSHING_DOOR) {
        gCutsceneTimer = CUTSCENE_STOP;
        c->cutscene = 0;
    }
}

/******************************************************************************************************
 * Cutscenes
 ******************************************************************************************************/

/**
 * Cutscene that plays when Mario beats the game.
 */
struct Cutscene sCutsceneEnding[] = {
    { cutscene_ending_mario_fall, 170 },
    { cutscene_ending_mario_land, 70 },
#ifdef VERSION_EU
    { cutscene_ending_mario_land_closeup, 0x44 },
    { cutscene_ending_stars_free_peach,  0x15c },
    { cutscene_ending_peach_appears, 0x6d  },
    { cutscene_ending_peach_descends, 0x212 },
    { cutscene_ending_mario_to_peach, 0x69 },
    { cutscene_ending_peach_wakeup, 0x1a4 },
    { cutscene_ending_dialog, 0x114 },
    { cutscene_ending_kiss, 0x10b },
#else
    { cutscene_ending_mario_land_closeup, 75 },
#ifdef VERSION_SH
    { cutscene_ending_stars_free_peach, 431 },
#else
    { cutscene_ending_stars_free_peach, 386 },
#endif
    { cutscene_ending_peach_appears, 139 },
    { cutscene_ending_peach_descends, 590 },
    { cutscene_ending_mario_to_peach, 95 },
#ifdef VERSION_SH
    { cutscene_ending_peach_wakeup, 455 },
    { cutscene_ending_dialog, 286 },
#else
    { cutscene_ending_peach_wakeup, 425 },
    { cutscene_ending_dialog, 236 },
#endif
    { cutscene_ending_kiss, 245 },
#endif
    { cutscene_ending_cake_for_mario, CUTSCENE_LOOP },
    { cutscene_ending_stop, 0 }
};

/**
 * Cutscene that plays when Mario collects the grand star from bowser.
 */
struct Cutscene sCutsceneGrandStar[] = {
    { cutscene_grand_star, 360 },
    { cutscene_grand_star_fly, CUTSCENE_LOOP }
};

struct Cutscene sCutsceneUnused[] = {
    { cutscene_unused_start, 1 },
    { cutscene_unused_loop, CUTSCENE_LOOP }
};

/**
 * Cutscene that plays when Mario enters a door that warps to another area.
 */
struct Cutscene sCutsceneDoorWarp[] = {
    { cutscene_door_start, 1 },
    { cutscene_door_loop, CUTSCENE_LOOP }
};

/**
 * Cutscene that plays after the credits, when Lakitu is flying away from the castle.
 */
struct Cutscene sCutsceneEndWaving[] = {
    { cutscene_end_waving, CUTSCENE_LOOP }
};

/**
 * The game's credits.
 */
struct Cutscene sCutsceneCredits[] = {
    { cutscene_credits, CUTSCENE_LOOP }
};

/**
 * Cutscene that plays when Mario pulls open a door.
 */
struct Cutscene sCutsceneDoorPull[] = {
    { cutscene_door_start, 1 },
    { cutscene_door_fix_cam, 30 },
    { cutscene_door_move_behind_mario, 1 },
    { cutscene_door_follow_mario, 50 },
    { cutscene_door_end, 0 }
};

/**
 * Cutscene that plays when Mario pushes open a door.
 */
struct Cutscene sCutsceneDoorPush[] = {
    { cutscene_door_start, 1 },
    { cutscene_door_fix_cam, 20 },
    { cutscene_door_move_behind_mario, 1 },
    { cutscene_door_follow_mario, 50 },
    { cutscene_door_end, 0 }
};

/**
 * Cutscene that plays when Mario pulls open a door that has some special mode requirement on the other
 * side.
 */
struct Cutscene sCutsceneDoorPullMode[] = {
    { cutscene_door_start, 1 },
    { cutscene_door_fix_cam, 30 },
    { cutscene_door_mode, CUTSCENE_LOOP }
};

/**
 * Cutscene that plays when Mario pushes open a door that has some special mode requirement on the other
 * side.
 */
struct Cutscene sCutsceneDoorPushMode[] = {
    { cutscene_door_start, 1 },
    { cutscene_door_fix_cam, 20 },
    { cutscene_door_mode, CUTSCENE_LOOP }
};

/**
 * Cutscene that plays when Mario enters the cannon and it rises out of the hole.
 */
struct Cutscene sCutsceneEnterCannon[] = {
    { cutscene_enter_cannon_start, 1 },
    { cutscene_enter_cannon_raise, 121 },
    { cutscene_enter_cannon_end, 0 }
};

/**
 * Cutscene that plays when a star spawns from ie a box or after a boss fight.
 */
struct Cutscene sCutsceneStarSpawn[] = {
    { cutscene_star_spawn, CUTSCENE_LOOP },
    { cutscene_star_spawn_back, 15 },
    { cutscene_star_spawn_end, 0 }
};

/**
 * Cutscene for the red coin star spawning. Compared to a regular star, this cutscene can warp long
 * distances.
 */
struct Cutscene sCutsceneRedCoinStarSpawn[] = {
    { cutscene_red_coin_star, CUTSCENE_LOOP },
    { cutscene_red_coin_star_end, 0 }
};

/**
 * Cutscene that plays when Mario enters a course painting.
 */
struct Cutscene sCutsceneEnterPainting[] = {
    { cutscene_enter_painting, CUTSCENE_LOOP }
};

/**
 * Cutscene that plays when Mario dies and warps back to the castle.
 */
struct Cutscene sCutsceneDeathExit[] = {
    { cutscene_exit_painting, 118 },
    { cutscene_exit_painting_end, 0 }
};

/**
 * Cutscene that plays when Mario warps to the castle after collecting a star.
 */
struct Cutscene sCutsceneExitPaintingSuccess[] = {
    { cutscene_exit_painting, 180 },
    { cutscene_exit_painting_end, 0 }
};

struct Cutscene sCutsceneUnusedExit[] = {
    { cutscene_unused_exit_start, 1 },
    { cutscene_unused_exit_focus_mario, 60 },
    { cutscene_exit_painting_end, 0 }
};

/**
 * The intro of the game. Peach reads her letter and Lakitu flies down to Mario's warp pipe.
 */
struct Cutscene sCutsceneIntroPeach[] = {
    { cutscene_intro_peach_letter, CUTSCENE_LOOP },
    { cutscene_intro_peach_reset_fov, 35 },
#ifdef VERSION_EU
    { cutscene_intro_peach_fly_to_pipe, 675 },
#else
    { cutscene_intro_peach_fly_to_pipe, 820 },
#endif
    { cutscene_intro_peach_mario_appears, 270 },
    { cutscene_intro_peach_dialog, CUTSCENE_LOOP }
};

/**
 * Cutscene that plays when a cannon door is opened.
 */
struct Cutscene sCutscenePrepareCannon[] = {
    { cutscene_prepare_cannon, 170 },
    { cutscene_prepare_cannon_end, 0 }
};

/**
 * Cutscene that plays when Mario enters the castle grounds after leaving CotMC through the waterfall.
 */
struct Cutscene sCutsceneExitWaterfall[] = {
    { cutscene_exit_waterfall, 52 },
    { cutscene_exit_to_castle_grounds_end, 0 }
};

/**
 * Cutscene that plays when Mario falls from WMOTR.
 */
struct Cutscene sCutsceneFallToCastleGrounds[] = {
    { cutscene_exit_fall_to_castle_grounds, 73 },
    { cutscene_exit_to_castle_grounds_end, 0 }
};

/**
 * Cutscene that plays when Mario enters the pyramid through the hole at the top.
 */
struct Cutscene sCutsceneEnterPyramidTop[] = {
    { cutscene_enter_pyramid_top, 90 },
    { cutscene_exit_to_castle_grounds_end, 0 }
};

/**
 * Unused cutscene for when the pyramid explodes.
 */
struct Cutscene sCutscenePyramidTopExplode[] = {
    { cutscene_mario_dialog, CUTSCENE_LOOP },
    { cutscene_pyramid_top_explode, 150 },
    { cutscene_pyramid_top_explode_end, 0 }
};

/**
 * Cutscene that plays when Mario dies while standing, or from electrocution.
 */
struct Cutscene sCutsceneStandingDeath[] = {
    { cutscene_death_standing, CUTSCENE_LOOP }
};

/**
 * Cutscene that plays when Mario enters HMC or CotMC.
 */
struct Cutscene sCutsceneEnterPool[] = {
    { cutscene_enter_pool, 100 },
    { cutscene_exit_to_castle_grounds_end, 0 }
};

/**
 * Cutscene that plays when Mario dies on his stomach.
 */
struct Cutscene sCutsceneDeathStomach[] = {
    { cutscene_death_stomach, CUTSCENE_LOOP }
};

/**
 * Cutscene that plays when Mario dies on his back.
 */
struct Cutscene sCutsceneDeathOnBack[] = {
    { cutscene_bbh_death, CUTSCENE_LOOP }
};

/**
 * Cutscene that plays when Mario dies in quicksand.
 */
struct Cutscene sCutsceneQuicksandDeath[] = {
    { cutscene_quicksand_death, CUTSCENE_LOOP },
};

/**
 * Unused cutscene for ACT_WATER_DEATH, which happens when Mario gets hit by an enemy under water.
 */
struct Cutscene sCutsceneWaterDeath[] = {
    { cutscene_quicksand_death, CUTSCENE_LOOP }
};

/**
 * Cutscene that plays when Mario suffocates.
 */
struct Cutscene sCutsceneSuffocation[] = {
    { cutscene_suffocation, CUTSCENE_LOOP }
};

/**
 * Cutscene that plays when entering bowser's arenas.
 */
struct Cutscene sCutsceneEnterBowserArena[] = {
    { cutscene_bowser_arena, 180 },
    { cutscene_bowser_arena_dialog, CUTSCENE_LOOP },
    { cutscene_bowser_arena_end, 0 }
};

// The dance cutscenes are automatically stopped since reset_camera() is called after Mario warps.

/**
 * Star dance cutscene.
 * For the default dance, the camera moves closer to Mario, then stays in place.
 * For the rotate dance, the camera moves closer and rotates clockwise around Mario.
 */
struct Cutscene sCutsceneDanceDefaultRotate[] = {
    { cutscene_dance_default_rotate, CUTSCENE_LOOP }
};

/**
 * Star dance cutscene.
 * The camera moves closer and rotates clockwise around Mario.
 */
struct Cutscene sCutsceneDanceFlyAway[] = {
    { cutscene_dance_fly_away, CUTSCENE_LOOP }
};

/**
 * Star dance cutscene.
 * The camera moves in for a closeup on Mario. Used in tight spaces and underwater.
 */
struct Cutscene sCutsceneDanceCloseup[] = {
    { cutscene_dance_closeup, CUTSCENE_LOOP }
};

/**
 * Star dance cutscene.
 * The camera moves closer and rotates clockwise around Mario.
 */
struct Cutscene sCutsceneKeyDance[] = {
    { cutscene_key_dance, CUTSCENE_LOOP }
};

/**
 * Cutscene that plays when Mario presses a cap switch.
 */
struct Cutscene sCutsceneCapSwitchPress[] = {
    { cutscene_cap_switch_press, CUTSCENE_LOOP }
};

/**
 * Cutscene that plays when Mario opens a sliding star door.
 */
struct Cutscene sCutsceneSlidingDoorsOpen[] = {
    { cutscene_sliding_doors_open, 50 },
    { cutscene_double_doors_end, 0 }
};

/**
 * Cutscene that plays when Mario unlocks the basement or upstairs key door.
 */
struct Cutscene sCutsceneUnlockKeyDoor[] = {
    { cutscene_unlock_key_door, 200 },
    { cutscene_double_doors_end, 0 }
};

/**
 * Cutscene that plays when Mario exits bowser's arena after getting the key.
 */
struct Cutscene sCutsceneExitBowserSuccess[] = {
    { cutscene_exit_bowser_succ, 190 },
    { cutscene_non_painting_end, 0 }
};

/**
 * Unused cutscene for when Mario dies in bowser's arena. Instead, Mario just respawns at the warp pipe.
 */
struct Cutscene sCutsceneExitBowserDeath[] = {
    { cutscene_exit_bowser_death, 120 },
    { cutscene_non_painting_end, 0 }
};

/**
 * Cutscene that plays when Mario exits a non-painting course, like HMC.
 */
struct Cutscene sCutsceneExitSpecialSuccess[] = {
    { cutscene_exit_non_painting_succ, 163 },
    { cutscene_non_painting_end, 0 }
};

/**
 * Cutscene that plays when Mario exits from dying in a non-painting course, like HMC.
 */
struct Cutscene sCutsceneNonPaintingDeath[] = {
    { cutscene_non_painting_death, 120 },
    { cutscene_non_painting_end, 0 }
};

/**
 * Cutscene that plays when Mario talks to a creature.
 */
struct Cutscene sCutsceneDialog[] = {
    { cutscene_dialog, CUTSCENE_LOOP },
    { cutscene_dialog_set_flag, 12 },
    { cutscene_dialog_end, 0 }
};

/**
 * Cutscene that plays when Mario reads a sign or message.
 */
struct Cutscene sCutsceneReadMessage[] = {
    { cutscene_read_message, CUTSCENE_LOOP },
    { cutscene_read_message_set_flag, 15 },
    { cutscene_read_message_end, 0 }
};

/* TODO:
 * The next two arrays are both related to levels, and they look generated.
 * These should be split into their own file.
 */

/**
 * Converts the u32 given in DEFINE_COURSE to a u8 with the odd and even digits rotated into the right
 * order for sDanceCutsceneIndexTable
 */
#define DROT(value, index) ((value >> (32 - (index + 1) * 8)) & 0xF0) >> 4 | \
                           ((value >> (32 - (index + 1) * 8)) & 0x0F) << 4

#define DANCE_ENTRY(c) { DROT(c, 0), DROT(c, 1), DROT(c, 2), DROT(c, 3) },

#define DEFINE_COURSE(_0, cutscenes) DANCE_ENTRY(cutscenes)
#define DEFINE_COURSES_END()
#define DEFINE_BONUS_COURSE(_0, cutscenes) DANCE_ENTRY(cutscenes)

/**
 * Each hex digit is an index into sDanceCutsceneTable.
 *
 * 0: Lakitu flies away after the dance
 * 1: Only rotates the camera, doesn't zoom out
 * 2: The camera goes to a close up of Mario
 * 3: Bowser keys and the grand star
 * 4: Default, used for 100 coin stars, 8 red coin stars in bowser levels, and secret stars
 */
u8 sDanceCutsceneIndexTable[][4] = {
    #include "levels/course_defines.h"
    { 0x44, 0x44, 0x44, 0x04 }, // (26) Why go to all this trouble to save bytes and do this?!
};
#undef DEFINE_COURSE
#undef DEFINE_COURSES_END
#undef DEFINE_BONUS_COURSE

#undef DANCE_ENTRY
#undef DROT

/**
 * These masks set whether or not the camera zooms out when game is paused.
 *
 * Each entry is used by two levels. Even levels use the low 4 bits, odd levels use the high 4 bits
 * Because areas are 1-indexed, a mask of 0x1 will make area 1 (not area 0) zoom out.
 *
 * In zoom_out_if_paused_and_outside(), the current area is converted to a shift.
 * Then the value of (1 << shift) is &'d with the level's mask,
 * and if the result is non-zero, the camera will zoom out.
 */
u8 sZoomOutAreaMasks[] = {
    ZOOMOUT_AREA_MASK(0,0,0,0, 0,0,0,0), // Unused         | Unused
    ZOOMOUT_AREA_MASK(0,0,0,0, 0,0,0,0), // Unused         | Unused
    ZOOMOUT_AREA_MASK(0,0,0,0, 1,0,0,0), // BBH            | CCM
    ZOOMOUT_AREA_MASK(0,0,0,0, 0,0,0,0), // CASTLE_INSIDE  | HMC
    ZOOMOUT_AREA_MASK(1,0,0,0, 1,0,0,0), // SSL            | BOB
    ZOOMOUT_AREA_MASK(1,0,0,0, 1,0,0,0), // SL             | WDW
    ZOOMOUT_AREA_MASK(0,0,0,0, 1,1,0,0), // JRB            | THI
    ZOOMOUT_AREA_MASK(0,0,0,0, 1,0,0,0), // TTC            | RR
    ZOOMOUT_AREA_MASK(1,0,0,0, 1,0,0,0), // CASTLE_GROUNDS | BITDW
    ZOOMOUT_AREA_MASK(0,0,0,0, 1,0,0,0), // VCUTM          | BITFS
    ZOOMOUT_AREA_MASK(0,0,0,0, 1,0,0,0), // SA             | BITS
    ZOOMOUT_AREA_MASK(1,0,0,0, 0,0,0,0), // LLL            | DDD
    ZOOMOUT_AREA_MASK(1,0,0,0, 0,0,0,0), // WF             | ENDING
    ZOOMOUT_AREA_MASK(0,0,0,0, 0,0,0,0), // COURTYARD      | PSS
    ZOOMOUT_AREA_MASK(0,0,0,0, 1,0,0,0), // COTMC          | TOTWC
    ZOOMOUT_AREA_MASK(1,0,0,0, 1,0,0,0), // BOWSER_1       | WMOTR
    ZOOMOUT_AREA_MASK(0,0,0,0, 1,0,0,0), // Unused         | BOWSER_2
    ZOOMOUT_AREA_MASK(1,0,0,0, 0,0,0,0), // BOWSER_3       | Unused
    ZOOMOUT_AREA_MASK(1,0,0,0, 0,0,0,0), // TTM            | Unused
    ZOOMOUT_AREA_MASK(0,0,0,0, 0,0,0,0), // Unused         | Unused
};

STATIC_ASSERT(ARRAY_COUNT(sZoomOutAreaMasks) - 1 == LEVEL_MAX / 2, "Make sure you edit sZoomOutAreaMasks when adding / removing courses.");

/*
 * credits spline paths.
 * TODO: Separate these into their own file(s)
 */

struct CutsceneSplinePoint sBobCreditsSplinePositions[] = {
    { 1, 0, { 5984, 3255, 4975 } },
    { 2, 0, { 4423, 3315, 1888 } },
    { 3, 0, { 776, 2740, -1825 } },
    { 4, 0, { -146, 3894, -3167 } },
    { -1, 0, { 741, 4387, -5474 } }
};

struct CutsceneSplinePoint sBobCreditsSplineFocus[] = {
    { 0, 30, { 5817, 3306, 4507 } },
    { 0, 40, { 4025, 3378, 1593 } },
    { 0, 50, { 1088, 2652, -2205 } },
    { 0, 60, { 205, 3959, -3517 } },
    { -1, 60, { 1231, 4400, -5649 } }
};

struct CutsceneSplinePoint sWfCreditsSplinePositions[] = {
    { 0, 0, { -301, 1399, 2643 } },
    { 0, 0, { -182, 2374, 4572 } },
    { 0, 0, { 4696, 3864, 413 } },
    { 0, 0, { 1738, 4891, -1516 } },
    { -1, 0, { 1783, 4891, -1516 } }
};

struct CutsceneSplinePoint sWfCreditsSplineFocus[] = {
    { 1, 30, { -249, 1484, 2153 } },
    { 2, 40, { -200, 2470, 4082 } },
    { 3, 40, { 4200, 3916, 370 } },
    { 4, 40, { 1523, 4976, -1072 } },
    { -1, 40, { 1523, 4976, -1072 } }
};

struct CutsceneSplinePoint sJrbCreditsSplinePositions[] = {
    { 0, 0, { 5538, -4272, 2376 } },
    { 0, 0, { 5997, -3303, 2261 } },
    { 0, 0, { 6345, -3255, 2179 } },
    { 0, 0, { 6345, -3255, 2179 } },
    { -1, 0, { 6694, -3203, 2116 } }
};

struct CutsceneSplinePoint sJrbCreditsSplineFocus[] = {
    { 0, 50, { 5261, -4683, 2443 } },
    { 0, 50, { 5726, -3675, 2456 } },
    { 0, 50, { 6268, -2817, 2409 } },
    { 0, 50, { 6596, -2866, 2369 } },
    { -1, 50, { 7186, -3153, 2041 } }
};

struct CutsceneSplinePoint sCcmSlideCreditsSplinePositions[] = {
    { 0, 0, { -6324, 6745, -5626 } },
    { 1, 0, { -6324, 6745, -5626 } },
    { 2, 0, { -6108, 6762, -5770 } },
    { 3, 0, { -5771, 6787, -5962 } },
    { -1, 0, { -5672, 6790, -5979 } }
};

struct CutsceneSplinePoint sCcmSlideCreditsSplineFocus[] = {
    { 0, 50, { -5911, 6758, -5908 } },
    { 1, 50, { -5911, 6758, -5908 } },
    { 2, 50, { -5652, 6814, -5968 } },
    { 3, 50, { -5277, 6801, -6043 } },
    { -1, 50, { -5179, 6804, -6060 } }
};

struct CutsceneSplinePoint sBbhCreditsSplinePositions[] = {
    { 1, 0, { 1088, 341, 2447 } },
    { 2, 0, { 1338, 610, 2808 } },
    { 3, 0, { 2267, 1612, 2966 } },
    { -1, 0, { 2296, 1913, 2990 } }
};

struct CutsceneSplinePoint sBbhCreditsSplineFocus[] = {
    { 1, 50, { 1160, 263, 1958 } },
    { 2, 50, { 1034, 472, 2436 } },
    { 3, 50, { 1915, 1833, 2688 } },
    { -1, 50, { 2134, 2316, 2742 } }
};

struct CutsceneSplinePoint sHmcCreditsSplinePositions[] = {
    { 1, 0, { -5952, 1807, -5882 } },
    { 2, 0, { -5623, 1749, -4863 } },
    { 3, 0, { -5472, 1955, -2520 } },
    { 4, 0, { -5544, 1187, -1085 } },
    { -1, 0, { -5547, 391, -721 } }
};

struct CutsceneSplinePoint sHmcCreditsSplineFocus[] = {
    { 1, 210, { -5952, 1884, -6376 } },
    { 2, 58, { -5891, 1711, -5283 } },
    { 3, 30, { -5595, 1699, -2108 } },
    { 4, 31, { -5546, 794, -777 } },
    { -1, 31, { -5548, -85, -572 } }
};

struct CutsceneSplinePoint sThiWigglerCreditsSplinePositions[] = {
    { 1, 0, { -1411, 2474, -1276 } },
    { 2, 0, { -1606, 2479, -434 } },
    { -1, 0, { -1170, 2122, 1337 } }
};

struct CutsceneSplinePoint sThiWigglerCreditsSplineFocus[] = {
    { 1, 50, { -1053, 2512, -928 } },
    { 2, 50, { -1234, 2377, -114 } },
    { -1, 50, { -758, 2147, 1054 } }
};

struct CutsceneSplinePoint sVolcanoCreditsSplinePositions[] = {
    { 0, 0, { -1445, 1094, 1617 } },
    { 0, 0, { -1509, 649, 871 } },
    { 0, 0, { -1133, 420, -248 } },
    { 0, 0, { -778, 359, -1052 } },
    { 0, 0, { -565, 260, -1730 } },
    { -1, 0, { 1274, 473, -275 } }
};

struct CutsceneSplinePoint sVolcanoCreditsSplineFocus[] = {
    { 0, 50, { -1500, 757, 1251 } },
    { 0, 50, { -1401, 439, 431 } },
    { 0, 50, { -749, 270, -532 } },
    { 0, 50, { -396, 270, -1363 } },
    { 0, 50, { -321, 143, -2151 } },
    { -1, 50, { 1002, 460, -694 } }
};

struct CutsceneSplinePoint sSslCreditsSplinePositions[] = {
    { 0, 0, { -4262, 4658, -5015 } },
    { 0, 0, { -3274, 2963, -4661 } },
    { 0, 0, { -2568, 812, -6528 } },
    { 0, 0, { -414, 660, -7232 } },
    { 0, 0, { 1466, 660, -6898 } },
    { -1, 0, { 2724, 660, -6298 } }
};

struct CutsceneSplinePoint sSslCreditsSplineFocus[] = {
    { 0, 50, { -4083, 4277, -4745 } },
    { 0, 50, { -2975, 2574, -4759 } },
    { 0, 50, { -2343, 736, -6088 } },
    { 0, 50, { -535, 572, -6755 } },
    { 0, 50, { 1311, 597, -6427 } },
    { -1, 50, { 2448, 612, -5884 } }
};

struct CutsceneSplinePoint sDddCreditsSplinePositions[] = {
    { 0, 0, { -874, -4933, 366 } },
    { 0, 0, { -1463, -4782, 963 } },
    { 0, 0, { -1893, -4684, 1303 } },
    { 0, 0, { -2818, -4503, 1583 } },
    { 0, 0, { -4095, -2924, 730 } },
    { 0, 0, { -4737, -1594, -63 } },
    { -1, 0, { -4681, -1084, -623 } }
};

struct CutsceneSplinePoint sDddCreditsSplineFocus[] = {
    { 0, 50, { -1276, -4683, 622 } },
    { 0, 50, { -1858, -4407, 1097 } },
    { 0, 50, { -2324, -4332, 1318 } },
    { 0, 50, { -3138, -4048, 1434 } },
    { 0, 50, { -4353, -2444, 533 } },
    { 0, 50, { -4807, -1169, -436 } },
    { -1, 50, { -4665, -664, -1007 } }
};

struct CutsceneSplinePoint sSlCreditsSplinePositions[] = {
    { 0, 0, { 939, 6654, 6196 } },
    { 0, 0, { 1873, 5160, 3714 } },
    { 0, 0, { 3120, 3564, 1314 } },
    { -1, 0, { 2881, 4231, 573 } }
};

struct CutsceneSplinePoint sSlCreditsSplineFocus[] = {
    { 0, 50, { 875, 6411, 5763 } },
    { 0, 50, { 1659, 4951, 3313 } },
    { 0, 50, { 2630, 3565, 1215 } },
    { -1, 50, { 2417, 4056, 639 } }
};

struct CutsceneSplinePoint sWdwCreditsSplinePositions[] = {
    { 0, 0, { 3927, 2573, 3685 } },
    { 0, 0, { 2389, 2054, 1210 } },
    { 0, 0, { 2309, 2069, 22 } },
    { -1, 0, { 2122, 2271, -979 } }
};

struct CutsceneSplinePoint sWdwCreditsSplineFocus[] = {
    { 0, 50, { 3637, 2460, 3294 } },
    { 0, 50, { 1984, 2067, 918 } },
    { 0, 50, { 1941, 2255, -261 } },
    { -1, 50, { 1779, 2587, -1158 } }
};

struct CutsceneSplinePoint sTtmCreditsSplinePositions[] = {
    { 0, 0, { 386, 2535, 644 } },
    { 0, 0, { 1105, 2576, 918 } },
    { 0, 0, { 3565, 2261, 2098 } },
    { 0, 0, { 6715, -2791, 4554 } },
    { 0, 0, { 3917, -3130, 3656 } },
    { -1, 0, { 3917, -3130, 3656 } }
};

struct CutsceneSplinePoint sTtmCreditsSplineFocus[] = {
    { 1, 50, { 751, 2434, 318 } },
    { 2, 50, { 768, 2382, 603 } },
    { 3, 60, { 3115, 2086, 1969 } },
    { 4, 30, { 6370, -3108, 4727 } },
    { 5, 50, { 4172, -3385, 4001 } },
    { -1, 50, { 4172, -3385, 4001 } }
};

struct CutsceneSplinePoint sThiHugeCreditsSplinePositions[] = {
    { 0, 0, { 6990, -1000, -4858 } },
    { 0, 0, { 7886, -1055, 2878 } },
    { 0, 0, { 1952, -1481, 10920 } },
    { 0, 0, { -1684, -219, 2819 } },
    { 0, 0, { -2427, -131, 2755 } },
    { 0, 0, { -3246, 416, 3286 } },
    { -1, 0, { -3246, 416, 3286 } }
};

struct CutsceneSplinePoint sThiHugeCreditsSplineFocus[] = {
    { 1, 70, { 7022, -965, -5356 } },
    { 2, 40, { 7799, -915, 2405 } },
    { 3, 60, { 1878, -1137, 10568 } },
    { 4, 50, { -1931, -308, 2394 } },
    { 5, 50, { -2066, -386, 2521 } },
    { 6, 50, { -2875, 182, 3045 } },
    { -1, 50, { -2875, 182, 3045 } }
};

struct CutsceneSplinePoint sTtcCreditsSplinePositions[] = {
    { 1, 0, { -1724, 277, -994 } },
    { 2, 0, { -1720, 456, -995 } },
    { 3, 0, { -1655, 810, -1014 } },
    { -1, 0, { -1753, 883, -1009 } }
};

struct CutsceneSplinePoint sTtcCreditsSplineFocus[] = {
    { 1, 50, { -1554, 742, -1063 } },
    { 2, 50, { -1245, 571, -1102 } },
    { 3, 50, { -1220, 603, -1151 } },
    { -1, 50, { -1412, 520, -1053 } }
};

struct CutsceneSplinePoint sRrCreditsSplinePositions[] = {
    { 0, 0, { -1818, 4036, 97 } },
    { 0, 0, { -575, 3460, -505 } },
    { 0, 0, { 1191, 3611, -1134 } },
    { -1, 0, { 2701, 3777, -3686 } }
};

struct CutsceneSplinePoint sRrCreditsSplineFocus[] = {
    { 0, 50, { -1376, 3885, -81 } },
    { 0, 50, { -146, 3343, -734 } },
    { 0, 50, { 1570, 3446, -1415 } },
    { -1, 50, { 2794, 3627, -3218 } }
};

struct CutsceneSplinePoint sSaCreditsSplinePositions[] = {
    { 0, 0, { -295, -396, -585 } },
    { 1, 0, { -295, -396, -585 } },
    { 2, 0, { -292, -856, -573 } },
    { 3, 0, { -312, -856, -541 } },
    { -1, 0, { 175, -856, -654 } }
};

struct CutsceneSplinePoint sSaCreditsSplineFocus[] = {
    { 0, 50, { -175, -594, -142 } },
    { 1, 50, { -175, -594, -142 } },
    { 2, 50, { -195, -956, -92 } },
    { 3, 50, { -572, -956, -150 } },
    { -1, 50, { -307, -956, -537 } }
};

struct CutsceneSplinePoint sCotmcCreditsSplinePositions[] = {
    { 0, 0, { -296, 495, 1607 } },
    { 0, 0, { -430, 541, 654 } },
    { 0, 0, { -466, 601, -359 } },
    { 0, 0, { -217, 433, -1549 } },
    { -1, 0, { -95, 366, -2922 } }
};

struct CutsceneSplinePoint sCotmcCreditsSplineFocus[] = {
    { 0, 50, { -176, 483, 2092 } },
    { 0, 50, { -122, 392, 1019 } },
    { 0, 50, { -268, 450, -792 } },
    { 0, 50, { -172, 399, -2046 } },
    { -1, 50, { -51, 355, -3420 } }
};

struct CutsceneSplinePoint sDddSubCreditsSplinePositions[] = {
    { 0, 0, { 4656, 2171, 5028 } },
    { 0, 0, { 4548, 1182, 4596 } },
    { 0, 0, { 5007, 813, 3257 } },
    { 0, 0, { 5681, 648, 1060 } },
    { -1, 0, { 4644, 774, 113 } }
};

struct CutsceneSplinePoint sDddSubCreditsSplineFocus[] = {
    { 0, 50, { 4512, 2183, 4549 } },
    { 0, 50, { 4327, 838, 4308 } },
    { 0, 50, { 4774, 749, 2819 } },
    { 0, 50, { 5279, 660, 763 } },
    { -1, 50, { 4194, 885, -75 } }
};

struct CutsceneSplinePoint sCcmOutsideCreditsSplinePositions[] = {
    { 1, 0, { 1427, -1387, 5409 } },
    { 2, 0, { -1646, -1536, 4526 } },
    { 3, 0, { -3852, -1448, 3913 } },
    { -1, 0, { -5199, -1366, 1886 } }
};

struct CutsceneSplinePoint sCcmOutsideCreditsSplineFocus[] = {
    { 1, 50, { 958, -1481, 5262 } },
    { 2, 50, { -2123, -1600, 4391 } },
    { 3, 50, { -3957, -1401, 3426 } },
    { -1, 50, { -4730, -1215, 1795 } }
};

/**
 * Play the current cutscene until either gCutsceneTimer reaches the max time, or c->cutscene is set to 0
 *
 * Note that CAM_FLAG_SMOOTH_MOVEMENT is cleared while a cutscene is playing, so cutscenes set it for
 * the duration they want the flag to be active.
 */
void play_cutscene(struct Camera *c) {
    UNUSED u32 pad[3];
    UNUSED s16 unusedYawFocToMario;
    s16 cutsceneDuration;
    u8 oldCutscene;

    unusedYawFocToMario = sAreaYaw;
    oldCutscene = c->cutscene;
    sStatusFlags &= ~CAM_FLAG_SMOOTH_MOVEMENT;
    gCameraMovementFlags &= ~CAM_MOVING_INTO_MODE;

#define CUTSCENE(id, cutscene)                                                                            \
    case id:                                                                                              \
        cutsceneDuration = cutscene[sCutsceneShot].duration;                                              \
        cutscene[sCutsceneShot].shot(c);                                                                  \
        break;

    switch (c->cutscene) {
        CUTSCENE(CUTSCENE_STAR_SPAWN, sCutsceneStarSpawn)
        CUTSCENE(CUTSCENE_RED_COIN_STAR_SPAWN, sCutsceneRedCoinStarSpawn)
        CUTSCENE(CUTSCENE_ENDING, sCutsceneEnding)
        CUTSCENE(CUTSCENE_GRAND_STAR, sCutsceneGrandStar)
        CUTSCENE(CUTSCENE_DOOR_WARP, sCutsceneDoorWarp)
        CUTSCENE(CUTSCENE_DOOR_PULL, sCutsceneDoorPull)
        CUTSCENE(CUTSCENE_DOOR_PUSH, sCutsceneDoorPush)
        CUTSCENE(CUTSCENE_DOOR_PULL_MODE, sCutsceneDoorPullMode)
        CUTSCENE(CUTSCENE_DOOR_PUSH_MODE, sCutsceneDoorPushMode)
        CUTSCENE(CUTSCENE_ENTER_CANNON, sCutsceneEnterCannon)
        CUTSCENE(CUTSCENE_ENTER_PAINTING, sCutsceneEnterPainting)
        CUTSCENE(CUTSCENE_DEATH_EXIT, sCutsceneDeathExit)
        CUTSCENE(CUTSCENE_EXIT_PAINTING_SUCC, sCutsceneExitPaintingSuccess)
        CUTSCENE(CUTSCENE_UNUSED_EXIT, sCutsceneUnusedExit)
        CUTSCENE(CUTSCENE_INTRO_PEACH, sCutsceneIntroPeach)
        CUTSCENE(CUTSCENE_ENTER_BOWSER_ARENA, sCutsceneEnterBowserArena)
        CUTSCENE(CUTSCENE_DANCE_ROTATE, sCutsceneDanceDefaultRotate)
        CUTSCENE(CUTSCENE_DANCE_DEFAULT, sCutsceneDanceDefaultRotate)
        CUTSCENE(CUTSCENE_DANCE_FLY_AWAY, sCutsceneDanceFlyAway)
        CUTSCENE(CUTSCENE_DANCE_CLOSEUP, sCutsceneDanceCloseup)
        CUTSCENE(CUTSCENE_KEY_DANCE, sCutsceneKeyDance)
        CUTSCENE(CUTSCENE_0F_UNUSED, sCutsceneUnused)
        CUTSCENE(CUTSCENE_END_WAVING, sCutsceneEndWaving)
        CUTSCENE(CUTSCENE_CREDITS, sCutsceneCredits)
        CUTSCENE(CUTSCENE_CAP_SWITCH_PRESS, sCutsceneCapSwitchPress)
        CUTSCENE(CUTSCENE_SLIDING_DOORS_OPEN, sCutsceneSlidingDoorsOpen)
        CUTSCENE(CUTSCENE_PREPARE_CANNON, sCutscenePrepareCannon)
        CUTSCENE(CUTSCENE_UNLOCK_KEY_DOOR, sCutsceneUnlockKeyDoor)
        CUTSCENE(CUTSCENE_STANDING_DEATH, sCutsceneStandingDeath)
        CUTSCENE(CUTSCENE_ENTER_POOL, sCutsceneEnterPool)
        CUTSCENE(CUTSCENE_DEATH_ON_STOMACH, sCutsceneDeathStomach)
        CUTSCENE(CUTSCENE_DEATH_ON_BACK, sCutsceneDeathOnBack)
        CUTSCENE(CUTSCENE_QUICKSAND_DEATH, sCutsceneQuicksandDeath)
        CUTSCENE(CUTSCENE_SUFFOCATION_DEATH, sCutsceneSuffocation)
        CUTSCENE(CUTSCENE_EXIT_BOWSER_SUCC, sCutsceneExitBowserSuccess)
        CUTSCENE(CUTSCENE_EXIT_BOWSER_DEATH, sCutsceneExitBowserDeath)
        CUTSCENE(CUTSCENE_EXIT_SPECIAL_SUCC, sCutsceneExitSpecialSuccess)
        CUTSCENE(CUTSCENE_EXIT_WATERFALL, sCutsceneExitWaterfall)
        CUTSCENE(CUTSCENE_EXIT_FALL_WMOTR, sCutsceneFallToCastleGrounds)
        CUTSCENE(CUTSCENE_NONPAINTING_DEATH, sCutsceneNonPaintingDeath)
        CUTSCENE(CUTSCENE_DIALOG, sCutsceneDialog)
        CUTSCENE(CUTSCENE_READ_MESSAGE, sCutsceneReadMessage)
        CUTSCENE(CUTSCENE_RACE_DIALOG, sCutsceneDialog)
        CUTSCENE(CUTSCENE_ENTER_PYRAMID_TOP, sCutsceneEnterPyramidTop)
        CUTSCENE(CUTSCENE_SSL_PYRAMID_EXPLODE, sCutscenePyramidTopExplode)
    }

#undef CUTSCENE

    if ((cutsceneDuration != 0) && !(gCutsceneTimer & CUTSCENE_STOP)) {
        //! @bug This should check for 0x7FFF (CUTSCENE_LOOP)
        //! instead, cutscenes that last longer than 0x3FFF frames will never end on their own
        if (gCutsceneTimer < 0x3FFF) {
            gCutsceneTimer += 1;
        }
        //! Because gCutsceneTimer is often set to 0x7FFF (CUTSCENE_LOOP), this conditional can only
        //! check for == due to overflow
        if (gCutsceneTimer == cutsceneDuration) {
            sCutsceneShot += 1;
            gCutsceneTimer = 0;
        }
    } else {
        sMarioCamState->cameraEvent = 0;
        sCutsceneShot = 0;
        gCutsceneTimer = 0;
    }

    sAreaYawChange = 0;

    // The cutscene just ended
    if ((c->cutscene == 0) && (oldCutscene != 0)) {
        gRecentCutscene = oldCutscene;
    }
}

/**
 * Call the event while `start` <= gCutsceneTimer <= `end`
 * If `end` is -1, call for the rest of the shot.
 */
s32 cutscene_event(CameraEvent event, struct Camera *c, s16 start, s16 end) {
    if (start <= gCutsceneTimer) {
        if (end == -1 || end >= gCutsceneTimer) {
            event(c);
        }
    }
    return 0;
}

/**
 * Set gCutsceneObjSpawn when gCutsceneTimer == `frame`.
 *
 * @see intro_scene.inc.c for details on which objects are spawned.
 */
s32 cutscene_spawn_obj(u32 obj, s16 frame) {
    if (frame == gCutsceneTimer) {
        gCutsceneObjSpawn = obj;
    }
    return 0;
}

/**
 * Start shaking the camera's field of view.
 *
 * @param shakeSpeed How fast the shake should progress through its period. The shake offset is
 *                   calculated from coss(), so this parameter can be thought of as an angular velocity.
 */
void set_fov_shake(s16 amplitude, s16 decay, s16 shakeSpeed) {
    if (amplitude > sFOVState.shakeAmplitudeq) {
        sFOVState.shakeAmplitudeq = amplitude;
        sFOVState.decay = decay;
        sFOVState.shakeSpeed = shakeSpeed;
    }
}

/**
 * Start shaking the camera's field of view, but reduce `amplitude` by distance from camera
 */
void set_fov_shake_from_pointq(s16 amplitude, s16 decay, s16 shakeSpeed, q32 maxDistq, q32 posXq, q32 posYq, q32 posZq) {
    amplitude = reduce_by_dist_from_cameraq(amplitude, maxDistq, posXq, posYq, posZq);

    if (amplitude != 0) {
        if (amplitude > sFOVState.shakeAmplitudeq) { // literally use the function above you silly nintendo, smh
            sFOVState.shakeAmplitudeq = q(amplitude);
            sFOVState.decay = decay;
            sFOVState.shakeSpeed = shakeSpeed;
        }
    }
}

/**
 * Add a cyclic offset to the camera's field of view based on a cosine wave
 */
void shake_camera_fov(struct GraphNodePerspective *perspective) {
    if (sFOVState.shakeAmplitudeq != 0) {
        sFOVState.fovOffsetq = qmul(cosqs(sFOVState.shakePhase), sFOVState.shakeAmplitudeq) / 0x100;
        sFOVState.shakePhase += sFOVState.shakeSpeed;
        camera_approach_q32_symmetric_bool(&sFOVState.shakeAmplitudeq, 0, sFOVState.decay);
        perspective->fovq += sFOVState.fovOffsetq;
    } else {
        sFOVState.shakePhase = 0;
    }
}

static UNUSED void unused_deactivate_sleeping_camera(UNUSED struct MarioState *m) {
    sStatusFlags &= ~CAM_FLAG_SLEEPING;
}

void approach_fov_20(UNUSED struct MarioState *m) {
    camera_approach_q32_symmetric_bool(&sFOVState.fovq, q(20), q(0.3));
}

void zoom_fov_30(UNUSED struct MarioState *m) {
    // Pretty sure approach_f32_asymptotic_bool would do a much nicer job here, but you do you,
    // Nintendo.
    camera_approach_q32_symmetric_bool(&sFOVState.fovq, q(30), (q(30) - sFOVState.fovq) / 60);
}

/**
 * This is the default fov function. It makes fov approach 45 degrees, and it handles zooming in when
 * Mario falls a sleep.
 */
void fov_default(struct MarioState *m) {
    sStatusFlags &= ~CAM_FLAG_SLEEPING;

    if ((m->action == ACT_SLEEPING) || (m->action == ACT_START_SLEEPING)) {
        camera_approach_q32_symmetric_bool(&sFOVState.fovq, q(30), (q(30) - sFOVState.fovq) / 30);
        sStatusFlags |= CAM_FLAG_SLEEPING;
    } else {
        camera_approach_q32_symmetric_bool(&sFOVState.fovq, q(45), (q(45) - sFOVState.fovq) / 30);
        sFOVState.unusedIsSleeping = 0;
    }
    if (m->area->camera->cutscene == CUTSCENE_0F_UNUSED) {
        sFOVState.fovq = q(45);
    }
}

void approach_fov_30(UNUSED struct MarioState *m) {
    camera_approach_q32_symmetric_bool(&sFOVState.fovq, q(30), QONE);
}

void approach_fov_60(UNUSED struct MarioState *m) {
    camera_approach_q32_symmetric_bool(&sFOVState.fovq, q(60), QONE);
}

void approach_fov_45(UNUSED struct MarioState *m) {
    //q32 targetFoVq = sFOVState.fovq;
    //if (m->area->camera->mode == CAMERA_MODE_FIXED && m->area->camera->cutscene == 0) {
    //    targetFoVq = q(45);
    //} else {
    //    targetFoVq = q(45);
    //}
	// ^ What did they originally mean by that?
    sFOVState.fovq = approach_q32(sFOVState.fovq, q(45), q(2), q(2));
}

void approach_fov_80(UNUSED struct MarioState *m) {
    camera_approach_q32_symmetric_bool(&sFOVState.fovq, q(80), q(3.5));
}

/**
 * Sets the fov in BBH.
 * If there's a cutscene, sets fov to 45. Otherwise sets fov to 60.
 */
void set_fov_bbh(struct MarioState *m) {
    q32 targetFoVq = sFOVState.fovq;

    if (m->area->camera->mode == CAMERA_MODE_FIXED && m->area->camera->cutscene == 0) {
        targetFoVq = q(60);
    } else {
        targetFoVq = q(45);
    }

    sFOVState.fovq = approach_q32(sFOVState.fovq, targetFoVq, q(2), q(2));
}

/**
 * Sets the field of view for the GraphNodeCamera
 */
Gfx *geo_camera_fov(s32 callContext, struct GraphNode *g, UNUSED void *context) {
    struct GraphNodePerspective *perspective = (struct GraphNodePerspective *) g;
    struct MarioState *marioState = &gMarioStates[0];
    u8 fovFunc = sFOVState.fovFunc;

    if (callContext == GEO_CONTEXT_RENDER) {
        switch (fovFunc) {
            case CAM_FOV_SET_45:
                sFOVState.fovq = q(45);
                break;
            case CAM_FOV_SET_29:
                sFOVState.fovq = q(29);
                break;
            case CAM_FOV_ZOOM_30:
                zoom_fov_30(marioState);
                break;
            case CAM_FOV_DEFAULT:
                fov_default(marioState);
                break;
            case CAM_FOV_BBH:
                set_fov_bbh(marioState);
                break;
            case CAM_FOV_APP_45:
                approach_fov_45(marioState);
                break;
            case CAM_FOV_SET_30:
                sFOVState.fovq = q(30);
                break;
            case CAM_FOV_APP_20:
                approach_fov_20(marioState);
                break;
            case CAM_FOV_APP_80:
                approach_fov_80(marioState);
                break;
            case CAM_FOV_APP_30:
                approach_fov_30(marioState);
                break;
            case CAM_FOV_APP_60:
                approach_fov_60(marioState);
                break;
            //! No default case
        }
    }

    perspective->fovq = sFOVState.fovq;
    shake_camera_fov(perspective);
    return NULL;
}

/**
 * Change the camera's FOV mode.
 *
 * @see geo_camera_fov
 */
void set_fov_function(u8 func) {
    sFOVState.fovFunc = func;
}

/**
 * Start a preset fov shake. Used in cutscenes
 */
void cutscene_set_fov_shake_preset(u8 preset) {
    switch (preset) {
        case 1:
            set_fov_shake(0x100, 0x30, 0x8000);
            break;
        case 2:
            set_fov_shake(0x400, 0x20, 0x4000);
            break;
    }
}

/**
 * Start a preset fov shake that is reduced by the point's distance from the camera.
 * Used in set_camera_shake_from_point
 *
 * @see set_camera_shake_from_point
 */
void set_fov_shake_from_point_presetq(u8 preset, q32 posXq, q32 posYq, q32 posZq) {
    switch (preset) {
        case SHAKE_FOV_SMALL:
            set_fov_shake_from_pointq(0x100, 0x30, 0x8000, q(3000), posXq, posYq, posZq);
            break;
        case SHAKE_FOV_MEDIUM:
            set_fov_shake_from_pointq(0x200, 0x30, 0x8000, q(4000), posXq, posYq, posZq);
            break;
        case SHAKE_FOV_LARGE:
            set_fov_shake_from_pointq(0x300, 0x30, 0x8000, q(6000), posXq, posYq, posZq);
            break;
        case SHAKE_FOV_UNUSED:
            set_fov_shake_from_pointq(0x800, 0x20, 0x4000, q(3000), posXq, posYq, posZq);
            break;
    }
}

/**
 * Offset an object's position in a random direction within the given bounds.
 */
//static UNUSED void unused_displace_obj_randomlyq(struct Object *o, q32 xRangeq, q32 yRangeq, q32 zRangeq) {
//    q32 rndq = random_q32();
//    FMODFIELD(o, oPosX,  += qtof(qmul(rndq, xRangeq) - xRangeq / 2);
//    FMODFIELD(o, oPosY,  += qtof(qmul(rndq, yRangeq) - yRangeq / 2);
//    FMODFIELD(o, oPosZ,  += qtof(qmul(rndq, zRangeq) - zRangeq / 2);
//}

/**
 * Rotate an object in a random direction within the given bounds.
 */
//static UNUSED void unused_rotate_obj_randomly(struct Object *o, f32 pitchRange, f32 yawRange) {
//    q32 rndq = random_q32();
//    o->oMoveAnglePitch += (s16)(qmul(rndq, pitchRange) - pitchRange / 2);
//    o->oMoveAngleYaw += (s16)(qmul(rndq, yawRange) - yawRange / 2);
//}

/**
 * Rotate the object towards the point `point`.
 */
void obj_rotate_towards_pointq(struct Object *o, Vec3q pointq, s16 pitchOff, s16 yawOff, s16 pitchDiv, s16 yawDiv) {
    q32 distq;
    s16 pitch, yaw;
    Vec3q oPosq;

    object_pos_to_vec3q(oPosq, o);
    vec3q_get_dist_and_angle(oPosq, pointq, &distq, &pitch, &yaw);
    o->oMoveAnglePitch = approach_s16_asymptotic(o->oMoveAnglePitch, pitchOff - pitch, pitchDiv);
    o->oMoveAngleYaw = approach_s16_asymptotic(o->oMoveAngleYaw, yaw + yawOff, yawDiv);
}

#include "behaviors/intro_peach.inc.c"
#include "behaviors/intro_lakitu.inc.c"
#include "behaviors/end_birds_1.inc.c"
#include "behaviors/end_birds_2.inc.c"
#include "behaviors/intro_scene.inc.c"
