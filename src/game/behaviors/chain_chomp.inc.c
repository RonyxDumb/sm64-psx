
/**
 * Behavior for bhvChainChomp, bhvChainChompChainPart, bhvWoodenPost, and bhvChainChompGate.
 * bhvChainChomp spawns its bhvWoodenPost in its behavior script. It spawns 5 chain
 * parts. Part 0 is the "pivot", which is positioned at the wooden post while
 * the chomp is chained up. Parts 1-4 are the other parts, starting from the
 * chain chomp and moving toward the pivot.
 * Processing order is bhvWoodenPost, bhvChainChompGate, bhvChainChomp, bhvChainChompChainPart.
 * The chain parts are processed starting at the post and ending at the chomp.
 */

/**
 * Hitbox for chain chomp.
 */
static struct ObjectHitbox sChainChompHitbox = {
    /* interactType: */ INTERACT_MR_BLIZZARD,
    /* downOffset: */ 0,
    /* damageOrCoinValue: */ 3,
    /* health: */ 1,
    /* numLootCoins: */ 0,
    /* radius: */ 80,
    /* height: */ 160,
    /* hurtboxRadius: */ 80,
    /* hurtboxHeight: */ 160,
};

/**
 * Update function for chain chomp part / pivot.
 */
void bhv_chain_chomp_chain_part_update(void) {
    if (o->parentObj->oAction == CHAIN_CHOMP_ACT_UNLOAD_CHAIN) {
        obj_mark_for_deletion(o);
    } else if (o->oBehParams2ndByte != CHAIN_CHOMP_CHAIN_PART_BP_PIVOT) {
        struct ChainSegment *segment = &o->parentObj->oChainChompSegments[o->oBehParams2ndByte];

        // Set position relative to the pivot
        FSETFIELD(o, oPosX, FFIELD(o->parentObj->parentObj, oPosX) + segment->posX);
        FSETFIELD(o, oPosY, FFIELD(o->parentObj->parentObj, oPosY) + segment->posY);
        FSETFIELD(o, oPosZ, FFIELD(o->parentObj->parentObj, oPosZ) + segment->posZ);
    } else if (o->parentObj->oChainChompReleaseStatus != CHAIN_CHOMP_NOT_RELEASED) {
        cur_obj_update_floor_and_walls();
        cur_obj_move_standard(78);
    }
}

/**
 * When mario gets close enough, allocate chain segments and spawn their objects.
 */
static void chain_chomp_act_uninitialized(void) {
    struct ChainSegment *segments;
    s32 i;

    if (QFIELD(o, oDistanceToMario) < q(3000.0)) {
        segments = mem_pool_alloc(gObjectMemoryPool, 5 * sizeof(struct ChainSegment));
        if (segments != NULL) {
            // Each segment represents the offset of a chain part to the pivot.
            // Segment 0 connects the pivot to the chain chomp itself. Segment
            // 1 connects the pivot to the chain part next to the chain chomp
            // (chain part 1), etc.
            o->oChainChompSegments = segments;
            for (i = 0; i <= 4; i++) {
                chain_segment_init(&segments[i]);
            }

            cur_obj_set_pos_to_home();

            // Spawn the pivot and set to parent
            if ((o->parentObj =
                     spawn_object(o, CHAIN_CHOMP_CHAIN_PART_BP_PIVOT, bhvChainChompChainPart))
                != NULL) {
                // Spawn the non-pivot chain parts, starting from the chain
                // chomp and moving toward the pivot
                for (i = 1; i <= 4; i++) {
                    spawn_object_relative(i, 0, 0, 0, o, MODEL_METALLIC_BALL, bhvChainChompChainPart);
                }

                o->oAction = CHAIN_CHOMP_ACT_MOVE;
                cur_obj_unhide();
            }
        }
    }
}

/**
 * Apply gravity to each chain part, and cap its distance to the previous
 * part as well as from the pivot.
 */
