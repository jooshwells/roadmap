# RoadMap Telemetry

This folder contains the Python side of RoadMap's saved-run analysis. Unreal calls the packaged `run_pipeline.exe`, so teammates do not need Python installed just to run the project.

## What gets saved

Each simulation is stored under its map instead of sharing one global run list:

```text
outputs/runs/<map-id>/<run-id>/
```

A run keeps its summary, edge metrics, active network graph, edge metadata, generated heatmaps, and any FDOT comparison results together. Unreal currently shows the newest 10 runs for the active map.

Generated files in `outputs/`, `.venv/`, `build/`, `dist/`, `__pycache__/`, and `telemetry_done.txt` should not be committed.

## Telemetry panel

The Unreal panel has three workspaces:

- **Overview** shows the main run statistics in plain language.
- **Heatmaps** generates sharp SVG bottleneck, speed, flow, and wait-time maps on demand. A JSON file beside each map stores its summary, legend, and road interaction data.
- **FDOT** compares a run with map-specific FDOT design-hour estimates. GEH is included for engineers, with plain-language result labels for other users.

FDOT comparison requires a mapping CSV in `data/fdot/mappings/` for the selected map. `src/fdot/fdot_option_a_matcher.py` is a development utility for creating mappings for additional maps.

## Development

Create the virtual environment and install dependencies as described in `Python_Setup_Guide.txt`. Run the test suite from the repository root with:

```powershell
& .\python_pipeline\telemetry\.venv\Scripts\python.exe -m pytest python_pipeline/telemetry/tests -q
```

Build the packaged executable without touching the simulation libraries or frontend mirror:

```powershell
& .\scripts\build_all.ps1 -SkipSim -SkipSync
```

After a successful build, copy the new executable and approved source changes into `frontend/Content/ThirdParty/python_pipeline/telemetry/`. Avoid a full mirror while another teammate's active-network files are being preserved.
