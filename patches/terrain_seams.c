#include "patches.h"
#include "mouse_camera.h"

// Terrain seams: every terrain tile is placed by its own PSX style matrix (4.12 rotation, integer translation), so
// the edges of neighboring tiles land up to a unit apart. That's invisible at 320x240 but shows up as thin lines
// between tiles at higher resolutions. The terrain draw marks its tiles so they get scaled up very slightly, making
// each tile overlap its neighbors enough to cover the gap.

typedef struct {
    s16 m[3][3];
    s16 pad;
    s32 t[3];
} TerrainMatrix; // size = 0x20

void func_80031E10_D210(TerrainMatrix* in, TerrainMatrix* out); // TransposeMatrix
void func_8002C420_7820(TerrainMatrix* in, Mtx* out); // PSX matrix to Mtx

// Extra size given to terrain tiles, in 1/4096ths (~3%), only along the tile's own x and z (not its height) and only
// far from the camera: growing near tiles moved doors and walls off the actors (a gap showed beside the S door).
#define TERRAIN_TILE_OVERLAP 0 // Small on purpose: geometry stays glued to the actors; thin tile limits may show on the ground. Distant tiles would need ~100.
#define TERRAIN_OVERLAP_NEAR 0 // Near tiles only need a little; growing them moves doors and walls off the actors.
#define TERRAIN_OVERLAP_NEAR_DIST 1200
#define TERRAIN_OVERLAP_FAR_DIST 3500
extern s32 func_80032424_D824(s32 value); // Integer square root.

extern TerrainMatrix D_801D4760_1AFB60; // Current view matrix.
extern s32 gDbgNoOverlap;
s32 gDbgOverlap = TERRAIN_TILE_OVERLAP; // live switch (tools/re/poke.py), 1/4096ths
s32 gSeamLog = 0, gSeamOk = 0, gSeamSkip = 0; // live: counters, and gSeamLog = 1 prints skipped tiles
s32 gSeamFallback = 24; // live: growth (1/4096) for tiles the exact placement skips, x/z only
s32 gSeamExact = 1; // live: 1 = rebuild tile translations exactly (see below)
s32 gOvNoY = 1; // live: 1 = grow only along the tile's x and z (keeps heights, doors and walls in place)
static bool sTerrainTile = FALSE;

// Exact tile translation, straight from the game's own math. func_8003912C computes each tile's view-space translation
// as (R * world) >> 12 + view.t and stores the integer; the floor is what puts neighbours up to a unit apart. Hooks
// before each "sra 12" (us.rev1.toml) hand the full 4.12 sums here, and the conversion below keeps their fraction.
s32 gSeamSums = 1; // live switch (tools/re/poke.py)
static s32 sTileSum[3];
static s32 sTileSumMask = 0;
void terrain_tile_sum(s32 axis, s32 sum) {
    sTileSum[axis] = sum;
    sTileSumMask |= 1 << axis;
}

// Called by the us.rev1.toml hooks around the terrain draw's matrix conversions.
void terrain_tile_matrix_begin(void) {
    sTerrainTile = TRUE;
}

void terrain_tile_matrix_end(void) {
    sTerrainTile = FALSE;
}