static void chain_chomp_update_chain_segments(void) {
    struct ChainSegment *prevSegment;
    struct ChainSegment *segment;
    f32 offsetX;
    f32 offsetY;
    f32 offsetZ;
    f32 offset;
    f32 segmentVelY;
    f32 maxTotalOffset;
    s32 i;

    if (QFIELD(o, oVelY) < 0) {
        segmentVelY = FFIELD(o, oVelY);
    } else {
        segmentVelY = -20.0f;
    }

    // Segment 0 connects the pivot to the chain chomp itself, and segment i>0
    // connects the pivot to chain part i (1 is closest to the chain chomp).

    for (i = 1; i <= 4; i++) {
        prevSegment = &o->oChainChompSegments[i - 1];
        segment = &o->oChainChompSegments[i];

        // Apply gravity

        if ((segment->posY += segmentVelY) < 0.0f) {
            segment->posY = 0.0f;
        }

        // Cap distance to previous chain part (so that the tail follows the
        // chomp)

        offsetX = segment->posX - prevSegment->posX;
        offsetY = segment->posY - prevSegment->posY;
        offsetZ = segment->posZ - prevSegment->posZ;
        offset = sqrtf(offsetX * offsetX + offsetY * offsetY + offsetZ * offsetZ);

        if (offset > FFIELD(o, oChainChompMaxDistBetweenChainParts)) {
            offset = FFIELD(o, oChainChompMaxDistBetweenChainParts) / offset;
            offsetX *= offset;
            offsetY *= offset;
            offsetZ *= offset;
        }

        // Cap distance to pivot (so that it stretches when the chomp moves far
        // from the wooden post)

        offsetX += prevSegment->posX;
        offsetY += prevSegment->posY;
        offsetZ += prevSegment->posZ;
        offset = sqrtf(offsetX * offsetX + offsetY * offsetY + offsetZ * offsetZ);

        maxTotalOffset = FFIELD(o, oChainChompMaxDistFromPivotPerChainPart) * (5 - i);
        if (offset > maxTotalOffset) {
            offset = maxTotalOffset / offset;
            offsetX *= offset;
            offsetY *= offset;
            offsetZ *= offset;
        }

        segment->posX = offsetX;
        segment->posY = offsetY;
        segment->posZ = offsetZ;
    }
}

/**
 * Lunging increases the maximum distance from the pivot and changes the maximum
 * distance between chain parts. Restore these values to normal.
 */
static void chain_chomp_restore_normal_chain_lengths(void) {
    APPROACH_F32_FIELD(o, oChainChompMaxDistFromPivotPerChainPart, 750.0f / 5, 4.0f);
    QSETFIELD(o,  oChainChompMaxDistBetweenChainParts, QFIELD(o,  oChainChompMaxDistFromPivotPerChainPart));
}

/**
 * Turn toward mario. Wait a bit and then enter the lunging sub-action.
 */
static void chain_chomp_sub_act_turn(void) {
    QSETFIELD(o, oGravity, q(-4.0f));

    chain_chomp_restore_normal_chain_lengths();
    obj_move_pitch_approach(0, 0x100);

    if (o->oMoveFlags & OBJ_MOVE_MASK_ON_GROUND) {
        cur_obj_rotate_yaw_toward(o->oAngleToMario, 0x400);
        if (abs_angle_diff(o->oAngleToMario, o->oMoveAngleYaw) < 0x800) {
            if (o->oTimer > 30) {
                if (cur_obj_check_anim_frame(0)) {
                    cur_obj_reverse_animation();
                    if (o->oTimer > 40) {
                        // Increase the maximum distance from the pivot and enter
                        // the lunging sub-action.
                        cur_obj_play_sound_2(SOUND_GENERAL_CHAIN_CHOMP2);

                        o->oSubAction = CHAIN_CHOMP_SUB_ACT_LUNGE;
                        FSETFIELD(o, oChainChompMaxDistFromPivotPerChainPart, 900.0f / 5);

                        QSETFIELD(o,  oForwardVel, q(140));
                        QSETFIELD(o,  oVelY, q(20));
                        QSETFIELD(o, oGravity, q(0.0f));
                        o->oChainChompTargetPitch = obj_get_pitch_from_vel();
                    }
                } else {
                    o->oTimer -= 1;
                }
            } else {
                QSETFIELD(o,  oForwardVel, q(0));
            }
        } else {
            cur_obj_play_sound_2(SOUND_GENERAL_CHAIN_CHOMP1);
            QSETFIELD(o,  oForwardVel, q(10));
            QSETFIELD(o,  oVelY, q(20));
        }
    } else {
        cur_obj_rotate_yaw_toward(o->oAngleToMario, 0x190);
        o->oTimer = 0;
    }
}

