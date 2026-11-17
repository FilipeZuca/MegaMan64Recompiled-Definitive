#include "modding.h"
#include "mm64.h"

// libultra clock: osGetTime ticks at 46.875 MHz.
extern unsigned long long osGetTime(void);
#define TICKS_PER_MS 46875ULL

#define N64_BIT(b) (1 << (b))

// Phase 1: tap Start until the title screen is passed (0x801BC442 turns non-zero, seen on every recorded boot).
// Phase 2: keep tapping Start through the menu, file select and confirm for MENU_MS.
// Phase 3: tap C-Down (Circle) to advance the opening text until Mega Man can move.
#define TAP_PERIOD_MS 300
#define MENU_MS 4500
// Each tap is held this long so the game sees it on at least one of its frames.
#define PRESS_MS 100
// Give up after this long so a slow boot never leaves the mod pressing buttons forever.
#define GIVE_UP_MS 40000
#define gTitleDone (*(volatile u8*)0x801BC442)

RECOMP_IMPORT("*", int recomp_printf(const char* fmt, ...));

// The game resets the OS clock (osSetTime) once the title is passed, so time is accumulated from per-frame deltas
// and a backwards step counts as zero.
static unsigned long long sPrevTicks = 0;
static unsigned long long sElapsedTicks = 0;
static unsigned int sTitleDoneMs = 0;
static int sDone = 0;

static int player_controllable(void) {
    return (gPlayer[0x0] & gPlayer[0x6] & 1) != 0;
}

// Runs every frame right after the game reads the controllers (also on the title and menus).
RECOMP_HOOK_RETURN("func_80028C14_4014") void quick_continue_frame(void) {
    unsigned long long now;
    unsigned int ms;
    u16 inject = 0;

    if (sDone) {
        return;
    }
    now = osGetTime();
    if (sPrevTicks != 0 && now > sPrevTicks) {
        sElapsedTicks += now - sPrevTicks;
    }
    sPrevTicks = now;
    ms = (unsigned int)(sElapsedTicks / TICKS_PER_MS);

    // Stop once Mega Man can move, or if the boot took too long.
    if (player_controllable() || ms > GIVE_UP_MS) {
        recomp_printf("[quick_continue] done at %u ms (in game %d)\n", ms, player_controllable());
        sDone = 1;
        return;
    }

    if (sTitleDoneMs == 0 && gTitleDone != 0) {
        sTitleDoneMs = ms;
        recomp_printf("[quick_continue] title passed at %u ms\n", ms);
    }
    if (ms % TAP_PERIOD_MS < PRESS_MS) {
        if (sTitleDoneMs == 0 || ms < sTitleDoneMs + MENU_MS) {
            inject = N64_BIT(N64_START);
        } else {
            inject = N64_BIT(N64_C_DOWN);
        }
    }
    if (inject != 0 && ms % 1000 < 20) {
        recomp_printf("[quick_continue] %u ms: pressing %04X\n", ms, inject);
    }
    gRawPads[0].button |= inject;
}
