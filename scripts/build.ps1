# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Cavanshirpro
# Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
param([string]$MsysRoot = 'C:\msys64', [switch]$SkipTests)
$ErrorActionPreference = 'Stop'
$taskBin = Join-Path $MsysRoot 'mingw64\bin'
$taskCmake = Join-Path $taskBin 'cmake.exe'
if (!(Test-Path -LiteralPath $taskCmake)) { throw "MSYS2 MINGW64 CMake not found: $taskCmake" }
$taskOldPath = $env:PATH
try {
    $env:PATH = "$taskBin;$env:PATH"
    Push-Location (Split-Path $PSScriptRoot -Parent)
    try {
        & $taskCmake -S . -B build/mingw64 -G Ninja "-DCMAKE_CXX_COMPILER=$taskBin/g++.exe" "-DCMAKE_MAKE_PROGRAM=$taskBin/ninja.exe" "-DCMAKE_PREFIX_PATH=$MsysRoot/mingw64" -DCMAKE_BUILD_TYPE=Debug
        if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
        & $taskCmake --build build/mingw64 --parallel 4
        if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
        if (!$SkipTests) {
            & (Join-Path $taskBin 'ctest.exe') --test-dir build/mingw64 --output-on-failure
            if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
        }
    } finally { Pop-Location }
} finally { $env:PATH = $taskOldPath }