static void chain_chomp_sub_act_lunge(void) {
    obj_face_pitch_approach(o->oChainChompTargetPitch, 0x400);

    if (QFIELD(o, oForwardVel) != 0) {
        f32 val04;

        if (o->oChainChompRestrictedByChain == TRUE) {
            FSETFIELD(o, oForwardVel, QSETFIELD(o,  oVelY, q(0)));
            QSETFIELD(o,  oChainChompUnk104, q(30));
        }

        // TODO: What is this
        if ((val04 = 900.0f - FFIELD(o, oChainChompDistToPivot)) > 220.0f) {
            val04 = 220.0f;
        }

        FSETFIELD(o, oChainChompMaxDistBetweenChainParts,
            val04 / 220.0f * FFIELD(o, oChainChompMaxDistFromPivotPerChainPart));
        o->oTimer = 0;
    } else {
        // Turn toward pivot
        cur_obj_rotate_yaw_toward(atan2s(o->oChainChompSegments[0].posZ, o->oChainChompSegments[0].posX),
                              0x1000);

        if (QFIELD(o, oChainChompUnk104) != 0) {
            APPROACH_F32_FIELD(o, oChainChompUnk104, 0.0f, 0.8f);
        } else {
            o->oSubAction = CHAIN_CHOMP_SUB_ACT_TURN;
        }

        QSETFIELD(o,  oChainChompMaxDistBetweenChainParts, QFIELD(o,  oChainChompUnk104));
        if (gGlobalTimer % 2 != 0) {
            QSETFIELD(o,  oChainChompMaxDistBetweenChainParts, QFIELD(-o,  oChainChompUnk104));
        }
    }

    if (o->oTimer < 30) {
        cur_obj_reverse_animation();
    }
}

/**
 * Fall to the ground and interrupt mario into a cutscene action.
 */
static void chain_chomp_released_trigger_cutscene(void) {
    QSETFIELD(o,  oForwardVel, q(0));
    QSETFIELD(o, oGravity, q(-4.0f));

    //! Can delay this if we get into a cutscene-unfriendly action after the
    //  last post ground pound and before this
    if (set_mario_npc_dialog(MARIO_DIALOG_LOOK_UP) == MARIO_DIALOG_STATUS_SPEAK
        && (o->oMoveFlags & OBJ_MOVE_MASK_ON_GROUND) && cutscene_object(CUTSCENE_STAR_SPAWN, o) == 1) {
        o->oChainChompReleaseStatus = CHAIN_CHOMP_RELEASED_LUNGE_AROUND;
        o->oTimer = 0;
    }
}

/**
 * Lunge 4 times, each time moving toward mario +/- 0x2000 angular units.
 * Finally, begin a lunge toward x=1450, z=562 (near the gate).
 */
static void chain_chomp_released_lunge_around(void) {
    chain_chomp_restore_normal_chain_lengths();

    // Finish bounce
    if (o->oMoveFlags & OBJ_MOVE_MASK_ON_GROUND) {
        // Before first bounce, turn toward mario and wait 2 seconds
        if (o->oChainChompNumLunges == 0) {
            if (cur_obj_rotate_yaw_toward(o->oAngleToMario, 0x320)) {
                if (o->oTimer > 60) {
                    o->oChainChompNumLunges += 1;
                    // enable wall collision
                    QSETFIELD(o, oWallHitboxRadius, q(200));
                }
            } else {
                o->oTimer = 0;
            }
        } else {
            if (++o->oChainChompNumLunges <= 5) {
                cur_obj_play_sound_2(SOUND_GENERAL_CHAIN_CHOMP1);
                o->oMoveAngleYaw = o->oAngleToMario + random_sign() * 0x2000;
                QSETFIELD(o, oForwardVel, q(30));
                QSETFIELD(o, oVelY, q(50));
            } else {
                o->oChainChompReleaseStatus = CHAIN_CHOMP_RELEASED_BREAK_GATE;
                QSETFIELD(o, oHomeX, q(1450));
                QSETFIELD(o, oHomeZ, q(562));
                o->oMoveAngleYaw = cur_obj_angle_to_home();
                QSETFIELD(o, oForwardVel, q(cur_obj_lateral_dist_to_home()) / 8);
                QSETFIELD(o, oVelY, q(50));
            }
        }
    }
}

