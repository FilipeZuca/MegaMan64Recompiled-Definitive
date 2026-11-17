#include "patches.h"
#include "mouse_camera.h"
#include "tag_helper.h"
#include "graphics.h"

// Sprint effects (patches/sprint.c).
extern float gSprintAmount;
extern s32 gAimDiagonal;
extern s32 gAimSteer, gAimSteerYaw; // sprint.c: move along this yaw instead of the strafe heading
extern float gAimDiagLateral, gAimDiagForward;
void sprint_frame_update(void);
// Sprint camera, in the style of modern third person games: the field of view widens, the camera tucks in a little
// closer and lower behind the character, and a faint footstep bob plays, all eased in and out with the sprint.
#define SPRINT_CLOSER 0.08f       // Fraction of the distance the camera moves in at full sprint.
#define SPRINT_LOWER 22.0f        // How far the look target drops at full sprint (+Y is down).
#define SPRINT_FOV_WIDEN 0.12f    // Fraction the horizontal view widens at full sprint.
#define SPRINT_BOB_HEIGHT 2.5f    // Footstep bob amplitude.
#define SPRINT_BOB_SPEED 300      // Bob phase step per game frame (0x1000 = one cycle, ~2.2 steps per second).
static u32 sSprintBobPhase = 0;


// Mouse camera mod: first/third person mouse look.
//
// The game is a port of Mega Man Legends and uses PSX GTE style math: 12-bit angles (0x1000 = 360 degrees),
// 3x3 s16 rotation matrices in 4.12 fixed point and a world space where -Y is up. Every gameplay camera
// ends up in func_80032FC0 (camera_set_orbit), which builds the view matrix from a target point, a distance,
// a yaw and a pitch. The patch below is a straight C port of that function with a hook for the main gameplay
// camera, which lets the mouse turn Mega Man and tilt the camera.

typedef struct {
    s16 m[3][3];
    s16 pad;
    s32 t[3];
} PsxMatrix; // size = 0x20

typedef struct {
    s32 x, y, z;
    s32 pad;
} Vec3i; // size = 0x10

typedef struct {
    s16 x, y, z;
    s16 pad;
} SVec3; // size = 0x8

typedef struct {
    /* 0x00 */ u8 unk00;
    /* 0x01 */ u8 unk01;
    /* 0x02 */ u8 blendAmount; // Non-zero while blending between two camera setups.
    /* 0x03 */ u8 unk03;
    /* 0x04 */ u8 enabled;
    /* 0x05 */ u8 mode; // 0 follow, 4/5 locked behind Mega Man, others track actors.
    /* 0x06 */ u8 scripted; // Non-zero while a cutscene drives the camera.
    /* 0x07 */ u8 shakeY;
    /* 0x08 */ u8 unk08[4];
    /* 0x0C */ s32 distance;
    /* 0x10 */ s32 yaw;
    /* 0x14 */ s32 pitch;
    /* 0x18 */ s32 unk18;
    /* 0x1C */ s32 yawOffset;
    /* 0x20 */ s32 pitchOffset;
    /* 0x24 */ u8 unk24[0x24];
    /* 0x48 */ Vec3i target;
    /* 0x58 */ s32 unk58;
    /* 0x5C */ s32 targetHeight; // Target is placed this far above Mega Man's feet.
} GameCamera;

extern GameCamera D_80204348_1DF748;
extern Vec3i D_80162830_13DC30;
extern s32 D_80162840_13DC40;
extern s32 D_80162844_13DC44;
extern s32 D_80162848_13DC48;
extern SVec3 D_80204400_1DF800;
extern PsxMatrix D_80210990_1EBD90[2]; // [0] rotation, [1] its transpose.
extern u8 D_80210B61_1EBF61; // Coordinate precision shift.
extern PsxMatrix D_801D4760_1AFB60; // Current view matrix.
extern Vec3i D_802043E0_1DF7E0; // Camera position.
extern Vec3i D_802043F0_1DF7F0; // Previous camera position.
extern s16 D_80195E90_171290[3];
extern s32 D_80206B58_1E1F58[3];

extern u8 D_802049B0_1DFDB0[]; // Mega Man.
#define PLAYER_FLAGS(p) (*(u8*)((p) + 0x00))
#define PLAYER_ACTIVE(p) (*(u8*)((p) + 0x06))
#define PLAYER_POS_X(p) (*(s16*)((p) + 0x14))
#define PLAYER_POS_Y(p) (*(s16*)((p) + 0x16))
#define PLAYER_POS_Z(p) (*(s16*)((p) + 0x18))
#define PLAYER_YAW(p) (*(s16*)((p) + 0x56))
#define PLAYER_INPUT_FLAGS(p) (*(u8*)((p) + 0xBD)) // 0x40 input cleared, 0x20 input driven by a script.

void func_80031600_CA00(SVec3* angles, PsxMatrix* out); // RotMatrix
PsxMatrix* func_80031E10_D210(PsxMatrix* in, PsxMatrix* out); // TransposeMatrix
s32 func_800324B4_D8B4(s32 angle); // rsin
s32 func_80032498_D898(s32 angle); // rcos
void func_80030AA0_BEA0(PsxMatrix* m, Vec3i* in, Vec3i* out); // ApplyMatrixLV
s32 func_80032424_D824(s32 value); // Integer square root.
s32 func_80032664_DA64(s32 x, s32 z); // ratan2: 12-bit angle of the direction (x, z), matching the camera's yaw.
void func_800306F0_BAF0(PsxMatrix* a, PsxMatrix* b, PsxMatrix* out); // CompMatrix
void func_800308E8_BCE8(PsxMatrix* m, SVec3* in, Vec3i* out); // ApplyMatrix
void func_800283A8_37A8(s8 bufferIndex, s8 poolIndex, void* func, void* funcData); // gfx_add_render_task
void func_8008128C_5C68C(void* arg);
void func_80084870_5FC70(s32 bufferIndex, s32 poolIndex, s32 part, PsxMatrix* mtx, s32 arg4); // draw Mega Man part
void func_8008498C_5FD8C(s32 bufferIndex, s32 poolIndex, s32 face, s32 mouth, PsxMatrix* mtx); // draw Mega Man face

extern u8 D_800AC9BC_87DBC[]; // Part counts per render group, 5 groups per body variant.
extern u8 D_800AC9CC_87DCC[]; // Part (bone) indices, in draw order.
extern u8 D_80210B62_1EBF62;

#define PLAYER_BONE_MATRICES(p) ((PsxMatrix*)((p) + 0x1C8))
#define PLAYER_BONE_POSITIONS(p) ((SVec3*)((p) + 0x3A8))
#define PLAYER_FACE(p) (*(s8*)((p) + 0x168))
#define PLAYER_MOUTH(p) (*(s8*)((p) + 0x169))
#define MEGAMAN_HEAD_BONE 1
#define MEGAMAN_HELMET_PART 15
// Only the arms are drawn in first person (3-4 right arm with the buster, 6-7 left arm for special weapons; the
// shoulder pads 2 and 5 sit right next to the camera and are hidden too): they
// stay out of view until an animation (shooting, special weapons) swings them up, like a shooter's viewmodel. The torso,
// hips and legs (0, 8-14) and the head (1) are hidden so the camera can sit behind the head without seeing them.
#define FIRST_PERSON_PART_MASK ((1 << 3) | (1 << 4) | (1 << 6) | (1 << 7))
#define PLAYER_COMBAT_STATE(p) (*(u8*)((p) + 0x171)) // Zero in towns, where Mega Man kicks instead of shooting.

// Angle units applied per (sensitivity scaled) mouse pixel.
#define MOUSE_CAMERA_YAW_SCALE 12.0f
#define MOUSE_CAMERA_PITCH_SCALE 8.0f

#define THIRD_PERSON_PITCH_OFFSET_MIN -0x200
#define THIRD_PERSON_PITCH_OFFSET_MAX 0x300
#define THIRD_PERSON_PITCH_MIN -0x180
#define THIRD_PERSON_PITCH_MAX 0x3C0

#define FIRST_PERSON_PITCH_MAX 0x380
// Mega Man is ~130 units tall and his head bone sits ~116 units above his feet.
#define FIRST_PERSON_EYE_HEIGHT 122
// The first person camera sits this far behind the head (the body is hidden), so walls Mega Man walks up to stay
// beyond the near plane instead of being cut open. The viewmodel arms still use a view at the head.
#define FIRST_PERSON_BACK_OFFSET 32
// The game's near plane is ~190 units, which would clip Mega Man's own arms in first person, and his back when the
// third person camera aims over his shoulder or zooms in. They're no closer than needed: the fog is remapped to the
// game's distances (mouse_camera_fog_position), and with a very close near plane the remapped fog band gets too
// narrow for the RSP's 16-bit fog multiplier and has to start early.
#define FIRST_PERSON_NEAR_PLANE 20.0f
#define THIRD_PERSON_NEAR_PLANE 64.0f

// Only the gameplay camera sets these. Other cameras (the sky, cutscenes) also go through camera_set_orbit, often later
// in the same frame, so they must not clear them. The main draw counts sMouseCameraFrames down instead, which turns
// the mouse camera's effects off once the gameplay camera stops updating.
static bool sFirstPersonActive = FALSE;
s32 gDbgForceAim = 0; // debug (poke.py): acts as if the aim button were held
static s32 sMouseCameraFrames = 0;
// Set when the projection is adjusted for a first person frame where Mega Man is armed.
static bool sCrosshairVisible = FALSE;
// View matrix used to draw Mega Man's body in first person: at his eyes, facing where he faces, but ignoring the
// camera pitch. His own animations (shooting, special weapons, kicks) then play in view like a shooter's viewmodel
// instead of swinging around as the player looks up and down.
static PsxMatrix sViewmodelView;

static float sFirstPersonPitch = 0.0f;
static float sThirdPersonPitchOffset = 0.0f;
static float sYawRemainder = 0.0f;

// Third person: the mouse orbits the camera around Mega Man, and his movement becomes relative to the camera.
static s32 sOrbitYaw = 0;
static s32 sLastMode = MOUSE_CAMERA_OFF;
static bool sFreeCameraActive = FALSE; // Set by the camera for the player input hook.
// Starts on the game's own distance.
static float sZoom = 1.0f;
// Camera Distance presets, cycled with the Camera Distance input: close, medium, the game's own, far.
static const float sZoomPresets[] = { 0.4f, 0.7f, 1.0f, 1.3f };
#define ZOOM_MIN 0.4f
#define ZOOM_MAX 2.0f
#define ZOOM_STEP 0.1f
// Fastest Mega Man turns towards the direction pressed, per frame.
#define FREE_CAMERA_TURN_RATE 0x1C0
#define FREE_CAMERA_TURN_EASE 0.35f // Fraction of the remaining turn applied per frame.
#define FREE_CAMERA_TURN_MIN 0x30
#define FREE_CAMERA_STICK_MIN 16   // Stick counts (of ~80) before its angle is used.
extern u8 D_801BC380_197780[]; // Controller 1: u16 buttons, s8 stick x, s8 stick y.

// Over the shoulder aiming (right mouse button in third person).
static bool sAiming = FALSE;
static bool sFpAimHeld = FALSE; // first person aim button (zoom) held this frame
static float sAimPitch = 0.0f;
s32 gAimDistance = 230; // live (poke.py)
#define AIM_DISTANCE gAimDistance
// L2 outside combat: a closer, zoomed look over the shoulder.
#define LOOK_DISTANCE 140
#define LOOK_TURN_SCALE 0.6f   // Slower camera turning while looking around.
#define AIM_TURN_SCALE 0.65f   // Slower camera turning while aiming the buster.
#define LOOK_FOV_ZOOM 0.6f     // View narrows by this fraction (1.6x zoom) in look mode.
#define AIM_FOV_ZOOM 0.12f     // Slight zoom when aiming.
#define AIM_BLEND_SPEED 0.22f  // Per game frame easing into and out of L2.
static float sAimBlend = 0.0f;
// Free aim (third person, armed, not aiming and not locked on): the crosshair stays on screen a little above Mega Man
// (the camera looks at his head) and shots go where it points. Firing turns him toward it for one shot animation.
float gFreeAimRaise = 0.16f;   // live (float bits): crosshair height above the center, fraction of half the screen
#define FIRE_FACE_FRAMES 20
static float sProjYScale = 0.0f;     // last projection's vertical scale: screen fraction per unit of tan(angle)
static float sCrosshairRaise = 0.0f; // this frame's crosshair height above the center, fraction of half the screen
static s32 sFireFaceFrames = 0;
s32 gDbgForceMode = -1; // debug (poke.py): 1 third person, 2 first person, -1 the setting

// Target focus (Souls-like lock-on camera): R3 locks onto the enemy nearest the center of the view; the camera turns
// and tilts to keep it framed, Mega Man faces it and left/right circle-strafe around it. R3 again, the target dying or
// getting too far releases it. Enemies are the active actors of the two actor lists that have no talk handler (NPCs
// have one at +0x10).
static u8* sFocusTarget = NULL;
#define FOCUS_PICK_ANGLE 0x300    // Only enemies within about 67 degrees of where the camera looks.
#define FOCUS_SWITCH_STICK 35.0f  // Yaw change per frame (12 per stick count, ~68 at full tilt) that counts as a flick.
static s32 sFocusSwitchCooldown = 0;
#define FOCUS_PITCH_TURN 0.15f    // Fraction of the remaining pitch the camera tilts per frame toward the target.
// Lock-on reach is the buster's: the farthest any of Mega Man's shots has flown (aim_player_shots), which grows with
// the Range upgrades. 1836 measured with the test save (shot flight logged per frame). A locked target is kept a little further.
static s32 sShotRange = 1800;
// Lock-on reaches further than the buster (asked for both views); the buster's reach wins once upgrades pass it.
s32 gFocusRange = 3600; // live (poke.py)
static s32 focus_reach(void) {
    return sShotRange > gFocusRange ? sShotRange : gFocusRange;
}
#define FOCUS_KEEP_MARGIN 300
#define FOCUS_TURN 0.18f          // Fraction of the remaining yaw the camera turns per frame toward the target.
#define FOCUS_TARGET_HEIGHT 60    // Look at this far above the enemy's origin.

// Height of an enemy's body center: the average of its joint world positions (s16 x, y, z every 8 bytes from +0x180,
// ended by an empty entry), so the focus and aim go to the middle of big enemies instead of their feet. Falls back
// to a fixed height above the origin for actors outside the enemy list or without joints.
#define ACTOR_JOINTS_OFFSET 0x180
#define ACTOR_JOINTS_MAX 12
static s32 actor_center_y(u8* actor, s32 fallbackHeight) {
    s32 x = *(s16*)(actor + 0x14), y = *(s16*)(actor + 0x16), z = *(s16*)(actor + 0x18);
    s32 sum = 0, count = 0, j;
    if ((u32)actor < 0x801FFBE0 || (u32)actor >= 0x801FFBE0 + 18 * 0x3C8) {
        return y - fallbackHeight;
    }
    for (j = 0; j < ACTOR_JOINTS_MAX; j++) {
        s16* joint = (s16*)(actor + ACTOR_JOINTS_OFFSET + j * 8);
        if (joint[0] == 0 && joint[2] == 0) {
            break;
        }
        // Skip anything that isn't near the actor (unused or stale entries).
        if (joint[0] - x > 600 || x - joint[0] > 600 || joint[2] - z > 600 || z - joint[2] > 600) {
            continue;
        }
        sum += joint[1];
        count++;
    }
    // Small enemies have joints at their feet only: keep at least the fixed height above the origin.
    if (count == 0 || sum / count > y - fallbackHeight) {
        return y - fallbackHeight;
    }
    return sum / count;
}

static s32 wrap_angle(s32 angle);

// side: 0 picks the enemy nearest the view center; +1/-1 picks the nearest one on that side of the current target.
static u8* focus_pick_side(u8* player, s32 cameraYaw, s32 side);

s32 func_8003F780_1AB80(s16* a, s16* b, void* out, s32 mask);
#define FOCUS_SIGHT_STEP 24 // point tests, so walls thinner than this could be missed

// True when no level collision lies between Mega Man's chest and the actor's, so enemies in other rooms are ignored.
static bool focus_line_clear(u8* player, u8* actor) {
    s32 x0 = PLAYER_POS_X(player), y0 = PLAYER_POS_Y(player) - FOCUS_TARGET_HEIGHT, z0 = PLAYER_POS_Z(player);
    s32 dx = *(s16*)(actor + 0x14) - x0, dy = actor_center_y(actor, FOCUS_TARGET_HEIGHT) - y0;
    s32 dz = *(s16*)(actor + 0x18) - z0;
    s32 length = func_80032424_D824(dx * dx + dz * dz);
    s32 t;
    s16 point[4]; // The collision test reads 8 bytes per point.

    for (t = FOCUS_SIGHT_STEP; t < length - FOCUS_SIGHT_STEP; t += FOCUS_SIGHT_STEP) {
        point[0] = (s16)(x0 + dx * t / length);
        point[1] = (s16)(y0 + dy * t / length);
        point[2] = (s16)(z0 + dz * t / length);
        point[3] = 0;
        if (func_8003F780_1AB80(point, point, NULL, 0xFFFF) != 0) {
            return FALSE;
        }
    }
    return TRUE;
}

static u8* focus_pick(u8* player, s32 cameraYaw) {
    return focus_pick_side(player, cameraYaw, 0);
}

static u8* focus_pick_side(u8* player, s32 cameraYaw, s32 side) {
    u8* best = NULL;
    s32 bestScore = 0x7FFFFFFF;
    u32 l, i;
    for (l = 0; l < 2; l++) {
        u32 base = l == 0 ? 0x801FFBE0 : 0x801BA0C0;
        u32 size = l == 0 ? 0x3C8 : 0x5AC;
        u32 count = l == 0 ? 18 : 4;
        for (i = 0; i < count; i++) {
            u8* actor = (u8*)(base + i * size);
            s32 dx, dz, dist, angle, score;
            // Alive and drawn (bit 2 of both flag bytes, as tag_actors.c checks) and not an NPC. Bit 0 of byte 0 is
            // not required: the eye tower robots in the ruins (type 0x02) keep it clear. Anything with a talk handler
            // at +0x10 is an NPC or a prop (type 0x35 in the ruins never changes when shot).
            if ((actor[0] & actor[6] & 2) == 0 ||
                (l == 0 && *(u32*)(actor + 0x10) != 0) || actor == sFocusTarget) {
                continue;
            }
            dx = *(s16*)(actor + 0x14) - PLAYER_POS_X(player);
            dz = *(s16*)(actor + 0x18) - PLAYER_POS_Z(player);
            if (dx * dx + dz * dz >= focus_reach() * focus_reach()) {
                continue;
            }
            dist = func_80032424_D824(dx * dx + dz * dz);
            angle = wrap_angle(func_80032664_DA64(dx, dz) - cameraYaw);
            if (side != 0) {
                // Switching: only enemies on the flicked side, the nearest in angle wins.
                if ((side > 0 && angle <= 0) || (side < 0 && angle >= 0) || angle > 0x600 || angle < -0x600) {
                    continue;
                }
            }
            if (angle < 0) {
                angle = -angle;
            }
            if ((side == 0 && angle > FOCUS_PICK_ANGLE) || !focus_line_clear(player, actor)) {
                continue;
            }
            // Prefer what's in the middle of the view, then what's close. Distance weighs as much as ~40 degrees per
            // 1000 units, so an enemy in front of Mega Man beats one far behind it near the screen center.
            score = angle * 2 + dist;
            if (score < bestScore) {
                bestScore = score;
                best = actor;
            }
        }
    }
    return best;
}

static bool focus_target_valid(u8* player) {
    s32 dx, dz;
    if (sFocusTarget == NULL || (sFocusTarget[0] & 2) == 0) {
        return FALSE;
    }
    dx = *(s16*)(sFocusTarget + 0x14) - PLAYER_POS_X(player);
    dz = *(s16*)(sFocusTarget + 0x18) - PLAYER_POS_Z(player);
    return dx * dx + dz * dz < (focus_reach() + FOCUS_KEEP_MARGIN) * (focus_reach() + FOCUS_KEEP_MARGIN);
}
static float sFpAimBlend = 0.0f;
#define FP_AIM_FOV_ZOOM 0.6f        // First person L2 zoom (view narrows to 1/1.6).
#define FP_AIM_SLOWDOWN 0.5f        // Look sensitivity drops by half at full zoom.
// Aiming no longer pulls the viewmodel camera back to show the arm (that showed the idle pose and the body): the arm
// comes into view with the shooting animation, the same as firing without L2.
#define FP_AIM_VIEWMODEL_BACK 0.0f
#define FP_AIM_VIEWMODEL_PITCH 0
static float sAimFovZoom = 1.0f;
#define AIM_HEIGHT 118
// Aiming turns Mega Man toward the crosshair by this share of the gap each frame (~0.1 s to face it at 30 fps).
#define AIM_FACE_EASE 0.45f
#define AIM_FACE_MIN_DIST 96
// Negative: the camera sits over the left shoulder, so Mega Man stands on the right of the screen.
#define AIM_SIDE -60

// Hitmarker, frames left to show it.
static s32 sHitmarkerFrames = 0;
#define HITMARKER_FRAMES 10

// Aim assist: inside the cone, mouse turning slows down and the aim is pulled gently towards the enemy.
#define AIM_ASSIST_CONE 0x50 // ~7 degrees.
#define AIM_ASSIST_RANGE focus_reach()
// Kept light on purpose (asked for): live (poke.py, float bits). Friction = turn speed left at the enemy's center.
float gAimFriction = 0.75f;
float gAimPull = 0.06f;
#define AIM_ASSIST_FRICTION gAimFriction
#define AIM_ASSIST_PULL gAimPull
#define AIM_ASSIST_INPUT_FULL 8.0f // Turn input (angle units per frame) at which the pull reaches full strength.
#define AIM_ASSIST_TARGET_HEIGHT 40 // Aim this far above an enemy's origin.

