#include "patches.h"
#include "common_structs.h"
#include "tag_helper.h"
#include "gfx_patches.h"
#include "mouse_camera.h"

bool skip_all_interpolation = FALSE;
bool skip_terrain_interpolation = FALSE;
static bool sTerrainWrapFrame = FALSE; // Mode 3: only the terrain skips interpolation, on the frame of the loop.
static bool sWrapFrame = FALSE; // Mega Man just crossed a world loop, see recomp_check_camera_jump.
// Debug switches (poke.py): find what causes a one frame texture glitch.
s32 gDbgNoInterp = 0;
s32 gDbgNoOverlap = 0;
s32 gDbgDrawScale = 3;
s32 gDbgSkipMask = 0; // Bit n: never interpolate matrices tagged with id n (terrain is 10, xlu terrain 11, actors 1..7).
s32 gDbgWrapMode = 3; // How the world loop is handled: 0 tag offset (+ actors skipped), 1 skip everything, 2 nothing.

// Called by the us.rev1.toml draw distance hook: scales each area's far plane.
s32 draw_distance_scale(s32 far) {
    return far * gDbgDrawScale;
}

void recomp_check_camera_jump();

void yield_self_1ms(void);
extern volatile u32 D_801FFBB0_1DAFB0;

//nuGfxTaskAllEndWait
RECOMP_PATCH void func_80092EB0_6E2B0() {
    while (D_801FFBB0_1DAFB0) {
        yield_self_1ms();
    }
}

extern s32 D_800A6CA8_820A8;
extern GfxTaskNode *D_800D6FAC_B23AC;
extern GfxTaskNode *D_800D6FA8_B23A8; //gTaskPoolHead
extern GfxContext D_802047F0_1DFBF0;
//gfx_add_render_task
RECOMP_PATCH void func_800283A8_37A8(s8 bufferIndex, s8 poolIndex, void *func, void *funcData) {
    GfxTaskNode *taskNode;
    GfxTaskNode *headNode;
    GfxContext *gfxContext;
    GfxContext *gfxContext2;

    gfxContext = &D_802047F0_1DFBF0;

    if (gfxContext->bufferEnabled != 0) {
        taskNode = NULL;

        if (D_800A6CA8_820A8 < 2) {
            headNode = D_800D6FA8_B23A8;
            taskNode = (GfxTaskNode *) ((char *) headNode + 0x18);

            if ((u32) D_800D6FAC_B23AC < (u32) taskNode) {
                taskNode = NULL;
            } else {
                D_800D6FA8_B23A8 = taskNode;
                taskNode = headNode;
            }
        }

        if (taskNode != NULL) {
            gfxContext2 = (GfxContext *) ((char *) gfxContext + (poolIndex << 2));

            taskNode->next = NULL;
            taskNode->prev = NULL;
            taskNode->func = func;
            taskNode->poolIndex = poolIndex;
            taskNode->arg = funcData;
            taskNode->unk12 = 0;

            //@recomp read and embed transform id into padding
            if (funcData != 0) {
                taskNode->unk14 = gCurrGfxTag;
                gCurrGfxTag = 0;
            }

            if (gfxContext2->taskPoolEnd[0] != NULL) {
                taskNode->prev = gfxContext2->taskPoolEnd[0];
                gfxContext2->taskPoolEnd[0]->next = taskNode;
            } else {
                gfxContext2->taskPoolStart[0] = taskNode;
            }
            gfxContext2->taskPoolEnd[0] = taskNode;
        }
    }
}

void func_80025D14_1114(); /* extern */
void func_80092E10_6E210(void *); /* extern */
void func_80093090_6E490(); /* extern */
void func_8009CC40_78040(void *); /* extern */
extern s16 D_800A6C9A_8209A;
extern s16 D_800A6C9C_8209C;
extern s32 D_800A6CA0_820A0;
extern s32 D_800A6CA4_820A4;
extern s32 D_800A6CA8_820A8;
extern GfxTaskNode *D_800D6FA0_B23A0;
extern GfxTaskNode *D_800D6FA4_B23A4;
extern GfxTaskNode *D_800D6FA8_B23A8;
extern GfxBuffers D_800D6FB0_B23B0;
extern Gfx *D_800D6FB4_B23B4;
extern void *D_800D6FB8_B23B8;
extern Gfx D_800D6FC0_B23C0[15360];
extern Gfx D_800F4FC0_D03C0[15360];
extern TaskNode D_80112FC0_EE3C0[2901];
extern TaskNode D_80123FC0_FF3C0[2901];
extern void *D_80134FC0_1103C0;
extern GfxContext *D_8019A6A4_175AA4;
extern Gfx *D_801A90F0_1844F0;