/**
 * Continue lunging until a wall collision occurs. Mark the gate as destroyed,
 * wait for the chain chomp to land, and then begin a jump toward the final
 * target, x=3288, z=-1770.
 */
static void chain_chomp_released_break_gate(void) {
    if (!o->oChainChompHitGate) {
        // If hit wall, assume it's the gate and bounce off of it
        //! The wall may not be the gate
        //! If the chain chomp gets stuck, it may never hit a wall, resulting
        //  in a softlock
        if (o->oMoveFlags & OBJ_MOVE_HIT_WALL) {
            o->oChainChompHitGate = TRUE;
            o->oMoveAngleYaw = cur_obj_reflect_move_angle_off_wall();
            QSETFIELD(o, oForwardVel, QFIELD(o, oForwardVel) * 2 / 5);
        }
    } else if (o->oMoveFlags & OBJ_MOVE_MASK_ON_GROUND) {
        o->oChainChompReleaseStatus = CHAIN_CHOMP_RELEASED_JUMP_AWAY;
        QSETFIELD(o,  oHomeX, q(3288));
        QSETFIELD(o,  oHomeZ, q(-1770));
        o->oMoveAngleYaw = cur_obj_angle_to_home();
        FSETFIELD(o, oForwardVel, cur_obj_lateral_dist_to_home() / 50.0f);
        QSETFIELD(o,  oVelY, q(120));
    }
}

/**
 * Wait until the chain chomp lands.
 */
static void chain_chomp_released_jump_away(void) {
    if (o->oMoveFlags & OBJ_MOVE_MASK_ON_GROUND) {
        gObjCutsceneDone = TRUE;
        o->oChainChompReleaseStatus = CHAIN_CHOMP_RELEASED_END_CUTSCENE;
    }
}

/**
 * Release mario and transition to the unload chain action.
 */
static void chain_chomp_released_end_cutscene(void) {
    if (cutscene_object(CUTSCENE_STAR_SPAWN, o) == -1) {
        set_mario_npc_dialog(MARIO_DIALOG_STOP);
        o->oAction = CHAIN_CHOMP_ACT_UNLOAD_CHAIN;
    }
}

/**
 * All chain chomp movement behavior, including the cutscene after being
 * released.
 */