static s32 clamp_s32(s32 value, s32 min, s32 max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static float clamp_f32(float value, float min, float max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

// Builds a view matrix for a camera sitting at pos, the same way camera_set_orbit does with a distance of zero.
static void build_view_at(Vec3i* pos, s32 yaw, s32 pitch, PsxMatrix* out) {
    SVec3 angles;
    PsxMatrix rot;
    Vec3i offset;
    s32 shift = D_80210B61_1EBF61;

    angles.x = -pitch & 0xFFF;
    angles.y = yaw & 0xFFF;
    angles.z = 0;
    func_80031600_CA00(&angles, &rot);
    func_80031E10_D210(&rot, out);

    offset.x = (s32)((u32)-pos->x << shift);
    offset.y = (s32)((u32)-pos->y << shift);
    offset.z = (s32)((u32)-pos->z << shift);
    func_80030AA0_BEA0(out, &offset, (Vec3i*)out->t);
}

// Whether the gameplay camera is following Mega Man, even if the game has taken control of him (talking to someone).
static bool mouse_camera_following_player(void) {
    GameCamera* cam = &D_80204348_1DF748;
    u8* player = D_802049B0_1DFDB0;

    if (!cam->enabled || cam->scripted) {
        return FALSE;
    }
    if (cam->mode != 0 && cam->mode != 4 && cam->mode != 5) {
        return FALSE;
    }
    return (PLAYER_FLAGS(player) & PLAYER_ACTIVE(player) & 1) != 0;
}

static bool mouse_camera_player_controllable(void) {
    GameCamera* cam = &D_80204348_1DF748;
    u8* player = D_802049B0_1DFDB0;

    if (!cam->enabled || cam->scripted) {
        return FALSE;
    }
    // Leave actor tracking cameras (lock-on, bosses...) to the game.
    if (cam->mode != 0 && cam->mode != 4 && cam->mode != 5) {
        return FALSE;
    }
    if (!(PLAYER_FLAGS(player) & PLAYER_ACTIVE(player) & 1)) {
        return FALSE;
    }
    if (PLAYER_INPUT_FLAGS(player) & 0x60) {
        return FALSE;
    }
    // Talking (state 1, substate 0x10): the game's own conversation camera frames Mega Man and the NPC (first
    // person keeps its view instead, see mouse_camera_update).
    if (player[0x8] == 1 && player[0x9] == 0x10) {
        return FALSE;
    }
    return TRUE;
}

// Mega Man's shots, 16 slots of 0x118 bytes. Byte 0 bit 0 marks a live shot, the position is at 0x1C as 16.16 fixed
// point with s16 copies at 0x14 and 0x90. Shots only fly horizontally in the original game.
extern u8 D_802059A0_1E0DA0[];
#define PLAYER_SHOT_COUNT 16
#define PLAYER_SHOT_SIZE 0x118

// Where the crosshair points in the world this frame (see compute_aim_point). Shots converge on it.
static float sAimPoint[3];
static bool sAimPointValid = FALSE;
static bool sAimOnEnemy = FALSE; // the crosshair point is on an enemy, not on level collision

static bool sShotTracked[PLAYER_SHOT_COUNT];
static s32 sShotSlope[PLAYER_SHOT_COUNT]; // Vertical travel per unit of horizontal travel, 4.12 fixed point.
static s32 sShotLastX[PLAYER_SHOT_COUNT];
static s32 sShotLastZ[PLAYER_SHOT_COUNT];
static s32 sShotY[PLAYER_SHOT_COUNT];      // Height the shot keeps, same format as its position (16.16).
static u8* sShotTarget[PLAYER_SHOT_COUNT]; // Enemy a shot fired without L2 rises or drops toward.
static s16 sShotStart[PLAYER_SHOT_COUNT][2]; // Where it spawned (x, z), for sShotRange.
// Horizontal direction toward the crosshair for a shot fired aiming, applied once the game sets its velocity (a few
// frames after the shot appears, from Mega Man's facing). Zero = nothing pending.
static float sShotAimDir[PLAYER_SHOT_COUNT][2];

// Velocity is s16 8.8 at +0x44 (x) and +0x48 (z); +0x64 is the heading (shotvel.py).
static void aim_shot_heading(s32 i, u8* shot) {
    s16* vx = (s16*)(shot + 0x44);
    s16* vz = (s16*)(shot + 0x48);
    float speed;
    if ((sShotAimDir[i][0] == 0.0f && sShotAimDir[i][1] == 0.0f) || (*vx == 0 && *vz == 0)) {
        return;
    }
    speed = __builtin_sqrtf((float)*vx * *vx + (float)*vz * *vz);
    *vx = (s16)(speed * sShotAimDir[i][0]);
    *vz = (s16)(speed * sShotAimDir[i][1]);
    // Actor yaw is the camera's yaw + 0x800.
    *(s16*)(shot + 0x64) = (func_80032664_DA64((s32)(sShotAimDir[i][0] * 4096.0f), (s32)(sShotAimDir[i][1] * 4096.0f)) + 0x800) & 0xFFF;
    sShotAimDir[i][0] = sShotAimDir[i][1] = 0.0f;
}

// Makes shots follow the first person aim vertically: each frame, a shot rises or drops by its horizontal travel
// times the slope of the camera pitch it was fired at. Measuring the travel keeps this independent of shot speed.
static void aim_player_shots(bool aiming, s32 pitch) {
    u8* player = D_802049B0_1DFDB0;
    s32 slope = 0;
    s32 i;

    if (aiming) {
        s32 cosPitch = func_80032498_D898(pitch);
        if (cosPitch > 0) {
            slope = (func_800324B4_D8B4(pitch) << 12) / cosPitch;
        }
    }

    for (i = 0; i < PLAYER_SHOT_COUNT; i++) {
        u8* shot = &D_802059A0_1E0DA0[i * PLAYER_SHOT_SIZE];
        s32* pos = (s32*)(shot + 0x1C);
        s32 dx, dz, travel;

        if (!(shot[0] & 1)) {
            sShotTracked[i] = FALSE;
            continue;
        }
        if (!sShotTracked[i]) {
            sShotTracked[i] = TRUE;
            sShotSlope[i] = slope;
            sShotTarget[i] = NULL;
            sShotY[i] = pos[1];
            sShotStart[i][0] = (s16)(pos[0] >> 16);
            sShotStart[i][1] = (s16)(pos[2] >> 16);
            sShotAimDir[i][0] = sShotAimDir[i][1] = 0.0f;
            if (aiming && sAimPointValid) {
                // Aim from the buster to the point under the crosshair, not parallel to the camera: the camera sits
                // over the shoulder (or at the eyes), so a parallel shot passes beside and below what the crosshair is on.
                float sx = (float)(pos[0] >> 16), sy = (float)(pos[1] >> 16), sz = (float)(pos[2] >> 16);
                float hx = sAimPoint[0] - sx, hz = sAimPoint[2] - sz;
                float horizontal = __builtin_sqrtf(hx * hx + hz * hz);
                if (horizontal > 16.0f) {
                    float rise = (sAimPoint[1] - sy) / horizontal;
                    // The heading too: the game fires along Mega Man's facing, which follows the movement (strafe,
                    // free movement while aiming), not the crosshair.
                    sShotAimDir[i][0] = hx / horizontal;
                    sShotAimDir[i][1] = hz / horizontal;
                    aim_shot_heading(i, shot);
                    rise = rise > 4.0f ? 4.0f : (rise < -4.0f ? -4.0f : rise);
                    // Free aim (third person without L2) looks down past Mega Man: unless it is on an enemy, shots
                    // keep their height instead of diving into the floor at the crosshair (+Y is down).
                    if (!sAiming && !sFirstPersonActive && !sAimOnEnemy && rise > 0.0f) {
                        rise = 0.0f;
                    }
                    sShotSlope[i] = (s32)(rise * 4096.0f);
                }
            } else if (!aiming) {
                // Without L2, shots rise or drop toward the focused enemy, or the one Mega Man faces, so they don't
                // follow the ground's slope into the floor on ramps. The game's own sideways homing is kept.
                sShotTarget[i] = sFocusTarget != NULL ? sFocusTarget : focus_pick(player, PLAYER_YAW(player) + 0x800);
            }
            sShotLastX[i] = pos[0];
            sShotLastZ[i] = pos[2];
            continue;
        }
        aim_shot_heading(i, shot);
        {
            s32 fx = (pos[0] >> 16) - sShotStart[i][0], fz = (pos[2] >> 16) - sShotStart[i][1];
            // Below the city's 7168 world wrap, which moves everything at once.
            if (fx < 4000 && fx > -4000 && fz < 4000 && fz > -4000 && fx * fx + fz * fz > sShotRange * sShotRange) {
                sShotRange = func_80032424_D824(fx * fx + fz * fz);
            }
        }

        // Work in 1/256 units so the squares fit in 32 bits.
        dx = (pos[0] - sShotLastX[i]) >> 8;
        dz = (pos[2] - sShotLastZ[i]) >> 8;
        sShotLastX[i] = pos[0];
        sShotLastZ[i] = pos[2];
        if (dx > 0x7FFF || dx < -0x7FFF || dz > 0x7FFF || dz < -0x7FFF) {
            continue;
        }
        if (sShotTarget[i] != NULL) {
            u8* target = sShotTarget[i];
            s32 hx = *(s16*)(target + 0x14) - (pos[0] >> 16), hz = *(s16*)(target + 0x18) - (pos[2] >> 16);
            s32 remaining = func_80032424_D824(hx * hx + hz * hz);
            if (!(target[0] & target[6] & 2)) {
                sShotTarget[i] = NULL;
            } else if (remaining > 16) {
                // Re-aim every frame at the target's body center over the remaining distance (steepest 45 degrees).
                s32 rise = ((actor_center_y(target, AIM_ASSIST_TARGET_HEIGHT) - (sShotY[i] >> 16)) << 12) / remaining;
                sShotSlope[i] = rise > 0x1000 ? 0x1000 : (rise < -0x1000 ? -0x1000 : rise);
            }
        }
        if (sShotSlope[i] == 0 && sShotTarget[i] == NULL) {
            sShotY[i] = pos[1];
            continue;
        }

        travel = func_80032424_D824(dx * dx + dz * dz);
        // Positive pitch looks down and +Y points down, so the slope applies as is. The height is kept here rather
        // than added to the game's, which moves shots along the ground's slope on ramps.
        sShotY[i] += ((travel * sShotSlope[i]) >> 12) << 8;
        pos[1] = sShotY[i];
        *(s16*)(shot + 0x16) = pos[1] >> 16;
        *(s16*)(shot + 0x92) = pos[1] >> 16;
    }
}

// Called by the us.rev1.toml hook where a flying buster shot ends on an enemy.
void mouse_camera_register_hit(void) {
    sHitmarkerFrames = HITMARKER_FRAMES;
}

// Enemies: 18 actors of 0x3C8 bytes and 4 large (multi part) actors of 0x5AC bytes. Like Mega Man and his shots,
// byte 0 & byte 6 & 1 marks an active one and the position is an s16 vector at 0x14.
extern u8 D_801FFBE0_1DAFE0[];
extern u8 D_801BA0C0_1954C0[];

static s32 wrap_angle(s32 angle) {
    angle &= 0xFFF;
    return angle >= 0x800 ? angle - 0x1000 : angle;
}

// Finds the active enemy closest to the aim within the assist cone, as yaw and pitch offsets from the aim.
static bool find_aim_assist_target(s32 eyeX, s32 eyeY, s32 eyeZ, s32 yaw, s32 pitch, s32* yawDiff, s32* pitchDiff) {
    static u8* const tables[] = { D_801FFBE0_1DAFE0, D_801BA0C0_1954C0 };
    static const s32 counts[] = { 18, 4 };
    static const s32 sizes[] = { 0x3C8, 0x5AC };
    s32 best = 0x7FFFFFFF;
    s32 t, i;

    for (t = 0; t < 2; t++) {
        for (i = 0; i < counts[t]; i++) {
            u8* actor = tables[t] + i * sizes[t];
            s32 dx, dy, dz, horizontal, yd, pd, score;

            // Same filter as the focus: byte 6 bit 0 stays clear on some enemies for long stretches.
            if (!(actor[0] & actor[6] & 2)) {
                continue;
            }
            dx = *(s16*)(actor + 0x14) - eyeX;
            dy = actor_center_y(actor, AIM_ASSIST_TARGET_HEIGHT) - eyeY;
            dz = *(s16*)(actor + 0x18) - eyeZ;
            if (dx > AIM_ASSIST_RANGE || dx < -AIM_ASSIST_RANGE || dz > AIM_ASSIST_RANGE || dz < -AIM_ASSIST_RANGE) {
                continue;
            }
            horizontal = func_80032424_D824(dx * dx + dz * dz);
            if (horizontal < 24 || horizontal > AIM_ASSIST_RANGE) {
                continue;
            }

            yd = wrap_angle(func_80032664_DA64(dx, dz) - yaw);
            pd = wrap_angle(func_80032664_DA64(dy, horizontal) - pitch);
            if (yd > AIM_ASSIST_CONE || yd < -AIM_ASSIST_CONE || pd > AIM_ASSIST_CONE * 2 || pd < -AIM_ASSIST_CONE * 2) {
                continue;
            }
            score = (yd < 0 ? -yd : yd) + (pd < 0 ? -pd : pd);
            if (score < best) {
                best = score;
                *yawDiff = yd;
                *pitchDiff = pd;
            }
        }
    }
    return best != 0x7FFFFFFF;
}

// Applies aim assist to this frame's mouse input, already turned into yaw and pitch changes (angle units).
static void apply_aim_assist(s32 eyeX, s32 eyeY, s32 eyeZ, s32 yaw, s32 pitch, float* yawChange, float* pitchChange) {
    s32 yawDiff, pitchDiff;

    if (!find_aim_assist_target(eyeX, eyeY, eyeZ, yaw, pitch, &yawDiff, &pitchDiff)) {
        return;
    }
    // Modern console style: friction grows smoothly toward the target's center instead of switching on at the cone
    // edge, and the pull only follows the player's own input so the reticle never drifts by itself.
    {
        float yawOff = (float)(yawDiff < 0 ? -yawDiff : yawDiff) / AIM_ASSIST_CONE;
        float closeness = 1.0f - (yawOff > 1.0f ? 1.0f : yawOff);
        float friction = 1.0f - (1.0f - AIM_ASSIST_FRICTION) * closeness;
        float input = (*yawChange < 0.0f ? -*yawChange : *yawChange) + (*pitchChange < 0.0f ? -*pitchChange : *pitchChange);
        float pull = AIM_ASSIST_PULL * (input > AIM_ASSIST_INPUT_FULL ? 1.0f : input / AIM_ASSIST_INPUT_FULL);
        *yawChange = *yawChange * friction + yawDiff * pull;
        *pitchChange = *pitchChange * friction + pitchDiff * pull;
    }
}

// Keeps the fractional part of yaw changes so slow mouse movements still add up.
static s32 take_yaw_change(float change) {
    s32 whole;
    sYawRemainder += change;
    whole = (s32)sYawRemainder;
    sYawRemainder -= (float)whole;
    return whole;
}

// Wall avoidance for the free third person camera. func_8003F780 tells whether a point is inside level collision (the
// lock-on uses it to march a point towards its target). The camera position is marched from the target outwards the
// same way and the camera stops just before the first solid point. It snaps in immediately and eases back out.
s32 func_8003F780_1AB80(s16* a, s16* b, void* out, s32 mask);
#define CAMERA_WALL_STEP 12
#define CAMERA_WALL_MARGIN 20
#define CAMERA_WALL_MIN_DISTANCE 40
#define CAMERA_WALL_EASE_OUT 0.15f
#define CAMERA_THIN_OBSTACLE 48 // Obstacles at most this thick between Mega Man and the camera are ignored.
static float sWallSafeDistance = -1.0f;

static void camera_avoid_walls(Vec3i* target, s32 yaw, s32 pitch, s32* distance) {
    s32 sinYaw = func_800324B4_D8B4(yaw);
    s32 cosYaw = func_80032498_D898(yaw);
    s32 sinPitch = func_800324B4_D8B4(pitch);
    s32 cosPitch = func_80032498_D898(pitch);
    s32 limit = *distance;
    s32 t;
    // The collision test reads 8 bytes per point.
    s16 point[4];

    for (t = CAMERA_WALL_MIN_DISTANCE; t <= *distance; t += CAMERA_WALL_STEP) {
        // Same camera position formula as func_80032FC0 below.
        point[0] = (s16)(target->x - ((((t * sinYaw) >> 12) * cosPitch) >> 12));
        point[1] = (s16)(target->y - ((t * sinPitch) >> 12));
        point[2] = (s16)(target->z - ((((t * cosYaw) >> 12) * cosPitch) >> 12));
        point[3] = 0;
        if (func_8003F780_1AB80(point, point, NULL, 0xFFFF) != 0) {
            // A thin obstacle (tree trunk, post) with free space right after it doesn't pull the camera in, as long as
            // the camera's own spot is free: only walls and big blocks do.
            s32 after;
            bool thin = FALSE;
            for (after = t + CAMERA_WALL_STEP; after <= t + CAMERA_THIN_OBSTACLE && after <= *distance; after += CAMERA_WALL_STEP) {
                point[0] = (s16)(target->x - ((((after * sinYaw) >> 12) * cosPitch) >> 12));
                point[1] = (s16)(target->y - ((after * sinPitch) >> 12));
                point[2] = (s16)(target->z - ((((after * cosYaw) >> 12) * cosPitch) >> 12));
                if (func_8003F780_1AB80(point, point, NULL, 0xFFFF) == 0) {
                    thin = TRUE;
                    break;
                }
            }
            if (thin && t + CAMERA_THIN_OBSTACLE < *distance) {
                point[0] = (s16)(target->x - ((((*distance * sinYaw) >> 12) * cosPitch) >> 12));
                point[1] = (s16)(target->y - ((*distance * sinPitch) >> 12));
                point[2] = (s16)(target->z - ((((*distance * cosYaw) >> 12) * cosPitch) >> 12));
                if (func_8003F780_1AB80(point, point, NULL, 0xFFFF) == 0) {
                    t = after;
                    continue;
                }
            }
            limit = t - CAMERA_WALL_MARGIN;
            break;
        }
    }
    if (limit < CAMERA_WALL_MIN_DISTANCE / 2) {
        limit = CAMERA_WALL_MIN_DISTANCE / 2;
    }

    if (sWallSafeDistance < 0.0f || limit < sWallSafeDistance) {
        sWallSafeDistance = (float)limit;
    } else {
        sWallSafeDistance += ((float)limit - sWallSafeDistance) * CAMERA_WALL_EASE_OUT;
    }
    *distance = (s32)sWallSafeDistance;
}

// Keeps the first person eye away from walls so the near plane doesn't cut into them: probes forward, left and right
// at the eye's height and moves the eye back from any solid point closer than FP_WALL_CLEARANCE.
#define FP_WALL_CLEARANCE 60
#define FP_WALL_STEP 4
// The walls Mega Man collides with are axis aligned planes that the segment tests above never report. The game finds
// them per direction with func_8003F334 (position, the actor's box at +0x78, direction): the plane's coordinate, or
// +-32767 when there is none. Directions: 0 = +z, 1 = +x, 2 = -z, 3 = -x.
s32 func_8003F334_1A734(s16* pos, s16* box, s32 dir, s32 flags);
#define FP_WALL_PLANE_MARGIN 42
#define FP_WALL_PLANE_RANGE 300
static float sPlaneCorrX = 0.0f, sPlaneCorrZ = 0.0f; // Applied correction, eased so the eye never snaps.
#define FP_WALL_PLANE_EASE 0.3f
// The direction a plane is reported under doesn't tell which side of it Mega Man is on, so every plane near the eye
// pushes the eye away to the side it already is on.
static s32 plane_push(s32 eye, s32 player, s32 wall) {
    s32 side = eye != wall ? eye - wall : player - wall;
    s32 gap = side < 0 ? -side : side;
    if (gap >= FP_WALL_PLANE_MARGIN || gap > FP_WALL_PLANE_RANGE) {
        return eye;
    }
    return side < 0 ? wall - FP_WALL_PLANE_MARGIN : wall + FP_WALL_PLANE_MARGIN;
}

static void fp_keep_off_wall_planes(s32* eyeX, s32* eyeZ) {
    s32 wantX = *eyeX, wantZ = *eyeZ, dir;
    u8* player = D_802049B0_1DFDB0;
    s32 px = PLAYER_POS_X(player), pz = PLAYER_POS_Z(player);

    for (dir = 0; dir < 4; dir++) {
        s32 wall = func_8003F334_1A734((s16*)(player + 0x14), *(s16**)(player + 0x78), dir, 0);
        if (wall == 32767 || wall == -32767) {
            continue;
        }
        if (dir & 1) {
            wantX = plane_push(wantX, px, wall);
        } else {
            wantZ = plane_push(wantZ, pz, wall);
        }
    }
    sPlaneCorrX += ((float)(wantX - *eyeX) - sPlaneCorrX) * FP_WALL_PLANE_EASE;
    sPlaneCorrZ += ((float)(wantZ - *eyeZ) - sPlaneCorrZ) * FP_WALL_PLANE_EASE;
    *eyeX += (s32)sPlaneCorrX;
    *eyeZ += (s32)sPlaneCorrZ;
}

static void fp_keep_off_walls(s32* eyeX, s32 eyeY, s32 footY, s32* eyeZ, s32 yaw) {
    // Probes all around the camera (8 directions) at four heights from the camera down to Mega Man's knees: some walls
    // are only solid low down, where Mega Man collides with them, while their model reaches up past the camera.
    s32 sinYaw = func_800324B4_D8B4(yaw), cosYaw = func_80032498_D898(yaw);
    s16 point[4]; // The collision test reads 8 bytes per point.
    s32 d, h, t;

    for (d = 0; d < 8; d++) {
        // 45 degree steps: rotate (sin, cos) by d * 45 degrees. 2896 is cos(45 degrees) in 4.12.
        s32 dx = sinYaw, dz = cosYaw, k;
        for (k = 0; k < d; k++) {
            s32 nx = ((dx + dz) * 2896) >> 12, nz = ((dz - dx) * 2896) >> 12;
            dx = nx;
            dz = nz;
        }
        for (h = 0; h < 4; h++) {
            for (t = FP_WALL_STEP; t <= FP_WALL_CLEARANCE; t += FP_WALL_STEP) {
                point[0] = (s16)(*eyeX + ((t * dx) >> 12));
                point[1] = (s16)(eyeY + (footY - 20 - eyeY) * h / 3);
                point[2] = (s16)(*eyeZ + ((t * dz) >> 12));
                point[3] = 0;
                if (func_8003F780_1AB80(point, point, NULL, 0xFFFF) != 0) {
                    s32 push = FP_WALL_CLEARANCE - t + FP_WALL_STEP;
                    *eyeX -= (push * dx) >> 12;
                    *eyeZ -= (push * dz) >> 12;
                    break;
                }
            }
        }
    }
    fp_keep_off_wall_planes(eyeX, eyeZ);
}

// How far behind the head the first person camera can sit: FIRST_PERSON_BACK_OFFSET, shortened when a wall is right
// behind Mega Man so the camera never ends up on the far side of it (same march as the third person wall avoidance).
#define FP_BACK_MARGIN 16
static s32 fp_back_offset(Vec3i* head, s32 yaw, s32 pitch) {
    s32 sinYaw = func_800324B4_D8B4(yaw), cosYaw = func_80032498_D898(yaw);
    s32 sinPitch = func_800324B4_D8B4(pitch), cosPitch = func_80032498_D898(pitch);
    s16 point[4]; // The collision test reads 8 bytes per point.
    s32 t;

    for (t = 4; t <= FIRST_PERSON_BACK_OFFSET + FP_BACK_MARGIN; t += 4) {
        point[0] = (s16)(head->x - ((((t * sinYaw) >> 12) * cosPitch) >> 12));
        point[1] = (s16)(head->y - ((t * sinPitch) >> 12));
        point[2] = (s16)(head->z - ((((t * cosYaw) >> 12) * cosPitch) >> 12));
        point[3] = 0;
        if (func_8003F780_1AB80(point, point, NULL, 0xFFFF) != 0) {
            return t > FP_BACK_MARGIN ? t - FP_BACK_MARGIN : 0;
        }
    }
    return FIRST_PERSON_BACK_OFFSET;
}

// Finds the point the crosshair is on: marches the camera's center ray through level collision (the same point test
// the wall avoidance uses) and checks enemies near the ray, keeping the closest hit, or a far point if nothing is hit.
#define AIM_RAY_STEP 24
#define AIM_RAY_MAX 2400
#define AIM_RAY_ENEMY_RADIUS 50.0f
static float sAimRayCam[3], sAimRayDir[3];
static bool sAimRayValid = FALSE; // the ray of the last gameplay frame (sAimPointValid is cleared each frame first)
// raise: the crosshair sits this angle above the screen center (free aim in third person), 0 at the center.
static void compute_aim_point(Vec3i* target, s32 distance, s32 yaw, s32 pitch, s32 raise) {
    float sinYaw = func_800324B4_D8B4(yaw) / 4096.0f, cosYaw = func_80032498_D898(yaw) / 4096.0f;
    float sinPitch = func_800324B4_D8B4(pitch) / 4096.0f, cosPitch = func_80032498_D898(pitch) / 4096.0f;
    // Direction from the camera toward its target (same formula as func_80032FC0).
    float dir[3] = { sinYaw * cosPitch, sinPitch, cosYaw * cosPitch };
    float cam[3] = { target->x - distance * dir[0], target->y - distance * dir[1], target->z - distance * dir[2] };
    float hit = (float)AIM_RAY_MAX;
    float t;
    s16 point[4];
    u32 l, i;

    if (raise != 0) {
        sinPitch = func_800324B4_D8B4(pitch - raise) / 4096.0f;
        cosPitch = func_80032498_D898(pitch - raise) / 4096.0f;
        dir[0] = sinYaw * cosPitch;
        dir[1] = sinPitch;
        dir[2] = cosYaw * cosPitch;
    }
    for (i = 0; i < 3; i++) {
        sAimRayCam[i] = cam[i];
        sAimRayDir[i] = dir[i];
    }
    sAimOnEnemy = FALSE;

    for (t = (distance > 0 ? distance : 0) + 48.0f; t < hit; t += AIM_RAY_STEP) {
        point[0] = (s16)(cam[0] + t * dir[0]);
        point[1] = (s16)(cam[1] + t * dir[1]);
        point[2] = (s16)(cam[2] + t * dir[2]);
        point[3] = 0;
        if (func_8003F780_1AB80(point, point, NULL, 0xFFFF) != 0) {
            hit = t;
            break;
        }
    }

    // Enemies: 18 actors of 0x3C8 bytes and 4 multi part actors of 0x5AC bytes, active when byte 0 & byte 6 & 1.
    for (l = 0; l < 2; l++) {
        u8* base = l == 0 ? D_801FFBE0_1DAFE0 : D_801BA0C0_1954C0;
        u32 size = l == 0 ? 0x3C8 : 0x5AC;
        u32 count = l == 0 ? 18 : 4;
        for (i = 0; i < count; i++) {
            u8* enemy = base + i * size;
            float ex, ey, ez, te, px, py, pz;
            if ((enemy[0] & enemy[6] & 1) == 0) {
                continue;
            }
            ex = *(s16*)(enemy + 0x14) - cam[0];
            ey = actor_center_y(enemy, AIM_ASSIST_TARGET_HEIGHT) - cam[1];
            ez = *(s16*)(enemy + 0x18) - cam[2];
            te = ex * dir[0] + ey * dir[1] + ez * dir[2];
            if (te <= 0.0f || te >= hit) {
                continue;
            }
            px = ex - te * dir[0];
            py = ey - te * dir[1];
            pz = ez - te * dir[2];
            if (px * px + py * py + pz * pz < AIM_RAY_ENEMY_RADIUS * AIM_RAY_ENEMY_RADIUS) {
                hit = te;
                sAimOnEnemy = TRUE;
            }
        }
    }

    sAimPoint[0] = cam[0] + hit * dir[0];
    sAimPoint[1] = cam[1] + hit * dir[1];
    sAimPoint[2] = cam[2] + hit * dir[2];
    sAimPointValid = TRUE;
}

// Lock-on: an enemy near the crosshair's line wins (generous radius, so what the player points at gets locked even off
// the exact center), within the buster's reach and in sight. Same filter as focus_pick_side.
#define FOCUS_RAY_RADIUS 160.0f
#define FOCUS_RAY_SLOPE 0.12f // Far away the radius grows with distance (~7 degrees), or far enemies are hard to pick.
static u8* focus_pick_ray(u8* player) {
    u8* best = NULL;
    float bestD2 = 1.0f; // Squared distance from the line over the squared radius.
    u32 l, i;
    if (!sAimRayValid) {
        return NULL;
    }
    for (l = 0; l < 2; l++) {
        u8* base = l == 0 ? D_801FFBE0_1DAFE0 : D_801BA0C0_1954C0;
        u32 size = l == 0 ? 0x3C8 : 0x5AC;
        u32 count = l == 0 ? 18 : 4;
        for (i = 0; i < count; i++) {
            u8* actor = base + i * size;
            s32 hx, hz;
            float ex, ey, ez, te, px, py, pz, d2, radius;
            if ((actor[0] & actor[6] & 2) == 0 || (l == 0 && *(u32*)(actor + 0x10) != 0)) {
                continue;
            }
            hx = *(s16*)(actor + 0x14) - PLAYER_POS_X(player);
            hz = *(s16*)(actor + 0x18) - PLAYER_POS_Z(player);
            if (hx * hx + hz * hz >= focus_reach() * focus_reach()) {
                continue;
            }
            ex = *(s16*)(actor + 0x14) - sAimRayCam[0];
            ey = actor_center_y(actor, FOCUS_TARGET_HEIGHT) - sAimRayCam[1];
            ez = *(s16*)(actor + 0x18) - sAimRayCam[2];
            te = ex * sAimRayDir[0] + ey * sAimRayDir[1] + ez * sAimRayDir[2];
            if (te <= 0.0f) {
                continue;
            }
            px = ex - te * sAimRayDir[0];
            py = ey - te * sAimRayDir[1];
            pz = ez - te * sAimRayDir[2];
            radius = te * FOCUS_RAY_SLOPE > FOCUS_RAY_RADIUS ? te * FOCUS_RAY_SLOPE : FOCUS_RAY_RADIUS;
            d2 = (px * px + py * py + pz * pz) / (radius * radius);
            if (d2 < bestD2 && focus_line_clear(player, actor)) {
                bestD2 = d2;
                best = actor;
            }
        }
    }
    return best;
}

// First person lock-on aim correction (stick offset from the target, ~13 degrees each way at 0x90).
s32 gFocusAdjust = 0x90;          // live
float gFocusAdjustSens = 0.6f;    // live: share of the stick's turn that moves the offset
static u8* sFocusAdjustFor;       // target the offset belongs to (NULL = not locked in first person)
static float sFocusOffYaw, sFocusOffPitch, sFocusBasePitch;
static s32 sFocusBaseYaw;         // Mega Man's yaw facing the target, eased like before; the offset adds to it

// R3 / F toggles the lock-on, in either view; it ends when the target dies or leaves the buster's reach.
static void focus_handle_presses(u8* player, s32 cameraYaw) {
    s32 presses = recomp_get_focus_presses();
    while (presses-- > 0) {
        if (sFocusTarget != NULL) {
            sFocusTarget = NULL;
        } else {
            sFocusTarget = focus_pick_ray(player);
            if (sFocusTarget == NULL) {
                sFocusTarget = focus_pick(player, cameraYaw);
            }
        }
    }
    if (!focus_target_valid(player)) {
        sFocusTarget = NULL;
    }
}

// A right stick flick while locked switches to the nearest enemy on that side; returns the yaw to the target.
static s32 focus_flick(u8* player, float yawChange) {
    if (sFocusSwitchCooldown > 0) {
        sFocusSwitchCooldown--;
    } else if (yawChange > FOCUS_SWITCH_STICK || yawChange < -FOCUS_SWITCH_STICK) {
        u8* next = focus_pick_side(player, func_80032664_DA64(*(s16*)(sFocusTarget + 0x14) - PLAYER_POS_X(player),
                                                              *(s16*)(sFocusTarget + 0x18) - PLAYER_POS_Z(player)),
                                   yawChange > 0.0f ? 1 : -1);
        if (next != NULL) {
            sFocusTarget = next;
        }
        sFocusSwitchCooldown = 12;
    }
    return func_80032664_DA64(*(s16*)(sFocusTarget + 0x14) - PLAYER_POS_X(player),
                              *(s16*)(sFocusTarget + 0x18) - PLAYER_POS_Z(player));
}

// Conversations whose framing is part of the scene (Data, the monkey, type 0x11, with his save and heal menu) keep
// the game's own camera in first person too.
#define TALK_GAME_CAMERA_TYPE 0x11
static u8* sTalkPartner;
// What the interaction button would pick right now (set every frame by the prompt update while Mega Man is free to
// move). When a conversation starts, that is the NPC or object being talked to, whatever the camera was looking at.
static u8* sPendingTarget = NULL;
static bool sPendingIsNpc = FALSE;
static s16 sPendingPos[3];
static bool sTalkIsNpc = FALSE;
static bool sTalkLocked = FALSE; // The partner came from the interaction target, no facing based pick needed.
static s16 sTalkPos[3];

static bool in_npc_list(u8* a) {
    return (u32)a >= 0x801FFBE0 && (u32)a < 0x801FFBE0 + 18 * 0x3C8;
}

static bool talk_uses_game_camera(void) {
    u8* who = sTalkPartner != NULL ? sTalkPartner : sPendingTarget;
    return who != NULL && in_npc_list(who) && who[2] == TALK_GAME_CAMERA_TYPE;
}

// First person conversations: the view turns to the NPC being talked to and tilts to its head, easing in, instead of
// staying wherever it pointed when the talk started. The partner is the nearest NPC (an actor with a talk handler).
#define TALK_FRAME_EASE 0.12f
#define TALK_LOOK_EASE 0.05f   // How fast the look point follows the NPC's head (smooths its animations).
#define TALK_PICK_FRAMES 20    // The partner is chosen over this many frames, then kept for the conversation.
static s32 sTalkFrames = 0;
static float sTalkLook[3];
#define TALK_PARTNER_RANGE 400
#define TALK_PENDING_RANGE 700 // An interaction target farther than this is stale (a scripted talk), ignore it.
#define TALK_PARTNER_ANGLE 0x180 // Only NPCs within ~34 degrees of where Mega Man faces (not one beside a sign or car).
static float sTalkYaw = 0.0f;

static s32 actor_top_y(u8* actor, s32 fallbackHeight) {
    s32 x = *(s16*)(actor + 0x14), z = *(s16*)(actor + 0x18), top = *(s16*)(actor + 0x16) - fallbackHeight, j;
    for (j = 0; j < ACTOR_JOINTS_MAX; j++) {
        s16* joint = (s16*)(actor + ACTOR_JOINTS_OFFSET + j * 8);
        if (joint[0] == 0 && joint[2] == 0) {
            break;
        }
        if (joint[0] - x > 600 || x - joint[0] > 600 || joint[2] - z > 600 || z - joint[2] > 600) {
            continue;
        }
        if (joint[1] < top) {
            top = joint[1]; // +Y is down: the highest joint is the head.
        }
    }
    return top;
}

static void fp_frame_talk_partner(u8* player, s32 eyeX, s32 eyeY, s32 eyeZ, s32* yaw) {
    s32 horizontal, wantedYaw, wantedPitch, yawDiff;
    float dx, dy, dz;
    if (!(player[0x8] == 1 && player[0x9] == 0x10)) {
        sTalkPartner = NULL;
        sTalkFrames = 0;
        return;
    }
    sTalkFrames++;
    if (sTalkFrames == 1 && sPendingTarget != NULL &&
        (sPendingPos[0] - eyeX) * (sPendingPos[0] - eyeX) + (sPendingPos[2] - eyeZ) * (sPendingPos[2] - eyeZ) <
            TALK_PENDING_RANGE * TALK_PENDING_RANGE) {
        // The first frame of the conversation: lock on what the interaction button picked (NPC or object).
        sTalkPartner = sPendingTarget;
        sTalkLocked = TRUE;
        sTalkIsNpc = sPendingIsNpc;
        sTalkPos[0] = sPendingPos[0];
        sTalkPos[1] = sPendingPos[1];
        sTalkPos[2] = sPendingPos[2];
        sTalkYaw = (float)*yaw;
        sTalkLook[0] = sPendingPos[0];
        sTalkLook[1] = sTalkIsNpc ? actor_top_y(sTalkPartner, 100) + 10 : sPendingPos[1] - 60;
        sTalkLook[2] = sPendingPos[2];
    } else if (sTalkFrames == 1) {
        sTalkPartner = NULL;
        sTalkLocked = FALSE;
        sTalkIsNpc = TRUE;
    }
    // The partner is the NPC Mega Man faces: the game turns him toward whoever he talks to, so it's picked over the
    // first frames of the conversation and then kept, so NPCs walking past don't steal the view.
    if (!sTalkLocked && (sTalkFrames <= TALK_PICK_FRAMES || sTalkPartner == NULL || !(sTalkPartner[0] & sTalkPartner[6] & 2))) {
        s32 facing = (PLAYER_YAW(player) + 0x800) & 0xFFF;
        s32 bestScore = 0x7FFFFFFF, i;
        u8* best = NULL;
        for (i = 0; i < 18 && sTalkFrames <= TALK_PICK_FRAMES; i++) {
            u8* actor = (u8*)(0x801FFBE0 + i * 0x3C8);
            s32 ax, az, angle, score;
            if (!(actor[0] & actor[6] & 2) || *(u32*)(actor + 0x10) == 0) {
                continue;
            }
            ax = *(s16*)(actor + 0x14) - eyeX;
            az = *(s16*)(actor + 0x18) - eyeZ;
            if (ax * ax + az * az > TALK_PARTNER_RANGE * TALK_PARTNER_RANGE) {
                continue;
            }
            angle = wrap_angle(func_80032664_DA64(ax, az) - facing);
            if (angle > TALK_PARTNER_ANGLE || angle < -TALK_PARTNER_ANGLE) {
                continue;
            }
            score = (angle < 0 ? -angle : angle) + (actor == sTalkPartner ? 0 : 0x80); // Stick with the current one.
            if (score < bestScore) {
                bestScore = score;
                best = actor;
            }
        }
        if (best == NULL) {
            if (sTalkPartner == NULL || !(sTalkPartner[0] & sTalkPartner[6] & 2)) {
                sTalkPartner = NULL;
                return;
            }
        } else if (best != sTalkPartner) {
            if (sTalkPartner == NULL) {
                sTalkYaw = (float)*yaw;
            }
            sTalkPartner = best;
            sTalkLook[0] = *(s16*)(best + 0x14);
            sTalkLook[1] = actor_top_y(best, 100) + 10;
            sTalkLook[2] = *(s16*)(best + 0x18);
        }
    }
    // Look at a softly trailing point on the NPC's head, so its idle and talking animations don't drag the view along.
    if (sTalkPartner == NULL) {
        return;
    }
    if (sTalkIsNpc) {
        sTalkLook[0] += (*(s16*)(sTalkPartner + 0x14) - sTalkLook[0]) * TALK_LOOK_EASE;
        sTalkLook[1] += (actor_top_y(sTalkPartner, 100) + 10 - sTalkLook[1]) * TALK_LOOK_EASE;
        sTalkLook[2] += (*(s16*)(sTalkPartner + 0x18) - sTalkLook[2]) * TALK_LOOK_EASE;
    } else {
        // Objects and doors don't move: the point recorded when the interaction started, a little above its base.
        sTalkLook[0] += (sTalkPos[0] - sTalkLook[0]) * TALK_LOOK_EASE;
        sTalkLook[1] += (sTalkPos[1] - 60 - sTalkLook[1]) * TALK_LOOK_EASE;
        sTalkLook[2] += (sTalkPos[2] - sTalkLook[2]) * TALK_LOOK_EASE;
    }
    dx = sTalkLook[0] - eyeX;
    dy = sTalkLook[1] - eyeY;
    dz = sTalkLook[2] - eyeZ;
    horizontal = (s32)__builtin_sqrtf(dx * dx + dz * dz);
    if (horizontal < 16) {
        *yaw = ((s32)sTalkYaw) & 0xFFF;
        return;
    }
    wantedYaw = func_80032664_DA64((s32)dx, (s32)dz);
    wantedPitch = wrap_angle(func_80032664_DA64((s32)dy, horizontal));
    yawDiff = wrap_angle(wantedYaw - (s32)sTalkYaw);
    sTalkYaw += yawDiff * TALK_FRAME_EASE;
    *yaw = ((s32)sTalkYaw) & 0xFFF;
    sFirstPersonPitch += (clamp_f32((float)wantedPitch, -FIRST_PERSON_PITCH_MAX, FIRST_PERSON_PITCH_MAX) - sFirstPersonPitch) *
                         TALK_FRAME_EASE;
}

// Debug warp (tools/re/warp.py): asks for a scene change exactly as a door does (func_8005D4CC): scene id, entrance and the
// 0x80 flag, then the game's own transition request, which tears the old scene down before the new overlay loads.
s32 gDbgWarpScene = -1, gDbgWarpEntrance = 0;
void func_8003E1B8_195B8(void);
void func_800292E8_46E8(void* callback);
extern u8 D_801BC380_197780[]; // +0xB8 scene id, +0xB9 entrance, +0xEA transition flag
static void debug_warp_check(void) {
    if (gDbgWarpScene >= 0) {
        D_801BC380_197780[0xB8] = (u8)gDbgWarpScene;
        D_801BC380_197780[0xB9] = (u8)gDbgWarpEntrance;
        D_801BC380_197780[0xEA] = 0x80;
        gDbgWarpScene = -1;
        func_800292E8_46E8((void*)func_8003E1B8_195B8);
    }
}

// Called for the main gameplay camera only. Can rewrite the parameters passed to the camera builder.
static void mouse_camera_update(Vec3i* target, s32* distance, s32* yaw, s32* pitch) {
    GameCamera* cam = &D_80204348_1DF748;
    u8* player = D_802049B0_1DFDB0;
    s32 mode = gDbgForceMode >= 0 ? gDbgForceMode : recomp_get_mouse_camera_mode();
    debug_warp_check(); // game logic phase: the transition request must run here, not while the display list is built
    bool armed = PLAYER_COMBAT_STATE(player) != 0;
    float dx = 0.0f;
    float dy = 0.0f;
    float yawChange;
    float pitchChange;
    s32 wheel;

    sFirstPersonActive = FALSE;
    sFreeCameraActive = FALSE;
    sAiming = FALSE;
    sFpAimHeld = FALSE;
    sAimFovZoom = 1.0f;
    sAimRayValid = sAimPointValid;
    sAimPointValid = FALSE;
    sCrosshairRaise = 0.0f;
    sMouseCameraFrames = 0;

    // Always consume the input so none of it is applied late once Mega Man can move again.
    recomp_get_mouse_camera_deltas(&dx, &dy);
    wheel = recomp_get_mouse_camera_wheel();

    // First person stays on while the game walks Mega Man through a door (input locked, camera not scripted), so the
    // view doesn't jump to third person for the entrance; the look input is ignored meanwhile.
    // Likewise in a conversation (even one the game runs a scripted camera for, like the monkey's): the view stays in
    // first person and turns to the NPC (fp_frame_talk_partner).
    // This covers every interaction (doors, objects like cars, NPCs): first person stays on whenever the game would
    // hand the camera to an interaction or actor tracking camera, as long as it isn't a scripted cutscene.
    if (mode == MOUSE_CAMERA_FIRST_PERSON && cam->enabled && !cam->scripted && !mouse_camera_player_controllable() &&
        !(player[0x8] == 1 && player[0x9] == 0x10 && talk_uses_game_camera())) {
        dx = 0.0f;
        dy = 0.0f;
        wheel = 0;
    } else if (mode == MOUSE_CAMERA_OFF || !mouse_camera_player_controllable()) {
        aim_player_shots(FALSE, 0);
        sLastMode = MOUSE_CAMERA_OFF;
        return;
    }

    sMouseCameraFrames = 2;
    yawChange = dx * MOUSE_CAMERA_YAW_SCALE;
    pitchChange = dy * MOUSE_CAMERA_PITCH_SCALE;

    if (mode == MOUSE_CAMERA_FIRST_PERSON) {
        s32 eyeX = PLAYER_POS_X(player);
        s32 eyeY = PLAYER_POS_Y(player) - FIRST_PERSON_EYE_HEIGHT;
        s32 eyeZ = PLAYER_POS_Z(player);

        bool fpAimHeld = recomp_get_mouse_camera_aim() != 0;
        sFpAimHeld = fpAimHeld || gDbgForceAim;
        float fpBlend;

        // L2 in first person: zoom in (narrower view) and slow the look down for precise aiming, and pull the
        // viewmodel camera back and down so Mega Man's arm and buster come into view.
        sFpAimBlend += ((fpAimHeld ? 1.0f : 0.0f) - sFpAimBlend) * AIM_BLEND_SPEED;
        if (sFpAimBlend < 0.001f) {
            sFpAimBlend = 0.0f;
        }
        fpBlend = sFpAimBlend * sFpAimBlend * (3.0f - 2.0f * sFpAimBlend);
        yawChange *= 1.0f - FP_AIM_SLOWDOWN * fpBlend;
        pitchChange *= 1.0f - FP_AIM_SLOWDOWN * fpBlend;
        sAimFovZoom = 1.0f + FP_AIM_FOV_ZOOM * fpBlend;

        focus_handle_presses(player, PLAYER_YAW(player) + 0x800);
        if (sFocusTarget != NULL && !sFpAimHeld) {
            // Locked on: the view (Mega Man, tank controls) follows the target and the stick moves the aim around it
            // within gFocusAdjust; pushing hard past that edge switches target (focus_flick). Aiming frees it again.
            float offYaw, offPitch, wanted;
            s32 toTarget, dx, dz, dy;
            if (sFocusAdjustFor != sFocusTarget) {
                sFocusAdjustFor = sFocusTarget;
                sFocusOffYaw = sFocusOffPitch = 0.0f;
                sFocusBaseYaw = PLAYER_YAW(player);
                sFocusBasePitch = sFirstPersonPitch;
            }
            offYaw = sFocusOffYaw + yawChange * gFocusAdjustSens;
            offPitch = sFocusOffPitch + pitchChange * gFocusAdjustSens;
            toTarget = focus_flick(player, (offYaw > gFocusAdjust || offYaw < -gFocusAdjust) ? yawChange : 0.0f);
            if (sFocusAdjustFor != sFocusTarget) { // switched: aim at the new one's center
                sFocusAdjustFor = sFocusTarget;
                offYaw = offPitch = 0.0f;
            }
            sFocusOffYaw = clamp_f32(offYaw, (float)-gFocusAdjust, (float)gFocusAdjust);
            sFocusOffPitch = clamp_f32(offPitch, (float)-gFocusAdjust, (float)gFocusAdjust);
            dx = *(s16*)(sFocusTarget + 0x14) - eyeX;
            dz = *(s16*)(sFocusTarget + 0x18) - eyeZ;
            dy = actor_center_y(sFocusTarget, FOCUS_TARGET_HEIGHT) - eyeY;
            wanted = (float)wrap_angle(func_80032664_DA64(dy, func_80032424_D824(dx * dx + dz * dz)));
            sFocusBaseYaw = (sFocusBaseYaw + (s32)(wrap_angle(toTarget - 0x800 - sFocusBaseYaw) * FOCUS_TURN)) & 0xFFF;
            sFocusBasePitch += (wanted - sFocusBasePitch) * FOCUS_PITCH_TURN;
            PLAYER_YAW(player) = (sFocusBaseYaw + (s32)sFocusOffYaw) & 0xFFF;
            sFirstPersonPitch = sFocusBasePitch + sFocusOffPitch;
            yawChange = 0.0f;
            pitchChange = 0.0f;
        } else {
            sFocusAdjustFor = NULL;
            if (armed) {
                apply_aim_assist(eyeX, eyeY, eyeZ, PLAYER_YAW(player) + 0x800, (s32)sFirstPersonPitch, &yawChange,
                                 &pitchChange);
            }
        }

        // Mega Man uses tank controls, so turning him turns the camera as well.
        PLAYER_YAW(player) = (PLAYER_YAW(player) + take_yaw_change(yawChange)) & 0xFFF;
        // Stay locked behind Mega Man instead of the game's lazy follow so the mouse feels direct. The game's own
        // yaw is kept in sync so its follow logic doesn't swing the camera around when the mouse camera is turned off.
        cam->yaw = PLAYER_YAW(player) + 0x800;
        *yaw = cam->yaw + cam->yawOffset;

        sFirstPersonPitch = clamp_f32(sFirstPersonPitch + pitchChange, -FIRST_PERSON_PITCH_MAX, FIRST_PERSON_PITCH_MAX);
        fp_frame_talk_partner(player, eyeX, eyeY, eyeZ, yaw);
        target->x = eyeX;
        target->y = eyeY;
        target->z = eyeZ;
        // A positive distance puts the camera behind the target, looking the same way.
        *pitch = (s32)sFirstPersonPitch;
        *distance = fp_back_offset(target, *yaw, *pitch);
        {
            // Keep the camera itself (behind the head) clear of walls, moving the whole view sideways if needed.
            s32 camX = target->x - ((*distance * func_800324B4_D8B4(*yaw)) >> 12);
            s32 camZ = target->z - ((*distance * func_80032498_D898(*yaw)) >> 12);
            s32 movedX = camX, movedZ = camZ;
            fp_keep_off_walls(&movedX, eyeY, PLAYER_POS_Y(player), &movedZ, *yaw);
            target->x += movedX - camX;
            target->z += movedZ - camZ;
        }
        sFirstPersonActive = TRUE;
        compute_aim_point(target, *distance, *yaw, *pitch, 0);
        aim_player_shots(TRUE, *pitch);

        {
            Vec3i eyePos;
            float back = FP_AIM_VIEWMODEL_BACK * fpBlend;
            eyePos.x = eyeX - (s32)(back * func_800324B4_D8B4(cam->yaw) / 4096.0f);
            eyePos.y = eyeY;
            eyePos.z = eyeZ - (s32)(back * func_80032498_D898(cam->yaw) / 4096.0f);
            build_view_at(&eyePos, cam->yaw, (s32)(FP_AIM_VIEWMODEL_PITCH * fpBlend), &sViewmodelView);
        }
    } else {
        // Start orbiting from behind Mega Man when entering third person.
        if (sLastMode != MOUSE_CAMERA_THIRD_PERSON) {
            sOrbitYaw = (PLAYER_YAW(player) + 0x800) & 0xFFF;
            sAimPitch = 0.0f;
        }
        sFreeCameraActive = TRUE;
        {
            bool aimHeld = recomp_get_mouse_camera_aim() != 0 || gDbgForceAim;
            Vec3i orbitTarget;
            Vec3i aimTarget;
            s32 orbitPitch, orbitDistance, aimDistance;
            s32 sinYaw, cosYaw;
            float blend;

            // L2 works everywhere: with the buster out it aims, elsewhere (towns, where Mega Man can't shoot) it's a
            // look mode that zooms in over the shoulder to take in the scenery, with slower, smoother turning.
            // Mega Man faces where the camera looks in both (mouse_camera_player_input).
            sAiming = aimHeld;
            focus_handle_presses(player, sOrbitYaw);
            if (sFocusTarget != NULL && !aimHeld) {
                // Turn the camera to look past Mega Man at the target; the stick still tilts it up and down.
                s32 toTarget = focus_flick(player, yawChange);
                sOrbitYaw = (sOrbitYaw + (s32)(wrap_angle(toTarget - sOrbitYaw) * FOCUS_TURN)) & 0xFFF;
                yawChange = 0.0f;
                // Tilt to frame the target: look from Mega Man's head height toward the enemy's center.
                {
                    s32 dx = *(s16*)(sFocusTarget + 0x14) - PLAYER_POS_X(player);
                    s32 dz = *(s16*)(sFocusTarget + 0x18) - PLAYER_POS_Z(player);
                    s32 dy = actor_center_y(sFocusTarget, FOCUS_TARGET_HEIGHT) - (PLAYER_POS_Y(player) - AIM_HEIGHT);
                    s32 horizontal = func_80032424_D824(dx * dx + dz * dz);
                    // +Y is down and positive pitch looks down, so the angle of (dy, horizontal) is the pitch.
                    s32 wanted = wrap_angle(func_80032664_DA64(dy, horizontal)) + 0x60; // A little from above.
                    s32 base = *pitch;
                    float wantedOffset = clamp_f32((float)(wanted - base), THIRD_PERSON_PITCH_OFFSET_MIN,
                                                   THIRD_PERSON_PITCH_OFFSET_MAX);
                    sThirdPersonPitchOffset += (wantedOffset - sThirdPersonPitchOffset) * FOCUS_PITCH_TURN;
                    pitchChange = 0.0f;
                }
            }
            orbitTarget = *target;
            sAimBlend += ((aimHeld ? 1.0f : 0.0f) - sAimBlend) * AIM_BLEND_SPEED;
            if (sAimBlend < 0.001f) {
                sAimBlend = 0.0f;
            } else if (sAimBlend > 0.999f) {
                sAimBlend = 1.0f;
            }

            if (aimHeld) {
                if (armed) {
                    // Slower, more precise turning while aiming, like modern third person shooters' ADS multiplier.
                    yawChange *= AIM_TURN_SCALE;
                    pitchChange *= AIM_TURN_SCALE;
                    apply_aim_assist(D_802043E0_1DF7E0.x, D_802043E0_1DF7E0.y, D_802043E0_1DF7E0.z, sOrbitYaw,
                                     (s32)sAimPitch, &yawChange, &pitchChange);
                } else {
                    yawChange *= LOOK_TURN_SCALE;
                    pitchChange *= LOOK_TURN_SCALE;
                }
                sOrbitYaw = (sOrbitYaw + take_yaw_change(yawChange)) & 0xFFF;
                sAimPitch = clamp_f32(sAimPitch + pitchChange, -FIRST_PERSON_PITCH_MAX, FIRST_PERSON_PITCH_MAX);
            } else {
                sOrbitYaw = (sOrbitYaw + take_yaw_change(yawChange)) & 0xFFF;
                sThirdPersonPitchOffset = clamp_f32(sThirdPersonPitchOffset + pitchChange,
                                                    THIRD_PERSON_PITCH_OFFSET_MIN, THIRD_PERSON_PITCH_OFFSET_MAX);
                // Scrolling up zooms in.
                sZoom = clamp_f32(sZoom - wheel * ZOOM_STEP, ZOOM_MIN, ZOOM_MAX);
                // Each Camera Distance press moves to the next preset further out, wrapping back to the closest.
                {
                    s32 presses = recomp_get_camera_distance_presses();
                    while (presses-- > 0) {
                        u32 i;
                        float next = sZoomPresets[0];
                        for (i = 0; i < sizeof(sZoomPresets) / sizeof(sZoomPresets[0]); i++) {
                            if (sZoomPresets[i] > sZoom + 0.01f) {
                                next = sZoomPresets[i];
                                break;
                            }
                        }
                        sZoom = next;
                    }
                }
            }

            // Orbit camera, with the sprint tuck and footstep bob.
            orbitPitch = clamp_s32(*pitch + (s32)sThirdPersonPitchOffset, THIRD_PERSON_PITCH_MIN, THIRD_PERSON_PITCH_MAX);
            orbitDistance = (s32)(*distance * sZoom * (1.0f - SPRINT_CLOSER * gSprintAmount));
            if (gSprintAmount > 0.0f) {
                sSprintBobPhase = (sSprintBobPhase + SPRINT_BOB_SPEED) & 0xFFF;
                orbitTarget.y += (s32)(gSprintAmount * (SPRINT_LOWER + SPRINT_BOB_HEIGHT *
                                                        (float)func_800324B4_D8B4(sSprintBobPhase) / 4096.0f));
            }

            // Over the shoulder camera: close behind the right shoulder. The camera's right is (cos, 0, -sin).
            sinYaw = func_800324B4_D8B4(sOrbitYaw);
            cosYaw = func_80032498_D898(sOrbitYaw);
            aimTarget.x = PLAYER_POS_X(player) + ((AIM_SIDE * cosYaw) >> 12);
            aimTarget.y = PLAYER_POS_Y(player) - AIM_HEIGHT;
            aimTarget.z = PLAYER_POS_Z(player) - ((AIM_SIDE * sinYaw) >> 12);
            aimDistance = armed ? AIM_DISTANCE : LOOK_DISTANCE;

            // Ease between the two with a smoothstep so entering and leaving L2 glides instead of cutting.
            blend = sAimBlend * sAimBlend * (3.0f - 2.0f * sAimBlend);
            target->x = orbitTarget.x + (s32)((aimTarget.x - orbitTarget.x) * blend);
            target->y = orbitTarget.y + (s32)((aimTarget.y - orbitTarget.y) * blend);
            target->z = orbitTarget.z + (s32)((aimTarget.z - orbitTarget.z) * blend);
            *distance = orbitDistance + (s32)((aimDistance - orbitDistance) * blend);
            *pitch = orbitPitch + (s32)(((s32)sAimPitch - orbitPitch) * blend);
            // Narrower view while L2 is held: a little when aiming, a real zoom in look mode.
            sAimFovZoom = 1.0f + blend * (armed ? AIM_FOV_ZOOM : LOOK_FOV_ZOOM);
            // Free aim crosshair, gliding to the center as the aim camera comes in.
            if (sFocusTarget == NULL) {
                sCrosshairRaise = gFreeAimRaise * (1.0f - blend);
            }
        }

        cam->yaw = sOrbitYaw;
        *yaw = sOrbitYaw + cam->yawOffset;
        camera_avoid_walls(target, *yaw, *pitch, distance);
        // Locked on without aiming, shots keep rising or dropping toward the target (aim_player_shots).
        if (armed && (sAiming || sFocusTarget == NULL)) {
            s32 raise = sProjYScale > 0.0f ? func_80032664_DA64((s32)(sCrosshairRaise * 4096.0f),
                                                                (s32)(sProjYScale * 4096.0f)) : 0;
            compute_aim_point(target, *distance, *yaw, *pitch, raise);
        }
        // After compute_aim_point: new shots need this frame's crosshair point to take its heading and slope.
        aim_player_shots(armed && (sAiming || sFocusTarget == NULL), *pitch);
    }

    sLastMode = mode;
}

// Called by the us.rev1.toml hook in Mega Man's update, right after it copies the controller into his own pad
// (0x11C held, 0x11E pressed, PSX style bits: 0x10 up, 0x20 right, 0x40 down, 0x80 left, 0x400/0x800 L1/R1 strafe).
// With the free third person camera, the direction pressed is relative to the camera: Mega Man turns towards it
// and walks forward with his own tank controls, keeping the game's collision and animations.
// Interaction prompts, in the spirit of modern Nintendo games: small icons float over everything Mega Man can interact
// with nearby, each shaped for what it does, and the one Circle would use right now is bigger, brighter and bobs.
// Interactables are the entries of the four lists func_8006C6D8 searches (NPCs, boss parts, objects and static actors)
// that are active ((flags & 3) == 3) and have an interaction handler (+0x10). That function is also run every frame with
// the button faked as held, and everything it touches is restored, to learn which one is the active target.
s32 func_8006C6D8_47AD8(u8* player);
extern s16 D_801BC65C_197A5C; // Player yaw sine, written by the search.
extern s16 D_8020D9F4_1E8DF4; // Player yaw cosine, written by the search.

typedef enum {
    PROMPT_TALK,  // NPCs and boss parts: speech bubble.
    PROMPT_USE,   // Objects: diamond.
    PROMPT_ENTER, // Static actors (doors, signs and the like): up arrow.
    PROMPT_FOCUS, // Target focus reticle.
} PromptKind;

typedef struct {
    u32 base;
    u32 size;
    u32 count;
    PromptKind kind;
} PromptList;

static const PromptList sPromptLists[] = {
    { 0x801FFBE0, 0x3C8, 18, PROMPT_TALK },
    { 0x801BA0C0, 0x5AC, 4, PROMPT_TALK },
    { 0x8020DF00, 0x118, 32, PROMPT_USE },
    { 0x80206B70, 0x5C, 32, PROMPT_ENTER },
};

#define PROMPT_MAX 10
// RT64 matrix group id for a prompt, keyed by its target. Group 0x8 is unused by the game's own tags (tag_helper.h).
#define PROMPT_TAG(addr) ((u32)(0x8u << 28) | ((addr) & 0x0FFFFFFFu))
// Interactables closer than this (world units, horizontally) get an icon. Icons fade in over the outer part.
#define PROMPT_RADIUS 650
#define PROMPT_FADE_START 450
// How far above the target's origin the icon floats (-Y is up).
#define PROMPT_HEIGHT 110

typedef struct {
    s16 pos[3];
    u8 kind;
    u8 active;
    u32 tag;
    u16 distance; // Horizontal distance from Mega Man.
    u8 read;      // Talk prompts: Mega Man already talked to this NPC since it appeared.
} Prompt;

// NPCs Mega Man has talked to, so their bubble can show the conversation as read. An NPC is identified by its slot,
// its type and its talk handler; the entry is forgotten when any of those changes (new NPC in the slot, area change or
// the game swapping in a new conversation).
// shortcut: only tracks talking this session, not the game's own story flags; read those once their layout is known.
#define TALKED_MAX 32
typedef struct {
    u32 entry;
    u32 handler;
    u8 type;
} TalkedNpc;
static TalkedNpc sTalked[TALKED_MAX];
static u32 sTalkedNext = 0;

static bool npc_talked(u8* entry) {
    u32 i;
    for (i = 0; i < TALKED_MAX; i++) {
        if (sTalked[i].entry == (u32)entry && sTalked[i].handler == *(u32*)(entry + 0x10) &&
            sTalked[i].type == entry[2]) {
            return TRUE;
        }
    }
    return FALSE;
}

static void npc_mark_talked(u8* entry) {
    if (npc_talked(entry)) {
        return;
    }
    sTalked[sTalkedNext].entry = (u32)entry;
    sTalked[sTalkedNext].handler = *(u32*)(entry + 0x10);
    sTalked[sTalkedNext].type = entry[2];
    sTalkedNext = (sTalkedNext + 1) % TALKED_MAX;
}

static volatile s32 sPromptDrawsSinceUpdate = 0; // Draws since the last prompt update; the update stops while paused.
static Prompt sPrompts[2][PROMPT_MAX];
static volatile s32 sPromptCount[2];
static volatile s32 sPromptRead = 0; // Buffer the renderer reads; the game thread fills the other one.

static s16* prompt_target_pos(u8* target, PromptKind kind) {
    if (kind == PROMPT_ENTER) {
        return (s16*)(target + ((target[0] & 4) ? 0x38 : 0x1C));
    }
    return (s16*)(target + 0x14);
}

static void interact_prompt_update(void) {
    u8* player = D_802049B0_1DFDB0;
    sPromptDrawsSinceUpdate = 0;
    u16* held = (u16*)(player + 0x11C);
    u16 savedHeld = *held;
    s16 savedScore = *(s16*)(player + 0x182);
    u32 savedTarget = *(u32*)(player + 0x184);
    s16 savedSin = D_801BC65C_197A5C;
    s16 savedCos = D_8020D9F4_1E8DF4;
    u8* activeTarget = NULL;
    s32 write = sPromptRead ^ 1;
    Prompt* out = sPrompts[write];
    s32 count = 0;
    u32 l, i;

    // A real interaction leaves its target in player+0x184 (the search clears it whenever the button isn't held).
    if (savedTarget >= sPromptLists[0].base && savedTarget < sPromptLists[0].base + sPromptLists[0].size * sPromptLists[0].count) {
        npc_mark_talked((u8*)savedTarget);
    }

    if ((PLAYER_FLAGS(player) & PLAYER_ACTIVE(player) & 1) == 0 || (PLAYER_INPUT_FLAGS(player) & 0x60) != 0) {
        sPromptCount[write] = 0;
        sPromptRead = write;
        return;
    }

    *held |= *(u16*)(player + 0x136);
    if (func_8006C6D8_47AD8(player) != 0) {
        activeTarget = *(u8**)(player + 0x184);
    }
    // Remember what the button would pick, for the first person conversation camera.
    sPendingTarget = NULL;
    if (activeTarget != NULL) {
        for (l = 0; l < sizeof(sPromptLists) / sizeof(sPromptLists[0]); l++) {
            const PromptList* list = &sPromptLists[l];
            if ((u32)activeTarget >= list->base && (u32)activeTarget < list->base + list->size * list->count) {
                s16* pos = prompt_target_pos(activeTarget, list->kind);
                sPendingTarget = activeTarget;
                sPendingIsNpc = l == 0;
                sPendingPos[0] = pos[0];
                sPendingPos[1] = pos[1];
                sPendingPos[2] = pos[2];
            }
        }
    }
    *held = savedHeld;
    *(s16*)(player + 0x182) = savedScore;
    *(u32*)(player + 0x184) = savedTarget;
    D_801BC65C_197A5C = savedSin;
    D_8020D9F4_1E8DF4 = savedCos;

    for (l = 0; l < sizeof(sPromptLists) / sizeof(sPromptLists[0]); l++) {
        const PromptList* list = &sPromptLists[l];
        for (i = 0; i < list->count && count < PROMPT_MAX; i++) {
            u8* entry = (u8*)(list->base + i * list->size);
            s16* pos;
            s32 dx, dz;
            if ((entry[0] & 3) != 3 || *(u32*)(entry + 0x10) == 0) {
                continue;
            }
            pos = prompt_target_pos(entry, list->kind);
            dx = pos[0] - PLAYER_POS_X(player);
            dz = pos[2] - PLAYER_POS_Z(player);
            if (entry != activeTarget && dx * dx + dz * dz > PROMPT_RADIUS * PROMPT_RADIUS) {
                continue;
            }
            out[count].pos[0] = pos[0];
            out[count].pos[1] = pos[1] - PROMPT_HEIGHT;
            out[count].pos[2] = pos[2];
            out[count].kind = list->kind;
            out[count].active = entry == activeTarget;
            out[count].tag = PROMPT_TAG((u32)entry);
            out[count].read = list->kind == PROMPT_TALK && npc_talked(entry);
            {
                float d = __builtin_sqrtf((float)(dx * dx + dz * dz));
                out[count].distance = (u16)(d > 65535.0f ? 65535.0f : d);
            }
            count++;
        }
    }
    // Hidden while Mega Man can't be controlled (pause menu, talking, cutscenes).
    if (sFocusTarget != NULL && count < PROMPT_MAX && mouse_camera_player_controllable()) {
        out[count].pos[0] = *(s16*)(sFocusTarget + 0x14);
        out[count].pos[1] = actor_center_y(sFocusTarget, FOCUS_TARGET_HEIGHT);
        out[count].pos[2] = *(s16*)(sFocusTarget + 0x18);
        out[count].kind = PROMPT_FOCUS;
        out[count].active = FALSE;
        out[count].tag = PROMPT_TAG((u32)sFocusTarget + 1);
        out[count].distance = 0;
        out[count].read = FALSE;
        count++;
    }
    sPromptCount[write] = count;
    sPromptRead = write;
}

// Debug switches (poke.py): turn pieces off to find which one breaks a menu or dialogue.
s32 gDbgSkipPrompt = 0, gDbgSkipSprint = 0, gDbgSkipInput = 0;

// Debug (tools/re/btnscan.py): gDbgPadOn = 1 (or 2, see func_80093140_6E540) replaces controller 1 with gDbgPadButtons (N64 bits) and gDbgPadX/Y, before the
// game derives the "pressed" pulse, so injected presses behave like real ones.
s32 gDbgPadOn = 0, gDbgPadButtons = 0, gDbgPadX = 0, gDbgPadY = 0;

extern u8 D_801BC738_197B38[]; // raw OSContPad x4 (6 bytes each), filled by the controller read
void func_8009412C_6F52C(void);
void func_80094154_6F554(void);

// Modern layout (bindings in input.cpp / controls.json): Cross = A, Circle = C-Down, Square = Z, R2 = B, R1 = C-Left,
// L1 = L. Here it is turned into what the game expects for the context:
// - menus (pause, map, title/load, dialogue): Circle confirms (A), Cross goes back (B), R1 is next page (R);
// - gameplay: Square is the hidden side hop (substate 9: standing + sideways stick + A on the same frame), L1 held
//   makes the stick's x strafe with the game's L/R instead of turning.
#define PAD_A 0x8000
#define PAD_B 0x4000
#define PAD_Z 0x2000
#define PAD_L 0x0020
#define PAD_R 0x0010
#define PAD_C_DOWN 0x0004
#define PAD_C_LEFT 0x0002
#define PAD_C_UP 0x0008
#define PAD_START 0x1000
static bool sMenuIsMap;
#define MENU_CALLBACKS (*(volatile u32*)0x801D90C0) // non-zero while the pause menu or the map is open (pausefind.py)
#define PAD_STRAFE_THRESHOLD 40
#define DODGE_WAIT_FRAMES 6
s32 gModernPad = 1; // live switch (poke.py): 0 = buttons reach the game as bound
// Definitive Features (General menu): 0 plays like the base port. Read from the runtime once per pad read.
s32 gDefinitive = 1;

static u16 sPadLatch;    // buttons held when the context changed: ignored until released
static s32 sPadWasMenu;
static s32 sDodgeWait = -1; // frames left waiting for Mega Man to stand still before the hop (-1 = no hop pending)
static u16 sPadPrevRaw;
static s8 sDodgeDir;
// The hop goes where the stick points (camera relative) or, without one, where Mega Man faces, in either view: the
// game's side hop moves along yaw - the strafe offset (+0x116), so that offset is steered for the whole hop.
static s32 sDodgeAngle;  // wanted direction, camera yaw convention
static s32 sDodgePhase;  // 0 none, 1 waiting for the hop (substate 9), 2 hopping
static s32 sDodgeTimer;

static void dodge_steer(u8* player) {
    if (sDodgePhase == 0) {
        return;
    }
    if (--sDodgeTimer < 0) {
        sDodgePhase = 0;
        return;
    }
    if (player[0x8] == 1 && player[0x9] == 9) {
        if (sDodgePhase == 1) {
            sDodgePhase = 2;
            sDodgeTimer = 45;
        }
    } else if (sDodgePhase == 2 && !(player[0x8] == 1 && player[0x9] == 8 && player[0x112] != 0)) {
        sDodgePhase = 0; // landed
        return;
    } else if (sDodgePhase == 1) {
        return;
    }
    *(s16*)(player + 0x116) = (s16)wrap_angle(PLAYER_YAW(player) + 0x800 - sDodgeAngle);
}

static s32 pad_menu_context(void) {
    u8* player = D_802049B0_1DFDB0;
    if (MENU_CALLBACKS != 0 || *(u16*)&D_801BC380_197780[0xD0] == 1) {
        return TRUE;
    }
    return player[0x8] == 1 && player[0x9] == 0x10;
}

static void modern_pad(u8* pad, u16 prevRaw) {
    u8* player = D_802049B0_1DFDB0;
    u16 held = *(u16*)pad, out;
    s32 menu = pad_menu_context();
    s8 x = (s8)pad[2];

    if (menu != sPadWasMenu) {
        sPadLatch = held;
        sPadWasMenu = menu;
        sDodgeWait = -1;
    }
    sPadLatch &= held;
    held &= ~sPadLatch;
    out = held & ~(PAD_A | PAD_B | PAD_Z | PAD_L | PAD_C_DOWN | PAD_C_LEFT);

    if (menu) {
        // Dialogue keeps C-Down too: the game advances text with it.
        if (held & PAD_C_DOWN) out |= PAD_A | (player[0x9] == 0x10 ? PAD_C_DOWN : 0);
        // One job per button (menu_legend_draw shows them): Circle confirms, Cross goes back, L1/R1 change page,
        // Triangle (C-Up, passed through) opens the game's Reset/Resume; Square, R2 and L2 do nothing.
        // Keyboard keeps PC habits: Space/E confirm, Backspace (B) goes back, Q (Z) / R change page.
        if (recomp_get_prompt_style() == 2) {
            out |= held & (PAD_A | PAD_B);
            if (held & PAD_Z) out |= PAD_L;
        } else if (held & PAD_A) {
            out |= PAD_B;
        }
        if (held & PAD_C_LEFT) out |= PAD_R;
        if (held & PAD_L) out |= PAD_L;
        *(u16*)pad = out;
        return;
    }

    out |= held & (PAD_A | PAD_B | PAD_C_DOWN | PAD_C_LEFT);
    // Which menu the next one is, for menu_legend_draw: C-Up opens the map, START the pause menu.
    if (held & ~prevRaw & PAD_C_UP) sMenuIsMap = TRUE;
    else if (held & ~prevRaw & PAD_START) sMenuIsMap = FALSE;
    if (sFirstPersonActive) {
        // First person: the stick already strafes through the game's L/R (controls.cpp), and L1 is cleared there.
        out |= held & PAD_L;
    } else if (held & PAD_L) {
        if (x <= -PAD_STRAFE_THRESHOLD) out |= PAD_L;
        else if (x >= PAD_STRAFE_THRESHOLD) out |= PAD_R;
        pad[2] = 0;
    }

    // Square pressed: hop toward the stick (or Mega Man's facing), see dodge_steer. The game only hops from standing,
    // so a walking Mega Man gets a few neutral frames to stop first.
    if ((held & PAD_Z) && !(prevRaw & PAD_Z) && sDodgeWait < 0) {
        s32 sx = x, sy = (s8)pad[3];
        s32 facing = PLAYER_YAW(player) + 0x800;
        if (sFirstPersonActive) {
            // controls.cpp already turned the stick's x into L/R here.
            sx = (held & PAD_L) ? -80 : ((held & PAD_R) ? 80 : sx);
        }
        sDodgeAngle = (sx * sx + sy * sy > PAD_STRAFE_THRESHOLD * PAD_STRAFE_THRESHOLD)
                          ? D_80162844_13DC44 + func_80032664_DA64(sx, sy) : facing;
        // Hop to the side nearer the wanted direction (the natural left hop goes to facing - 0x400).
        sDodgeDir = wrap_angle(sDodgeAngle - facing) < 0 ? -80 : 80;
        sDodgeWait = DODGE_WAIT_FRAMES;
        sDodgePhase = 1;
        sDodgeTimer = 20;
    }
    if (sDodgeWait >= 0) {
        if (player[0x8] == 1 && player[0x9] == 0) {
            out |= PAD_A;
            out &= ~(PAD_L | PAD_R);
            pad[2] = (u8)sDodgeDir;
            pad[3] = 0;
            sDodgeWait = -1;
        } else if (sDodgeWait-- > 0) {
            out &= ~(PAD_A | PAD_L | PAD_R);
            pad[2] = 0;
            pad[3] = 0;
        }
    }
    *(u16*)pad = out;
}

// Same as the original: copy each raw pad into the game's 8 byte slot and set pressed = held & ~previous held (+6).
RECOMP_PATCH void func_80093140_6E540(u8* pads) {
    u32 i;
    func_8009412C_6F52C();
    for (i = 0; i < 4; i++) {
        u8* pad = pads + i * 8;
        u16 prev = *(u16*)pad;
        bcopy(&D_801BC738_197B38[i * 6], pad, 6);
        if (i == 0 && gDbgPadOn) {
            *(u16*)pad = (u16)gDbgPadButtons;
            pad[2] = (u8)(s8)gDbgPadX;
            pad[3] = (u8)(s8)gDbgPadY;
        }
        // The runtime turns the stick into L/R in first person; menus and dialogue need the plain stick (next poll).
        if (i == 0) {
            recomp_set_pad_menu(pad_menu_context());
            gDefinitive = recomp_get_definitive();
        }
        // gDbgPadOn = 2 injects the buttons as bound (before the modern layout), 1 as the game sees them.
        if (i == 0 && gModernPad && gDefinitive && gDbgPadOn != 1) {
            u16 raw = *(u16*)pad;
            modern_pad(pad, sPadPrevRaw);
            sPadPrevRaw = raw;
        }
        *(u16*)(pad + 6) = *(u16*)pad & ~prev;
    }
    func_80094154_6F554();
}

// Aim pose (posetrace.py): Mega Man has two animation layers. Each starts with two pointers into the animation data:
// +0x94 base / +0x98 current keyframe (whole body, frame counter +0xA4, flags +0xAC) and +0xF8 / +0xFC (upper body,
// counter +0x100, flags +0x104); the current pointer moves 8 bytes per frame. Firing starts the buster animation
// (a different one standing, walking, strafing and backpedaling) and sets 0x80 in +0x174 until it ends. While aiming
// armed in third person the pose is held a few frames in, with the arm out, without firing. The animation comes from
// the game's own data: the resident player overlay keeps the loaded animation banks at 0x8021F2B8 (filled by
// func_8021D9D0); bank 5 holds the buster animations (0 standing, 1 walking, 2 strafe right, 3 strafe left) and an
// animation's id, stored twice in the layer flags, is 0x60 + its index (bank 0 walking is 0x01, the same scheme).
// A pointer seen on one of the game's own shots wins over the table (per movement kind, reset on scene change).
s32 gAimPoseFrame = 6; // live: keyframe the pose holds
#define AIM_POSE_FRAMES gAimPoseFrame
#define AIM_POSE_KINDS 5
s32 gAimPose = 1; // live switch (poke.py)
s32 gDbgBackSteer = 1; // debug: 0 lets the game backpedal when gAimWalkSteer is 0
s32 gAimWalkSteer = 1; // live: aiming walks "forward" steered along the stick (0 = the game's strafe/backpedal)
s32 gDbgFpStandPose = 0; // debug: first person uses the standing pose on the upper body
#define AIM_POSE_BANKS ((u32*)0x8021F2B8)
#define AIM_POSE_BANK 5
#define AIM_POSE_ID_BASE 0x60
static const s8 sPoseTableIndex[] = { 0, 1, 3, 0, 2 }; // per aim_pose_kind; backpedaling: the standing one on the upper body
static u32 sPoseBaseB[AIM_POSE_KINDS], sPoseBaseA;
static u16 sPoseFlagsB[AIM_POSE_KINDS], sPoseFlagsA;
static u16 sPoseScene = 0xFFFF;

static s32 aim_pose_kind(u8* player) {
    // Standing, walking, then the walk modes of +0x112: 1 strafe left, 2 backpedal, 3 strafe right (each its own
    // buster animation).
    if (player[0x9] == 0) return 0;
    if (player[0x112] >= 1 && player[0x112] <= 3) return 1 + player[0x112];
    return 1;
}

static bool aim_pose_pointer(u32 ptr) {
    return ptr >= 0x80000400 && ptr < 0x80800000 && (ptr & 3) == 0;
}

// Puts a layer at the hold frame of the given animation, unless that animation is still in its first frames (a shot
// just fired: it must play through the frame that spawns the shot).
static void aim_pose_hold(u8* player, s32 baseOff, s32 curOff, s32 countOff, s32 flagsOff, u32 base, u16 flags) {
    u32* layerBase = (u32*)(player + baseOff);
    if (*layerBase == base && player[countOff] < AIM_POSE_FRAMES) {
        return;
    }
    *layerBase = base;
    *(u32*)(player + curOff) = base + AIM_POSE_FRAMES * 8;
    player[countOff] = AIM_POSE_FRAMES;
    *(u16*)(player + flagsOff) = flags;
}

static void aim_pose_update(u8* player) {
    u16 scene = *(u16*)&D_801BC380_197780[0xD0];
    s32 kind, upper;

    if (scene != sPoseScene) {
        sPoseScene = scene;
        bzero(sPoseBaseB, sizeof(sPoseBaseB));
        sPoseBaseA = 0;
    }
    if (player[0x8] != 1 || (player[0x9] != 0 && player[0x9] != 2)) {
        return;
    }
    kind = aim_pose_kind(player);
    // Learn the buster animation from the game's own shot, on its first frames.
    if ((player[0x174] & 0x80) && player[0x100] <= 1 && aim_pose_pointer(*(u32*)(player + 0xF8))) {
        sPoseBaseB[kind] = *(u32*)(player + 0xF8);
        sPoseFlagsB[kind] = *(u16*)(player + 0x104);
        if (kind == 0 && player[0xA4] <= 1 && aim_pose_pointer(*(u32*)(player + 0x94))) {
            sPoseBaseA = *(u32*)(player + 0x94);
            sPoseFlagsA = *(u16*)(player + 0xAC);
        }
    }
    // First person shows only the arms, so holding the aim raises the buster into view the same way.
    if (!gAimPose || !(sFirstPersonActive ? sFpAimHeld : sAiming) || PLAYER_COMBAT_STATE(player) == 0) {
        return;
    }
    // First person keeps the standing pose on the upper body whatever the legs do: the walking buster animations
    // swing the buster out of the first person view (fp_sheet.png), the standing one holds it up in front.
    upper = (sFirstPersonActive && gDbgFpStandPose) ? 0 : kind;
    if (sPoseBaseB[upper] == 0 && sPoseTableIndex[upper] >= 0) {
        u32* bank = (u32*)AIM_POSE_BANKS[AIM_POSE_BANK];
        u32 anim = aim_pose_pointer((u32)bank) ? bank[(s32)sPoseTableIndex[upper]] : 0;
        // Unused slots point back at the bank itself.
        if (aim_pose_pointer(anim) && anim != (u32)bank) {
            u16 id = AIM_POSE_ID_BASE + sPoseTableIndex[upper];
            sPoseBaseB[upper] = anim;
            sPoseFlagsB[upper] = (id << 8) | id;
            if (upper == 0) {
                sPoseBaseA = anim;
                sPoseFlagsA = sPoseFlagsB[0];
            }
        }
    }
    if (sPoseBaseB[upper] == 0) {
        return;
    }
    // A new press of fire while the pose is held: the game would see a shot still playing and ignore it (no shot at
    // all while walking, firedelay traces), so drop the shooting flag and let it start a fresh shot this frame.
    if ((*(u16*)(player + 0x11E) & 0x8000) && (player[0x174] & 0x80) &&
        *(u32*)(player + 0xF8) == sPoseBaseB[upper] && player[0x100] >= AIM_POSE_FRAMES) {
        player[0x174] &= ~0x80;
        return;
    }
    // Holding fire: the game loops the animation to fire again, leave it alone.
    if (*(u16*)(player + 0x11C) & 0x8000) {
        return;
    }
    aim_pose_hold(player, 0xF8, 0xFC, 0x100, 0x104, sPoseBaseB[upper], sPoseFlagsB[upper]);
    if (kind == 0 && sPoseBaseA != 0) {
        aim_pose_hold(player, 0x94, 0x98, 0xA4, 0xAC, sPoseBaseA, sPoseFlagsA);
    }
    // The game's own shots set 0x82; without 0x02 the upper body layer isn't drawn, so walking kept the run cycle's
    // arms (measured on the layer flags while aiming). First person draws its arms from kept poses and stays as it was.
    player[0x174] |= sFirstPersonActive ? 0x80 : 0x82;
}

// Aim legs (third person, gAimLegs): while aiming Mega Man faces the crosshair and the game runs him "forward", but
// the game has one facing for the whole body and one run animation (its strafe and backpedal turn the whole model,
// nat_sheet.png). So the lower body is turned toward the movement only when drawn (bones 8-14 around the pelvis,
// draw_megaman_part), and moving back the legs face the crosshair and play the run cycle backwards (16 frames, body
// layer pointer +0x98 / counter +0xA4): a walk backwards. Nothing the game reads for its logic is turned.
#define AIM_LEGS_MAX_TWIST 0x380   // ~79 degrees
#define AIM_LEGS_BACK_ANGLE 0x480  // moves more than ~101 degrees from the facing count as backwards
#define AIM_LEGS_EASE 0.35f
#define AIM_LEGS_RUN_FRAMES 16
#define AIM_LEGS_PELVIS_BONE 8
s32 gAimLegs = 1; // live switch (poke.py)
s32 gAimLegsSign = 1; // live: direction convention of the drawing turn (checked with screenshots)
static float sLegTwist;    // lower body turn while drawing, 12-bit angle
static s32 sLegPhase;      // backwards run frame

static void aim_legs_update(u8* player) {
    float target = 0.0f;
    if (gAimLegs && gAimSteer && !sFirstPersonActive) {
        s32 delta = wrap_angle(gAimSteerYaw - PLAYER_YAW(player)); // movement vs facing (same convention)
        bool back = delta > AIM_LEGS_BACK_ANGLE || delta < -AIM_LEGS_BACK_ANGLE;
        s32 twist = back ? wrap_angle(delta - 0x800) : delta;
        target = (float)clamp_s32(twist, -AIM_LEGS_MAX_TWIST, AIM_LEGS_MAX_TWIST);
        if (back) {
            u32* bank0 = (u32*)AIM_POSE_BANKS[0];
            u32 run = aim_pose_pointer((u32)bank0) ? bank0[1] : 0;
            // Only over the game's own run cycle on the body layer; each value written is one the cycle itself uses.
            if (run != 0 && *(u32*)(player + 0x94) == run) {
                sLegPhase = (sLegPhase + AIM_LEGS_RUN_FRAMES - 1) % AIM_LEGS_RUN_FRAMES;
                *(u32*)(player + 0x98) = run + sLegPhase * 8;
                player[0xA4] = (u8)sLegPhase;
            }
        }
    }
    sLegTwist += (target - sLegTwist) * AIM_LEGS_EASE;
    if (target == 0.0f && sLegTwist > -2.0f && sLegTwist < 2.0f) {
        sLegTwist = 0.0f;
    }
}

// Turns Mega Man toward the crosshair while aiming in third person.
static void aim_face_crosshair(u8* player) {
    // Toward the point under the crosshair, which corrects for the over the shoulder camera offset (the camera's
    // own yaw when the point is missing or right at his feet). A quick ease instead of a snap when aiming starts.
    s32 ax = (s32)sAimPoint[0] - PLAYER_POS_X(player), az = (s32)sAimPoint[2] - PLAYER_POS_Z(player);
    s32 want = (sAimPointValid && ax * ax + az * az > AIM_FACE_MIN_DIST * AIM_FACE_MIN_DIST)
                   ? func_80032664_DA64(ax, az) - 0x800 : sOrbitYaw - 0x800;
    s32 turn = wrap_angle(want - PLAYER_YAW(player));
    s32 step = (s32)(turn * AIM_FACE_EASE);
    if (step == 0) {
        step = turn;
    }
    PLAYER_YAW(player) = (PLAYER_YAW(player) + step) & 0xFFF;
}

// First person needs the kept buster pose (fp_arm_capture) from the start, or the buster follows the walking
// animation out of view until Mega Man first aims or fires standing. So while he stands idle in first person without
// it, the standing aim pose is held for one frame with the arms hidden (draw_megaman), captured, and the animation
// layers are put back the next frame. A few tries at most, in case the capture doesn't take.
static bool sFpArmValid;
static s32 sFpProbe = 0, sFpProbeTries = 0;
#define FP_PROBE_TRIES 4
static u32 sFpProbeLayer[4];
static u16 sFpProbeFlags[2];
static u8 sFpProbeCount[2], sFpProbeAction;

static void fp_arm_probe(u8* player) {
    u32* bank = (u32*)AIM_POSE_BANKS[AIM_POSE_BANK];
    u32 anim;
    u16 id = (AIM_POSE_ID_BASE << 8) | AIM_POSE_ID_BASE;
    s32 sx = (s8)D_801BC380_197780[2], sy = (s8)D_801BC380_197780[3];
    if (sFpProbe) {
        *(u32*)(player + 0x94) = sFpProbeLayer[0];
        *(u32*)(player + 0x98) = sFpProbeLayer[1];
        *(u32*)(player + 0xF8) = sFpProbeLayer[2];
        *(u32*)(player + 0xFC) = sFpProbeLayer[3];
        *(u16*)(player + 0xAC) = sFpProbeFlags[0];
        *(u16*)(player + 0x104) = sFpProbeFlags[1];
        player[0xA4] = sFpProbeCount[0];
        player[0x100] = sFpProbeCount[1];
        player[0x174] = sFpProbeAction;
        sFpProbe = 0;
        return;
    }
    if (sFpArmValid || sFpProbeTries >= FP_PROBE_TRIES || !sFirstPersonActive || PLAYER_COMBAT_STATE(player) == 0 ||
        player[0x8] != 1 || player[0x9] != 0 || (player[0x174] & 0x80) || *(u16*)(player + 0x11C) != 0 ||
        sx * sx + sy * sy > FREE_CAMERA_STICK_MIN * FREE_CAMERA_STICK_MIN || !aim_pose_pointer((u32)bank)) {
        return;
    }
    anim = bank[0];
    if (!aim_pose_pointer(anim) || anim == (u32)bank) {
        return;
    }
    sFpProbeLayer[0] = *(u32*)(player + 0x94);
    sFpProbeLayer[1] = *(u32*)(player + 0x98);
    sFpProbeLayer[2] = *(u32*)(player + 0xF8);
    sFpProbeLayer[3] = *(u32*)(player + 0xFC);
    sFpProbeFlags[0] = *(u16*)(player + 0xAC);
    sFpProbeFlags[1] = *(u16*)(player + 0x104);
    sFpProbeCount[0] = player[0xA4];
    sFpProbeCount[1] = player[0x100];
    sFpProbeAction = player[0x174];
    sPoseBaseA = anim;
    sPoseFlagsA = id;
    aim_pose_hold(player, 0x94, 0x98, 0xA4, 0xAC, anim, id);
    aim_pose_hold(player, 0xF8, 0xFC, 0x100, 0x104, anim, id);
    player[0x174] |= 0x80;
    sFpProbe = 1;
    sFpProbeTries++;
}

void mouse_camera_player_input(void) {
    u8* player = D_802049B0_1DFDB0;
    u16* held = (u16*)(player + 0x11C);
    u16* pressed = (u16*)(player + 0x11E);
    s32 side, forward, desired, diff;

    // Some conversations (the monkey's) stop the game's camera update, which froze the first person view wherever it
    // pointed: build the gameplay camera here instead so the view can turn to the NPC.

    if (!gDefinitive) {
        // Base port controls: no sprint, aim steering, pose or prompts (sprint.c reads these too).
        gAimDiagonal = 0;
        gAimSteer = 0;
        return;
    }
    if (!gDbgSkipPrompt) {
        interact_prompt_update();
    }
    if (!gDbgSkipSprint) {
        sprint_frame_update();
    }

    // Firing (0x8000, the buster) in third person steers like aiming for one shot animation: Mega Man turns to the
    // free aim crosshair. (First person armed always steers, see below.)
    if (PLAYER_COMBAT_STATE(player) != 0 && ((*held | *pressed) & 0x8000)) {
        sFireFaceFrames = FIRE_FACE_FRAMES;
    } else if (sFireFaceFrames > 0) {
        sFireFaceFrames--;
    }
    fp_arm_probe(player);
    aim_pose_update(player);
    dodge_steer(player);
    gAimDiagonal = 0;
    gAimSteer = 0;
    if (sLegTwist != 0.0f) {
        aim_legs_update(player); // eases the legs back while not aiming (gAimSteer is 0 here)
    }
    // Aiming and moving (either view, gAimWalkSteer): the game gets "walk forward", whose buster animation keeps the
    // arm up and in the first person view (fpw_sheet.png), and sprint.c moves Mega Man along the stick. The game's own
    // strafe turns the body sideways (the buster leaves the first person view) and its backpedal stops and turns
    // Mega Man before every shot (10-18 frames, firedelay2.py). In first person controls.cpp already turned the
    // stick's x into the game's L/R (0x400/0x800 here). sFreeCameraActive is third person only, so this comes first.
    // First person armed steers this way all the time, aiming or not: switching the walk mode when firing lost the
    // press (dbl.py), and only the arms are drawn there. Not during a dodge hop, which steers itself.
    if (gAimWalkSteer && !gDbgSkipInput && sMouseCameraFrames > 0 && !(PLAYER_INPUT_FLAGS(player) & 0x60) &&
        PLAYER_COMBAT_STATE(player) != 0 && sDodgePhase == 0 &&
        (sFirstPersonActive || ((sAiming || sFocusTarget != NULL || sFireFaceFrames > 0) && sFreeCameraActive))) {
        s32 sx = (s8)D_801BC380_197780[2], sy = (s8)D_801BC380_197780[3];
        if (sFirstPersonActive) {
            sx = (*held & 0x0400) ? -80 : ((*held & 0x0800) ? 80 : 0);
        }
        if (sx * sx + sy * sy > PAD_STRAFE_THRESHOLD * PAD_STRAFE_THRESHOLD) {
            gAimSteer = 1;
            gAimSteerYaw = (D_80162844_13DC44 + func_80032664_DA64(sx, sy) - 0x800) & 0xFFF;
            *held = (*held & ~0x0CF0) | 0x10;
            *pressed &= ~0x0CF0;
        }
        if (sFirstPersonActive) {
            return;
        }
        // Locked on (R3 / F) without aiming: face the target the same way, so backing off walks backwards too.
        if (sAiming || sFocusTarget == NULL) {
            aim_face_crosshair(player);
        } else {
            PLAYER_YAW(player) = (func_80032664_DA64(*(s16*)(sFocusTarget + 0x14) - PLAYER_POS_X(player),
                                                     *(s16*)(sFocusTarget + 0x18) - PLAYER_POS_Z(player)) - 0x800) & 0xFFF;
        }
        aim_legs_update(player);
        return;
    }
    if (gDbgSkipInput || !sFreeCameraActive || sMouseCameraFrames <= 0 || (PLAYER_INPUT_FLAGS(player) & 0x60)) {
        return;
    }

    // Aiming armed (L2 / right mouse) and the lock-on (R3) use shooter style movement: Mega Man faces the crosshair or
    // the target and the stick strafes and backpedals, like modern third person shooters.
    if ((sAiming && PLAYER_COMBAT_STATE(player) != 0 && !sFirstPersonActive) || (!sAiming && sFocusTarget != NULL)) {
        s32 stickX = (s8)D_801BC380_197780[2], stickY = (s8)D_801BC380_197780[3];
        if (gDbgBackSteer && sAiming && stickY < -PAD_STRAFE_THRESHOLD) {
            // Moving back while aiming: the game's backpedal stops and turns Mega Man before every shot (10-18 frames,
            // firedelay2.py), and switching to its strafe at the shot costs as much. Its strafe fires at once, so the
            // game strafes the whole time and sprint.c moves him along the stick.
            gAimSteer = 1;
            gAimSteerYaw = (D_80162844_13DC44 + func_80032664_DA64(stickX, stickY) - 0x800) & 0xFFF;
            *held = (*held & ~0xF0) | (stickX < 0 ? 0x80 : 0x20);
            *pressed &= ~0xF0;
        } else
        // Diagonal input: keep only the side step for the game, the forward/back part is added in sprint.c.
        {
            // With a side step held and the stick pushed forward or back at all, the move follows the stick's angle
            // like the free camera does: the side step stays the game's, the forward/back part is added in sprint.c.
            s32 sd = ((*held & 0x20) ? 1 : 0) - ((*held & 0x80) ? 1 : 0);
            s32 sx = (s8)D_801BC380_197780[2], sy = (s8)D_801BC380_197780[3];
            s32 mag = func_80032424_D824(sx * sx + sy * sy);
            if (sd != 0 && mag > 0 && (sy * 5 > mag || -sy * 5 > mag)) {
                gAimDiagonal = 1;
                gAimDiagLateral = (float)(sx < 0 ? -sx : sx) / (float)mag;
                gAimDiagForward = (float)sy / (float)mag;
                *held &= ~0x50;
                *pressed &= ~0x50;
            }
        }
        // Face where the camera aims (or the focus target) and strafe with left and right, like a shooter.
        if (!sAiming) {
            PLAYER_YAW(player) = (func_80032664_DA64(*(s16*)(sFocusTarget + 0x14) - PLAYER_POS_X(player),
                                                     *(s16*)(sFocusTarget + 0x18) - PLAYER_POS_Z(player)) - 0x800) & 0xFFF;
        } else {
            aim_face_crosshair(player);
        }
        if (*held & 0x80) {
            *held |= 0x400;
        }
        if (*held & 0x20) {
            *held |= 0x800;
        }
        if (*pressed & 0x80) {
            *pressed |= 0x400;
        }
        if (*pressed & 0x20) {
            *pressed |= 0x800;
        }
        *held &= ~0xA0;
        *pressed &= ~0xA0;
        return;
    }

    // Leave the game's own strafing and lock-on alone.
    if (*held & 0xE00) {
        return;
    }

    forward = ((*held & 0x10) ? 1 : 0) - ((*held & 0x40) ? 1 : 0);
    side = ((*held & 0x20) ? 1 : 0) - ((*held & 0x80) ? 1 : 0);
    if (forward == 0 && side == 0) {
        return;
    }

    // Mega Man faces half a turn away from the camera's yaw when it's behind him. The analog stick's own angle is
    // used when it's pushed (the held bits only give 8 directions, which made turning feel notchy).
    {
        s8 stickX = (s8)D_801BC380_197780[2], stickY = (s8)D_801BC380_197780[3];
        if (stickX * stickX + stickY * stickY > FREE_CAMERA_STICK_MIN * FREE_CAMERA_STICK_MIN) {
            desired = sOrbitYaw - 0x800 + func_80032664_DA64(stickX, stickY);
        } else {
            desired = sOrbitYaw - 0x800 + func_80032664_DA64(side, forward);
        }
    }
    // Ease toward the direction: quick for big turns, gentle for small corrections, never slower than a minimum.
    diff = wrap_angle(desired - PLAYER_YAW(player));
    {
        s32 step = (s32)(diff * FREE_CAMERA_TURN_EASE);
        if (step == 0 || (step > -FREE_CAMERA_TURN_MIN && step < FREE_CAMERA_TURN_MIN)) {
            step = clamp_s32(diff, -FREE_CAMERA_TURN_MIN, FREE_CAMERA_TURN_MIN);
        }
        diff = clamp_s32(step, -FREE_CAMERA_TURN_RATE, FREE_CAMERA_TURN_RATE);
    }
    PLAYER_YAW(player) = (PLAYER_YAW(player) + diff) & 0xFFF;

    *held = (*held & ~0xF0) | 0x10;
    if (*pressed & 0xF0) {
        *pressed = (*pressed & ~0xF0) | 0x10;
    }
}

// camera_set_orbit
RECOMP_PATCH void func_80032FC0_E3C0(Vec3i* target, s32 distance, s32 yaw, s32 pitch) {
    PsxMatrix* rot = &D_80210990_1EBD90[0];
    PsxMatrix* view = &D_80210990_1EBD90[1];
    Vec3i localTarget;
    Vec3i offset;
    Vec3i viewOffset;
    s32 shift;
    s32 scaledDistance;

    //@recomp Let the mouse camera drive the main gameplay camera, which always orbits the camera's own target.
    if (target == &D_80204348_1DF748.target) {
        localTarget = *target;
        target = &localTarget;
        mouse_camera_update(target, &distance, &yaw, &pitch);
    }

    memcpy(&D_80162830_13DC30, target, sizeof(Vec3i));
    D_80162840_13DC40 = distance;
    D_80162844_13DC44 = yaw;
    D_80162848_13DC48 = pitch;

    D_80204400_1DF800.x = -pitch & 0xFFF;
    D_80204400_1DF800.y = yaw & 0xFFF;
    D_80204400_1DF800.z = 0;
    func_80031600_CA00(&D_80204400_1DF800, rot);
    func_80031E10_D210(rot, view);

    // Shifts go through u32 like the original sllv, since shifting negative values left is undefined in C.
    shift = D_80210B61_1EBF61;
    scaledDistance = (s32)((u32)distance << shift);

    offset.x = (s32)((u32)-target->x << shift) +
               ((((scaledDistance * func_800324B4_D8B4(yaw)) >> 12) * func_80032498_D898(pitch)) >> 12);
    offset.y = (s32)((u32)-target->y << shift) + ((scaledDistance * func_800324B4_D8B4(pitch)) >> 12);
    offset.z = (s32)((u32)-target->z << shift) +
               ((((scaledDistance * func_80032498_D898(yaw)) >> 12) * func_80032498_D898(pitch)) >> 12);
    func_80030AA0_BEA0(view, &offset, (Vec3i*)view->t);
    memcpy(&D_801D4760_1AFB60, view, sizeof(PsxMatrix));

    offset.x = ((((distance * func_800324B4_D8B4(yaw)) >> 12) * func_80032498_D898(pitch)) >> 12) - target->x;
    offset.y = ((distance * func_800324B4_D8B4(pitch)) >> 12) - target->y;
    offset.z = ((((distance * func_80032498_D898(yaw)) >> 12) * func_80032498_D898(pitch)) >> 12) - target->z;

    memcpy(&D_802043F0_1DF7F0, &D_802043E0_1DF7E0, sizeof(Vec3i));

    D_80195E90_171290[0] = -offset.x;
    D_802043E0_1DF7E0.x = (s16)-offset.x;
    D_80195E90_171290[1] = -offset.y;
    D_802043E0_1DF7E0.y = (s16)-offset.y;
    D_80195E90_171290[2] = -offset.z;
    D_802043E0_1DF7E0.z = (s16)-offset.z;

    func_80030AA0_BEA0(view, &offset, &viewOffset);
    D_80206B58_1E1F58[0] = viewOffset.x;
    D_80206B58_1E1F58[1] = viewOffset.y;
    D_80206B58_1E1F58[2] = viewOffset.z;
}

// Tag for the Mega Man part being queued. Each part gets its own so RT64 interpolates it against the same part on the
// previous frame (they used to share one), and the viewmodel uses different ones so it never blends with the body.
#define MEGAMAN_PART_GFX_TAG(part) (0x4D454700 | (part))
#define MEGAMAN_VIEWMODEL_GFX_TAG(part) (0x4D455600 | (part))
static u32 sMegaManPartTag = MEGAMAN_PART_GFX_TAG(0);

// Called by the us.rev1.toml hook right before a Mega Man part is queued.
void set_megaman_part_gfx_tag(void) {
    gCurrGfxTag = sMegaManPartTag;
}

// Turns a bone (matrix and position) about the vertical axis through pivot by a 12-bit angle, then moves it by
// (shiftX, shiftY, shiftZ). Same convention as ratan2: the bone's heading grows by angle.
static void turn_bone(PsxMatrix* mtx, SVec3* pos, s32 angle, s32 pivotX, s32 pivotZ, s32 shiftX, s32 shiftY, s32 shiftZ,
                      PsxMatrix* outMtx, SVec3* outPos) {
    s32 c = func_80032498_D898(angle), sn = func_800324B4_D8B4(angle);
    s32 dx = pos->x - pivotX, dz = pos->z - pivotZ;
    s32 j;
    for (j = 0; j < 3; j++) {
        s32 m0 = mtx->m[0][j], m2 = mtx->m[2][j];
        outMtx->m[0][j] = (s16)((c * m0 + sn * m2) >> 12);
        outMtx->m[1][j] = mtx->m[1][j];
        outMtx->m[2][j] = (s16)((-sn * m0 + c * m2) >> 12);
    }
    outMtx->pad = mtx->pad;
    outMtx->t[0] = mtx->t[0];
    outMtx->t[1] = mtx->t[1];
    outMtx->t[2] = mtx->t[2];
    outPos->x = (s16)(pivotX + ((c * dx + sn * dz) >> 12) + shiftX);
    outPos->y = (s16)(pos->y + shiftY);
    outPos->z = (s16)(pivotZ + ((-sn * dx + c * dz) >> 12) + shiftZ);
    outPos->pad = pos->pad;
}

// First person arm hold (gFpArmHold): running while aiming, the game's walking buster animation swings the buster arm
// high into the middle of the view, where its open end shows (fpshot_sheet.png). The standing aim pose looks right,
// so its arm bones (3-4, the buster arm) are kept from a frame standing in that pose, relative to Mega Man's position
// and facing, and drawn from there while walking. Drawing only; the other arm keeps its live animation.
// Arm bones: 6-7 the buster arm (Mega Man's left, left of the first person view; ft_sheet.png), 3-4 the other one. Both are kept from the standing aim pose. In first person the
// buster is also drawn lower and further ahead (its open shoulder end left the bottom of the view floating), and the
// other arm, while running, swings as in the plain run cycle (kept per frame of the 16, from running without the
// buster) and a little lower, so it follows the legs instead of the buster and shows less.
static const u8 sFpArmBones[] = { 3, 4, 6, 7 };
#define FP_ARM_BONES 4
s32 gFpArmHold = 1;       // live switch (poke.py)
s32 gFpBusterDown = 4;    // live: first person buster offset, down / ahead (world units)
s32 gFpBusterAhead = 0;
s32 gFpOtherDown = 2;     // live: the other arm while running with the buster up, down / ahead
s32 gFpOtherAhead = 0;
s32 gFpKick = 25; // live: buster kick on a shot while walking, in tenths of sFpKick (0 = off)
#define FP_KICK_FRAMES 8
static const s8 sFpKick[FP_KICK_FRAMES] = { 3, 7, 10, 8, 5, 2, 0, 0 }; // per frame of the shot animation (6-7: the aim hold)
static PsxMatrix sFpArmMtx[FP_ARM_BONES];
static SVec3 sFpArmPos[FP_ARM_BONES];
static s32 sFpArmYaw, sFpArmRootX, sFpArmRootY, sFpArmRootZ;
static bool sFpArmValid = FALSE; // declared earlier for fp_arm_probe
static u16 sFpArmScene = 0xFFFF;
#define FP_RUN_FRAMES 16
#define FP_OTHER_FIRST 0 // slots 0-1 of sFpArmBones are the other arm (3-4)
static PsxMatrix sFpRunMtx[FP_RUN_FRAMES][2];
static SVec3 sFpRunPos[FP_RUN_FRAMES][2];
static s32 sFpRunYaw[FP_RUN_FRAMES], sFpRunRoot[FP_RUN_FRAMES][3];
static u32 sFpRunValid; // bit per run frame

static s32 fp_arm_slot(s32 bone) {
    s32 i;
    for (i = 0; i < FP_ARM_BONES; i++) {
        if (sFpArmBones[i] == bone) return i;
    }
    return -1;
}

// True while the upper body plays one of the buster animations (bank 5), aimed or fired.
static bool fp_buster_active(u8* player) {
    u32* bank = (u32*)AIM_POSE_BANKS[AIM_POSE_BANK];
    u32 b = *(u32*)(player + 0xF8);
    s32 i;
    if (!aim_pose_pointer((u32)bank)) return FALSE;
    for (i = 0; i < 4; i++) {
        if (bank[i] == b && b != (u32)bank) return TRUE;
    }
    return FALSE;
}

static void fp_arm_capture(u8* player) {
    u16 scene = *(u16*)&D_801BC380_197780[0xD0];
    s32 i;
    // The kept arm pose is relative to Mega Man's position and facing, so it stays valid across scenes (losing it left
    // the buster to the walking animation, out of view, until the next standing shot).
    if (scene != sFpArmScene) {
        sFpArmScene = scene;
        sFpRunValid = 0;
    }
    // Plain running (run cycle on both layers, no buster): keep the other arm for this frame of the cycle.
    {
        u32* bank0 = (u32*)AIM_POSE_BANKS[0];
        u32 run = aim_pose_pointer((u32)bank0) ? bank0[1] : 0;
        if (run != 0 && player[0x8] == 1 && player[0x9] == 2 && *(u32*)(player + 0x94) == run &&
            *(u32*)(player + 0xF8) == run) {
            s32 f = player[0xA4] % FP_RUN_FRAMES;
            for (i = 0; i < 2; i++) {
                sFpRunMtx[f][i] = PLAYER_BONE_MATRICES(player)[sFpArmBones[FP_OTHER_FIRST + i]];
                sFpRunPos[f][i] = PLAYER_BONE_POSITIONS(player)[sFpArmBones[FP_OTHER_FIRST + i]];
            }
            sFpRunYaw[f] = PLAYER_YAW(player);
            sFpRunRoot[f][0] = PLAYER_POS_X(player);
            sFpRunRoot[f][1] = PLAYER_POS_Y(player);
            sFpRunRoot[f][2] = PLAYER_POS_Z(player);
            sFpRunValid |= 1u << f;
        }
    }
    // Standing with both layers on the standing buster animation at the hold frame (the aim pose holds it there; a
    // standing shot passes it once).
    if (sPoseBaseA == 0 || player[0x8] != 1 || player[0x9] != 0 ||
        *(u32*)(player + 0x94) != sPoseBaseA || *(u32*)(player + 0xF8) != sPoseBaseA ||
        player[0x100] < AIM_POSE_FRAMES || player[0x100] > AIM_POSE_FRAMES + 1 ||
        player[0xA4] < AIM_POSE_FRAMES || player[0xA4] > AIM_POSE_FRAMES + 1) {
        return;
    }
    for (i = 0; i < FP_ARM_BONES; i++) {
        sFpArmMtx[i] = PLAYER_BONE_MATRICES(player)[sFpArmBones[i]];
        sFpArmPos[i] = PLAYER_BONE_POSITIONS(player)[sFpArmBones[i]];
    }
    sFpArmYaw = PLAYER_YAW(player);
    sFpArmRootX = PLAYER_POS_X(player);
    sFpArmRootY = PLAYER_POS_Y(player);
    sFpArmRootZ = PLAYER_POS_Z(player);
    sFpArmValid = TRUE;
}

// Queues one of Mega Man's parts, placed by its bone and seen through the given view matrix.
static void draw_megaman_part(u8* player, PsxMatrix* view, s32 bone, s32 part, u32 tag) {
    PsxMatrix* partMtx = &D_80210990_1EBD90[0];
    PsxMatrix* boneMtx = &PLAYER_BONE_MATRICES(player)[bone];
    SVec3* boneVec = &PLAYER_BONE_POSITIONS(player)[bone];
    PsxMatrix turnedMtx;
    SVec3 turnedVec;
    Vec3i bonePos;

    // Aim legs: the lower body turned around the pelvis by sLegTwist about the vertical axis (drawing only).
    if (bone >= AIM_LEGS_PELVIS_BONE && bone <= 14 && sLegTwist != 0.0f && view != &sViewmodelView) {
        SVec3* pivot = &PLAYER_BONE_POSITIONS(player)[AIM_LEGS_PELVIS_BONE];
        turn_bone(boneMtx, boneVec, (s32)sLegTwist * gAimLegsSign, pivot->x, pivot->z, 0, 0, 0, &turnedMtx, &turnedVec);
        boneMtx = &turnedMtx;
        boneVec = &turnedVec;
    }
    // First person arms (see sFpArmBones): kept pose while walking or for the other arm, plus the view offsets.
    // Only while the buster is up (aimed or firing): the other arm also plays the special weapons, left as they are.
    if (view == &sViewmodelView && gFpArmHold && fp_arm_slot(bone) >= 0 && (sFpAimHeld || fp_buster_active(player))) {
        s32 i = fp_arm_slot(bone);
        bool buster = bone >= 6;
        bool busterUp = TRUE;
        bool walking = player[0x8] == 1 && player[0x9] == 2;
        s32 facing = PLAYER_YAW(player) + 0x800; // camera convention
        s32 down = buster ? gFpBusterDown : gFpOtherDown;
        s32 ahead = buster ? gFpBusterAhead : gFpOtherAhead;
        s32 aheadX, aheadZ;
        s32 f = player[0xA4] % FP_RUN_FRAMES;
        // Walking or aiming, the buster is drawn from the kept pose (aimed and standing, a new shot restarts the
        // animation, whose first frames drop the arm out of view). It would stay still when firing: a fresh shot (the
        // shot leaves at frame 2-3) kicks it back and up, then it settles.
        bool kept = sFpArmValid && busterUp && (walking || !buster || sFpAimHeld);
        if (buster && kept && fp_buster_active(player) && player[0x100] < FP_KICK_FRAMES) {
            s32 kick = sFpKick[player[0x100]] * gFpKick / 10;
            ahead -= kick;
            down -= kick / 2;
        }
        aheadX = (ahead * func_800324B4_D8B4(facing)) >> 12;
        aheadZ = (ahead * func_80032498_D898(facing)) >> 12;
        if (!buster && walking && (sFpRunValid & (1u << f))) {
            turn_bone(&sFpRunMtx[f][i - FP_OTHER_FIRST], &sFpRunPos[f][i - FP_OTHER_FIRST],
                      wrap_angle(PLAYER_YAW(player) - sFpRunYaw[f]), sFpRunRoot[f][0], sFpRunRoot[f][2],
                      PLAYER_POS_X(player) - sFpRunRoot[f][0] + aheadX, PLAYER_POS_Y(player) - sFpRunRoot[f][1] + down,
                      PLAYER_POS_Z(player) - sFpRunRoot[f][2] + aheadZ, &turnedMtx, &turnedVec);
        } else if (kept) {
            turn_bone(&sFpArmMtx[i], &sFpArmPos[i], wrap_angle(PLAYER_YAW(player) - sFpArmYaw), sFpArmRootX, sFpArmRootZ,
                      PLAYER_POS_X(player) - sFpArmRootX + aheadX, PLAYER_POS_Y(player) - sFpArmRootY + down,
                      PLAYER_POS_Z(player) - sFpArmRootZ + aheadZ, &turnedMtx, &turnedVec);
        } else {
            turn_bone(boneMtx, boneVec, 0, 0, 0, aheadX, down, aheadZ, &turnedMtx, &turnedVec);
        }
        boneMtx = &turnedMtx;
        boneVec = &turnedVec;
    }

    func_800306F0_BAF0(view, boneMtx, partMtx);
    func_800308E8_BCE8(view, boneVec, &bonePos);
    partMtx->t[0] = view->t[0] + bonePos.x;
    partMtx->t[1] = view->t[1] + bonePos.y;
    partMtx->t[2] = view->t[2] + bonePos.z;
    sMegaManPartTag = tag;
    func_80084870_5FC70(0, 6, part, partMtx, D_80210B62_1EBF62);
}

// draw_megaman
RECOMP_PATCH void func_80039FE0_153E0(u8* player) {
    PsxMatrix* view = &D_801D4760_1AFB60;
    s32 variant;
    u8* groupCounts;
    s32 group;
    s32 partIndex = 0;
    s32 i;
    //@recomp In first person the camera sits inside the head, so the head, helmet and face are hidden and the rest of
    // the body is drawn through the viewmodel camera.
    bool firstPerson = sFirstPersonActive && sMouseCameraFrames > 0;

    if (PLAYER_COMBAT_STATE(player) == 0) {
        variant = 0;
    } else if (*(s8*)(player + 0x170) != 0) {
        variant = 1;
    } else {
        variant = 2;
    }

    func_800283A8_37A8(0, 6, func_8008128C_5C68C, NULL);
    // Either view: the bones are the same, so aiming or firing standing in third person also keeps the pose.
    fp_arm_capture(player);

    groupCounts = &D_800AC9BC_87DBC[variant * 5];
    for (group = 0; group < 5; group++) {
        for (i = 0; i < groupCounts[group]; i++) {
            s32 part = D_800AC9CC_87DCC[partIndex++];
            if (!firstPerson) {
                draw_megaman_part(player, view, part, part, MEGAMAN_PART_GFX_TAG(part));
            } else if ((FIRST_PERSON_PART_MASK & (1 << part)) && !(sFpProbe && fp_arm_slot(part) >= 0)) {
                draw_megaman_part(player, &sViewmodelView, part, part, MEGAMAN_VIEWMODEL_GFX_TAG(part));
            }
        }
    }

    if (firstPerson) {
        return;
    }

    // The helmet and face are both placed by the head bone, and the face reuses the helmet's matrix.
    draw_megaman_part(player, view, MEGAMAN_HEAD_BONE, MEGAMAN_HELMET_PART, MEGAMAN_PART_GFX_TAG(MEGAMAN_HELMET_PART));
    func_8008498C_5FD8C(0, 6, PLAYER_FACE(player), PLAYER_MOUTH(player), &D_80210990_1EBD90[0]);
}

void guMtxF2L(float mf[4][4], Mtx* m);
float sinf(float x);
float cosf(float x);

static void mtx_l2f(Mtx* m, float mf[4][4]) {
    s32* m1 = &m->m[0][0];
    s32* m2 = &m->m[2][0];
    s32 r, c;

    for (r = 0; r < 4; r++) {
        for (c = 0; c < 2; c++) {
            s32 hi = *m1++;
            s32 lo = *m2++;
            mf[r][2 * c] = (float)(s32)((hi & 0xFFFF0000) | ((lo >> 16) & 0xFFFF)) / 65536.0f;
            mf[r][2 * c + 1] = (float)(s32)((hi << 16) | (lo & 0xFFFF)) / 65536.0f;
        }
    }
}

// The game's own perspective builder (func_8002C238) keeps the PSX conventions: Y points down, so m[1][1] is negative,
// and Z points forward, so m[2][3] is +scale. It also leaves the near and far planes it used in these.
extern f32 D_80210B18_1EBF18; // Near plane.
extern f32 D_8021D928_1F8D28; // Far plane.

// Rebuilds the game's perspective matrix with the first person field of view, and a close near plane in first
// person so Mega Man's arms aren't clipped (the game uses a near plane of ~190 units). The scene's aspect ratio and far
// plane are kept since each area sets up its own projection. Returns FALSE if nothing changed.
// Near/far of the game's projection and the near plane used instead, for the fog remap. Equal when not adjusted.
static float sFogGameNear = 0.0f, sFogNear = 0.0f, sFogFar = 0.0f;
#define FOG_MIN_RANGE 4

// Fog positions (0..1000) cover normalized depth -1..1, which depends on the near plane. Converts the game's positions
// to depths with its near plane and back with the one in use, so the fog starts and ends at the same distances.
static s32 fog_remap(s32 position) {
    float n0 = sFogGameNear, n1 = sFogNear, f = sFogFar;
    float z = position / 500.0f - 1.0f;
    float denom = (f + n0) - z * (f - n0);
    float depth, z1;
    if (denom <= 0.0f) {
        return position;
    }
    depth = 2.0f * f * n0 / denom;
    z1 = (f + n1) / (f - n1) - 2.0f * f * n1 / ((f - n1) * depth);
    position = (s32)((z1 + 1.0f) * 500.0f + 0.5f);
    return position < 0 ? 0 : (position > 1000 ? 1000 : position);
}

void mouse_camera_fog_position(s32* start, s32* end) {
    if (sFogNear <= 0.0f || sFogGameNear <= 0.0f || sFogFar <= sFogGameNear || sFogNear == sFogGameNear) {
        return;
    }
    *start = fog_remap(*start);
    *end = fog_remap(*end);
    // The RSP's fog multiplier is 128000 / (end - start) in 16 bits: a range under 4 overflows and turns everything
    // white. The remapped band is very narrow near 1000, so keep the smallest safe range (4).
    // When the game's fog band is that narrow (far draw distances), widening it would start the fog much closer than the
    // game does, so the fog is pushed past the far plane instead (start 1000 means no fog in front of it).
    if (*end - *start < FOG_MIN_RANGE) {
        *start = 1000;
        *end = 1000 + FOG_MIN_RANGE;
    }
    if (sFirstPersonActive) {
        // First person has its own look: none, or a light haze (the band ends past the far plane, so it never gets
        // fully opaque).
        if (recomp_get_first_person_fog() == 1) {
            *start = 996;
            *end = 1006;
        } else {
            *start = 1000;
            *end = 1000 + FOG_MIN_RANGE;
        }
    }
}

bool mouse_camera_adjust_projection(Mtx* projection, u16* perspNorm) {
    float mf[4][4];
    float scale;
    float aspect;
    float near = D_80210B18_1EBF18;
    float far = D_8021D928_1F8D28;
    float halfFov;
    float xScale;
    s32 r, c;

    sCrosshairVisible = FALSE;
    sFogNear = 0.0f; // No fog remap unless the projection is replaced below.
    if (sMouseCameraFrames <= 0) {
        return FALSE;
    }
    sMouseCameraFrames--;
    sCrosshairVisible = (sFirstPersonActive || sAiming || (sFreeCameraActive && sFocusTarget == NULL)) &&
                        PLAYER_COMBAT_STATE(D_802049B0_1DFDB0) != 0;

    mtx_l2f(projection, mf);
    scale = mf[2][3];
    // Only touch matrices that look like the game's perspective.
    if (scale <= 0.0f || mf[3][3] != 0.0f || mf[0][0] <= 0.0f || mf[1][1] >= 0.0f || near <= 0.0f || far <= near) {
        return FALSE;
    }

    aspect = -mf[1][1] / mf[0][0];
    sFogGameNear = near;
    sFogFar = far;
    if (sFirstPersonActive) {
        near = FIRST_PERSON_NEAR_PLANE;
    } else if (sFreeCameraActive) {
        near = THIRD_PERSON_NEAR_PLANE;
    }
    sFogNear = near;

    // The field of view is horizontal, measured at the game's native 4:3, and only replaces the game's in first person.
    if (sFirstPersonActive) {
        halfFov = (float)recomp_get_mouse_camera_fov() * (3.14159265f / 360.0f);
        xScale = cosf(halfFov) / sinf(halfFov) * (4.0f / 3.0f) / aspect * sAimFovZoom;
    } else {
        // Wider view while sprinting (smaller scale = wider).
        xScale = mf[0][0] / scale / (1.0f + SPRINT_FOV_WIDEN * gSprintAmount) * sAimFovZoom;
    }

    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) {
            mf[r][c] = 0.0f;
        }
    }
    mf[0][0] = xScale * scale;
    mf[1][1] = -xScale * aspect * scale;
    sProjYScale = xScale * aspect;
    mf[2][2] = -(near + far) / (near - far) * scale;
    mf[2][3] = scale;
    mf[3][2] = 2.0f * near * far / (near - far) * scale;
    guMtxF2L(mf, projection);

    if (near + far <= 2.0f) {
        *perspNorm = 65535;
    } else {
        *perspNorm = (u16)((2.0f * 65536.0f) / (near + far));
        if (*perspNorm == 0) {
            *perspNorm = 1;
        }
    }
    return TRUE;
}

