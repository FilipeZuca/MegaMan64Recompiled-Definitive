#ifndef MM64_H
#define MM64_H

// Addresses found by disassembling megaman64.us.rev1 (main segment). Names are ours.

typedef unsigned char u8;
typedef unsigned short u16;
typedef signed char s8;
typedef signed short s16;

// Raw pad as read each frame by func_80028C14_4014 (nusys NUContData, 8 bytes per port).
typedef struct {
    u16 button;
    s8 stick_x;
    s8 stick_y;
    u8 errno;
    u16 trigger;
} RawPad;
#define gRawPads ((RawPad*)0x801BC380)

// func_80032D70_E170 turns the raw pad into a PSX-layout mask through this table,
// indexed by N64 button bit (0 = C-Right ... 15 = A). The stick becomes d-pad bits too.
#define gPadMap ((u16*)0x800A8D78)
enum { N64_C_RIGHT, N64_C_LEFT, N64_C_DOWN, N64_C_UP, N64_R, N64_L, N64_UNUSED6, N64_UNUSED7,
       N64_D_RIGHT, N64_D_LEFT, N64_D_DOWN, N64_D_UP, N64_START, N64_Z, N64_B, N64_A };

// Virtual pad masks (PSX bit layout). Stock mapping in parentheses.
#define VPAD_SELECT   0x0001 // (C-Up)
#define VPAD_START    0x0008
#define VPAD_UP       0x0010
#define VPAD_RIGHT    0x0020
#define VPAD_DOWN     0x0040
#define VPAD_LEFT     0x0080
#define VPAD_L2       0x0100 // (C-Right)
#define VPAD_LOCKON   0x0200 // R2: the game sets it itself when L1+R1 are both held
#define VPAD_TURN_L   0x0400 // L1 (L and Z)
#define VPAD_TURN_R   0x0800 // R1 (R)
#define VPAD_TRIANGLE 0x1000 // (C-Left)
#define VPAD_CIRCLE   0x2000 // (C-Down)
#define VPAD_JUMP     0x4000 // Cross (A)
#define VPAD_SHOOT    0x8000 // Square (B)

// Virtual pad written by func_80032D70_E170.
typedef struct {
    u8 unk0;
    u8 unk1;
    u16 held;
    u16 prev;
    u16 pressed;
    u16 released;
} VPad;
#define gVPad ((VPad*)0x801D87C0)

// Player actors: 2 slots of 0x5F4 bytes. Slot 0 is Mega Man.
// Known offsets: 0x14/0x16/0x18 = x/z/y (s16), 0x56 = yaw (s16), 0x6E = hp (s16).
#define gPlayer ((u8*)0x802049B0)

#endif