// Menu screens (title, Load Data: loaded scene 1) keep their 2D background at 4:3, but their 3D characters are drawn
// across the whole widescreen frame and showed up on the black side bars. Black bars are drawn over both sides last.
extern u8 D_801BC380_197780[]; // +0xD0: loaded scene (u16)
s32 gMenuScissor = 1; // live switch (tools/re/poke.py)
static bool menu_scene(void) {
    return gMenuScissor && *(u16*)&D_801BC380_197780[0xD0] == 1;
}

// Model fixes (pieces found with RT64's draw call inspector): models that leave a crack at high resolution, fixed by
// moving vertices in RAM. Models are loaded per area, so every half second look for each entry's vertices (a run of
// consecutive Vtx matching `match`) and write `fixed` over them; a fixed model no longer matches. Add a row per bug.
typedef struct {
    s16 count;          // consecutive vertices to match (<= 4)
    s16 match[4][3];    // their x, y, z as the game loads them
    s16 fixed[4][3];    // the new x, y, z
} ModelFix;

static const ModelFix sModelFixes[] = {
    // Tent door leaf (actor, trapezoid at z -400): bottom edge sat above the ground and inside the jambs, a strip of
    // sky showed under it. Push the bottom below the ground (y 24) and behind the jambs (x +-120).
    { 4, { { -64, -256, -400 }, { 64, -256, -400 }, { -96, 0, -400 }, { 96, 0, -400 } },
         { { -64, -256, -400 }, { 64, -256, -400 }, { -120, 24, -400 }, { 120, 24, -400 } } },
};

s32 gModelFix = 1; // live switch (tools/re/poke.py)
s32 gModelFixScans = 0, gModelFixed = 0; // live counters
static s32 sModelFixFrame = 0;
static void model_fixes_update(void) {
    if (!gModelFix || (sModelFixFrame++ % 30) != 0) {
        return;
    }
    gModelFixScans++;
    // shortcut: scans the area/actor heap range where the models were found (0x802FAE78); widen if one is missed.
    // Vertex arrays are only 8 byte aligned: step by 8.
    for (u8 *p = (u8 *)0x80200000; p < (u8 *)0x80400000 - 4 * sizeof(Vtx); p += 8) {
        Vtx *v = (Vtx *)p;
        for (u32 f = 0; f < sizeof(sModelFixes) / sizeof(sModelFixes[0]); f++) {
            const ModelFix *fix = &sModelFixes[f];
            s32 i;
            for (i = 0; i < fix->count; i++) {
                if (v[i].v.ob[0] != fix->match[i][0] || v[i].v.ob[1] != fix->match[i][1] || v[i].v.ob[2] != fix->match[i][2]) {
                    break;
                }
            }
            if (i == fix->count) {
                for (i = 0; i < fix->count; i++) {
                    v[i].v.ob[0] = fix->fixed[i][0];
                    v[i].v.ob[1] = fix->fixed[i][1];
                    v[i].v.ob[2] = fix->fixed[i][2];
                }
                gModelFixed++;
            }
        }
    }
}

extern void *D_801D8828_1B3C28; //unk
extern GfxContext D_802047F0_1DFBF0;
extern s8 D_8020489B_1DFC9B;
extern s8 D_8020559C_1E099C;


GfxTaskNode extendedArena1[2901 * 16];
GfxTaskNode extendedArena2[2901 * 16];
Gfx extendedDisplayList1[15360 * 16];
Gfx extendedDisplayList2[15360 * 16];