extern Gfx* D_801A90F0_1844F0; // Display list head.

static void draw_rect(s32 ulx, s32 uly, s32 lrx, s32 lry, u8 r, u8 g, u8 b, u8 a) {
    gDPSetPrimColor(D_801A90F0_1844F0++, 0, 0, r, g, b, a);
    gDPFillRectangle(D_801A90F0_1844F0++, ulx, uly, lrx, lry);
}

extern Mtx D_801AF370_18A770; // Projection used this frame (PSX conventions: Y down, Z forward).
void* func_800281C8_35C8(s32 size); // Per-frame graphics allocation.
static u32 sPromptFrame = 0;

// Icon outlines, in icon units (about 1/16 of the icon size), Y down, as closed polygons.
typedef struct {
    s8 x, y;
} PromptPoint;

// Speech bubble with a tail at the bottom left.
static const PromptPoint sTalkShape[] = { { -7, -6 }, { -4, -9 }, { 4, -9 }, { 7, -6 }, { 7, 1 },
                                          { 4, 4 }, { -2, 4 }, { -6, 8 }, { -4, 4 }, { -7, 1 } };
// Diamond.
static const PromptPoint sUseShape[] = { { 0, -9 }, { 8, 0 }, { 0, 9 }, { -8, 0 } };
// Up arrow.
static const PromptPoint sEnterShape[] = { { 0, -10 }, { 8, -2 }, { 3, -2 }, { 3, 9 }, { -3, 9 }, { -3, -2 }, { -8, -2 } };
// Focus reticle: a 12 sided ring.
static const PromptPoint sFocusShape[] = { { 0, -7 }, { 4, -6 }, { 6, -4 }, { 7, 0 }, { 6, 4 }, { 4, 6 },
                                           { 0, 7 }, { -4, 6 }, { -6, 4 }, { -7, 0 }, { -6, -4 }, { -4, -6 } };
