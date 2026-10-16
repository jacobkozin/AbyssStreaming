param(
    [string]$ObsRoot = "C:\Program Files\obs-studio",
    [string]$AbyssExe = "$PSScriptRoot\..\build\Release\AbyssStream.exe"
)

$ErrorActionPreference = 'Stop'
$obsRootResolved = (Resolve-Path $ObsRoot).Path
$obsBin = Join-Path $obsRootResolved 'bin\64bit'
$exeResolved = (Resolve-Path $AbyssExe).Path

if (-not (Test-Path (Join-Path $obsBin 'obs.dll'))) {
    throw "obs.dll was not found in $obsBin"
}
if (-not (Test-Path (Join-Path $obsRootResolved 'obs-plugins\64bit'))) {
    throw "OBS plugin directory was not found under $obsRootResolved"
}
if (-not (Test-Path (Join-Path $obsRootResolved 'data\obs-plugins'))) {
    throw "OBS plugin data directory was not found under $obsRootResolved"
}

$env:ABYSS_OBS_RUNTIME = $obsRootResolved
$env:PATH = "$obsBin;$env:PATH"

Write-Host "Abyss Stream executable: $exeResolved"
Write-Host "OBS runtime:            $obsRootResolved"
Write-Host ""

$process = Start-Process -FilePath $exeResolved -WorkingDirectory (Split-Path $exeResolved -Parent) -PassThru
Start-Sleep -Milliseconds 1500
if ($process.HasExited -and $process.ExitCode -ne 0) {
    throw "Abyss Stream exited during startup with code $($process.ExitCode)."
}
exit 0
