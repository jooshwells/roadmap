<#
.SYNOPSIS
    Cross-component build for the Road Map project (Windows / PowerShell).

.DESCRIPTION
    Automates the full build pipeline:
      1. Builds the sim static libs + sim.exe via the CMake project in sim/
         (Windows equivalent of sim/build.sh) and installs them to sim/INSTALL.
      2. Rebuilds the telemetry run_pipeline.exe with PyInstaller, using the
         venv at python_pipeline/telemetry/.venv and run_pipeline.spec.
      3. Syncs the runtime-needed pipeline files (run_pipeline.exe,
         run_pipeline.py, src/, data/, requirements.txt) into the frontend ThirdParty tree, mirroring the
         src/ and data/ subdirs with robocopy /MIR and excluding .venv, build/,
         and outputs/.
      4. Optionally (-PrunePackaging) deletes build-only leftovers from that
         same tree so they stay out of packaged builds.

.PARAMETER Clean
    Remove sim/build and sim/INSTALL (and the telemetry build/ + dist/) before
    building, for a from-scratch build.

.PARAMETER BuildType
    CMake build configuration for the sim project. Default: Release.

.PARAMETER SkipSim
    Skip the sim CMake build step.

.PARAMETER SkipPipeline
    Skip the PyInstaller rebuild step.

.PARAMETER SkipSync
    Skip syncing pipeline files into the frontend ThirdParty tree.

.PARAMETER PrunePackaging
    Delete build-only leftovers from frontend/Content/ThirdParty (the staged
    .venv, PyInstaller build/ + dist/, __pycache__, and the orphaned Telemetry/
    folder). UE stages that tree wholesale, so these otherwise ship inside every
    package. All of it is untracked and unreferenced by the runtime; the sync
    step never recreates it. Off by default - run it before packaging.

.PARAMETER PruneRuns
    With -PrunePackaging, also delete the saved telemetry runs under
    telemetry/outputs. This is the single largest contributor to package size,
    but it is your local run history, so it only goes when asked for explicitly.
    The runs are regenerable and the telemetry panel treats a missing outputs
    folder as "no saved runs yet".

.EXAMPLE
    pwsh -File scripts/build_all.ps1
    Build everything (Release) and sync to the frontend.

.EXAMPLE
    pwsh -File scripts/build_all.ps1 -Clean -BuildType Debug
    Clean, then build the sim in Debug, rebuild the pipeline, and sync.

.EXAMPLE
    pwsh -File scripts/build_all.ps1 -PrunePackaging -PruneRuns
    Build, sync, then strip the staged ThirdParty tree down to what the
    packaged game actually loads. Run this immediately before packaging.
#>