// Small diamond, used for the unread dots and the active fill.
static const PromptPoint sDot[] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };

static void prompt_shape(PromptKind kind, const PromptPoint** points, s32* count) {
    switch (kind) {
        case PROMPT_TALK:
            *points = sTalkShape;
            *count = sizeof(sTalkShape) / sizeof(sTalkShape[0]);
            break;
        case PROMPT_USE:
            *points = sUseShape;
            *count = sizeof(sUseShape) / sizeof(sUseShape[0]);
            break;
        case PROMPT_FOCUS:
            *points = sFocusShape;
            *count = sizeof(sFocusShape) / sizeof(sFocusShape[0]);
            break;
        default:
            *points = sEnterShape;
            *count = sizeof(sEnterShape) / sizeof(sEnterShape[0]);
            break;
    }
}

static void prompt_vtx(Vtx* v, float x, float y, u8 r, u8 g, u8 b, u8 a) {
    v->v.ob[0] = (s16)x;
    v->v.ob[1] = (s16)y;
    v->v.ob[2] = 0;
    v->v.flag = 0;
    v->v.tc[0] = v->v.tc[1] = 0;
    v->v.cn[0] = r;
    v->v.cn[1] = g;
    v->v.cn[2] = b;
    v->v.cn[3] = a;
}

