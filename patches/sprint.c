#include "patches.h"
#include "input.h"

// Moves an actor by a velocity in its own frame (forward, vertical, side), with collision.
extern void func_8004DA90_28E90(u8* actor, s32 forward, s32 vertical, s32 side);
extern u8 D_802049B0_1DFDB0[]; // Mega Man.
extern s32 gDefinitive; // mouse_camera.c: 0 = base port controls (no sprint or walk easing)

// Diagonal movement while aiming or focused: the game stops Mega Man when forward/back and a side step are held
// together, so the camera patch leaves only the side step to the game and sets this to +1 (forward) or -1 (back);
// the forward/back part is then added to the side step's movement here.
s32 gAimDiagonal = 0;
s32 gAimSteer = 0, gAimSteerYaw = 0; // set by mouse_camera_player_input: strafe along this yaw instead
float gAimDiagLateral = 0.7071f, gAimDiagForward = 0.7071f; // The side step and forward/back share of the stick (unit vector).

// How strongly the sprint effects (camera pull back, wider FOV, vignette) apply, 0..1, eased each frame.
float gSprintAmount = 0.0f;
static s32 sNotWalkingFrames = 0;
// Movement feel: the sprint speed builds up and drops off gradually, walking starts with a short ramp, and when Mega
// Man stops he glides a few frames instead of halting dead.
static float sSprintSpeed = 0.0f;   // 0..1 share of the extra sprint speed, eased.
static float sWalkRamp = 1.0f;      // Walk speed multiplier while starting to walk.
static s32 sGlideForward = 0;       // Last forward/side velocity applied, carried on after stopping.
static s32 sGlideSide = 0;
static bool sMovedThisFrame = FALSE;
#define WALK_RAMP_START 0.45f
#define WALK_RAMP_RATE 0.18f
#define SPRINT_SPEED_IN 0.06f
#define SPRINT_SPEED_OUT 0.12f
#define GLIDE_DECAY 0.6f
extern void func_8006FC34_4B034(u8* actor, s16 forward, s16 vertical, s16 side);
// Mega Man must be walking (state 1, substate 2) to keep sprinting; brief interruptions are tolerated.
#define SPRINT_STOP_FRAMES 12

// Called once per game frame from the player input hook.
void sprint_frame_update(void) {
    u8* player = D_802049B0_1DFDB0;
    bool walking = player[0x8] == 1 && player[0x9] == 2;
    bool sprinting = recomp_get_sprint_held() != 0;

    if (sprinting && !walking) {
        if (++sNotWalkingFrames > SPRINT_STOP_FRAMES) {
            recomp_stop_sprint();
            sprinting = FALSE;
        }
    } else {
        sNotWalkingFrames = 0;
    }
    // Ease in over ~10 frames and out a bit faster.
    if (sprinting && walking) {
        gSprintAmount += (1.0f - gSprintAmount) * 0.12f;
    } else {
        gSprintAmount -= gSprintAmount * 0.18f;
    }
    if (gSprintAmount < 0.001f) {
        gSprintAmount = 0.0f;
    }
    if (sprinting && walking) {
        sSprintSpeed += (1.0f - sSprintSpeed) * SPRINT_SPEED_IN;
    } else {
        sSprintSpeed -= sSprintSpeed * SPRINT_SPEED_OUT;
    }

    if (walking) {
        sWalkRamp += (1.0f - sWalkRamp) * WALK_RAMP_RATE;
    } else {
        sWalkRamp = WALK_RAMP_START;
        // Just stopped on the ground (standing, state 1 substate 0 or 1): glide off the last velocity.
        if (!sMovedThisFrame && player[0x8] == 1 && player[0x9] <= 1 && (sGlideForward != 0 || sGlideSide != 0)) {
            sGlideForward = (s32)(sGlideForward * GLIDE_DECAY);
            sGlideSide = (s32)(sGlideSide * GLIDE_DECAY);
            if (sGlideForward * sGlideForward + sGlideSide * sGlideSide > 24 * 24) {
                func_8006FC34_4B034(player, (s16)sGlideForward, 0, (s16)sGlideSide);
            } else {
                sGlideForward = sGlideSide = 0;
            }
        } else if (player[0x8] != 1) {
            sGlideForward = sGlideSide = 0;
        }
    }
    sMovedThisFrame = FALSE;
}

// Sprint speed as a ratio of the walk speed.
#define SPRINT_NUM 3
#define SPRINT_DEN 2

// move_actor_local
RECOMP_PATCH void func_8006FC34_4B034(u8* actor, s16 forward, s16 vertical, s16 side) {
    s32 diagonalForward = 0;
    // @recomp L3 sprint: Mega Man on the ground (state 1) and walking (substate 2) moves faster. Movement still goes
    // through the game's collision below, so walls and ledges behave as usual.
    if (gDefinitive && actor == D_802049B0_1DFDB0 && actor[0x8] == 1 && actor[0x9] == 2) {
        // Walking: ramp in after starting, and blend toward the sprint speed as it builds up.
        float scale = sWalkRamp * (1.0f + (float)(SPRINT_NUM - SPRINT_DEN) / SPRINT_DEN * sSprintSpeed);
        forward = (s16)(forward * scale);
        side = (s16)(side * scale);
        sGlideForward = forward;
        sGlideSide = side;
        sMovedThisFrame = TRUE;
    }

    if (actor == D_802049B0_1DFDB0 && gAimSteer != 0) {
        u16 yaw = *(u16*)(actor + 0x56);
        *(u16*)(actor + 0x56) = (u16)gAimSteerYaw;
        func_8004DA90_28E90(actor, forward, vertical, side);
        *(u16*)(actor + 0x56) = yaw;
        return;
    }

    // Aiming or focused with a diagonal held: the game strafes by moving "forward" along a heading turned a quarter of a
    // turn (+0x116), so that move is the side step and the forward/back part has to be a second move along Mega Man's
    // own facing, split by the stick's angle (unit vector) so the speed is the same in every direction.
    if (actor == D_802049B0_1DFDB0 && gAimDiagonal != 0 && forward != 0) {
        diagonalForward = (s32)(forward * gAimDiagForward);
        forward = (s32)(forward * gAimDiagLateral);
    }

    *(u16*)(actor + 0x56) -= *(u16*)(actor + 0x116);
    func_8004DA90_28E90(actor, forward, vertical, side);
    *(u16*)(actor + 0x56) += *(u16*)(actor + 0x116);
    if (diagonalForward != 0) {
        func_8004DA90_28E90(actor, diagonalForward, 0, 0);
    }
}