static void chain_chomp_act_move(void) {
    f32 maxDistToPivot;

    // Unload chain if mario is far enough
    if (o->oChainChompReleaseStatus == CHAIN_CHOMP_NOT_RELEASED && QFIELD(o, oDistanceToMario) > q(4000.0)) {
        o->oAction = CHAIN_CHOMP_ACT_UNLOAD_CHAIN;
        FSETFIELD(o, oForwardVel, QSETFIELD(o,  oVelY, q(0)));
    } else {
        cur_obj_update_floor_and_walls();

        switch (o->oChainChompReleaseStatus) {
            case CHAIN_CHOMP_NOT_RELEASED:
                switch (o->oSubAction) {
                    case CHAIN_CHOMP_SUB_ACT_TURN:
                        chain_chomp_sub_act_turn();
                        break;
                    case CHAIN_CHOMP_SUB_ACT_LUNGE:
                        chain_chomp_sub_act_lunge();
                        break;
                }
                break;
            case CHAIN_CHOMP_RELEASED_TRIGGER_CUTSCENE:
                chain_chomp_released_trigger_cutscene();
                break;
            case CHAIN_CHOMP_RELEASED_LUNGE_AROUND:
                chain_chomp_released_lunge_around();
                break;
            case CHAIN_CHOMP_RELEASED_BREAK_GATE:
                chain_chomp_released_break_gate();
                break;
            case CHAIN_CHOMP_RELEASED_JUMP_AWAY:
                chain_chomp_released_jump_away();
                break;
            case CHAIN_CHOMP_RELEASED_END_CUTSCENE:
                chain_chomp_released_end_cutscene();
                break;
        }

        cur_obj_move_standard(78);

        // Segment 0 connects the pivot to the chain chomp itself
        o->oChainChompSegments[0].posX = FFIELD(o, oPosX) - FFIELD(o->parentObj, oPosX);
        o->oChainChompSegments[0].posY = FFIELD(o, oPosY) - FFIELD(o->parentObj, oPosY);
        o->oChainChompSegments[0].posZ = FFIELD(o, oPosZ) - FFIELD(o->parentObj, oPosZ);

        FSETFIELD(o, oChainChompDistToPivot,
            sqrtf(o->oChainChompSegments[0].posX * o->oChainChompSegments[0].posX
                  + o->oChainChompSegments[0].posY * o->oChainChompSegments[0].posY
                  + o->oChainChompSegments[0].posZ * o->oChainChompSegments[0].posZ));

        // If the chain is fully stretched
        maxDistToPivot = FFIELD(o, oChainChompMaxDistFromPivotPerChainPart) * 5;
        if (FFIELD(o, oChainChompDistToPivot) > maxDistToPivot) {
            f32 ratio = maxDistToPivot / FFIELD(o, oChainChompDistToPivot);
            FSETFIELD(o, oChainChompDistToPivot, maxDistToPivot);

            o->oChainChompSegments[0].posX *= ratio;
            o->oChainChompSegments[0].posY *= ratio;
            o->oChainChompSegments[0].posZ *= ratio;

            if (o->oChainChompReleaseStatus == CHAIN_CHOMP_NOT_RELEASED) {
                // Restrict chain chomp position
                FSETFIELD(o, oPosX, FFIELD(o->parentObj, oPosX) + o->oChainChompSegments[0].posX);
                FSETFIELD(o, oPosY, FFIELD(o->parentObj, oPosY) + o->oChainChompSegments[0].posY);
                FSETFIELD(o, oPosZ, FFIELD(o->parentObj, oPosZ) + o->oChainChompSegments[0].posZ);

                o->oChainChompRestrictedByChain = TRUE;
            } else {
                // Move pivot like the chain chomp is pulling it along
                f32 oldPivotY = FFIELD(o->parentObj, oPosY);

                FSETFIELD(o->parentObj, oPosX, FFIELD(o, oPosX) - o->oChainChompSegments[0].posX);
                FSETFIELD(o->parentObj, oPosY, FFIELD(o, oPosY) - o->oChainChompSegments[0].posY);
                FSETFIELD(o->parentObj, oVelY, FFIELD(o->parentObj, oPosY) - oldPivotY);
                FSETFIELD(o->parentObj, oPosZ, FFIELD(o, oPosZ) - o->oChainChompSegments[0].posZ);
            }
        } else {
            o->oChainChompRestrictedByChain = FALSE;
        }

        chain_chomp_update_chain_segments();

        // Begin a lunge if mario tries to attack
        if (obj_check_attacks(&sChainChompHitbox, o->oAction)) {
            o->oSubAction = CHAIN_CHOMP_SUB_ACT_LUNGE;
            FSETFIELD(o, oChainChompMaxDistFromPivotPerChainPart, 900.0f / 5);
            QSETFIELD(o,  oForwardVel, q(0));
            QSETFIELD(o,  oVelY, q(300));
            QSETFIELD(o, oGravity, q(-4.0f));
            o->oChainChompTargetPitch = -0x3000;
        }
    }
}

/**
 * Hide and free the chain chomp segments. The chain objects will unload
 * themselves when they see that the chain chomp is in this action.
 */