// Draws a closed polygon as a stroke of the given width (icon units), offset by (ox, oy) icon units.
static void prompt_outline(const PromptPoint* points, s32 count, float unit, float width, float ox, float oy, u8 r,
                           u8 g, u8 b, u8 a) {
    Vtx* v = (Vtx*)func_800281C8_35C8(sizeof(Vtx) * 2 * count);
    s32 i;

    if (v == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        const PromptPoint* prev = &points[(i + count - 1) % count];
        const PromptPoint* cur = &points[i];
        const PromptPoint* next = &points[(i + 1) % count];
        float ax = cur->x - prev->x, ay = cur->y - prev->y;
        float bx = next->x - cur->x, by = next->y - cur->y;
        float la = __builtin_sqrtf(ax * ax + ay * ay), lb = __builtin_sqrtf(bx * bx + by * by);
        float nx, ny, nl, d, miter;
        // Average of the two edge normals, stretched so the stroke keeps its width at the corner (miter, capped).
        nx = -ay / la - by / lb;
        ny = ax / la + bx / lb;
        nl = __builtin_sqrtf(nx * nx + ny * ny);
        if (nl < 0.001f) {
            nx = -ay / la;
            ny = ax / la;
            nl = 1.0f;
        }
        nx /= nl;
        ny /= nl;
        d = nx * (-ay / la) + ny * (ax / la);
        miter = (d > 0.35f ? 0.5f * width / d : 0.5f * width / 0.35f);
        prompt_vtx(&v[2 * i], (cur->x + ox + nx * miter) * unit, (cur->y + oy + ny * miter) * unit, r, g, b, a);
        prompt_vtx(&v[2 * i + 1], (cur->x + ox - nx * miter) * unit, (cur->y + oy - ny * miter) * unit, r, g, b, a);
    }
    gSPVertex(D_801A90F0_1844F0++, v, 2 * count, 0);
    for (i = 0; i < count; i++) {
        s32 j = (i + 1) % count;
        gSP2Triangles(D_801A90F0_1844F0++, 2 * i, 2 * i + 1, 2 * j, 0, 2 * i + 1, 2 * j + 1, 2 * j, 0);
    }
}

