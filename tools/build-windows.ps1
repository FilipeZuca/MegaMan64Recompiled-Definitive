# Windows build in one command. Stages: tools (builds N64Recomp), recomp (ROM -> C), game.
# Usage (Windows PowerShell 5 is enough):
#   powershell -ExecutionPolicy Bypass -File tools\build-windows.ps1 [-Stage tools,recomp,game] [-Config Release]
#                                                                    [-N64Recomp <N64Recomp checkout>] [-Llvm <LLVM 18 bin>]
# Needs Visual Studio 2022 (C++ desktop, Clang, CMake) and LLVM 18 for the MIPS patches (VS's clang has no MIPS target;
# LLVM 19.1.0 miscompiles MIPS). The ROM goes in the repository root as MegaMan64.us.rev1.z64.
param([string[]]$Stage = @('recomp', 'game'), [string]$Config = 'Release',
      [string]$N64Recomp = "$PSScriptRoot\..\..\N64Recomp", [string]$Llvm = 'C:\Program Files\LLVM\bin')
$ErrorActionPreference = 'Stop'
$game = (Resolve-Path "$PSScriptRoot\..").Path
# With -File, "-Stage recomp,game" arrives as one string.
$Stage = $Stage -split ','

# Load the MSVC x64 environment (cl, clang-cl, cmake, ninja, Windows SDK) into this process.
$env:PATH = $env:PATH -replace '"', ''   # a stray quote in PATH breaks vcvars64.bat
$pf86 = if (${env:ProgramFiles(x86)}) { ${env:ProgramFiles(x86)} } else { 'C:\Program Files (x86)' }
$vs = & "$pf86\Microsoft Visual Studio\Installer\vswhere.exe" -products * -latest -property installationPath
if (-not $vs) { throw 'Visual Studio 2022 not found (vswhere)' }
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] }
}
$env:PATH = "$vs\VC\Tools\Llvm\x64\bin;$env:PATH"

function Run { & $args[0] @($args | Select-Object -Skip 1); if ($LASTEXITCODE) { throw "$($args[0]) failed ($LASTEXITCODE)" } }

if ($Stage -contains 'tools') {
    Set-Location $N64Recomp
    Run cmake -G Ninja -S . -B cmake-build -DCMAKE_BUILD_TYPE=Release
    Run cmake --build cmake-build --target N64RecompCLI RSPRecomp
    Copy-Item cmake-build\N64Recomp.exe, cmake-build\RSPRecomp.exe $game -Force
}

if ($Stage -contains 'recomp') {
    Set-Location $game
    Run .\N64Recomp.exe us.rev1.toml
    Run .\RSPRecomp.exe aspMain.us.rev1.toml
}

if ($Stage -contains 'game') {
    Set-Location $game
    # register_patches.cpp includes the generated patch tables but ninja doesn't track them: rebuild it every time,
    # otherwise renamed patch functions leave undefined symbols at link time.
    Remove-Item "$game\cmake-build\CMakeFiles\MegaMan64Recompiled.dir\src\main\register_patches.cpp.obj" -ErrorAction SilentlyContinue
    Run cmake -G Ninja -S . -B cmake-build "-DCMAKE_BUILD_TYPE=$Config" `
        -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl `
        "-DPATCHES_C_COMPILER=$Llvm\clang.exe" "-DPATCHES_LD=$Llvm\ld.lld.exe" `
        '-DCMAKE_CXX_FLAGS=-Xclang -fexceptions -Xclang -fcxx-exceptions'
    Run cmake --build cmake-build --target MegaMan64Recompiled
}
