# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Cavanshirpro
param([string]$TokenFile, [string]$MsysRoot = 'C:\msys64')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskExecutable = Join-Path $taskRoot 'build\mingw64\discord_self_check_connection.exe'
if (!(Test-Path -LiteralPath $taskExecutable)) { throw 'Run .\scripts\build.ps1 first' }
$taskOldPath = $env:PATH
$taskOldToken = $env:DISCORD_TOKEN
try {
    $env:PATH = (Join-Path $MsysRoot 'mingw64\bin') + ';' + $env:PATH
    if ($TokenFile) {
        $taskTokenPath = (Resolve-Path -LiteralPath $TokenFile).Path
        $env:DISCORD_TOKEN = [System.IO.File]::ReadAllText($taskTokenPath).Trim()
    }
    & $taskExecutable
    if ($LASTEXITCODE -ne 0) { throw 'Connection check failed; see the status codes above' }
} finally {
    $env:DISCORD_TOKEN = $taskOldToken
    $env:PATH = $taskOldPath
}
