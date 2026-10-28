# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Cavanshirpro
# Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
param(
    [Parameter(Mandatory)][string]$GuildId,
    [Parameter(Mandatory)][string]$ChannelId,
    [string]$Video = '', [string]$Audio = '', [string]$TokenFile = '',
    [ValidateSet('Both','Camera','GoLive','Microphone','Receive')][string]$Mode = 'Both',
    [int]$DurationSeconds = 0, [int]$Width = 1280, [int]$Height = 720,
    [int]$Fps = 30, [int]$BitrateKbps = 1800,
    [switch]$NoReceiveAudio, [switch]$NoReceiveVideo,
    [switch]$WatchStreams, [switch]$PlayAudio, [switch]$EmitMediaPackets,
    [string]$RecordDirectory = '', [int]$AudioSeconds = 20, [int]$VideoSeconds = 10,
    [switch]$WaitForMedia, [string]$LogFile = '', [switch]$NoLoop,
    [switch]$Headless, [switch]$InCurrentTerminal
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskExe = Join-Path $taskRoot 'build/cpp-media/discord_self_media.exe'
if (!(Test-Path -LiteralPath $taskExe)) { throw 'Run scripts/setup-cpp-media.ps1 first.' }
if (!$TokenFile) { $TokenFile = Join-Path $taskRoot 'test_token' }
$taskTokenPath = (Resolve-Path -LiteralPath $TokenFile).Path
if ($Video) { $Video = (Resolve-Path -LiteralPath $Video).Path }
if ($Audio) { $Audio = (Resolve-Path -LiteralPath $Audio).Path }
$taskFfmpeg = (Get-Command ffmpeg.exe -ErrorAction Stop).Source
if ($RecordDirectory) { $RecordDirectory = [IO.Path]::GetFullPath($RecordDirectory) }
$taskConfig = Join-Path $taskRoot ('build/media-session-' + [guid]::NewGuid().ToString('N') + '.json')
if (!$LogFile) { $LogFile = [IO.Path]::ChangeExtension($taskConfig, '.jsonl') }
$taskLog = [IO.Path]::GetFullPath($LogFile)
@{
    guild_id=$GuildId; channel_id=$ChannelId; ffmpeg=$taskFfmpeg
    video_source=$Video; audio_source=$Audio
    camera=($Mode -eq 'Both' -or $Mode -eq 'Camera')
    go_live=($Mode -eq 'Both' -or $Mode -eq 'GoLive')
    loop=!$NoLoop; width=$Width; height=$Height; fps=$Fps; bitrate_kbps=$BitrateKbps
    receive_audio=!$NoReceiveAudio; receive_video=!$NoReceiveVideo
    watch_streams=[bool]$WatchStreams; play_received_audio=[bool]$PlayAudio
    emit_media_packets=[bool]$EmitMediaPackets
    recording_directory=$RecordDirectory; record_audio_seconds=$AudioSeconds
    record_video_seconds=$VideoSeconds; record_wait_for_media=[bool]$WaitForMedia
    cache_directory=(Join-Path $taskRoot 'build/cpp-media-cache')
    duration_seconds=$DurationSeconds
} | ConvertTo-Json | Set-Content -LiteralPath $taskConfig -Encoding utf8
$taskBin = 'C:/msys64/mingw64/bin'
$taskQuote = { param($value) "'" + $value.Replace("'", "''") + "'" }
$taskInvocation = "& $(& $taskQuote $taskExe) $(& $taskQuote $taskConfig) $(& $taskQuote $taskTokenPath) 2>&1 | Tee-Object -FilePath $(& $taskQuote $taskLog)"
if ($InCurrentTerminal) {
    $taskOldPath = $env:PATH
    try {
        $env:PATH = "$taskBin;$env:PATH"
        & $taskExe $taskConfig $taskTokenPath 2>&1 | Tee-Object -FilePath $taskLog
    } finally {
        $env:PATH = $taskOldPath
        Remove-Item -LiteralPath $taskConfig -ErrorAction SilentlyContinue
    }
} else {
    $taskLauncher = [IO.Path]::ChangeExtension($taskConfig,'.ps1')
    @"
`$Host.UI.RawUI.WindowTitle = 'discord.cpp-self | C++ media | Ctrl+C stops'
`$env:PATH = '$taskBin;' + `$env:PATH
try {
    $taskInvocation
    Write-Host 'Media session ended.'
} finally {
    Remove-Item -LiteralPath $(& $taskQuote $taskConfig) -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $(& $taskQuote $taskLauncher) -ErrorAction SilentlyContinue
}
"@ | Set-Content -LiteralPath $taskLauncher -Encoding utf8
    $taskProcess = Start-Process powershell.exe -ArgumentList @('-NoLogo','-NoProfile','-NoExit','-ExecutionPolicy','Bypass','-File',('"'+$taskLauncher+'"')) -WindowStyle Normal -PassThru
    Write-Output "Opened C++ media console (PID $($taskProcess.Id)). Ctrl+C stops; closing it terminates its encoder children."
}
Write-Output "Local event log: $taskLog"
