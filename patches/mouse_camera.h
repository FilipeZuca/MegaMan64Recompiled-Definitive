#ifndef MOUSE_CAMERA_H
#define MOUSE_CAMERA_H

#include "patch_helpers.h"

typedef enum {
    MOUSE_CAMERA_OFF = 0,
    MOUSE_CAMERA_THIRD_PERSON = 1,
    MOUSE_CAMERA_FIRST_PERSON = 2,
} MouseCameraMode;

DECLARE_FUNC(s32, recomp_get_mouse_camera_mode);
// Mouse movement since the previous call, scaled by the mouse sensitivity.
DECLARE_FUNC(void, recomp_get_mouse_camera_deltas, float* x, float* y);
// Horizontal field of view in degrees at 4:3.
DECLARE_FUNC(s32, recomp_get_mouse_camera_fov);
// Whether over the shoulder aiming (right mouse button in third person) is held.
DECLARE_FUNC(s32, recomp_get_mouse_camera_aim);
// Mouse wheel notches scrolled since the previous call, positive when scrolling up.
DECLARE_FUNC(s32, recomp_get_mouse_camera_wheel);
// Presses of the Camera Distance input (Create by default) since the previous call.
DECLARE_FUNC(s32, recomp_get_camera_distance_presses);
// R3 presses since the previous call (target focus).
DECLARE_FUNC(s32, recomp_get_focus_presses);
DECLARE_FUNC(s32, recomp_get_prompt_style);
DECLARE_FUNC(s32, recomp_get_first_person_fog);
// Texture filter from the Graphics menu: 0 three-point, 1 bilinear, 2 bilinear with point sampled terrain.
DECLARE_FUNC(s32, recomp_get_texture_filter);
// Non-zero while a menu or dialogue reads the pad: the first person stick strafe (controls.cpp) is skipped.
DECLARE_FUNC(void, recomp_set_pad_menu, s32 menu);
// 1 with Definitive Features On (General menu), 0 for the base port's controls; read into gDefinitive once per frame.
DECLARE_FUNC(s32, recomp_get_definitive);
extern s32 gDefinitive;

#ifdef MIPS
bool mouse_camera_adjust_projection(Mtx* projection, u16* perspNorm);
void mouse_camera_fog_position(s32* start, s32* end);
void mouse_camera_draw_crosshair(void);
#endif

#endif //MOUSE_CAMERA_H
