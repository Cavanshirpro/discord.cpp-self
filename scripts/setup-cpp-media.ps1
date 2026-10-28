# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Cavanshirpro
# Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
param([int]$Jobs = 1, [string]$ToolchainBin = 'C:/msys64/mingw64/bin')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskOldPath = $env:PATH
if ($ToolchainBin -and (Test-Path -LiteralPath $ToolchainBin)) { $env:PATH = "$ToolchainBin;$env:PATH" }
function Invoke-Checked { param([string]$Exe, [string[]]$Arguments)
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed ($LASTEXITCODE)" }
}
function Get-PinnedSource { param([string]$Name, [string]$Url, [string]$Revision)
    $taskSource = Join-Path $taskRoot "build/$Name-src"
    if (!(Test-Path -LiteralPath $taskSource)) {
        Invoke-Checked git @('clone', '--no-checkout', $Url, $taskSource)
        Invoke-Checked git @('-C', $taskSource, 'checkout', '--detach', $Revision)
    }
    $taskActual = (& git -C $taskSource rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $taskActual -ne $Revision) { throw "Unexpected $Name source revision in $taskSource; preserve that checkout and choose a clean build directory." }
    return $taskSource
}
try {
    New-Item -ItemType Directory -Path (Join-Path $taskRoot 'build') -Force | Out-Null
    $taskMls = Get-PinnedSource mlspp 'https://github.com/cisco/mlspp.git' 'fc724c3100ce3b5d8565dbd6d93648a440991a8c'
    $taskDave = Get-PinnedSource libdave 'https://github.com/discord/libdave.git' '6396c90aedbb9a6ee117ea9ce1ce9832af6a3375'
    $taskPrefix = (Join-Path $taskRoot 'build/native-prefix').Replace('\', '/')
    $taskMlsBuild = Join-Path $taskRoot 'build/mlspp-build'
    $taskDaveBuild = Join-Path $taskRoot 'build/libdave-build'
    $taskMediaBuild = Join-Path $taskRoot 'build/cpp-media'
    Invoke-Checked cmake @('-S', $taskMls, '-B', $taskMlsBuild, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_POSITION_INDEPENDENT_CODE=ON', "-DCMAKE_INSTALL_PREFIX=$taskPrefix", '-DMLS_CXX_NAMESPACE=mlspp', '-DDISABLE_GREASE=ON', '-DDISABLE_PQ=ON', '-DTESTING=OFF')
    Invoke-Checked cmake @('--build', $taskMlsBuild, '--parallel', "$Jobs")
    Invoke-Checked cmake @('--install', $taskMlsBuild)
    Invoke-Checked cmake @('-S', (Join-Path $taskDave 'cpp'), '-B', $taskDaveBuild, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_POSITION_INDEPENDENT_CODE=ON', "-DCMAKE_PREFIX_PATH=$taskPrefix", "-DCMAKE_INSTALL_PREFIX=$taskPrefix", '-DTESTING=OFF', '-DPERSISTENT_KEYS=OFF')
    Invoke-Checked cmake @('--build', $taskDaveBuild, '--parallel', "$Jobs")
    Invoke-Checked cmake @('--install', $taskDaveBuild)
    Invoke-Checked cmake @('-S', $taskRoot, '-B', $taskMediaBuild, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_PREFIX_PATH=$taskPrefix", '-DDISCORD_SELF_NATIVE_MEDIA=ON', '-DDISCORD_SELF_BUILD_LIVE_EXAMPLES=ON', '-DDISCORD_SELF_BUILD_TESTS=OFF')
    Invoke-Checked cmake @('--build', $taskMediaBuild, '--parallel', "$Jobs")
    Write-Output "C++ media built in $taskMediaBuild. No Node, Python or Rust runtime is used."
} finally { $env:PATH = $taskOldPath }