//init_display_lists
RECOMP_PATCH void func_80027550_2950(void *arg0) {
    GfxContext *gfxContext2;
    GfxContext *gfxContext;
    GfxContext *var_v1;
    s32 poolIndex;
    s32 index2;
    s32 var_s1;
    s32 index;
    s32 poolSize;
    s32 displayListSize;

    poolSize = 0x11000 * 16;
    displayListSize = 0x1E000 * 16;

    D_800A6C9A_8209A = 0x140; //Scissor X
    D_800A6C9C_8209C = 0xF0; //Scissor Y
    D_800A6CA4_820A4 = -1;

    D_800D6FA0_B23A0 = extendedArena1; //Assign Arena 1 Ptr [2900]
    D_800D6FAC_B23AC = extendedArena2 + poolSize; //Assign Arena 1 Tail
    D_800D6FA8_B23A8 = extendedArena1; //Assign Arena 1 Head


    D_800D6FA4_B23A4 = extendedArena2; //Assign Arena 2 Ptr [2900]
    D_800D6FB0_B23B0.dls[0] = extendedDisplayList1; //gDisplayList1 Ptr

    D_8019A6A4_175AA4 = 0;
    D_8020559C_1E099C = 0;
    D_800A6CA0_820A0 = 0;

    D_800D6FB0_B23B0.dls[1] = extendedDisplayList2; //gDisplayList2 Ptr
    D_801A90F0_1844F0 = extendedDisplayList1 + displayListSize; //Gfx head
    D_800D6FB8_B23B8 = extendedDisplayList1; //Display List End
    D_80134FC0_1103C0 = arg0; //Gfx Thread Func (func_80026A30)

    D_800A6CA8_820A8 = 0; //Additional Scanlines

    func_8009CC40_78040(&D_801D8828_1B3C28);

    var_s1 = 0;
    gfxContext = &D_802047F0_1DFBF0;
    index = 0;
    do {
        bzero(gfxContext, sizeof(GfxContext));
        (&D_8020489B_1DFC9B)[var_s1] = index;
        gfxContext++;
        index += 1;
        var_s1 += sizeof(GfxContext);
    } while (index <= 0);
    bzero((void *) 0x803B5000, 0x4B000);
    func_80025D14_1114();
    func_80092E10_6E210(D_80134FC0_1103C0);
    func_80093090_6E490();
    index2 = 0;
    gfxContext2 = &D_802047F0_1DFBF0;
    do {
        poolIndex = 0;
        var_v1 = gfxContext2;
        do {
            var_v1->taskPoolStart[poolIndex] = 0;
            var_v1->taskPoolEnd[poolIndex] = 0;
            poolIndex += 1;
        } while (poolIndex < 16);
        index2 += 1;
        gfxContext2++;
    } while (index2 <= 0);
}

