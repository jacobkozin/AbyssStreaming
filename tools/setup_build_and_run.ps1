param(
    # Build and test only; do not start the app afterwards.
    [switch]$NoLaunch
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$BuildRoot = Join-Path $ProjectRoot 'build'
$DepsRoot = Join-Path $env:LOCALAPPDATA 'AbyssStream\deps'
$LogPath = Join-Path $BuildRoot 'AbyssStream-build.log'
$ObsVersion = '32.2.2'
$QtDepsVersion = '2026-07-15'

$ObsRuntimeUrl = 'https://github.com/obsproject/obs-studio/releases/download/32.2.2/OBS-Studio-32.2.2-Windows-x64.zip'
$ObsRuntimeSha256 = '4d6e40e3ab155f56b30de517380566a206d74b63cdf5ad49aa596924768f97e1'
$ObsSourceZipUrl = 'https://github.com/obsproject/obs-studio/archive/refs/tags/32.2.2.zip'
$QtUrl = 'https://github.com/obsproject/obs-deps/releases/download/2026-07-15/windows-deps-qt6-2026-07-15-x64.zip'
$QtSha256 = '7c7f985711d80467bdc1795b6592275a27d5b0e5a2c7a61db1f2c1d08d6a5579'

New-Item -ItemType Directory -Force -Path $BuildRoot, $DepsRoot | Out-Null

function Write-Step([string]$Text) {
    Write-Host ''
    Write-Host ('=' * 68) -ForegroundColor DarkCyan
    Write-Host ('  ' + $Text) -ForegroundColor Cyan
    Write-Host ('=' * 68) -ForegroundColor DarkCyan
}

function Get-CommandPath([string]$Name) {
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -Path $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-CachedDownload {
    param(
        [Parameter(Mandatory=$true)][string]$Url,
        [Parameter(Mandatory=$true)][string]$Destination,
        [Parameter(Mandatory=$true)][string]$ExpectedSha256,
        [Parameter(Mandatory=$true)][string]$Label
    )

    if (Test-Path $Destination) {
        $actual = Get-Sha256 $Destination
        if ($actual -eq $ExpectedSha256.ToLowerInvariant()) {
            Write-Host "Using cached $Label." -ForegroundColor DarkGray
            return
        }
        Write-Host "Cached $Label failed its checksum; replacing it." -ForegroundColor Yellow
        Remove-Item -Force $Destination
    }

    Write-Host "Downloading $Label..." -ForegroundColor Gray
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $Destination

    $actual = Get-Sha256 $Destination
    if ($actual -ne $ExpectedSha256.ToLowerInvariant()) {
        Remove-Item -Force $Destination -ErrorAction SilentlyContinue
        throw "$Label checksum mismatch. Expected $ExpectedSha256 but received $actual."
    }
}

function Import-VisualStudioEnvironment {
    $vswhereCandidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
    ) | Where-Object { $_ -and (Test-Path $_) }

    $devCmd = $null
    foreach ($vswhere in $vswhereCandidates) {
        $installPath = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null | Select-Object -First 1)
        if ($installPath) {
            $candidate = Join-Path $installPath 'Common7\Tools\VsDevCmd.bat'
            if (Test-Path $candidate) {
                $devCmd = $candidate
                break
            }
        }
    }

    if (-not $devCmd) {
        $fallbacks = @()
        foreach ($year in @('2026','2022')) {
            foreach ($edition in @('Community','Professional','Enterprise','BuildTools')) {
                $fallbacks += "C:\Program Files\Microsoft Visual Studio\$year\$edition\Common7\Tools\VsDevCmd.bat"
                $fallbacks += "C:\Program Files (x86)\Microsoft Visual Studio\$year\$edition\Common7\Tools\VsDevCmd.bat"
            }
        }
        $devCmd = $fallbacks | Where-Object { Test-Path $_ } | Select-Object -First 1
    }

    if (-not $devCmd) {
        throw @"
Microsoft Visual Studio C++ Build Tools were not found.

Install either Visual Studio 2022/2026 or the free Visual Studio Build Tools
with the "Desktop development with C++" workload, then double-click
BUILD_AND_RUN.bat again. This is the only large compiler prerequisite that
cannot safely be bundled into this project.
"@
    }

    Write-Host "Using Visual Studio environment: $devCmd" -ForegroundColor DarkGray
    $cmdLine = '"' + $devCmd + '" -no_logo -arch=x64 -host_arch=x64 && set'
    $envLines = & cmd.exe /s /c $cmdLine
    if ($LASTEXITCODE -ne 0) {
        throw 'Visual Studio developer environment failed to initialize.'
    }

    foreach ($line in $envLines) {
        if ($line -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
        }
    }

    if (-not (Get-CommandPath 'cl.exe')) {
        throw 'Visual Studio was detected, but cl.exe is unavailable. Add the Desktop development with C++ workload.'
    }
}

