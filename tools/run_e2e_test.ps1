<#

End-to-end test of the real streaming pipeline, entirely on this PC.



Starts two local RTMP test servers (tools/rtmp_test_server.py). Server B drops

its connection once to exercise automatic reconnect. Then it runs

abyss_stream_e2e, which streams one shared encode to both servers while

recording to disk, and finally checks what each server received: H.264 at the

output resolution, AAC 48 kHz stereo, keyframes, and the reconnect. It runs
twice: once with OBS's rtmp_output and once with Abyss's own RTMP client.



Run BUILD_AND_RUN.bat (or BUILD_AND_RUN.bat --no-launch) first. Needs Python 3.

Nothing is sent to the internet and no real stream keys are used.

#>

param(

    [int]$Seconds = 20

)



$ErrorActionPreference = 'Stop'

Set-StrictMode -Version 2.0



$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

$BuildRoot = Join-Path $ProjectRoot 'build'

foreach ($file in 'last_exe.txt', 'last_obs_root.txt') {

    if (-not (Test-Path (Join-Path $BuildRoot $file))) { throw 'Build Abyss Stream first: BUILD_AND_RUN.bat --no-launch' }

}

$exeDir = Split-Path ((Get-Content (Join-Path $BuildRoot 'last_exe.txt') -Raw).Trim()) -Parent

$obsRoot = (Get-Content (Join-Path $BuildRoot 'last_obs_root.txt') -Raw).Trim()

$e2eExe = Join-Path $exeDir 'abyss_stream_e2e.exe'

if (-not (Test-Path $e2eExe)) { throw "abyss_stream_e2e.exe was not built: $e2eExe" }



& powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'run_last_build.ps1') -StageOnly

if ($LASTEXITCODE -ne 0) { throw 'Staging the OBS runtime failed.' }



$python = Get-Command python.exe -ErrorAction SilentlyContinue

if (-not $python) { $python = Get-Command py.exe -ErrorAction SilentlyContinue }

if (-not $python) { throw 'Python 3 is required for the local RTMP test servers.' }



$work = Join-Path $env:TEMP ('abyss-e2e-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))

New-Item -ItemType Directory -Force -Path $work | Out-Null

$server = Join-Path $PSScriptRoot 'rtmp_test_server.py'

$failures = 0

function Check([bool]$Ok, [string]$What) {

    Write-Host ($(if ($Ok) { '[pass] ' } else { '[FAIL] ' }) + $What)

    if (-not $Ok) { $script:failures++ }

}



function Start-TestServer([int]$Port, [string]$Key, [string]$Name, [string[]]$Extra) {

    $arguments = @('-I', "`"$server`"", '--port', $Port, '--key', $Key, '--stats', "`"$(Join-Path $work "$Name.json")`"") + $Extra

    return Start-Process -FilePath $python.Source -ArgumentList $arguments -PassThru -WindowStyle Hidden `

        -RedirectStandardOutput (Join-Path $work "$Name.log") -RedirectStandardError (Join-Path $work "$Name.err")

}



function Wait-Port([int]$Port) {

    for ($i = 0; $i -lt 50; $i++) {

        $client = New-Object System.Net.Sockets.TcpClient

        try { $client.Connect('127.0.0.1', $Port); return } catch { Start-Sleep -Milliseconds 100 } finally { $client.Dispose() }

    }

    throw "RTMP test server on port $Port did not start."

}



# One run: two servers (B drops once), one shared encode to both, plus a recording.

function Invoke-Scenario([string]$Name, [int]$PortA, [string[]]$ExtraArgs) {

    Write-Host ''

    Write-Host "=== $Name ===" -ForegroundColor Cyan

    $tag = ($Name -replace '[^A-Za-z]', '').ToLowerInvariant()

    $keyA = 'e2e-a-' + [guid]::NewGuid().ToString('N').Substring(0, 12)

    $keyB = 'e2e-b-' + [guid]::NewGuid().ToString('N').Substring(0, 12)

    $portB = $PortA + 1

    $recordDir = Join-Path $work $tag

    New-Item -ItemType Directory -Force -Path $recordDir | Out-Null



    $servers = @(

        (Start-TestServer $PortA $keyA "$tag-server-a" @()),

        (Start-TestServer $portB $keyB "$tag-server-b" @('--drop-after', '6'))

    )

    $e2eExit = 1

    try {

        Wait-Port $PortA

        Wait-Port $portB

        $env:ABYSS_OBS_RUNTIME = $obsRoot

        $e2eArgs = @(

            '--server-a', "rtmp://127.0.0.1:$PortA/live", '--key-a', $keyA,

            '--server-b', "rtmp://127.0.0.1:$portB/live", '--key-b', $keyB,

            '--seconds', $Seconds, '--record-dir', $recordDir, '--expect-reconnect-b',

            '--log', (Join-Path $work "$tag-obs.log")

        ) + $ExtraArgs

        if (Get-Command nvidia-smi.exe -ErrorAction SilentlyContinue) { $e2eArgs += '--require-hardware-encoder' }

        Write-Host "Streaming for $Seconds s to two local servers (B drops once)..."

        & $e2eExe @e2eArgs

        $e2eExit = $LASTEXITCODE

    }

    finally {

        Start-Sleep -Milliseconds 500

        $servers | ForEach-Object { if (-not $_.HasExited) { Stop-Process -Id $_.Id -Force } }

    }



    Write-Host 'What the servers received:'

    $statsA = Get-Content (Join-Path $work "$tag-server-a.json") -Raw | ConvertFrom-Json

    $statsB = Get-Content (Join-Path $work "$tag-server-b.json") -Raw | ConvertFrom-Json

    $a = @($statsA.sessions | Where-Object { $_.publishing })

    $b = @($statsB.sessions | Where-Object { $_.publishing })



    Check ($a.Count -ge 1) 'server A accepted the stream key'

    if ($a.Count -ge 1) {

        $s = $a[0]

        Check ($s.video.width -eq 1920 -and $s.video.height -eq 1080) "server A got H.264 1920x1080 ($($s.video.width)x$($s.video.height) $($s.video.profile))"

        Check ($s.audio.sampleRate -eq 48000 -and $s.audio.channels -eq 2) "server A got AAC 48 kHz stereo ($($s.audio.sampleRate) Hz, $($s.audio.channels) ch)"

        Check ($s.keyframes -ge [math]::Floor($Seconds / 2) - 1) "server A got a keyframe every 2 s ($($s.keyframes) keyframes)"

        Check ($s.videoFrames -ge $Seconds * 50) "server A got ~60 FPS ($($s.videoFrames) frames in $($s.mediaSeconds) s, $($s.kbps) kbps)"

    }

    Check ($b.Count -ge 2) "server B saw the stream come back after the drop ($($b.Count) publishing sessions)"

    if ($b.Count -ge 2) {

        Check ($b[-1].video.width -eq 1920 -and $b[-1].keyframes -ge 1) 'server B got video again after reconnecting'

    }

    Check ($e2eExit -eq 0) 'abyss_stream_e2e checks passed'

}



Invoke-Scenario 'OBS rtmp_output' 19350 @()

Invoke-Scenario 'Abyss RTMP client' 19352 @('--abyss-rtmp')



Write-Host ''

Write-Host "Logs and the recording are in $work"

if ($failures -eq 0) {

    Write-Host 'END-TO-END TEST PASSED' -ForegroundColor Green

    exit 0

}

Write-Host "END-TO-END TEST FAILED ($failures check(s))" -ForegroundColor Red

exit 1

