# Builds <mod>\build\<mod_filename>.nrm with clang/lld directly (no make needed).
# Usage: build.ps1 [-Mod <folder with mod.toml and src>] [-Llvm <LLVM 18 bin folder>] [-ModTool <RecompModTool.exe>]
# LLVM must be 18.x: 19.1.0 miscompiles MIPS. RecompModTool comes from the N64Recomp build (or set RECOMP_MOD_TOOL).
param([string]$Mod = 'quick-continue', [string]$Llvm = 'C:\Program Files\LLVM\bin',
      [string]$ModTool = $env:RECOMP_MOD_TOOL)
$ErrorActionPreference = 'Stop'
if (-not $ModTool) { throw 'Pass -ModTool or set RECOMP_MOD_TOOL to RecompModTool.exe' }
$root = $PSScriptRoot
Set-Location (Join-Path $root $Mod)
$cflags = '-target', 'mips', '-mips2', '-mabi=32', '-O2', '-G0', '-mno-abicalls', '-mno-odd-spreg',
          '-mno-check-zero-division', '-fomit-frame-pointer', '-ffast-math', '-fno-unsafe-math-optimizations',
          '-fno-builtin-memset', '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-unsupported-floating-point-opt', '-Werror=section',
          '-D_LANGUAGE_C', '-DMIPS', '-nostdinc', '-ffunction-sections', '-I', "$root\include"

New-Item -ItemType Directory -Force build | Out-Null
$objs = @(foreach ($c in Get-ChildItem src -Filter *.c) {
    $o = "build\$($c.BaseName).o"
    & "$Llvm\clang.exe" @cflags $c.FullName -c -o $o
    if ($LASTEXITCODE) { exit 1 }
    $o
})
& "$Llvm\ld.lld.exe" @objs -nostdlib -T "$root\mod.ld" -Map build\mod.map --unresolved-symbols=ignore-all `
    --emit-relocs -e 0 --no-nmagic -gc-sections -o build\mod.elf
if ($LASTEXITCODE) { exit 1 }
& $ModTool mod.toml build
exit $LASTEXITCODE