function Find-CMake {
    $cmake = Get-CommandPath 'cmake.exe'
    if ($cmake) { return $cmake }

    $candidates = @(
        'C:\Program Files\CMake\bin\cmake.exe',
        'C:\Program Files (x86)\CMake\bin\cmake.exe'
    )

    if ($env:VSINSTALLDIR) {
        $candidates += (Join-Path $env:VSINSTALLDIR 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe')
    }

    return ($candidates | Where-Object { Test-Path $_ } | Select-Object -First 1)
}

function Ensure-CMake {
    $cmake = Find-CMake
    if ($cmake) { return $cmake }

    $winget = Get-CommandPath 'winget.exe'
    if ($winget) {
        Write-Host 'CMake was not found. Installing the official Kitware CMake package...' -ForegroundColor Yellow
        & $winget install --id Kitware.CMake --exact --silent --accept-package-agreements --accept-source-agreements
        if ($LASTEXITCODE -ne 0) {
            throw 'winget could not install CMake.'
        }
        $env:PATH = "C:\Program Files\CMake\bin;$env:PATH"
        $cmake = Find-CMake
    }

    if (-not $cmake) {
        throw 'CMake 3.28+ was not found. Install CMake, then run BUILD_AND_RUN.bat again.'
    }
    return $cmake
}

function Assert-CMakeVersion([string]$CMakeExe) {
    $firstLine = (& $CMakeExe --version | Select-Object -First 1)
    if ($firstLine -notmatch '(\d+)\.(\d+)\.(\d+)') {
        throw "Could not determine CMake version from: $firstLine"
    }
    $version = [Version]::new([int]$matches[1], [int]$matches[2], [int]$matches[3])
    if ($version -lt [Version]'3.28.0') {
        throw "CMake $version is too old. Abyss Stream requires CMake 3.28 or newer."
    }
    Write-Host "CMake: $version" -ForegroundColor DarkGray
}

function Ensure-ObsRuntime {
    $zip = Join-Path $DepsRoot "OBS-Studio-$ObsVersion-Windows-x64.zip"
    $extract = Join-Path $DepsRoot "obs-runtime-$ObsVersion"
    $marker = Join-Path $extract '.abyss-ready'

    if (-not (Test-Path $marker)) {
        Get-CachedDownload -Url $ObsRuntimeUrl -Destination $zip -ExpectedSha256 $ObsRuntimeSha256 -Label "OBS Studio $ObsVersion runtime"
        if (Test-Path $extract) { Remove-Item -Recurse -Force $extract }
        New-Item -ItemType Directory -Force -Path $extract | Out-Null
        Write-Host 'Extracting OBS runtime...' -ForegroundColor Gray
        Expand-Archive -Path $zip -DestinationPath $extract -Force
        New-Item -ItemType File -Force -Path $marker | Out-Null
    }

    $obsDll = Get-ChildItem -Path $extract -Recurse -File -Filter 'obs.dll' | Where-Object { $_.FullName -match '[\\/]bin[\\/]64bit[\\/]obs\.dll$' } | Select-Object -First 1
    if (-not $obsDll) { throw 'obs.dll was not found in the downloaded OBS runtime.' }

    $obsBin = $obsDll.Directory.FullName
    $binParent = Split-Path $obsBin -Parent
    $obsRoot = Split-Path $binParent -Parent

    return @{ Root = $obsRoot; Bin = $obsBin; Dll = $obsDll.FullName }
}

function Ensure-ObsHeaders {
    # R3 intentionally uses GitHub's generated tag ZIP instead of the official
    # release .tar.gz. Windows' bundled bsdtar cannot create one Unix helper
    # entry in the release tarball (build-aux/run-swift-format), which aborts
    # extraction on some Windows installations. GitHub's tag ZIP contains the
    # same tagged source tree in a Windows-friendly archive and can be handled
    # entirely by PowerShell's Expand-Archive.
    $archive = Join-Path $DepsRoot "obs-studio-$ObsVersion-tag.zip"
    $extract = Join-Path $DepsRoot "obs-source-$ObsVersion"
    $marker = Join-Path $extract '.abyss-ready-r3'

    if (-not (Test-Path $marker)) {
        # GitHub does not publish a checksum alongside its automatically
        # generated "Source code (zip)" asset. Download over HTTPS, then
        # validate the expected tagged source structure and version-specific
        # public libobs header before accepting it.
        if (-not (Test-Path $archive)) {
            Write-Host "Downloading OBS Studio $ObsVersion source ZIP..." -ForegroundColor Gray
            [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
            Invoke-WebRequest -UseBasicParsing -Uri $ObsSourceZipUrl -OutFile $archive
        } else {
            Write-Host "Using cached OBS Studio $ObsVersion source ZIP." -ForegroundColor DarkGray
        }

        if (Test-Path $extract) { Remove-Item -Recurse -Force $extract }
        New-Item -ItemType Directory -Force -Path $extract | Out-Null

        Write-Host 'Extracting OBS source headers with PowerShell (no tar.exe)...' -ForegroundColor Gray
        try {
            Expand-Archive -Path $archive -DestinationPath $extract -Force
        }
        catch {
            # A truncated cache is the most likely cause. Delete it so a
            # rerun automatically fetches a fresh copy instead of looping on
            # the same bad archive.
            Remove-Item -Force $archive -ErrorAction SilentlyContinue
            if (Test-Path $extract) { Remove-Item -Recurse -Force $extract -ErrorAction SilentlyContinue }
            throw "OBS source ZIP extraction failed. The cached ZIP was removed automatically; run BUILD_AND_RUN.bat once more. Details: $($_.Exception.Message)"
        }

        $obsHeaderAfterExtract = Get-ChildItem -Path $extract -Recurse -File -Filter 'obs.h' -ErrorAction SilentlyContinue |
            Where-Object { $_.Directory.Name -eq 'libobs' } |
            Select-Object -First 1

        if (-not $obsHeaderAfterExtract) {
            Remove-Item -Force $archive -ErrorAction SilentlyContinue
            throw 'The OBS tag ZIP extracted, but libobs\obs.h was not present. The cached ZIP was discarded so the next run downloads it again.'
        }

        # Validate that we really downloaded the requested OBS release tree,
        # not merely an arbitrary archive containing an obs.h file.
        $rootDir = $obsHeaderAfterExtract.Directory.Parent.FullName
        $versionHeader = Get-ChildItem -Path $rootDir -Recurse -File -Filter 'obsconfig.h.in' -ErrorAction SilentlyContinue | Select-Object -First 1
        $cmakeLists = Join-Path $rootDir 'CMakeLists.txt'
        if (-not (Test-Path $cmakeLists)) {
            Remove-Item -Force $archive -ErrorAction SilentlyContinue
            throw 'OBS source ZIP validation failed: root CMakeLists.txt is missing.'
        }

        New-Item -ItemType File -Force -Path $marker | Out-Null
    }

    $obsHeader = Get-ChildItem -Path $extract -Recurse -File -Filter 'obs.h' -ErrorAction SilentlyContinue |
        Where-Object { $_.Directory.Name -eq 'libobs' } |
        Select-Object -First 1
    if (-not $obsHeader) {
        Remove-Item -Force $marker -ErrorAction SilentlyContinue
        throw 'Could not locate libobs\obs.h in the extracted OBS source ZIP.'
    }

    # obsconfig.h is generated by OBS's own CMake build and is intentionally
    # absent from a clean source archive. Abyss Stream only consumes the public
    # libobs API, so generate the matching Windows header values locally rather
    # than building all of OBS just to obtain this tiny file.
    $obsConfig = Join-Path $obsHeader.Directory.FullName 'obsconfig.h'
    if (-not (Test-Path $obsConfig)) {
        @'
#pragma once
#define OBS_INSTALL_PREFIX ""
#define OBS_DATA_PATH "data"
#define OBS_PLUGIN_PATH "obs-plugins/64bit"
#define OBS_PLUGIN_DESTINATION "obs-plugins/64bit"
#define OBS_RELEASE_CANDIDATE 0
#define OBS_BETA 0
'@ | Set-Content -Path $obsConfig -Encoding ASCII
    }

    return $obsHeader.Directory.FullName
}

function Ensure-QtDevelopmentFiles {
    $zip = Join-Path $DepsRoot "windows-deps-qt6-$QtDepsVersion-x64.zip"
    $extract = Join-Path $DepsRoot "qt6-$QtDepsVersion-x64"
    $marker = Join-Path $extract '.abyss-ready'

    if (-not (Test-Path $marker)) {
        Get-CachedDownload -Url $QtUrl -Destination $zip -ExpectedSha256 $QtSha256 -Label "OBS matching Qt 6 development package ($QtDepsVersion)"
        if (Test-Path $extract) { Remove-Item -Recurse -Force $extract }
        New-Item -ItemType Directory -Force -Path $extract | Out-Null
        Write-Host 'Extracting Qt development files...' -ForegroundColor Gray
        Expand-Archive -Path $zip -DestinationPath $extract -Force
        New-Item -ItemType File -Force -Path $marker | Out-Null
    }

    $config = Get-ChildItem -Path $extract -Recurse -File -Filter 'Qt6Config.cmake' | Select-Object -First 1
    if (-not $config) { throw 'Qt6Config.cmake was not found after extracting the Qt development package.' }

    # <qt-root>\lib\cmake\Qt6\Qt6Config.cmake
    $qtRoot = Split-Path (Split-Path (Split-Path $config.Directory.FullName -Parent) -Parent) -Parent
    return $qtRoot
}

function New-ObsImportLibrary {
    param(
        [Parameter(Mandatory=$true)][string]$ObsDll,
        [Parameter(Mandatory=$true)][string]$OutputDirectory
    )

    $dumpbin = Get-CommandPath 'dumpbin.exe'
    $libexe = Get-CommandPath 'lib.exe'
    if (-not $dumpbin -or -not $libexe) {
        throw 'dumpbin.exe/lib.exe were not found after loading the Visual Studio C++ environment.'
    }

    New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
    $defPath = Join-Path $OutputDirectory 'obs.def'
    $libPath = Join-Path $OutputDirectory 'obs.lib'

    if ((Test-Path $libPath) -and ((Get-Item $libPath).LastWriteTimeUtc -ge (Get-Item $ObsDll).LastWriteTimeUtc)) {
        return $libPath
    }

    Write-Host 'Generating the libobs Windows import library...' -ForegroundColor Gray
    $exports = & $dumpbin /nologo /exports $ObsDll
    if ($LASTEXITCODE -ne 0) { throw 'dumpbin could not read obs.dll exports.' }

    $names = New-Object System.Collections.Generic.List[string]
    foreach ($line in $exports) {
        if ($line -match '^\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+([^\s=]+)') {
            $name = $matches[1]
            if ($name -and $name -ne '[NONAME]') { $names.Add($name) }
        }
    }
    if ($names.Count -lt 50) {
        throw "Only $($names.Count) exports were discovered in obs.dll; refusing to create an incomplete import library."
    }

    $defLines = @('LIBRARY obs.dll', 'EXPORTS') + ($names | Sort-Object -Unique | ForEach-Object { '    ' + $_ })
    Set-Content -Path $defPath -Value $defLines -Encoding ASCII

    # IMPORTANT: Native command stdout participates in PowerShell function
    # output. lib.exe prints a success banner containing the output paths; if
    # we let that escape the function, $obsLib becomes an array of banner
    # lines plus the real path and CMake receives a corrupted -D value.
    # Capture the native output explicitly, replay it with Write-Host (which
    # does not enter the success pipeline), then return exactly one string.
    $libOutput = @(& $libexe /nologo "/def:$defPath" /machine:x64 "/out:$libPath" 2>&1)
    $libExitCode = $LASTEXITCODE
    foreach ($line in $libOutput) {
        Write-Host ([string]$line) -ForegroundColor DarkGray
    }
    if ($libExitCode -ne 0 -or -not (Test-Path $libPath)) {
        throw "lib.exe failed to create obs.lib (exit code $libExitCode)."
    }

    # Resolve-Path also gives CMake a canonical absolute path. The unary comma
    # prevents PowerShell from enumerating anything if its type ever changes.
    $resolvedLibPath = (Resolve-Path $libPath).Path
    return ,([string]$resolvedLibPath)
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory=$true)][string]$Exe,
        [Parameter(Mandatory=$true)][string[]]$Arguments,
        [Parameter(Mandatory=$true)][string]$FailureMessage
    )
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$FailureMessage (exit code $LASTEXITCODE)" }
}