[CmdletBinding()]
param(
    [switch] $Clean,
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo', 'MinSizeRel')]
    [string] $BuildType = 'Release',
    [switch] $SkipSim,
    [switch] $SkipPipeline,
    [switch] $SkipSync,
    [switch] $PrunePackaging,
    [switch] $PruneRuns
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# --- Paths --------------------------------------------------------------------
$RepoRoot     = Split-Path -Parent $PSScriptRoot
$SimDir       = Join-Path $RepoRoot 'sim'
$SimBuildDir  = Join-Path $SimDir   'build'
$SimInstall   = Join-Path $SimDir   'INSTALL'
$TelemetryDir = Join-Path $RepoRoot 'python_pipeline/telemetry'
$VenvDir      = Join-Path $TelemetryDir '.venv'
$FrontendTP   = Join-Path $RepoRoot 'frontend/Content/ThirdParty'
$FrontendDest = Join-Path $FrontendTP 'python_pipeline/telemetry'

function Write-Step {
    param([string] $Message)
    Write-Host ''
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Assert-Tool {
    param([string] $Name, [string] $Hint)
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required tool '$Name' was not found on PATH. $Hint"
    }
}

# robocopy uses exit codes 0-7 for success (bit flags); >= 8 is a real failure.
function Invoke-Robocopy {
    param([string[]] $RoboArgs)
    Write-Host "robocopy $($RoboArgs -join ' ')"
    & robocopy @RoboArgs
    $code = $LASTEXITCODE
    if ($code -ge 8) {
        throw "robocopy failed with exit code $code"
    }
    # Normalize so the script's own exit code is not polluted by robocopy's flags.
    $global:LASTEXITCODE = 0
}

# Deletes a directory tree, returning $true on success. Remove-Item alone can
# choke on the deeply nested paths inside .venv (site-packages easily passes
# PowerShell 5.1's MAX_PATH limit), so fall back to mirroring an empty folder
# over the target - robocopy is not subject to that limit. A prune failure is
# reported and skipped rather than thrown, so it can never fail the build.
function Remove-Tree {
    param([string] $Path)

    try {
        Remove-Item -Recurse -Force -LiteralPath $Path -ErrorAction Stop
        return $true
    }
    catch {
        Write-Host "  Direct delete failed; retrying via robocopy mirror." -ForegroundColor Yellow
    }

    $emptyDir = Join-Path ([System.IO.Path]::GetTempPath()) ("prune_empty_" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $emptyDir | Out-Null
    try {
        Invoke-Robocopy @($emptyDir, $Path, '/MIR', '/NFL', '/NDL', '/NJH', '/NJS', '/NP')
        Remove-Item -Recurse -Force -LiteralPath $Path -ErrorAction Stop
        return $true
    }
    catch {
        Write-Warning "Could not remove $Path - $($_.Exception.Message)"
        return $false
    }
    finally {
        Remove-Item -Recurse -Force -LiteralPath $emptyDir -ErrorAction SilentlyContinue
    }
}

Write-Host "Repo root:      $RepoRoot"
Write-Host "Build type:     $BuildType"
Write-Host "Clean:          $Clean"

# --- 1. Build the sim (CMake) -------------------------------------------------
if (-not $SkipSim) {
    Write-Step "Building sim static libs (CMake, $BuildType)"
    Assert-Tool 'cmake' 'Install CMake (https://cmake.org/) and add it to PATH.'

    if ($Clean) {
        foreach ($dir in @($SimBuildDir, $SimInstall)) {
            if (Test-Path $dir) {
                Write-Host "Removing $dir"
                Remove-Item -Recurse -Force $dir
            }
        }
    }

    # Configure
    & cmake -S $SimDir -B $SimBuildDir -DCMAKE_INSTALL_PREFIX="$SimInstall" -DCMAKE_BUILD_TYPE="$BuildType"
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)" }

    # Build
    & cmake --build $SimBuildDir --config $BuildType --parallel
    if ($LASTEXITCODE -ne 0) { throw "CMake build failed ($LASTEXITCODE)" }

    # Install (static libs, headers, sim.exe -> sim/INSTALL)
    & cmake --install $SimBuildDir --config $BuildType
    if ($LASTEXITCODE -ne 0) { throw "CMake install failed ($LASTEXITCODE)" }

    Write-Host "sim built and installed to $SimInstall" -ForegroundColor Green
}
else {
    Write-Host "Skipping sim build (-SkipSim)."
}

# --- 2. Rebuild run_pipeline.exe (PyInstaller) --------------------------------
if (-not $SkipPipeline) {
    Write-Step 'Rebuilding run_pipeline.exe (PyInstaller)'

    $venvPy    = Join-Path $VenvDir 'Scripts/python.exe'
    $specFile  = Join-Path $TelemetryDir 'run_pipeline.spec'
    if (-not (Test-Path $venvPy)) {
        throw "venv Python not found at $venvPy. Create it and install requirements:`n" +
              "  python -m venv `"$VenvDir`"`n" +
              "  `"$venvPy`" -m pip install -r `"$(Join-Path $TelemetryDir 'requirements.txt')`" pyinstaller"
    }
    if (-not (Test-Path $specFile)) { throw "Spec file not found at $specFile" }

    if ($Clean) {
        foreach ($dir in @((Join-Path $TelemetryDir 'build'), (Join-Path $TelemetryDir 'dist'))) {
            if (Test-Path $dir) {
                Write-Host "Removing $dir"
                Remove-Item -Recurse -Force $dir
            }
        }
    }

    # Run PyInstaller from the telemetry dir so relative paths in the spec resolve.
    Push-Location $TelemetryDir
    try {
        & $venvPy -m PyInstaller 'run_pipeline.spec' --noconfirm --clean
        if ($LASTEXITCODE -ne 0) { throw "PyInstaller failed ($LASTEXITCODE)" }
    }
    finally {
        Pop-Location
    }

    # onefile build -> dist/run_pipeline.exe; publish it next to the sources.
    $builtExe = Join-Path $TelemetryDir 'dist/run_pipeline.exe'
    if (-not (Test-Path $builtExe)) { throw "Expected PyInstaller output not found: $builtExe" }
    Copy-Item $builtExe (Join-Path $TelemetryDir 'run_pipeline.exe') -Force
    Write-Host "run_pipeline.exe rebuilt." -ForegroundColor Green
}
else {
    Write-Host "Skipping pipeline build (-SkipPipeline)."
}

# --- 3. Sync runtime pipeline files into the frontend ThirdParty tree ---------
if (-not $SkipSync) {
    Write-Step 'Syncing pipeline files into frontend/Content/ThirdParty'

    if (-not (Test-Path $FrontendDest)) {
        New-Item -ItemType Directory -Force -Path $FrontendDest | Out-Null
    }

    $srcExe = Join-Path $TelemetryDir 'run_pipeline.exe'
    $srcPipeline = Join-Path $TelemetryDir 'run_pipeline.py'
    $srcReq = Join-Path $TelemetryDir 'requirements.txt'
    if (-not (Test-Path $srcExe)) { throw "Missing $srcExe (run the pipeline build first)." }
    if (-not (Test-Path $srcPipeline)) { throw "Missing $srcPipeline." }
    if (-not (Test-Path $srcReq)) { throw "Missing $srcReq." }

    # Single files
    Copy-Item $srcExe (Join-Path $FrontendDest 'run_pipeline.exe') -Force
    Copy-Item $srcPipeline (Join-Path $FrontendDest 'run_pipeline.py') -Force
    Copy-Item $srcReq (Join-Path $FrontendDest 'requirements.txt') -Force

    # Mirror the src/ and data/ subdirs. /MIR deletes stale files in the
    # destination; /XD keeps venv/build/output/cache trees out of the mirror.
    $excludeDirs = @('.venv', 'build', 'dist', 'outputs', '__pycache__', '.pytest_cache')
    Invoke-Robocopy (@("$(Join-Path $TelemetryDir 'src')", "$(Join-Path $FrontendDest 'src')", '/MIR', '/XD') + $excludeDirs)
    Invoke-Robocopy (@("$(Join-Path $TelemetryDir 'data')", "$(Join-Path $FrontendDest 'data')", '/MIR', '/XD') + $excludeDirs)

    Write-Host "Synced exe + run_pipeline.py + requirements.txt + src/ + data/ to $FrontendDest" -ForegroundColor Green
}
else {
    Write-Host "Skipping frontend sync (-SkipSync)."
}

# --- 4. Prune build-only leftovers from the staged ThirdParty tree ------------
if ($PrunePackaging) {
    Write-Step 'Pruning build-only leftovers from frontend/Content/ThirdParty'

    # DefaultGame.ini stages Content/ThirdParty wholesale via
    # DirectoriesToAlwaysStageAsNonUFS, so whatever sits in this tree at package
    # time ships inside the build. Everything below is untracked, unreferenced by
    # the runtime, and never recreated by the sync step above:
    #   .venv         - PyInstaller's build environment. run_pipeline.exe is a
    #                   frozen onefile and carries its own interpreter, and no
    #                   C++ path ever invokes python.exe. The venv the build
    #                   actually uses lives at python_pipeline/telemetry/.venv,
    #                   outside this tree, and is untouched.
    #   build, dist   - PyInstaller scratch output.
    #   Telemetry     - orphaned copy of an older run_pipeline.exe. Every call
    #                   site resolves python_pipeline/telemetry/run_pipeline.exe.
    $pruneTargets = @(
        (Join-Path $FrontendDest '.venv')
        (Join-Path $FrontendDest 'build')
        (Join-Path $FrontendDest 'dist')
        (Join-Path $FrontendDest '__pycache__')
        (Join-Path $FrontendDest '.pytest_cache')
        (Join-Path $FrontendTP   'Telemetry')
    )

    if ($PruneRuns) {
        # Saved run history. run_pipeline.py recreates outputs/ on the next run
        # and the telemetry panel treats a missing runs folder as "none saved
        # yet", so this is safe - but it is real local data, hence the opt-in.
        $pruneTargets += (Join-Path $FrontendDest 'outputs')
    }

    $freedBytes = 0
    foreach ($target in $pruneTargets) {
        if (-not (Test-Path -LiteralPath $target)) { continue }

        # Measure-Object emits nothing at all for an empty directory, so both the
        # result and its Sum have to be guarded under Set-StrictMode.
        $measured = Get-ChildItem -LiteralPath $target -Recurse -File -Force -ErrorAction SilentlyContinue |
                    Measure-Object -Property Length -Sum
        $size = 0
        if ($null -ne $measured -and $null -ne $measured.Sum) { $size = $measured.Sum }

        Write-Host ("Removing {0} ({1:N1} MB)" -f $target, ($size / 1MB))
        if (Remove-Tree $target) {
            $freedBytes += $size
        }
    }

    if ($freedBytes -gt 0) {
        Write-Host ("Pruned {0:N1} MB from the staged ThirdParty tree." -f ($freedBytes / 1MB)) -ForegroundColor Green
    }
    else {
        Write-Host 'Nothing to prune; the staged tree is already clean.' -ForegroundColor Green
    }

    if (-not $PruneRuns) {
        Write-Host 'Saved telemetry runs were kept. Add -PruneRuns to drop those too.'
    }
}
else {
    Write-Host ''
    Write-Host 'Staged ThirdParty tree not pruned (-PrunePackaging). Packaged builds will include build-only leftovers.'
}

Write-Step 'Build complete.'
