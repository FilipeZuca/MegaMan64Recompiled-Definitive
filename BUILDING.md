# Building Guide

You provide the game: the US Mega Man 64 ROM (`.z64`, big endian), sha1 `f24fe0aff01aec018e2dd558ec4f076cf328129f`.
The same file is used to generate the code and, later, selected in the game's launcher.

## 1. Clone with submodules

```bash
git clone --recurse-submodules <this repository>
# if you forgot --recurse-submodules:
git submodule update --init --recursive
```

## 2. Install dependencies

### Windows
- [Visual Studio 2022](https://visualstudio.microsoft.com/downloads/) with: Desktop development with C++, C++ Clang
  Compiler for Windows, C++ CMake tools for Windows.
- [LLVM](https://github.com/llvm/llvm-project/releases) 18 or 19.1.3 (the version the original project's CI uses),
  installed to `C:\Program Files\LLVM` (or pass `-Llvm`). It compiles the MIPS patches: Visual Studio's clang has no
  MIPS target, and LLVM 19.1.0 miscompiles MIPS.

### Linux
For Ubuntu (use the equivalent packages on other distributions):

```bash
sudo apt-get install cmake ninja-build libsdl2-dev libgtk-3-dev lld llvm clang
```

## 3. Place the ROM

Copy the ROM to the repository root as `MegaMan64.us.rev1.z64`.

## 4. Generate the C code

Build [N64Recomp](https://github.com/N64Recomp/N64Recomp) (targets `N64RecompCLI` and `RSPRecomp`, see its README) and
copy `N64Recomp` and `RSPRecomp` to the repository root. Then, from the root:

```bash
./N64Recomp us.rev1.toml
./RSPRecomp aspMain.us.rev1.toml
```

Run this again whenever `us.rev1.toml` changes.

## 5. Build

### Windows, one command

```bash
powershell -ExecutionPolicy Bypass -File tools/build-windows.ps1 -Stage recomp,game
```

It loads the Visual Studio environment, runs step 4 (`recomp`) and builds into `cmake-build/` (`game`). Add `tools`
and `-N64Recomp <path to its checkout>` to build N64Recomp too. After changes in `patches/` or `src/` only,
`-Stage game` is enough.

### CMake directly

```bash
cmake -S . -B build-cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build-cmake --target MegaMan64Recompiled
```

On Windows add `-DPATCHES_C_COMPILER=<LLVM 18>\clang.exe -DPATCHES_LD=<LLVM 18>\ld.lld.exe` and use `clang-cl` as the
C/C++ compiler (the script above shows the full set of flags).

## 6. Run

Run `MegaMan64Recompiled` from a folder that also has `assets/` (copy it next to the executable, or run from the
repository root) and select the same ROM in the launcher.

## Optional mods

```bash
powershell -ExecutionPolicy Bypass -File mods/build.ps1 -Mod quick-continue -ModTool <RecompModTool.exe>
```

`RecompModTool` is built with N64Recomp. Copy the resulting `.nrm` from `mods/quick-continue/build/` into the game's
`mods/` folder and enable it in the mods menu.
