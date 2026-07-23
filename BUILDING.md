# Building

The project has three build-time components that are wired together by a single
Windows build script, [`scripts/build_all.ps1`](scripts/build_all.ps1):

1. **`sim/`** — the C++ simulation. A CMake project that produces static libs
   (`sim/build/CentralLibs/`) and installs `sim.exe`, headers, and libs into
   `sim/INSTALL/`.
2. **`python_pipeline/telemetry/`** — the telemetry / heatmap pipeline. Packaged
   into a standalone `run_pipeline.exe` with PyInstaller from
   `run_pipeline.spec`, using the local venv at
   `python_pipeline/telemetry/.venv`.
3. **`frontend/`** — the Unreal project. It ships the pipeline at
   `frontend/Content/ThirdParty/python_pipeline/telemetry/`, which is a synced
   copy of the runtime-needed pipeline files.

## Quick start

```powershell
# From the repo root
pwsh -File scripts/build_all.ps1
```

This runs all three phases (sim → pipeline → sync) in order.

## Prerequisites

- **CMake** on `PATH` (used to build `sim/`).
- A C++ toolchain CMake can drive (Visual Studio / MSVC on Windows).
- The telemetry **venv** at `python_pipeline/telemetry/.venv` with `pyinstaller`
  installed. To create it:

  ```powershell
  python -m venv python_pipeline/telemetry/.venv
  python_pipeline/telemetry/.venv/Scripts/python.exe -m pip install `
      -r python_pipeline/telemetry/requirements.txt pyinstaller
  ```

## What `build_all.ps1` does

### 1. Build the sim (CMake)

The Windows equivalent of [`sim/build.sh`](sim/build.sh):

```powershell
cmake -S sim -B sim/build -DCMAKE_INSTALL_PREFIX=sim/INSTALL -DCMAKE_BUILD_TYPE=Release
cmake --build sim/build --config Release --parallel
cmake --install sim/build --config Release
```

Output libs/headers/exe land in `sim/INSTALL/`.

### 2. Rebuild `run_pipeline.exe` (PyInstaller)

Runs PyInstaller from the telemetry venv against `run_pipeline.spec` (a onefile
build). The resulting `dist/run_pipeline.exe` is published back to
`python_pipeline/telemetry/run_pipeline.exe`.

### 3. Sync into the frontend

Copies the runtime-needed pipeline files into
`frontend/Content/ThirdParty/python_pipeline/telemetry/`:

| Item              | How                                             |
| ----------------- | ----------------------------------------------- |
| `run_pipeline.exe`| `Copy-Item` (overwrite)                         |
| `requirements.txt`| `Copy-Item` (overwrite)                         |
| `src/`            | `robocopy /MIR` (mirror)                        |
| `data/`           | `robocopy /MIR` (mirror)                        |

The mirror **excludes** `.venv`, `build/`, `dist/`, `outputs/`, `__pycache__`,
and `.pytest_cache` so only source and bundled default map data are shipped.

> `robocopy /MIR` deletes files in the destination that no longer exist in the
> source. It only ever runs against the `src/` and `data/` subdirs, never the
> telemetry root, so unrelated frontend files are left untouched.

### 4. Prune before packaging (opt-in)

`DefaultGame.ini` stages `Content/ThirdParty` wholesale via
`DirectoriesToAlwaysStageAsNonUFS`, so **anything sitting in that tree at package
time ships inside the build**. Because the sync in step 3 mirrors only `src/` and
`data/`, leftovers at the telemetry root are never cleaned up on their own and
accumulate into packaged builds.

```powershell
pwsh -File scripts/build_all.ps1 -PrunePackaging            # drop build-only leftovers
pwsh -File scripts/build_all.ps1 -PrunePackaging -PruneRuns # also drop saved telemetry runs
```

`-PrunePackaging` removes, all of it untracked and unreferenced by the runtime:

| Removed                    | Why it is safe                                                                                                     |
| -------------------------- | ------------------------------------------------------------------------------------------------------------------ |
| `telemetry/.venv/`         | `run_pipeline.exe` is a frozen PyInstaller onefile with its own interpreter; no C++ path invokes `python.exe`         |
| `telemetry/build/`, `dist/`| PyInstaller scratch output                                                                                           |
| `__pycache__`, `.pytest_cache` | Bytecode caches                                                                                                  |
| `ThirdParty/Telemetry/`    | Orphaned older `run_pipeline.exe`; every call site resolves `python_pipeline/telemetry/run_pipeline.exe`              |

`-PruneRuns` additionally removes `telemetry/outputs/`, the saved run history.
It is the largest single contributor to package size. `run_pipeline.py` recreates
`outputs/` on the next run and `UTelemetryPanelBridge` treats a missing runs
folder as "no saved runs yet", so this is safe — but it is real local data, which
is why it needs its own flag.

> The venv PyInstaller builds against lives at `python_pipeline/telemetry/.venv`,
> **outside** the frontend tree, and is never touched by the prune.

Both flags are off by default: a plain `build_all.ps1` run behaves exactly as
before. Neither flag touches a git-tracked file.

## Options

```powershell
pwsh -File scripts/build_all.ps1 -Clean                 # wipe sim build/INSTALL + telemetry build/dist first
pwsh -File scripts/build_all.ps1 -BuildType Debug       # sim build config (default: Release)
pwsh -File scripts/build_all.ps1 -SkipSim               # skip the CMake phase
pwsh -File scripts/build_all.ps1 -SkipPipeline          # skip the PyInstaller phase
pwsh -File scripts/build_all.ps1 -SkipSync              # skip the frontend sync
pwsh -File scripts/build_all.ps1 -PrunePackaging        # strip build-only leftovers from the staged tree
pwsh -File scripts/build_all.ps1 -PrunePackaging -PruneRuns  # ...and the saved telemetry runs
```

## Generated artifacts (git-ignored)

These are produced by the build/run and are **not** committed (see
[`.gitignore`](.gitignore)):

- `sim/build/`, `sim/INSTALL/`
- `python_pipeline/telemetry/build/`, `python_pipeline/telemetry/dist/`
- `python_pipeline/telemetry/outputs/`, `telemetry_done.txt`
- `simulation_output.csv`, `network_graph_active.csv` (any location)
- any `.venv/`

The tracked, committed inputs are the source: `sim/` C++ sources + `CMakeLists.txt`,
`run_pipeline.spec`, `requirements.txt`, the pipeline `src/`, and the bundled
default map data under `data/network/` (`network_graph.csv`,
`network_graph_waterford.csv`).