// psx_matrix_to_mtx
RECOMP_PATCH Mtx* func_8002C5A4_79A4(TerrainMatrix* in, Mtx* out) {
    TerrainMatrix tmp;
    s32 fallbackGrow = 0; // set when the exact placement gives up on this tile

    memcpy(&tmp, in, sizeof(TerrainMatrix));
    func_80031E10_D210(in, &tmp);
    tmp.t[0] <<= 16;
    tmp.t[1] <<= 16;
    tmp.t[2] <<= 16;

    //@recomp Exact tile placement. The game rounds every tile's translation to an integer after concatenating it with
    // the view matrix, so neighbors end up up to a unit apart (the thin lines on the ground). Tiles sit on a 256 grid in
    // the world, so the position is recovered, snapped to the grid, and the translation is rebuilt with the fraction kept.
    if (sTerrainTile && gSeamSums && sTileSumMask == 7) {
        s32 i, ok = 1, n[3];
        for (i = 0; i < 3; i++) {
            n[i] = (s32)(((s64)sTileSum[i] << 4) + ((s64)D_801D4760_1AFB60.t[i] << 16)); // 4.12 sum -> 16.16, plus the view offset
            if (n[i] - tmp.t[i] >= 0x10000 || tmp.t[i] - n[i] > 0x10000) {
                ok = 0; // not the tile these sums belong to: keep the game's value
            }
        }
        if (ok) {
            tmp.t[0] = n[0];
            tmp.t[1] = n[1];
            tmp.t[2] = n[2];
            gSeamOk++;
            sTileSumMask = 0;
            goto placed;
        }
    }
    sTileSumMask = 0;
    if (sTerrainTile && gSeamExact) {
        s16* v = &D_801D4760_1AFB60.m[0][0];
        s32* vt = D_801D4760_1AFB60.t;
        s32 dx = in->t[0] - vt[0], dy = in->t[1] - vt[1], dz = in->t[2] - vt[2];
        s32 px = (v[0] * dx + v[3] * dy + v[6] * dz) >> 12;
        s32 py = (v[1] * dx + v[4] * dy + v[7] * dz) >> 12;
        s32 pz = (v[2] * dx + v[5] * dy + v[8] * dz) >> 12;
        s32 sx = ((px + 128) >> 8) << 8, sz = ((pz + 128) >> 8) << 8;
        s32 sy = ((py + 128) >> 8) << 8; // raised areas sit on the same 256 grid (e.g. -512)
        s32 ex = px - sx, ez = pz - sz;
        if (ex >= -40 && ex <= 40 && ez >= -40 && ez <= 40 && py - sy >= -40 && py - sy <= 40) { // far tiles: the estimate drifts with distance
            s64 e0 = ((s64)v[0] * sx + (s64)v[1] * sy + (s64)v[2] * sz) << 4;
            s64 e1 = ((s64)v[3] * sx + (s64)v[4] * sy + (s64)v[5] * sz) << 4;
            s64 e2 = ((s64)v[6] * sx + (s64)v[7] * sy + (s64)v[8] * sz) << 4;
            s32 n0 = (s32)(e0 + ((s64)vt[0] << 16)), n1 = (s32)(e1 + ((s64)vt[1] << 16)), n2 = (s32)(e2 + ((s64)vt[2] << 16));
            // Only trust it when it lands within a unit and a half of what the game computed.
            if (n0 - tmp.t[0] < 0x18000 && tmp.t[0] - n0 < 0x18000 && n1 - tmp.t[1] < 0x18000 && tmp.t[1] - n1 < 0x18000 &&
                n2 - tmp.t[2] < 0x18000 && tmp.t[2] - n2 < 0x18000) {
                tmp.t[0] = n0;
                tmp.t[1] = n1;
                tmp.t[2] = n2;
                gSeamOk++;
            } else {
                gSeamSkip++;
            fallbackGrow = gSeamFallback;
                fallbackGrow = gSeamFallback;
            }
        } else {
            gSeamSkip++;
            if (gSeamLog) {
                recomp_printf("seam skip pos=%d,%d,%d err=%d,%d\n", px, py, pz, ex, ez);
            }
        }
    }
placed:
    //@recomp Grow terrain tiles slightly so they overlap and hide the seams between them.
    if (sTerrainTile && !gDbgNoOverlap) {
        // The matrix is in view space, so the translation is the tile's offset from the camera.
        s32 dx = in->t[0], dz = in->t[2];
        s32 dist = func_80032424_D824(dx * dx + dz * dz);
        s32 overlap = gDbgOverlap;
        if (overlap == TERRAIN_TILE_OVERLAP && fallbackGrow > 0) {
            overlap = fallbackGrow; // only tiles the exact placement skipped (far ones): a tiny x/z growth hides their seam
        } else if (overlap == TERRAIN_TILE_OVERLAP) {
            s32 k = dist < TERRAIN_OVERLAP_NEAR_DIST ? 0 : (dist > TERRAIN_OVERLAP_FAR_DIST ? 1000 : (dist - TERRAIN_OVERLAP_NEAR_DIST) * 1000 / (TERRAIN_OVERLAP_FAR_DIST - TERRAIN_OVERLAP_NEAR_DIST));
            overlap = TERRAIN_OVERLAP_NEAR + (TERRAIN_TILE_OVERLAP - TERRAIN_OVERLAP_NEAR) * k / 1000;
        }
        s32 scale = 4096 + overlap;
        s32 r, c;
        for (r = 0; r < 3; r++) {
            for (c = 0; c < 3; c++) {
                if (c == 1 && gOvNoY) {
                    continue;
                }
                tmp.m[r][c] = (tmp.m[r][c] * scale) >> 12;
            }
        }
    }

    func_8002C420_7820(&tmp, out);
    return out;
}

// Terrain LOD: the game gives tiles past a distance threshold level 1 (tile flags bits 12-14). The upstream project
// forces level 0 (us.rev1.toml "Disable texture LOD"); this lets the game's own level come back live to compare.
s32 gLodRestore = 0; // live switch (tools/re/poke.py)
s32 terrain_lod_pick(s32 dist, s32 threshold, s32 lod) {
    return (gLodRestore && lod == 0 && dist >= threshold) ? 1 : lod;
}

// "Sharp Ground" texture filter (Graphics menu): the ground and walls draw point sampled. Each piece is a small clamped
// window of a bigger texture, and filtering smears every window's border into a visible line.
#define TEXTURE_FILTER_NEAREST_TERRAIN 2 // zelda64::TextureFilterMode::NearestTerrain
extern Gfx* D_801A90F0_1844F0; // Display list head.
static s32 sTerrainPoint = FALSE; // set for the duration of one terrain draw (func_8007F27C)

// Hooks in us.rev1.toml: before and after the terrain draw, and after each window's SetTileSize in func_8007CC94.
void terrain_filter_begin(void) {
    sTerrainPoint = recomp_get_texture_filter() == TEXTURE_FILTER_NEAREST_TERRAIN;
}

void terrain_filter_window(void) {
    if (sTerrainPoint) {
        gDPSetTextureFilter(D_801A90F0_1844F0++, G_TF_POINT);
    }
}

void terrain_filter_end(void) {
    if (sTerrainPoint) {
        gDPSetTextureFilter(D_801A90F0_1844F0++, G_TF_BILERP);
        sTerrainPoint = FALSE;
    }
}