$transcriptStarted = $false
try {
    Start-Transcript -Path $LogPath -Force | Out-Null
    $transcriptStarted = $true

    Write-Step '1/6  Checking the Windows C++ toolchain'
    Import-VisualStudioEnvironment
    $CMakeExe = Ensure-CMake
    Assert-CMakeVersion $CMakeExe
    Write-Host "MSVC: $((Get-CommandPath 'cl.exe'))" -ForegroundColor DarkGray

    Write-Step '2/6  Preparing the exact OBS 32.2.2 runtime and headers'
    $obs = Ensure-ObsRuntime
    $obsInclude = Ensure-ObsHeaders
    Write-Host "OBS runtime: $($obs.Root)" -ForegroundColor DarkGray
    Write-Host "OBS headers: $obsInclude" -ForegroundColor DarkGray

    Write-Step '3/6  Preparing the matching Qt development files'
    $qtRoot = Ensure-QtDevelopmentFiles
    Write-Host "Qt root: $qtRoot" -ForegroundColor DarkGray

    Write-Step '4/6  Creating the libobs link library'
    $obsLibResult = @(New-ObsImportLibrary -ObsDll $obs.Dll -OutputDirectory (Join-Path $DepsRoot 'obs-import-lib-32.2.2'))
    if ($obsLibResult.Count -ne 1) {
        throw "Internal builder error: expected exactly one obs.lib path, received $($obsLibResult.Count) values."
    }
    $obsLib = [string]$obsLibResult[0]
    if (-not (Test-Path -LiteralPath $obsLib -PathType Leaf)) {
        throw "Internal builder error: obs.lib path does not exist: $obsLib"
    }
    Write-Host "Import library: $obsLib" -ForegroundColor DarkGray

    Write-Step '5/6  Configuring, building and testing Abyss Stream'
    $nativeBuild = Join-Path $BuildRoot 'windows-x64'
    New-Item -ItemType Directory -Force -Path $nativeBuild | Out-Null

    # Ninja ships with Visual Studio's C++ CMake tools and builds in parallel;
    # NMake is the fallback that every MSVC install has.
    $generator = if (Get-CommandPath 'ninja.exe') { 'Ninja' } else { 'NMake Makefiles' }
    $cache = Join-Path $nativeBuild 'CMakeCache.txt'
    if ((Test-Path $cache) -and -not (Select-String -Path $cache -SimpleMatch "CMAKE_GENERATOR:INTERNAL=$generator" -Quiet)) {
        Write-Host "Switching the build folder to the $generator generator." -ForegroundColor DarkGray
        Remove-Item -Force $cache
        Remove-Item -Recurse -Force (Join-Path $nativeBuild 'CMakeFiles') -ErrorAction SilentlyContinue
    }
    Write-Host "Generator: $generator" -ForegroundColor DarkGray

    $configureArgs = @(
        '-S', $ProjectRoot,
        '-B', $nativeBuild,
        '-G', $generator,
        '-DCMAKE_BUILD_TYPE=Release',
        '-DABYSS_BUILD_UI=ON',
        '-DABYSS_WITH_OBS=ON',
        '-DABYSS_BUILD_TESTS=ON',
        "-DABYSS_OBS_INCLUDE_DIR=$obsInclude",
        "-DABYSS_OBS_LIBRARY=$obsLib",
        "-DCMAKE_PREFIX_PATH=$qtRoot"
    )
    Invoke-Checked -Exe $CMakeExe -Arguments $configureArgs -FailureMessage 'CMake configuration failed'
    Invoke-Checked -Exe $CMakeExe -Arguments @('--build', $nativeBuild, '--config', 'Release') -FailureMessage 'Abyss Stream compilation failed'

    $ctest = Join-Path (Split-Path $CMakeExe -Parent) 'ctest.exe'
    if (-not (Test-Path $ctest)) { $ctest = Get-CommandPath 'ctest.exe' }
    if ($ctest) {
        Invoke-Checked -Exe $ctest -Arguments @('--test-dir', $nativeBuild, '-C', 'Release', '--output-on-failure') -FailureMessage 'Abyss Stream tests failed'
    } else {
        Write-Host 'ctest.exe was not found; skipping the automated test command.' -ForegroundColor Yellow
    }

    $exe = Get-ChildItem -Path $nativeBuild -Recurse -File -Filter 'AbyssStream.exe' | Select-Object -First 1
    if (-not $exe) { throw 'Compilation reported success, but AbyssStream.exe could not be found.' }
    Set-Content -Path (Join-Path $BuildRoot 'last_exe.txt') -Value $exe.FullName -Encoding UTF8
    Set-Content -Path (Join-Path $BuildRoot 'last_obs_root.txt') -Value $obs.Root -Encoding UTF8
    Set-Content -Path (Join-Path $BuildRoot 'last_qt_root.txt') -Value $qtRoot -Encoding UTF8

    $runScript = Join-Path $PSScriptRoot 'run_last_build.ps1'
    if ($NoLaunch) {
        Write-Step '6/6  Staging the OBS runtime (launch skipped)'
        & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $runScript -StageOnly
        if ($LASTEXITCODE -ne 0) { throw 'Staging the OBS runtime failed.' }
        Write-Host "Build and tests passed. Start the app with RUN_LAST_BUILD.bat." -ForegroundColor Green
    } else {
        Write-Step '6/6  Launching Abyss Stream'
        Write-Host 'Build and tests passed. The application will open now.' -ForegroundColor Green
        Write-Host ''
        & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $runScript
        if ($LASTEXITCODE -ne 0) { throw "Abyss Stream exited with code $LASTEXITCODE." }
    }
}
catch {
    Write-Host ''
    Write-Host 'ABYSS STREAM BUILD ERROR' -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    Write-Host ''
    Write-Host "Full log: $LogPath" -ForegroundColor Yellow
    exit 1
}
finally {
    if ($transcriptStarted) {
        try { Stop-Transcript | Out-Null } catch {}
    }
}

exit 0
