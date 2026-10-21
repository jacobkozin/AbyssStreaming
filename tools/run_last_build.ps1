param(
    # Copy the OBS runtime next to the executable without starting it.
    [switch]$StageOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$BuildRoot = Join-Path $ProjectRoot 'build'
$exeFile = Join-Path $BuildRoot 'last_exe.txt'
$obsFile = Join-Path $BuildRoot 'last_obs_root.txt'
$qtFile = Join-Path $BuildRoot 'last_qt_root.txt'

if (-not (Test-Path $exeFile) -or -not (Test-Path $obsFile)) {
    throw 'No successful Abyss Stream build is recorded yet. Run BUILD_AND_RUN.bat first.'
}

$exe = (Get-Content $exeFile -Raw).Trim()
$obsRoot = (Get-Content $obsFile -Raw).Trim()
$qtRoot = if (Test-Path $qtFile) { (Get-Content $qtFile -Raw).Trim() } else { '' }

if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "AbyssStream.exe is missing: $exe" }
if (-not (Test-Path -LiteralPath $obsRoot -PathType Container)) { throw "OBS runtime is missing: $obsRoot" }

$obsDll = Get-ChildItem -Path $obsRoot -Recurse -File -Filter 'obs.dll' |
    Where-Object { $_.FullName -match '[\\/]bin[\\/]64bit[\\/]obs\.dll$' } |
    Select-Object -First 1
if (-not $obsDll) { throw 'obs.dll is missing from the cached OBS runtime.' }
$obsBin = $obsDll.Directory.FullName
$exeDir = Split-Path $exe -Parent

# Stage the exact OBS 32.2.2 runtime beside the executable:
#  - every DLL in bin\64bit (obs.dll and what OBS plugins link against);
#  - the helper programs libobs looks for next to the *running* executable:
#    obs-nvenc-test/obs-amf-test/obs-qsv-test decide whether the GPU encoders
#    are offered at all, and obs-ffmpeg-mux writes recordings. Without them
#    NVENC silently falls back to x264 and recording cannot start.
$helpers = @('obs-nvenc-test.exe', 'obs-amf-test.exe', 'obs-qsv-test.exe', 'obs-ffmpeg-mux.exe')
$runtimeFiles = @(Get-ChildItem -LiteralPath $obsBin -File -Filter '*.dll') +
    @($helpers | ForEach-Object { Get-Item -LiteralPath (Join-Path $obsBin $_) -ErrorAction SilentlyContinue } | Where-Object { $_ })
foreach ($file in $runtimeFiles) {
    $dest = Join-Path $exeDir $file.Name
    if (-not (Test-Path -LiteralPath $dest) -or (Get-Item -LiteralPath $dest).Length -ne $file.Length) {
        Copy-Item -LiteralPath $file.FullName -Destination $dest -Force
    }
}

# Qt's platform/style plugins and a pointer to the OBS runtime let
# AbyssStream.exe also start with a plain double-click, without this script.
foreach ($pluginDir in 'platforms', 'styles', 'imageformats', 'iconengines') {
    $source = Join-Path $obsBin $pluginDir
    if (Test-Path -LiteralPath $source) {
        $dest = Join-Path $exeDir $pluginDir
        New-Item -ItemType Directory -Force -Path $dest | Out-Null
        Get-ChildItem -LiteralPath $source -File | ForEach-Object {
            $target = Join-Path $dest $_.Name
            if (-not (Test-Path -LiteralPath $target) -or (Get-Item -LiteralPath $target).Length -ne $_.Length) {
                Copy-Item -LiteralPath $_.FullName -Destination $target -Force
            }
        }
    }
}
Set-Content -LiteralPath (Join-Path $exeDir 'obs-runtime.txt') -Value $obsRoot -Encoding UTF8

$graphicsDll = Join-Path $obsBin 'libobs-d3d11.dll'
if (-not (Test-Path -LiteralPath $graphicsDll -PathType Leaf)) {
    throw "libobs-d3d11.dll is missing from the official OBS runtime: $graphicsDll"
}

# Validate the core effect directory before launch so an incomplete runtime
# produces a clear launcher error rather than an opaque OBS_VIDEO_FAIL.
$coreData = Join-Path $obsRoot 'data\libobs'
$requiredEffects = @('default.effect','opaque.effect','solid.effect','format_conversion.effect','bicubic_scale.effect','bilinear_lowres_scale.effect','premultiplied_alpha.effect')
foreach ($effect in $requiredEffects) {
    if (-not (Test-Path -LiteralPath (Join-Path $coreData $effect) -PathType Leaf)) {
        throw "OBS core data file is missing: $(Join-Path $coreData $effect)"
    }
}

if ($StageOnly) {
    Write-Host "Staged the OBS runtime beside $exe" -ForegroundColor DarkGray
    exit 0
}

$env:ABYSS_OBS_RUNTIME = $obsRoot
$env:PATH = "$exeDir;$obsBin;$env:PATH"

if ($qtRoot -and (Test-Path (Join-Path $qtRoot 'bin'))) {
    $env:PATH = "$(Join-Path $qtRoot 'bin');$env:PATH"
}

$qwindows = Get-ChildItem -Path $obsRoot -Recurse -File -Filter 'qwindows.dll' | Select-Object -First 1
if ($qwindows) {
    $platformsDir = $qwindows.Directory.FullName
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $platformsDir
    $env:QT_PLUGIN_PATH = Split-Path $platformsDir -Parent
} elseif ($qtRoot) {
    $qtQwindows = Get-ChildItem -Path $qtRoot -Recurse -File -Filter 'qwindows.dll' | Select-Object -First 1
    if ($qtQwindows) {
        $env:QT_QPA_PLATFORM_PLUGIN_PATH = $qtQwindows.Directory.FullName
        $env:QT_PLUGIN_PATH = Split-Path $qtQwindows.Directory.FullName -Parent
    }
}

$obsLog = Join-Path $exeDir 'AbyssStream-obs.log'

Write-Host "Launching: $exe" -ForegroundColor Cyan
Write-Host "OBS runtime: $obsRoot" -ForegroundColor DarkGray
Write-Host "Diagnostic log: $obsLog" -ForegroundColor DarkGray
Write-Host "Settings and scenes: $env:LOCALAPPDATA\Abyss\Abyss Stream" -ForegroundColor DarkGray

$process = Start-Process -FilePath $exe -WorkingDirectory $exeDir -PassThru
Start-Sleep -Milliseconds 3500

if ($process.HasExited) {
    $exitCode = $process.ExitCode
    if ($exitCode -ne 0) {
        if (Test-Path -LiteralPath $obsLog) {
            Write-Host ''
            Write-Host '================ LAST LOG LINES ================' -ForegroundColor Yellow
            Get-Content -LiteralPath $obsLog -Tail 60 | ForEach-Object { Write-Host $_ }
            Write-Host '================================================' -ForegroundColor Yellow
        }
        throw "Abyss Stream exited during startup with code $exitCode. Full log: $obsLog"
    }
    Write-Host 'Abyss Stream opened and exited normally.' -ForegroundColor Green
} else {
    Write-Host "Abyss Stream is running (PID $($process.Id))." -ForegroundColor Green
}

exit 0