// Draws a small filled diamond centered at (cx, cy) icon units.
static void prompt_dot(float unit, float size, float cx, float cy, u8 r, u8 g, u8 b, u8 a) {
    Vtx* v = (Vtx*)func_800281C8_35C8(sizeof(Vtx) * 4);
    s32 i;
    if (v == NULL) {
        return;
    }
    for (i = 0; i < 4; i++) {
        prompt_vtx(&v[i], (cx + sDot[i].x * size) * unit, (cy + sDot[i].y * size) * unit, r, g, b, a);
    }
    gSPVertex(D_801A90F0_1844F0++, v, 4, 0);
    gSP2Triangles(D_801A90F0_1844F0++, 0, 1, 2, 0, 0, 2, 3, 0);
}

// Draws the prompts as camera facing shapes in 3D, each in its own matrix group like the game's actors, so RT64
// interpolates them with the world instead of moving them in 30 Hz steps. Style: white outline with a soft dark
// shadow so it reads on bright and dark scenes; unread conversations show three dots; the active target bobs and has
// a light fill.
static void interact_prompt_draw(void) {
    PsxMatrix* view = &D_801D4760_1AFB60;
    Prompt* prompts = sPrompts[sPromptRead];
    s32 count = sPromptCount[sPromptRead];
    s32 shift = D_80210B61_1EBF61;
    bool stateSet = FALSE;
    s32 p;
    s32 focus = -1;
    float focusOffset = 0.35f; // Only prompts within this much of the screen center (x / depth) can be focused.

    // The game logic (and the prompt update) stops while paused: hide the prompts instead of drawing stale ones.
    if (++sPromptDrawsSinceUpdate > 3) {
        return;
    }
    sPromptFrame++;
    // Focus: the prompt nearest the center of the view, which is where the player is looking with the camera.
    for (p = 0; p < count; p++) {
        float wx = (float)((s32)prompts[p].pos[0] << shift);
        float wy = (float)((s32)prompts[p].pos[1] << shift);
        float wz = (float)((s32)prompts[p].pos[2] << shift);
        float fx = (view->m[0][0] * wx + view->m[0][1] * wy + view->m[0][2] * wz) / 4096.0f + view->t[0];
        float fz = (view->m[2][0] * wx + view->m[2][1] * wy + view->m[2][2] * wz) / 4096.0f + view->t[2];
        float offset;
        if (fz < 120.0f || prompts[p].active) {
            continue;
        }
        offset = fx / fz;
        if (offset < 0.0f) {
            offset = -offset;
        }
        if (offset < focusOffset) {
            focusOffset = offset;
            focus = p;
        }
    }
    for (p = 0; p < count; p++) {
        Prompt* prompt = &prompts[p];
        const PromptPoint* points;
        s32 pointCount;
        float wx, wy, wz, cx, cy, cz, mf[4][4], unit, fade, closeness;
        u8 alpha, shadowAlpha;
        s32 i, j;
        Mtx* m;

        wx = (float)((s32)prompt->pos[0] << shift);
        wy = (float)((s32)prompt->pos[1] << shift);
        wz = (float)((s32)prompt->pos[2] << shift);
        cx = (view->m[0][0] * wx + view->m[0][1] * wy + view->m[0][2] * wz) / 4096.0f + view->t[0];
        cy = (view->m[1][0] * wx + view->m[1][1] * wy + view->m[1][2] * wz) / 4096.0f + view->t[1];
        cz = (view->m[2][0] * wx + view->m[2][1] * wy + view->m[2][2] * wz) / 4096.0f + view->t[2];
        if (cz < 120.0f) {
            continue; // Behind or too close to the camera.
        }

        // Fade in over the outer part of the radius instead of popping in.
        fade = prompt->active ? 1.0f
                              : ((float)PROMPT_RADIUS - (float)prompt->distance) / (float)(PROMPT_RADIUS - PROMPT_FADE_START);
        if (fade > 1.0f) {
            fade = 1.0f;
        }
        if (fade <= 0.02f) {
            continue;
        }
        if (prompt->active) {
            // Gentle bob for the active target.
            cy += ((sPromptFrame / 8) % 4 == 1 || (sPromptFrame / 8) % 4 == 2) ? -4.0f : 0.0f;
        }

        m = (Mtx*)func_800281C8_35C8(sizeof(Mtx));
        if (m == NULL) {
            return;
        }
        for (i = 0; i < 4; i++) {
            for (j = 0; j < 4; j++) {
                mf[i][j] = (i == j) ? 1.0f : 0.0f;
            }
        }
        mf[3][0] = cx;
        mf[3][1] = cy;
        mf[3][2] = cz;
        guMtxF2L(mf, m);

        if (!stateSet) {
            stateSet = TRUE;
            gDPPipeSync(D_801A90F0_1844F0++);
            gDPSetCycleType(D_801A90F0_1844F0++, G_CYC_1CYCLE);
            gDPSetRenderMode(D_801A90F0_1844F0++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
            gDPSetCombineMode(D_801A90F0_1844F0++, G_CC_SHADE, G_CC_SHADE);
            gSPTexture(D_801A90F0_1844F0++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
            gSPClearGeometryMode(D_801A90F0_1844F0++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH | G_FOG | G_TEXTURE_GEN |
                                                          G_TEXTURE_GEN_LINEAR);
            gSPSetGeometryMode(D_801A90F0_1844F0++, G_SHADE | G_SHADING_SMOOTH);
            gSPMatrix(D_801A90F0_1844F0++, &D_801AF370_18A770, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
        }

        // Same size on screen at any depth (icon units grow with cz), then smaller far from Mega Man and growing as he
        // gets closer: full size within 250 units, half at the radius.
        closeness = 1.0f - ((float)prompt->distance - 250.0f) / (float)(PROMPT_RADIUS - 250) * 0.5f;
        if (closeness > 1.0f) {
            closeness = 1.0f;
        } else if (closeness < 0.5f) {
            closeness = 0.5f;
        }
        unit = cz * closeness * (prompt->active ? 0.0036f : (p == focus ? 0.0031f : 0.0026f));
        alpha = (u8)(fade * (prompt->active || p == focus ? 255.0f : 190.0f) * (prompt->read ? 0.7f : 1.0f));
        shadowAlpha = (u8)(fade * 90.0f);

        prompt_shape(prompt->kind, &points, &pointCount);
        gEXMatrixGroupDecomposedNormal(D_801A90F0_1844F0++, prompt->tag, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_NONE);
        gSPMatrix(D_801A90F0_1844F0++, m, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
        // Interactables are only a small dot that guides the eye: a bit larger for the one the camera points at, and
        // gently pulsing for the one in reach. Only the target focus reticle keeps its full shape.
        if (prompt->kind != PROMPT_FOCUS) {
            float size = prompt->active ? 1.6f + 0.18f * (float)((sPromptFrame / 6) % 4 == 1 || (sPromptFrame / 6) % 4 == 2)
                                        : (p == focus ? 1.4f : 1.15f);
            prompt_dot(unit, size + 0.5f, 0.4f, 0.5f, 0, 0, 0, shadowAlpha);
            prompt_dot(unit, size, 0.0f, 0.0f, 255, 255, 255, (u8)(alpha * (prompt->read ? 0.7f : 0.9f)));
            gEXPopMatrixGroup(D_801A90F0_1844F0++, G_MTX_MODELVIEW);
            continue;
        }
        // Soft shadow: a wider dark stroke, slightly down and right.
        prompt_outline(points, pointCount, unit, 3.4f, 0.7f, 0.9f, 0, 0, 0, shadowAlpha);
        if (prompt->active) {
            // Light fill so the active one stands out, then the stroke.
            for (i = 0; i < 3; i++) {
                prompt_dot(unit, 3.2f - i, 0.0f, prompt->kind == PROMPT_TALK ? -2.5f : 0.0f, 255, 255, 255,
                           (u8)(40 * fade));
            }
        }
        prompt_outline(points, pointCount, unit, 1.6f, 0.0f, 0.0f, 255, 255, 255, alpha);
        if (prompt->kind == PROMPT_FOCUS) {
            prompt_dot(unit, 1.3f, 0.0f, 0.0f, 255, 255, 255, alpha);
        }
        if (prompt->kind == PROMPT_TALK && !prompt->read) {
            // Unread: three dots inside the bubble.
            for (i = -1; i <= 1; i++) {
                prompt_dot(unit, 1.1f, i * 3.0f, -2.5f, 0, 0, 0, (u8)(shadowAlpha));
                prompt_dot(unit, 0.9f, i * 3.0f - 0.2f, -2.7f, 255, 255, 255, alpha);
            }
        }
        gEXPopMatrixGroup(D_801A90F0_1844F0++, G_MTX_MODELVIEW);
    }
}

// Sprint vignette: a smooth radial darkening toward the screen edges, drawn as a camera space ring of shaded triangles
// (transparent inner ellipse, dark outer ellipse past the corners) so it gets a real gradient at any resolution.
#define VIGNETTE_SEGMENTS 16
#define VIGNETTE_DEPTH 200.0f
#define VIGNETTE_INNER 0.55f
#define VIGNETTE_MID 0.95f
#define VIGNETTE_OUTER 1.5f
#define VIGNETTE_ALPHA 135.0f
#define VIGNETTE_MID_SHARE 0.3f // Share of the full darkening reached at the middle ring: a soft start, a deep edge.
// A deep blue violet instead of black: closer to a Photoshop overlay/multiply vignette (darker, richer shadows that
// keep some color) than a flat gray fade. The N64 blender can't multiply two colors, so a true overlay isn't possible.
#define VIGNETTE_R 14
#define VIGNETTE_G 10
#define VIGNETTE_B 34

static void sprint_vignette_draw(void) {
    float proj[4][4], mf[4][4];
    float rx, ry, widen;
    Vtx* v;
    Mtx* m;
    s32 i, j;
    u8 a;

    if (gSprintAmount < 0.02f) {
        return;
    }
    v = (Vtx*)func_800281C8_35C8(sizeof(Vtx) * 3 * VIGNETTE_SEGMENTS);
    m = (Mtx*)func_800281C8_35C8(sizeof(Mtx));
    if (v == NULL || m == NULL) {
        return;
    }
    mtx_l2f(&D_801AF370_18A770, proj);
    if (proj[0][0] <= 0.0f || proj[1][1] == 0.0f) {
        return;
    }
    // Half extents of the view at VIGNETTE_DEPTH. RT64 widens the view to the window, so widen x to match.
    widen = recomp_get_target_aspect_ratio(4.0f / 3.0f) / (4.0f / 3.0f);
    rx = VIGNETTE_DEPTH / proj[0][0] * widen;
    ry = VIGNETTE_DEPTH / (proj[1][1] < 0.0f ? -proj[1][1] : proj[1][1]);
    a = (u8)(VIGNETTE_ALPHA * gSprintAmount);

    for (i = 0; i < VIGNETTE_SEGMENTS; i++) {
        s32 angle = i * 0x1000 / VIGNETTE_SEGMENTS;
        float c = func_80032498_D898(angle) / 4096.0f;
        float sn = func_800324B4_D8B4(angle) / 4096.0f;
        prompt_vtx(&v[3 * i], c * rx * VIGNETTE_INNER, sn * ry * VIGNETTE_INNER, VIGNETTE_R, VIGNETTE_G, VIGNETTE_B, 0);
        prompt_vtx(&v[3 * i + 1], c * rx * VIGNETTE_MID, sn * ry * VIGNETTE_MID, VIGNETTE_R, VIGNETTE_G, VIGNETTE_B,
                   (u8)(a * VIGNETTE_MID_SHARE));
        prompt_vtx(&v[3 * i + 2], c * rx * VIGNETTE_OUTER, sn * ry * VIGNETTE_OUTER, VIGNETTE_R, VIGNETTE_G, VIGNETTE_B, a);
        v[3 * i].v.ob[2] = v[3 * i + 1].v.ob[2] = v[3 * i + 2].v.ob[2] = (s16)VIGNETTE_DEPTH;
    }
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            mf[i][j] = (i == j) ? 1.0f : 0.0f;
        }
    }
    guMtxF2L(mf, m);

    gDPPipeSync(D_801A90F0_1844F0++);
    gDPSetCycleType(D_801A90F0_1844F0++, G_CYC_1CYCLE);
    gDPSetRenderMode(D_801A90F0_1844F0++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(D_801A90F0_1844F0++, G_CC_SHADE, G_CC_SHADE);
    gSPTexture(D_801A90F0_1844F0++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gSPClearGeometryMode(D_801A90F0_1844F0++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH | G_FOG | G_TEXTURE_GEN |
                                                  G_TEXTURE_GEN_LINEAR);
    gSPSetGeometryMode(D_801A90F0_1844F0++, G_SHADE | G_SHADING_SMOOTH);
    gSPMatrix(D_801A90F0_1844F0++, &D_801AF370_18A770, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    // Fixed to the camera, so nothing to interpolate.
    gEXMatrixGroupSkipAll(D_801A90F0_1844F0++, G_EX_ID_IGNORE, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_NONE);
    gSPMatrix(D_801A90F0_1844F0++, m, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    // The RSP's vertex buffer holds 32 vertices and the three rings have 48, so they're loaded in two halves of 8
    // segments each (9 spokes, the last one shared with the other half).
    for (j = 0; j < 2; j++) {
        s32 first = j * (VIGNETTE_SEGMENTS / 2);
        Vtx* half = (Vtx*)func_800281C8_35C8(sizeof(Vtx) * 3 * (VIGNETTE_SEGMENTS / 2 + 1));
        if (half == NULL) {
            break;
        }
        for (i = 0; i <= VIGNETTE_SEGMENTS / 2; i++) {
            s32 seg = (first + i) % VIGNETTE_SEGMENTS;
            half[3 * i] = v[3 * seg];
            half[3 * i + 1] = v[3 * seg + 1];
            half[3 * i + 2] = v[3 * seg + 2];
        }
        gSPVertex(D_801A90F0_1844F0++, half, 3 * (VIGNETTE_SEGMENTS / 2 + 1), 0);
        for (i = 0; i < VIGNETTE_SEGMENTS / 2; i++) {
            s32 a0 = 3 * i, b0 = 3 * (i + 1);
            gSP2Triangles(D_801A90F0_1844F0++, a0, a0 + 1, b0, 0, a0 + 1, b0 + 1, b0, 0);
            gSP2Triangles(D_801A90F0_1844F0++, a0 + 1, a0 + 2, b0 + 1, 0, a0 + 2, b0 + 2, b0 + 1, 0);
        }
    }
    gEXPopMatrixGroup(D_801A90F0_1844F0++, G_MTX_MODELVIEW);
}

// Draws a small ring and dot at the center of the screen.
// 3x5 pixel font for button letters: each glyph is 5 rows of 3 bits (bit 2 = left column).
static const u8 sGlyphFont[36][5] = {
    { 2, 5, 7, 5, 5 }, { 6, 5, 6, 5, 6 }, { 3, 4, 4, 4, 3 }, { 6, 5, 5, 5, 6 }, { 7, 4, 6, 4, 7 }, { 7, 4, 6, 4, 4 },
    { 3, 4, 5, 5, 3 }, { 5, 5, 7, 5, 5 }, { 7, 2, 2, 2, 7 }, { 1, 1, 1, 5, 2 }, { 5, 5, 6, 5, 5 }, { 4, 4, 4, 4, 7 },
    { 5, 7, 7, 5, 5 }, { 6, 5, 5, 5, 5 }, { 2, 5, 5, 5, 2 }, { 6, 5, 6, 4, 4 }, { 2, 5, 5, 6, 3 }, { 6, 5, 6, 5, 5 },
    { 3, 4, 2, 1, 6 }, { 7, 2, 2, 2, 2 }, { 5, 5, 5, 5, 7 }, { 5, 5, 5, 5, 2 }, { 5, 5, 7, 7, 5 }, { 5, 5, 2, 5, 5 },
    { 5, 5, 2, 2, 2 }, { 7, 1, 2, 4, 7 }, { 2, 5, 5, 5, 2 }, { 2, 6, 2, 2, 7 }, { 6, 1, 2, 4, 7 }, { 6, 1, 2, 1, 6 },
    { 5, 5, 7, 1, 1 }, { 7, 4, 6, 1, 6 }, { 3, 4, 6, 5, 2 }, { 7, 1, 2, 2, 2 }, { 2, 5, 2, 5, 2 }, { 2, 5, 3, 1, 6 },
};

static void glyph_char(s32 x, s32 y, char ch, u8 r, u8 g, u8 b, u8 a) {
    s32 index, row, col;
    if (ch >= 'A' && ch <= 'Z') {
        index = ch - 'A';
    } else if (ch >= '0' && ch <= '9') {
        index = 26 + ch - '0';
    } else {
        // Anything else (Space, Enter...): a small bar.
        draw_rect(x - 1, y + 2, x + 4, y + 3, r, g, b, a);
        return;
    }
    for (row = 0; row < 5; row++) {
        for (col = 0; col < 3; col++) {
            if (sGlyphFont[index][row] & (4 >> col)) {
                draw_rect(x + col, y + row, x + col + 1, y + row + 1, r, g, b, a);
            }
        }
    }
}

// Sky scroll speed. Each sky layer scrolls by the camera yaw times a per layer rate that matched the original 4:3 view.
// Stretched to widescreen and seen through a different field of view, the sky slid against the world (nearly twice
// as fast), which looked like it moved the wrong way. The yaw the sky update reads (func_80037F78, us.rev1.toml hook)
// is replaced by an unwrapped yaw scaled so the sky moves as many screen pixels as the world does.
static s32 sSkyLastYaw = -1;
static float sSkyAcc = 0.0f; // Screen distance the camera has turned, accumulated (per frame, not per layer).
// Pokeable (poke.py takes the float's bits) so the calibration can be tuned against measurements with the game running.
float gSkyCalibrationH = 1.0f;
float gSkyCalibrationV = 1.0f;
float gSkyZoomDamp = 1.0f; // Slows the sky while the view is zoomed in (aiming): its picture doesn't grow with the zoom.
float gSkyLead = 0.15f; // Fraction of the last frame's turn the sky is drawn ahead by.
s32 gSkyAspectFix = 1; // live: 1 = account for the window's aspect ratio (see sky_scale_yaw)
#define SKY_GAME_FRAME (*(volatile u32*)0x8016281C)
static u32 sSkyFrame = 0;
static float sSkyFrameStep = 0.0f;

s32 sky_scale_yaw(s32 yaw, s32 rate) {
    float mf[4][4], proj, world, sky;
    yaw &= 0xFFF;
    mtx_l2f(&D_801AF370_18A770, mf);
    if (rate <= 0 || mf[2][3] == 0.0f || mf[0][0] <= 0.0f) {
        sSkyLastYaw = yaw;
        return yaw;
    }
    proj = mf[0][0] / mf[2][3];
    // Screen pixels (of the 320 wide screen the sky is stretched across) the world moves per yaw unit.
    world = 160.0f * proj * gSkyCalibrationH * (6.2831853f / 4096.0f) / (1.0f + gSkyZoomDamp * (sAimFovZoom - 1.0f));
    // RT64 widens the 4:3 view to the window, which spreads the world over more pixels per degree, while the sky is
    // stretched from 320 to the full width: per sky unit the world then moves (4/3) / aspect as far.
    if (gSkyAspectFix) {
        world *= (4.0f / 3.0f) / recomp_get_target_aspect_ratio(4.0f / 3.0f);
    }
    if (sSkyFrame != SKY_GAME_FRAME) {
        sSkyFrame = SKY_GAME_FRAME;
        sSkyFrameStep = 0.0f;
    }
    if (sSkyLastYaw < 0) {
        sSkyAcc = (float)yaw * world;
    } else {
        // Accumulated, so a zoom (the projection changing while the camera doesn't turn) doesn't slide the sky.
        float step = (float)(((yaw - sSkyLastYaw + 0x800) & 0xFFF) - 0x800) * world;
        sSkyAcc += step;
        sSkyFrameStep += step; // Every layer calls this in the same frame, only the first one sees the turn.
    }
    sSkyLastYaw = yaw;
    sky = (float)rate / 128.0f;
    // Sky rectangles can't be interpolated by RT64 like the world, so the sky holds one position for the whole game
    // frame while the world glides through it. Showing it part of the way toward the next frame (this frame's turn
    // repeated) halves how far it trails the world on screen.
    return ((s32)((sSkyAcc + gSkyLead * sSkyFrameStep) / sky)) & 0xFFF;
}

// The same for looking up and down. The sky update turns the camera pitch (signed, 4096 per turn) into a vertical
// scroll of 240 / 13 / 32 = 0.577 screen pixels per unit, which matched the game's own 4:3 projection. A sky at infinity
// has to move exactly as far as the world does, so the pitch is scaled by the vertical size of the projection in use.
#define SKY_PITCH_ORIGINAL_PX 0.5769f
s32 sky_scale_pitch(s32 pitch) {
    float mf[4][4], proj, world;
    s32 signedPitch = (pitch & 0xFFF) >= 0x800 ? (pitch & 0xFFF) - 0x1000 : (pitch & 0xFFF);
    mtx_l2f(&D_801AF370_18A770, mf);
    if (mf[2][3] == 0.0f || mf[1][1] >= 0.0f) {
        return pitch;
    }
    proj = -mf[1][1] / mf[2][3];
    // Divided by the zoom like the horizontal one: sky_zoom_rect scales the tiles' positions by it afterwards.
    world = 120.0f * proj * gSkyCalibrationV * (6.2831853f / 4096.0f) / (1.0f + gSkyZoomDamp * (sAimFovZoom - 1.0f));
    // Kept inside the range the sky update reads as a plain signed angle (it wraps at 0x800).
    return clamp_s32((s32)(signedPitch * world / SKY_PITCH_ORIGINAL_PX), -0x7FF, 0x7FF);
}

// Sky zoom. The sky is drawn with texture rectangles that the projection never touches, so when the view zooms in
// (aiming) the world grows and the sky doesn't. Right before each sky rectangle's commands are done (us.rev1.toml hook in
// func_8002E8FC), the rectangle is scaled around the screen center by the zoom: its edges move out, and its texture step
// (dsdx, dtdy) shrinks so the picture is magnified. gSkyZoomRects = 0 turns it off. The sky scroll itself is divided by
// the zoom in sky_scale_yaw (gSkyZoomDamp = 1) since scaling the positions already magnifies the motion.
// f1: the rectangle's G_RDPHALF_2 command; before it come E4 (lr), ul, E1 (s, t), after it comes the D(s)/D(t) word.
s32 gSkyZoomRects = 1;
float gSkyZoomForce = 0.0f; // Debug: zoom the sky as if the view were zoomed by this much.
s32 gSkyZoomPad = 2;       // Debug: grow each tile by this many quarter pixels right and down.
void sky_zoom_rect(u32* f1) {
    float z = gSkyZoomForce > 1.0f ? gSkyZoomForce : sAimFovZoom;
    u32 w0 = f1[-4], w1 = f1[-3], st = f1[-1], step = f1[1];
    float x0, y0, x1, y1, nx0, ny0, nx1, ny1, s, t;
    s32 ulx, uly, lrx, lry, ss, tt, dsdx, dtdy;

    if (!gSkyZoomRects || z < 1.001f) {
        return;
    }
    // Quarter pixels; a rectangle cut by the screen's edge has its cut turned into the texture offset (8 per quarter).
    x0 = (float)((s32)((w1 >> 12) & 0xFFF)) - (float)(s16)(st >> 16) / 8.0f;
    y0 = (float)((s32)(w1 & 0xFFF)) - (float)(s16)(st & 0xFFFF) / 8.0f;
    x1 = (float)((s32)((w0 >> 12) & 0xFFF) + 1);
    y1 = (float)((s32)(w0 & 0xFFF) + 1);
    nx0 = 640.0f + (x0 - 640.0f) * z;
    ny0 = 480.0f + (y0 - 480.0f) * z;
    nx1 = 640.0f + (x1 - 640.0f) * z;
    ny1 = 480.0f + (y1 - 480.0f) * z;
    // The tile is wTex x hTex texels drawn nx1 - nx0 quarter pixels wide. RT64 samples at pixel centers and filters
    // bilinearly, so with a texture step under 1 the first and last pixels would read half a texel past the tile
    // (black). The step is set so the pixel centers span exactly [0.5, size - 0.5] texels, and the start is moved to
    // match (S10.5 for s and t, S5.10 for the steps); a cut at the screen edge skips the pixels cut off.
    {
        float wTex = (x1 - x0) / 4.0f, hTex = (y1 - y0) / 4.0f;
        float wPx = (nx1 - nx0) / 4.0f, hPx = (ny1 - ny0) / 4.0f;
        float stepS = wPx > 1.0f ? (wTex - 1.0f) / (wPx - 1.0f) : 1.0f;
        float stepT = hPx > 1.0f ? (hTex - 1.0f) / (hPx - 1.0f) : 1.0f;
        float cutX = nx0 < 0.0f ? -nx0 / 4.0f : 0.0f; // Pixels cut off the left / top.
        float cutY = ny0 < 0.0f ? -ny0 / 4.0f : 0.0f;
        s = (0.5f - 0.5f * stepS + cutX * stepS) * 32.0f;
        t = (0.5f - 0.5f * stepT + cutY * stepT) * 32.0f;
        dsdx = (s32)(stepS * 1024.0f);
        dtdy = (s32)(stepT * 1024.0f);
    }
    ulx = nx0 < 0.0f ? 0 : (s32)nx0;
    uly = ny0 < 0.0f ? 0 : (s32)ny0;
    lrx = (s32)nx1 - 1 + gSkyZoomPad;
    lry = (s32)ny1 - 1 + gSkyZoomPad;
    lrx = lrx < 0 ? 0 : (lrx > 0xFFF ? 0xFFF : lrx);
    lry = lry < 0 ? 0 : (lry > 0xFFF ? 0xFFF : lry);
    ss = (s32)s > 0x7FFF ? 0x7FFF : (s32)s;
    tt = (s32)t > 0x7FFF ? 0x7FFF : (s32)t;
    f1[-4] = (w0 & 0xFF000000) | ((u32)lrx << 12) | (u32)lry;
    f1[-3] = ((u32)(ulx > 0xFFF ? 0xFFF : ulx) << 12) | (u32)(uly > 0xFFF ? 0xFFF : uly);
    f1[-1] = ((u32)ss << 16) | (u32)tt;
    f1[1] = ((u32)dsdx << 16) | ((u32)dtdy & 0xFFFF);
}

// A pad button drawn like the dialogue prompt: PlayStation face buttons (button 0 cross, 1 circle, 2 square,
// 3 triangle) as shapes on a white button, Xbox face buttons as colored disks, anything else as a gray button with
// up to 2 letters (label).
static void draw_pad_glyph(s32 cx, s32 cy, bool sony, s32 button, const char* label, u8 alpha) {
    static const u8 xboxColors[4][3] = { { 60, 170, 60 }, { 220, 45, 40 }, { 40, 110, 220 }, { 230, 180, 30 } };
    static const u8 sonyColors[4][3] = { { 90, 140, 230 }, { 255, 102, 102 }, { 220, 120, 200 }, { 60, 200, 160 } };
    bool face = button >= 0 && button <= 3;
    s32 x, y;
    for (y = -6; y <= 6; y++) {
        for (x = -6; x <= 6; x++) {
            s32 d2 = x * x + y * y;
            bool mark = FALSE;
            if (d2 > 42) {
                continue;
            } else if (d2 > 30) {
                draw_rect(cx + x, cy + y, cx + x + 1, cy + y + 1, 0, 0, 0, (u8)(alpha * 2 / 3));
                continue;
            }
            if (sony && face) {
                switch (button) {
                    case 0: mark = (x == y || x == -y) && d2 <= 13; break;
                    case 1: mark = d2 >= 6 && d2 <= 13; break;
                    case 2: mark = (x == -3 || x == 3 || y == -3 || y == 3) && x >= -3 && x <= 3 && y >= -3 && y <= 3; break;
                    default: mark = (y == 2 && x >= -3 && x <= 3) || (y >= -3 && y <= 2 && (x == (y - 2) / 2 || x == -((y - 2) / 2))); break;
                }
                if (mark) {
                    draw_rect(cx + x, cy + y, cx + x + 1, cy + y + 1, sonyColors[button][0], sonyColors[button][1],
                              sonyColors[button][2], alpha);
                } else {
                    draw_rect(cx + x, cy + y, cx + x + 1, cy + y + 1, 235, 235, 240, alpha);
                }
            } else if (face) {
                draw_rect(cx + x, cy + y, cx + x + 1, cy + y + 1, xboxColors[button][0], xboxColors[button][1],
                          xboxColors[button][2], alpha);
            } else {
                draw_rect(cx + x, cy + y, cx + x + 1, cy + y + 1, 110, 110, 120, alpha);
            }
        }
    }
    if (!(sony && face) && label != NULL) {
        s32 n = label[1] != 0 ? 2 : 1;
        glyph_char(cx - 1 - (n - 1) * 2, cy - 2, label[0], 255, 255, 255, alpha);
        if (n == 2) {
            glyph_char(cx + 1, cy - 2, label[1], 255, 255, 255, alpha);
        }
    }
}

static s32 glyph_text(s32 x, s32 y, const char* text, u8 a) {
    for (; *text != 0; text++, x += 4) {
        if (*text != ' ') {
            glyph_char(x, y, *text, 235, 235, 240, a);
        }
    }
    return x;
}

// A key cap with its label, as the dialogue prompt draws keys; returns the x after it.
static s32 draw_key_cap(s32 x, s32 cy, const char* label, u8 alpha) {
    s32 w = 0;
    const char* c;
    for (c = label; *c != 0; c++) {
        w += 4;
    }
    draw_rect(x, cy - 6, x + w + 6, cy + 7, 0, 0, 0, (u8)(alpha * 2 / 3));
    draw_rect(x + 1, cy - 5, x + w + 5, cy + 6, 235, 235, 240, alpha);
    draw_rect(x + 2, cy - 4, x + w + 4, cy + 4, 60, 60, 70, alpha);
    for (c = label; *c != 0; c++) {
        glyph_char(x + 3 + (s32)(c - label) * 4, cy - 3, *c, 255, 255, 255, alpha);
    }
    return x + w + 7;
}

// Button legend under the pause menu and the map: what the modern layout's buttons do there (modern_pad), with the
// glyphs of the device in use. shortcut: shows the default layout's buttons, not the user's bindings; read the
// bindings (like the dialogue prompt's C-Down) if menu buttons ever become rebindable in the UI.
#define MENU_LEGEND_Y 229
static void menu_legend_draw(void) {
    struct Item { s32 pad[2]; const char* xlabel[2]; const char* key[2]; const char* text; };
    // pad: SDL button (0 south, 1 east, 2 west, 3 north, -1 none, 9/10 shoulders drawn with the label).
    static const struct Item items[] = {
        { { 1, -1 }, { NULL, NULL }, { "E", NULL }, "CONFIRM" },
        { { 0, -1 }, { NULL, NULL }, { "BKSP", NULL }, "BACK" },
        { { 9, 10 }, { "L1", "R1" }, { "Q", "R" }, "PAGE" },
        { { 3, -1 }, { NULL, NULL }, { "TAB", NULL }, "RESET" },
    };
    // The map only closes (Triangle, Cross or Options; Circle and the shoulders do nothing there, legendshot.py).
    static const struct Item mapItems[] = {
        { { 3, 0 }, { NULL, NULL }, { "TAB", "BKSP" }, "CLOSE" },
    };
    const struct Item* list = sMenuIsMap ? mapItems : items;
    s32 count = sMenuIsMap ? 1 : (s32)(sizeof(items) / sizeof(items[0]));
    s32 style = recomp_get_prompt_style() & 0xFF;
    bool keyboard = style == 2, sony = style == 0;
    s32 x = 52, i, k;
    u8 alpha = 230;

    if (!gModernPad || MENU_CALLBACKS == 0) {
        return;
    }
    gDPPipeSync(D_801A90F0_1844F0++);
    gDPSetCycleType(D_801A90F0_1844F0++, G_CYC_1CYCLE);
    gDPSetRenderMode(D_801A90F0_1844F0++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(D_801A90F0_1844F0++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gEXSetRectAspect(D_801A90F0_1844F0++, G_EX_ASPECT_AUTO);
    draw_rect(sMenuIsMap ? 112 : 44, MENU_LEGEND_Y - 9, sMenuIsMap ? 196 : 276, MENU_LEGEND_Y + 10, 0, 0, 20, 150);
    if (sMenuIsMap) {
        x = 120;
    }
    for (i = 0; i < count; i++) {
        for (k = 0; k < 2; k++) {
            if (keyboard) {
                if (list[i].key[k] != NULL) {
                    x = draw_key_cap(x, MENU_LEGEND_Y, list[i].key[k], alpha) + 1;
                }
            } else if (list[i].pad[k] >= 0) {
                const char* label = list[i].xlabel[k];
                // Xbox names the shoulders LB/RB.
                if (!sony && list[i].pad[k] >= 9) {
                    label = list[i].pad[k] == 9 ? "LB" : "RB";
                }
                draw_pad_glyph(x + 6, MENU_LEGEND_Y, sony, list[i].pad[k], label, alpha);
                x += 15;
            }
        }
        x = glyph_text(x + 2, MENU_LEGEND_Y - 2, list[i].text, alpha) + 8;
    }
    gDPPipeSync(D_801A90F0_1844F0++);
}

// Dialogue "continue" prompt: while a text box has finished typing and waits for the player, a PlayStation circle
// (the button bound to advance text, C-Down) bobs in the box's lower right corner, like modern games do.
// Found by recording RAM through a conversation: the talking player sits in state 1, substate 0x10, and this byte is
// 180 while a page is typing and 160 once it waits.
#define DIALOG_TYPING_BYTE (*(volatile u8*)0x801ADC8F)
#define DIALOG_WAITING_VALUE 160
#define DIALOG_PROMPT_X 287
#define DIALOG_PROMPT_Y 214
static u32 sDialogPromptFrames = 0;

static void dialog_prompt_draw(void) {
    u8* player = D_802049B0_1DFDB0;
    s32 cx, cy, style;
    u8 alpha;

    if (!(player[0x8] == 1 && player[0x9] == 0x10 && DIALOG_TYPING_BYTE == DIALOG_WAITING_VALUE)) {
        sDialogPromptFrames = 0;
        return;
    }
    sDialogPromptFrames++;
    // Fade in, then bob down and up by a pixel every half second.
    alpha = sDialogPromptFrames > 8 ? 255 : (u8)(sDialogPromptFrames * 32);
    cx = DIALOG_PROMPT_X;
    cy = DIALOG_PROMPT_Y + (((sDialogPromptFrames / 15) & 1) ? 1 : 0);

    gDPPipeSync(D_801A90F0_1844F0++);
    gDPSetCycleType(D_801A90F0_1844F0++, G_CYC_1CYCLE);
    gDPSetRenderMode(D_801A90F0_1844F0++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(D_801A90F0_1844F0++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gEXSetRectAspect(D_801A90F0_1844F0++, G_EX_ASPECT_AUTO);
    style = recomp_get_prompt_style();
    if ((style & 0xFF) == 2) {
        // Keyboard: a key cap with the bound key's letter.
        char key = (char)((style >> 8) & 0xFF);
        draw_rect(cx - 6, cy - 6, cx + 7, cy + 7, 0, 0, 0, (u8)(alpha * 2 / 3));
        draw_rect(cx - 5, cy - 5, cx + 6, cy + 6, 235, 235, 240, alpha);
        draw_rect(cx - 4, cy - 4, cx + 5, cy + 4, 60, 60, 70, alpha);
        glyph_char(cx - 1, cy - 3, key != 0 ? key : '?', 255, 255, 255, alpha);
    } else {
        // The pad button bound to C-Down: PlayStation face buttons as their shapes on a white button, Xbox face
        // buttons as colored letters, anything else (shoulders, d-pad...) as a gray button with a letter.
        static const char otherLetters[15] = { 'A', 'B', 'X', 'Y', 'S', 'H', 'S', 'L', 'R', 'L', 'R', 'U', 'D', 'L', 'R' };
        s32 button = (style >> 16) & 0xFF;
        char label[2];
        label[0] = button < 15 ? otherLetters[button] : '?';
        label[1] = 0;
        draw_pad_glyph(cx, cy, (style & 0xFF) == 0, button, label, alpha);
    }
    gDPPipeSync(D_801A90F0_1844F0++);
}

void mouse_camera_draw_crosshair(void) {
    const s32 cx = SCREEN_WIDTH / 2;
    const s32 cy = SCREEN_HEIGHT / 2 - (s32)(sCrosshairRaise * (SCREEN_HEIGHT / 2));

    if (!gDefinitive) {
        return; // base port: no crosshair, prompts, vignette or legend
    }
    interact_prompt_draw();
    sprint_vignette_draw();
    dialog_prompt_draw();
    menu_legend_draw();

    if (sHitmarkerFrames > 0) {
        sHitmarkerFrames--;
    }
    if (!sCrosshairVisible) {
        return;
    }

    gDPPipeSync(D_801A90F0_1844F0++);
    gDPSetCycleType(D_801A90F0_1844F0++, G_CYC_1CYCLE);
    gDPSetRenderMode(D_801A90F0_1844F0++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(D_801A90F0_1844F0++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gEXSetRectAspect(D_801A90F0_1844F0++, G_EX_ASPECT_AUTO);

    // Small ring with a center dot in Mega Man's buster blue, outlined in dark navy so it reads on any background.
    {
        s32 x, y;
        for (y = -6; y <= 6; y++) {
            for (x = -6; x <= 6; x++) {
                s32 d2 = x * x + y * y;
                if (d2 >= 9 && d2 <= 36 && !(d2 >= 13 && d2 <= 25)) {
                    draw_rect(cx + x, cy + y, cx + x + 1, cy + y + 1, 10, 20, 60, 170); // Outline: radius 3 and 6.
                } else if (d2 >= 13 && d2 <= 25) {
                    draw_rect(cx + x, cy + y, cx + x + 1, cy + y + 1, 90, 200, 255, 235); // Ring: radius 4-5.
                }
            }
        }
        draw_rect(cx - 1, cy - 1, cx + 2, cy + 2, 10, 20, 60, 170);
        draw_rect(cx, cy, cx + 1, cy + 1, 255, 255, 255, 255);
    }

    // Hitmarker: a red X between the arms of the cross, fading out.
    if (sHitmarkerFrames > 0) {
        u8 alpha = (u8)(255 * sHitmarkerFrames / HITMARKER_FRAMES);
        s32 d;
        for (d = 3; d <= 7; d++) {
            draw_rect(cx - d, cy - d, cx - d + 2, cy - d + 2, 255, 50, 40, alpha);
            draw_rect(cx + d - 1, cy - d, cx + d + 1, cy - d + 2, 255, 50, 40, alpha);
            draw_rect(cx - d, cy + d - 1, cx - d + 2, cy + d + 1, 255, 50, 40, alpha);
            draw_rect(cx + d - 1, cy + d - 1, cx + d + 1, cy + d + 1, 255, 50, 40, alpha);
        }
    }

    gDPPipeSync(D_801A90F0_1844F0++);
}