//Main Draw
RECOMP_PATCH void func_800276EC_2AEC(s32 arg0) {
    s32 sp14;
    s32 uly;
    s32 ulx;
    s32 lrx;
    s32 lry;
    GfxTaskNode *temp_v0_5;
    Gfx *gfx;
    GfxContext *gfxContext;
    GfxContext *gfxContext2;
    Vp *viewport;
    Mtx *identMtx;
    Mtx *mtxprojectionMtx;
    u16 perspNorm;
    s32 width;
    u32 zbuffer;
    s32 unkMtx;
    s32 var_a0;
    s32 var_a2;
    s32 var_a3;
    s32 poolNum;
    s32 gfxContextIndex;
    u32 var_a2_2;
    u8 temp_a0_2;
    u8 temp_a0_3;
    u8 temp_v0;
    s32 colorDither;
    GfxTaskNode *gfxTaskNode;


    sp14 = -1;
    D_801A53D8_1807D8 = 0;
    var_a2 = 0;
    if (arg0 < 2) {
        D_801BC17C_19757C += 1;
        gfxContextIndex = 0;
        func_8002CAD0_7ED0();
        gfxContext = &D_802047F0_1DFBF0;
        gfx = D_800D6FB0_B23B0.dls[D_8020559C_1E099C];
        D_800D6FB8_B23B8 = gfx + 15360 * 10;
        D_801A90F0_1844F0 = gfx;
        D_80193C30_16F030 = -1;
        D_801A90F0_1844F0 = gfx;
        D_801A90F0_1844F0 = gfx + 1;
        gSPDisplayList(gfx++, &D_80224E80);

        //@recomp Enable RT64 enhancements
        gEXEnable(D_801A90F0_1844F0++);
        gEXSetRDRAMExtended(D_801A90F0_1844F0++, TRUE);
        gEXSetRefreshRate(D_801A90F0_1844F0++, 30);
        gEXForceTrueBilerp(D_801A90F0_1844F0++, G_EX_BILERP_ONLY);
        // recomp_printf("-----Begin Frame----- \n");
        //@recomp check if the camera has jumped this frame
        recomp_check_camera_jump();
        do {
            temp_v0 = gfxContext->fadeoutTimer;
            if (temp_v0 != 0) {
                temp_a0_2 = gfxContext->rgba.c.a;
                temp_a0_3 = temp_a0_2 + (((u8) gfxContext->primAlpha2 - temp_a0_2) / temp_v0);
                gfxContext->rgba.c.a = temp_a0_3;
                gfxContext->fadeoutTimer--;
                if (!(gfxContext->fadeoutTimer & 0xFF) && (gfxContext->rgba.c.a >= 0xF1) && !(
                        gfxContext->rgba.rgba32 & ~0xFF)) {
                    func_80089EA0_652A0();
                }
            }
            gfxContext++;
            gfxContextIndex++;
        } while (gfxContextIndex <= 0);
        gfxContextIndex = 0;
        do {
            gfxContext = &D_802047F0_1DFBF0;
            D_8019A6A4_175AA4 = gfxContext;
            if (gfxContext->bufferEnabled != 0) {
                uly = gfxContext->uly;
                ulx = gfxContext->ulx;
                lrx = gfxContext->lrx + 1;
                lry = gfxContext->lry + 1;
                if (gfxContextIndex == 0) {
                    uly = 0;
                    ulx = 0;
                    lrx = D_800A6C9A_8209A;
                    lry = D_800A6C9C_8209C;
                }
                gDPSetScissor(D_801A90F0_1844F0++, G_SC_NON_INTERLACE, ulx, uly, lrx, lry);
                if ((gfxContext->hiResEnabled != 0) && (D_800A6CA0_820A0 == 0)) {
                    zbuffer = D_801ADC84_189084;
                    width = D_800A6C9A_8209A - 1;

                    gDPSetDepthImage(D_801A90F0_1844F0++, OS_K0_TO_PHYSICAL(zbuffer));
                    gDPSetCycleType(D_801A90F0_1844F0++, G_CYC_FILL);
                    gDPSetColorImage(D_801A90F0_1844F0++, G_IM_FMT_RGBA, G_IM_SIZ_16b, width + 1,
                                     OS_K0_TO_PHYSICAL(zbuffer));
                    gDPSetFillColor(D_801A90F0_1844F0++, 0xFFFCFFFC);
                    gDPFillRectangle(D_801A90F0_1844F0++, 0, 0, width, D_800A6C9C_8209C - 1);
                    gDPPipeSync(D_801A90F0_1844F0++);
                }
                if (gfxContext->unk_A8 != 0) {
                    gDPSetColorImage(D_801A90F0_1844F0++, G_IM_FMT_RGBA, G_IM_SIZ_16b, D_800A6C9A_8209A,
                                     osVirtualToPhysical(D_80210980_1EBD80));
                    gDPSetFillColor(D_801A90F0_1844F0++,
                                    FILL_COLOR(gfxContext->bgPrimRed, gfxContext->bgPrimGreen, gfxContext->bgPrimBlue));
                    gDPFillRectangle(D_801A90F0_1844F0++, 0, 0, D_800A6C9A_8209A - 1, D_800A6C9C_8209C - 1);
                    gDPPipeSync(D_801A90F0_1844F0++);
                }
                viewport = func_800281C8_35C8(0x10);
                mtxprojectionMtx = func_800281C8_35C8(0x40);
                identMtx = func_800281C8_35C8(0x40);
                if (((viewport != NULL) & (mtxprojectionMtx != NULL)) && (identMtx != NULL)) {
                    memcpy(viewport, &gfxContext->viewport, 0x10);
                    memcpy(mtxprojectionMtx, &gfxContext->projectionMtx, 0x40);
                    memcpy(identMtx, &gfxContext->identMtx, 0x40);
                    //@recomp Apply the mouse camera's field of view and near plane.
                    perspNorm = gfxContext->perspNorm;
                    mouse_camera_adjust_projection(mtxprojectionMtx, &perspNorm);
                    memcpy(&D_801AF370_18A770, mtxprojectionMtx, 0x40);

                    gSPViewport(D_801A90F0_1844F0++, viewport);
                    gSPPerspNormalize(D_801A90F0_1844F0++, perspNorm);
                    gSPMatrix(D_801A90F0_1844F0++, mtxprojectionMtx, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
                    gSPMatrix(D_801A90F0_1844F0++, identMtx, G_MTX_PROJECTION | G_MTX_MUL | G_MTX_NOPUSH);

                    // Original fog range: thinning it let the background show past the end of the loaded terrain.
                    // The mouse camera's closer near plane changes how depth maps to fog, so it's remapped to keep the
                    // fog at the same distances as the game's own camera.
                    {
                        s32 fogStart = gfxContext->fogStart, fogEnd = gfxContext->fogEnd;
                        mouse_camera_fog_position(&fogStart, &fogEnd);
                        gSPFogPosition(D_801A90F0_1844F0++, fogStart, fogEnd);
                    }
                    gDPSetFogColor(D_801A90F0_1844F0++, gfxContext->fogRed, gfxContext->fogGreen, gfxContext->fogBlue,
                                   gfxContext->fogAlpha);

                    poolNum = 0;
                    var_a3 = 4;
                    var_a2_2 = 0x1D800 * 16; //@recomp check using increased DL size
                    do {
                        gfxTaskNode = gfxContext->taskPoolStart[poolNum];
                        unkMtx = *(s32 *) ((u8 *) &gfxContext->taskPoolStart[poolNum] + 0x144);
                        if (unkMtx != D_801A53D8_1807D8) {
                            if (gfxTaskNode != NULL) {
                                D_801A53D8_1807D8 = unkMtx;
                                switch (unkMtx) {
                                    case 0:
                                        gSPLoadUcode(D_801A90F0_1844F0++, 0xA4770, 0xD1450);
                                        break;
                                    case 1:
                                        gSPLoadUcode(D_801A90F0_1844F0++, 0xA5B00, 0xD1870);
                                        break;
                                }
                                gSPMatrix(D_801A90F0_1844F0++, mtxprojectionMtx,
                                          G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
                                gSPMatrix(D_801A90F0_1844F0++, identMtx, G_MTX_PROJECTION | G_MTX_MUL | G_MTX_NOPUSH);
                            }
                        }

                        if (gfxTaskNode != NULL) {
                            do {
                                colorDither = G_CD_BAYER;
                                D_800D6FBC_B23BC =
                                        (u32) D_801A90F0_1844F0 - (u32) D_800D6FB0_B23B0.dls[D_8020559C_1E099C];
                                if (var_a2_2 >= D_800D6FBC_B23BC) {
                                    if ((sp14 == var_a3) && (D_80193C30_16F030 != sp14)) {
                                        gDPSetColorDither(D_801A90F0_1844F0++, colorDither);
                                    }
                                    sp14 = D_80193C30_16F030;
                                    //@recomp Basic Tagging, read ID from the padding
                                    recomp_interp(gfxTaskNode->unk14);
                                    gfxTaskNode->func(gfxTaskNode->arg);
                                    gEXPopMatrixGroup(D_801A90F0_1844F0++, G_MTX_MODELVIEW);
                                    gfxTaskNode = gfxTaskNode->next;
                                }
                            } while (gfxTaskNode != NULL);
                        }

                        poolNum += 1;
                    } while (poolNum < 16);
                    //@recomp Crosshair for the first person mouse camera, on top of the world and HUD.
                    mouse_camera_draw_crosshair();
                    if (gfxContext->rgba.c.a != 0) {
                        gSPDisplayList(D_801A90F0_1844F0++, &D_80224F68);
                        gDPSetPrimColor(D_801A90F0_1844F0++, 0, 0, gfxContext->rgba.c.r, gfxContext->rgba.c.g,
                                        gfxContext->rgba.c.b, gfxContext->rgba.c.a);
                        gSPTextureRectangle(D_801A90F0_1844F0++, ulx * 4, uly * 4, lrx * 4, lry * 4, 0, 0, 0, 1 << 10,
                                            1 << 10);
                        gDPPipeSync(D_801A90F0_1844F0++);
                        gDPSetAlphaDither(D_801A90F0_1844F0++, G_CD_BAYER);
                    }
                }
            }

            gfxContextIndex += 1;
        } while (gfxContextIndex <= 0);
        model_fixes_update();
        if (menu_scene()) {
            //@recomp Cover the widescreen side bars of the 4:3 menus with flat black.
            // The game's scissor is the 4:3 area, which would clip bars drawn outside it: open it to the whole screen.
            gEXPushScissor(D_801A90F0_1844F0++);
            gEXSetScissor(D_801A90F0_1844F0++, G_SC_NON_INTERLACE, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_RIGHT, 0, 0, 0, SCREEN_HEIGHT);
            gDPPipeSync(D_801A90F0_1844F0++);
            gDPSetCycleType(D_801A90F0_1844F0++, G_CYC_1CYCLE);
            gDPSetRenderMode(D_801A90F0_1844F0++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
            gDPSetCombineMode(D_801A90F0_1844F0++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
            gDPSetPrimColor(D_801A90F0_1844F0++, 0, 0, 0, 0, 0, 255);
            gEXTextureRectangle(D_801A90F0_1844F0++, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_NONE, -16 * 4, 0, 0, SCREEN_HEIGHT * 4,
                                0, 0, 0, 1 << 10, 1 << 10);
            gEXTextureRectangle(D_801A90F0_1844F0++, G_EX_ORIGIN_NONE, G_EX_ORIGIN_RIGHT, SCREEN_WIDTH * 4, 0, 16 * 4,
                                SCREEN_HEIGHT * 4, 0, 0, 0, 1 << 10, 1 << 10);
            gDPPipeSync(D_801A90F0_1844F0++);
            gEXPopScissor(D_801A90F0_1844F0++);
        }
        D_8019A6A4_175AA4 = 0;
        gDPFullSync(D_801A90F0_1844F0++);
        gSPEndDisplayList(D_801A90F0_1844F0++);
        D_800D6FBC_B23BC = (u32) D_801A90F0_1844F0 - (u32) D_800D6FB0_B23B0.dls[(u8) D_8020559C_1E099C];
        func_80092C6C_6E06C(D_800D6FB0_B23B0.dls[(u8) D_8020559C_1E099C], D_800D6FBC_B23BC, 0, 0x40001U);
        D_8020559C_1E099C = 1 - (u8) D_8020559C_1E099C;
    }

    //Reset Arena Pointers
    var_a2 = 0;
    gfxContext2 = &D_802047F0_1DFBF0;
    do {
        var_a0 = 0;
        do {
            gfxContext2->taskPoolStart[var_a0] = NULL;
            gfxContext2->taskPoolEnd[var_a0] = NULL;
            var_a0++;
        } while (var_a0 < 16);
        gfxContext2++;
        var_a2++;
    } while (var_a2 <= 0);

    temp_v0_5 = (&D_800D6FA0_B23A0)[D_8020559C_1E099C];
    D_800D6FA8_B23A8 = temp_v0_5;
    D_800D6FAC_B23AC = (void *) temp_v0_5 + 0x11000 * 16;

    //@recomp reset the interpolation flag after rendering frame
    set_all_interpolation_skipped(FALSE);
    sWrapFrame = FALSE;
    sTerrainWrapFrame = FALSE;
    skip_terrain_interpolation = FALSE;
    recomp_printf("-----End Frame----- \n");
}

void recomp_interp(u32 id) {
    if (all_interpolation_skipped() || gDbgNoInterp) {
        gEXMatrixGroupSkipAll(D_801A90F0_1844F0++, G_EX_ID_IGNORE, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_NONE);
        return;
    }

    if (sTerrainWrapFrame && (GET_TAG_ID(id) == TERRAIN_TAG_ID || GET_TAG_ID(id) == TERRAIN_XLU_TAG_ID)) {
        gEXMatrixGroupSkipAll(D_801A90F0_1844F0++, G_EX_ID_IGNORE, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_NONE);
        return;
    }

    if (gDbgSkipMask & (1 << GET_TAG_ID(id))) {
        gEXMatrixGroupSkipAll(D_801A90F0_1844F0++, G_EX_ID_IGNORE, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_NONE);
        return;
    }

    if (sWrapFrame && GET_TAG_ID(id) != TERRAIN_TAG_ID && GET_TAG_ID(id) != MEGAMAN_TAG_ID) {
        gEXMatrixGroupSkipAll(D_801A90F0_1844F0++, G_EX_ID_IGNORE, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_NONE);
        return;
    }

    switch (GET_TAG_ID(id)) {
        case TERRAIN_TAG_ID:
            if (skip_terrain_interpolation) {
                gEXMatrixGroupSkipAll(D_801A90F0_1844F0++, G_EX_ID_IGNORE, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_NONE);
            } else {
                gEXMatrixGroupDecomposedNormal(D_801A90F0_1844F0++, id, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_NONE);
            }
            break;
        case SPRITE_TAG_ID:
        case 0:
            gEXMatrixGroupSkipAll(D_801A90F0_1844F0++, G_EX_ID_IGNORE, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_NONE);
            break;
        default:
            gEXMatrixGroupDecomposedNormal(D_801A90F0_1844F0++, id, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_NONE);
    }
}

extern s32 D_801BC654_197A54; //Current Cutscene Pointer
s32 sPreviousCutscene = NULL;
// Some areas loop: when Mega Man crosses the edge he is moved by a whole chunk (7168 units in z in town) between two
// identical copies of the scenery. Terrain tiles are tagged by their map index, so the tiles seen on both sides
// (far ones, with the longer draw distance) would be interpolated across the jump. Instead the jump is added to the
// tag, so the same piece of scenery keeps its tag, and only actors, which stay where they are, skip interpolation.
extern u8 D_802049B0_1DFDB0[]; // Mega Man, position s16 x, y, z at +0x14.
#define WORLD_JUMP_MIN 3000
#define WORLD_CHUNK 7168
#define TERRAIN_TILE_SIZE 512
s32 gTerrainTagOffsetX = 0, gTerrainTagOffsetZ = 0;
static s16 sPreviousPlayerPos[3];

void recomp_check_camera_jump() {
    s32 i;
    if (D_801BC654_197A54 != sPreviousCutscene) {
        set_all_interpolation_skipped(TRUE);
    }
    sPreviousCutscene = D_801BC654_197A54;
    for (i = 0; i < 3; i++) {
        s32 pos = ((s16*)(D_802049B0_1DFDB0 + 0x14))[i];
        s32 delta = pos - sPreviousPlayerPos[i];
        s32 size = delta < 0 ? -delta : delta;
        sPreviousPlayerPos[i] = pos;
        if (size > WORLD_JUMP_MIN) {
            if (gDbgWrapMode == 2) {
                continue;
            }
            if (gDbgWrapMode == 3) {
                sTerrainWrapFrame = TRUE;
                continue;
            }
            if (gDbgWrapMode == 0 && i != 1 && size >= WORLD_CHUNK - 16 && size <= WORLD_CHUNK + 16) {
                if (i == 0) {
                    gTerrainTagOffsetX -= delta / TERRAIN_TILE_SIZE;
                } else {
                    gTerrainTagOffsetZ -= delta / TERRAIN_TILE_SIZE;
                }
                sWrapFrame = TRUE;
            } else {
                set_all_interpolation_skipped(TRUE);
            }
        }
    }
}

void set_all_interpolation_skipped(bool skipped) {
    skip_all_interpolation = skipped;
}

bool all_interpolation_skipped() {
    return skip_all_interpolation;
}

void rect_aspect_stretch() {
    gEXSetRectAspect(D_801A90F0_1844F0++, G_EX_ASPECT_STRETCH);
}

void rect_aspect_auto() {
    gEXSetRectAspect(D_801A90F0_1844F0++, G_EX_ASPECT_AUTO);
}
