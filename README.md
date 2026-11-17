# MegaMan64Recompiled — Definitive

A fork of [MegaMan64Recompiled](https://github.com/MegaMan64Recomp/MegaMan64Recompiled), the native PC port of
**Mega Man 64**, with modern controls, a free first and third person camera, aiming, lock-on, visual fixes and
**working mod support** (`.nrm` mods and RT64 texture packs).

> **Beta.** Everything below was tested piece by piece, but the game has not been played start to finish with it yet.
> Expect bugs; see [Known issues](#known-issues) and please report what you find in the Issues tab.

> You need your own copy of the game (US ROM). No game data is included in this repository.

### An edition, not a mod

The changes are compiled into the game itself instead of shipping as a `.nrm` mod: they rely on the C++ runtime
(controller handling, menus, options) and on hooks in the middle of game functions, which mods can't reach. To play
like the base port, set **Options → General → Definitive Features** to **Off** (it switches live): the game's own camera
and controls come back, while the graphics fixes and mod support stay.

## Features

- **Modern controls** for DualSense, Xbox and keyboard/mouse: one job per button in gameplay and in menus, a button
  legend in menus, and prompts that switch live to the device in use.
- **Free camera** in third person (kept out of walls) and a **first person** view with its own arms and buster;
  switch with L3 + R3.
- **Aiming**: over-the-shoulder aim, an always-visible crosshair in third person, shots that go where it points (also
  while walking or strafing), a raised buster pose and a light aim assist that never moves the view by itself.
- **Lock-on** in both views: picks what is under the crosshair, reaches past the buster's range, needs line of sight;
  in first person the stick still corrects the aim around the target.
- **Movement**: directional dodge, sprint with camera effects, smooth stick turning.
- **Graphics**: 3x draw distance with remapped fog, sky at infinity, exact terrain placement (no cracks at high
  resolution), texture filter option including point-sampled terrain, fixes for gaps that only show at high resolution.
- **Interface**: distance-aware interaction prompts, a "continue" prompt in dialogue, black bars on 4:3 menus.
- **Mods work**: the base port's mod menu looked for another game's mods (`sf64`), so nothing could be installed.
  It now uses this game's id (`mm`): code mods (`.nrm`) and RT64 texture packs (`.rtz`) load from the `mods` folder
  and can be toggled in the mods menu, texture packs even while playing. A `--autostart` flag skips the launcher.

## Controls

| Action | DualSense | Keyboard / mouse |
|---|---|---|
| Jump · Interact · Dodge | ✕ · ○ · □ | Space · E · Q |
| Fire · Special weapon | R2 · R1 | Left click · Middle click / R |
| Aim · Strafe (hold) | L2 · L1 | Right click · — |
| Lock-on | R3 | F |
| Sprint | L3 (toggle) | Shift (hold) |
| First / third person | L3 + R3 | C |
| Camera distance · Map · Pause | Create · △ · Options | V · M / Tab · Enter |
| Menus: confirm · back · page · reset | ○ · ✕ · L1/R1 · △ | Space/E · Backspace · Q/R · Tab |

Xbox uses the same positions (A, B, X, Y, LB/RB, LT/RT). Everything can be rebound in the controls menu. New menu
options: **Definitive Features** (on / off), **Modern Camera** (off / third / first person), field of view,
**First Person Strafe**, **Fog** (off / subtle) and **Texture Filter** (three-point / bilinear / sharp ground).

## Known issues

Not yet played from a new save to the end, so expect more. Press F9 with `tools/re/rec.py` running to save a clip.

Controls and menus
- Shops and some other screens still use the gameplay layout: Cross confirms there and R2 goes back.
- Dialogue: holding a button to fast-forward text (the old Square behavior) is not mapped to Circle yet.
- The minimap's controls are inverted.
- Dodge: Mega Man turns slightly to the side before the hop goes where the stick points; the animation is always the
  side hop.
- Some enemies can't be locked on.
- Options menu with the modern camera on: the first Esc frees the mouse and the second opens the menu; the first click
  after opening it is sometimes ignored.
- The menu button legend shows the default layout, not remapped buttons. With Definitive Features off the buttons
  still reach the game as bound (the defaults are the modern layout's).
- No aim assist on the third person free aim (only while holding aim).

Camera and view
- The camera can still pass into level geometry in tight spots.
- First person: things near the sides of the view disappear (the game's own culling was made for its narrower view),
  and the view can clip into a wall when standing pressed against it. In town some walls and backgrounds go missing for
  the same reason.
- First person: the arm floats in the air while firing.
- Fog differs between first person (off or subtle) and third person (the game's own) in dungeons.

Graphics
- Models switch to their lower detail versions close to the camera (visible pop-in); some ground textures sharpen only
  up close.
- Automatic dungeon doors briefly show geometry during their transition, in both views.
- Faint light lines can show on the ground in a few places (sub-pixel gaps between terrain pieces).
- The sky moves in small steps at high frame rates (it is drawn as 2D rectangles that RT64 doesn't interpolate).

Other
- The dungeon map only reveals walls Mega Man passes very close to.
- Occasional short audio stutters.

## Building

Windows, with Visual Studio 2022 (Desktop C++, Clang, CMake tools) and **LLVM 18 or 19.1.3** for the MIPS patches:

```bash
git clone --recurse-submodules <this repository>
```

1. Put the decompressed US ROM in the repository root as `MegaMan64.us.rev1.z64`.
2. Build [N64Recomp](https://github.com/N64Recomp/N64Recomp) and copy `N64Recomp.exe` and `RSPRecomp.exe` to the root
   (or add `tools` to the stages below with `-N64Recomp <path to its checkout>`).
3. Run:

   ```bash
   powershell -ExecutionPolicy Bypass -File tools/build-windows.ps1 -Stage recomp,game
   ```

`-Stage game` is enough after changes in `patches/` or `src/`; changes to `us.rev1.toml` need `recomp,game`.
More detail, and Linux, in [BUILDING.md](BUILDING.md). The optional [Quick Continue](mods/quick-continue) mod (boots
straight into your save) builds with `mods/build.ps1 -ModTool <RecompModTool.exe>`.

## How the project is organized

| Path | Contents |
|---|---|
| `patches/` | C compiled to MIPS that replaces game functions (`RECOMP_PATCH`): camera, aim and input in `mouse_camera.c`, sprint in `sprint.c`, frame building in `gfx_patches.c`, terrain in `terrain_seams.c` |
| `us.rev1.toml` | Recompiler config: hooks that run C++ at given game addresses, instruction replacements |
| `src/` | C++ runtime: input and bindings, config, menus, and the `recomp_*` functions the patches call |
| `assets/` | Menu documents (RmlUi) |
| `mods/` | Optional `.nrm` mods |
| `tools/` | Windows build script and the live debugging toolkit (`tools/re/`) |

Ways a change reaches the game, from least to most invasive: a **mod hook** (`RECOMP_HOOK`, coexists with other mods),
a **toml hook** (C++ before an instruction), a **patch** (whole function). C++ functions are exposed to patches in four
places: an address in `patches/syms.ld`, the `extern "C"` function, `REGISTER_FUNC` in `src/main/main.cpp` and
`DECLARE_FUNC` in the patch header.

Game conventions: 12-bit angles (`0x1000` = 360°), **+Y points down**, positive pitch looks down, game logic at 30 Hz
with RT64 interpolating to the display rate.

## Development workflow

- **Tune live.** Tunables are globals (`gFocusRange`, `gAimFriction`, `gDbgForceMode`...) that
  `tools/re/poke.py NAME VALUE` writes into the running game. Build once, then A/B with the game open.
- **Measure.** `tools/re/` reads RAM through `patches/patches.map`: `peek.py`, simulated input (`btnscan.py`,
  `gDbgPadOn`), screenshots (`fastshot.py`), per-frame traces, and `rec.py`, which saves the last ~15 s of video and
  state when you press F9. Run them from `tools/re/` with Python 3 and Pillow.
- **Reinstall before writing.** After a rebuild the map already describes the new build.
- **Comments say why**, including how each value was measured.

## Contributing

Smallest change that solves the problem, one effect at a time behind a live switch; prefer hooks over patches; never
rename the decompilation's symbols; no game data in the repository.

## Credits

- [MegaMan64Recompiled](https://github.com/MegaMan64Recomp/MegaMan64Recompiled) by the MegaMan64Recomp team
  (RaieFox and contributors), the base of this fork, and its
  [decompilation](https://github.com/MegaMan64Recomp/MegaMan64Decomp).
- [randygil](https://github.com/randygil): mouse camera and terrain seam work
  ([#20](https://github.com/MegaMan64Recomp/MegaMan64Recompiled/pull/20),
  [#21](https://github.com/MegaMan64Recomp/MegaMan64Recompiled/pull/21)) this fork builds on.
- [Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp), whose runtime and menus the port derives from.
- [N64Recomp](https://github.com/N64Recomp/N64Recomp) and [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime)
  by Wiseguy, [RT64](https://github.com/rt64/rt64), [RmlUi](https://github.com/mikke89/RmlUi),
  [lunasvg](https://github.com/sammycage/lunasvg) and [FreeType](https://freetype.org/).
- Fork changes by akuz.

Licensed under the [GNU GPL v3](LICENSE), like the original. Mega Man is a trademark of Capcom; this project is not
affiliated with Capcom.