static void chain_chomp_act_unload_chain(void) {
    cur_obj_hide();
    mem_pool_free(gObjectMemoryPool, o->oChainChompSegments);

    o->oAction = CHAIN_CHOMP_ACT_UNINITIALIZED;

    if (o->oChainChompReleaseStatus != CHAIN_CHOMP_NOT_RELEASED) {
        obj_mark_for_deletion(o);
    }
}

/**
 * Update function for chain chomp.
 */
void bhv_chain_chomp_update(void) {
    switch (o->oAction) {
        case CHAIN_CHOMP_ACT_UNINITIALIZED:
            chain_chomp_act_uninitialized();
            break;
        case CHAIN_CHOMP_ACT_MOVE:
            chain_chomp_act_move();
            break;
        case CHAIN_CHOMP_ACT_UNLOAD_CHAIN:
            chain_chomp_act_unload_chain();
            break;
    }
}

/**
 * Update function for wooden post.
 */
void bhv_wooden_post_update(void) {
    // When ground pounded by mario, drop by -45 + -20
    if (!o->oWoodenPostMarioPounding) {
        if ((o->oWoodenPostMarioPounding = cur_obj_is_mario_ground_pounding_platform())) {
            cur_obj_play_sound_2(SOUND_GENERAL_POUND_WOOD_POST);
            QSETFIELD(o,  oWoodenPostSpeedY, q(-70));
        }
    } else if (APPROACH_F32_FIELD(o, oWoodenPostSpeedY, 0.0f, 25.0f)) {
        // Stay still until mario is done ground pounding
        o->oWoodenPostMarioPounding = cur_obj_is_mario_ground_pounding_platform();
    } else if (QMODFIELD(o, oWoodenPostOffsetY, += FFIELD(o, oWoodenPostSpeedY)) < q(-190)) {
        // Once pounded, if this is the chain chomp's post, release the chain
        // chomp
        QSETFIELD(o,  oWoodenPostOffsetY, q(-190));
        if (o->parentObj != o) {
            play_puzzle_jingle();
            o->parentObj->oChainChompReleaseStatus = CHAIN_CHOMP_RELEASED_TRIGGER_CUTSCENE;
            o->parentObj = o;
        }
    }

    if (QFIELD(o, oWoodenPostOffsetY) != 0) {
        QSETFIELD(o, oPosY, QFIELD(o, oHomeY) + QFIELD(o, oWoodenPostOffsetY));
    } else if (!(o->oBehParams & WOODEN_POST_BP_NO_COINS_MASK)) {
        // Reset the timer once mario is far enough
        if (QFIELD(o, oDistanceToMario) > q(400.0)) {
            o->oTimer = o->oWoodenPostTotalMarioAngle = 0;
        } else {
            // When mario runs around the post 3 times within 200 frames, spawn
            // coins
            o->oWoodenPostTotalMarioAngle += (s16)(o->oAngleToMario - o->oWoodenPostPrevAngleToMario);
            if (absi(o->oWoodenPostTotalMarioAngle) > 0x30000 && o->oTimer < 200) {
                obj_spawn_loot_yellow_coins(o, 5, 20.0f);
                set_object_respawn_info_bits(o, 1);
            }
        }

        o->oWoodenPostPrevAngleToMario = o->oAngleToMario;
    }
}

/**
 * Init function for chain chomp gate.
 */
void bhv_chain_chomp_gate_init(void) {
    o->parentObj = cur_obj_nearest_object_with_behavior(bhvChainChomp);
}

/**
 * Update function for chain chomp gate
 */
void bhv_chain_chomp_gate_update(void) {
    if (o->parentObj->oChainChompHitGate) {
        spawn_mist_particles_with_sound(SOUND_GENERAL_WALL_EXPLOSION);
        set_camera_shake_from_pointq(SHAKE_POS_SMALL, QFIELD(o, oPosX), QFIELD(o, oPosY), QFIELD(o, oPosZ));
        spawn_mist_particles_variable(0, 0x7F, 200.0f);
        spawn_triangle_break_particles(30, MODEL_DIRT_ANIMATION, 3.0f, 4);
        obj_mark_for_deletion(o);
    }
}
