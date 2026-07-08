from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


# Finds the real telemetry folder in both development and packaged builds.
# PyInstaller extracts bundled Python files to a temporary folder, so packaged
# builds use the folder containing run_pipeline.exe instead.
def add_telemetry_root_to_path() -> Path:
    if getattr(sys, "frozen", False):
        telemetry_root = Path(sys.executable).resolve().parent
    else:
        telemetry_root = Path(__file__).resolve().parents[2]

    if str(telemetry_root) not in sys.path:
        sys.path.insert(0, str(telemetry_root))

    return telemetry_root


TELEMETRY_ROOT = add_telemetry_root_to_path()
RUNS_DIR = TELEMETRY_ROOT / "outputs" / "runs"


# Safely reads a JSON file and returns None if the file is missing or invalid.
def read_json_file(json_path: Path) -> dict[str, Any] | None:
    if not json_path.exists():
        return None

    try:
        with json_path.open("r", encoding="utf-8") as file:
            return json.load(file)
    except json.JSONDecodeError:
        return None


# Checks that the run ID looks safe before using it as a folder name.
def is_valid_run_id(run_id: str) -> bool:
    return run_id.startswith("run_") and ".." not in run_id and "/" not in run_id and "\\" not in run_id


# Finds a saved heatmap entry inside run_metadata.json for the requested metric.
def find_heatmap_entry(metadata: dict[str, Any], metric: str) -> dict[str, Any] | None:
    available_heatmaps = metadata.get("available_heatmaps", [])

    for heatmap in available_heatmaps:
        if heatmap.get("metric") == metric:
            return heatmap

    return None


# Returns the full PNG path for a generated heatmap if it exists.
def get_heatmap_path(run_id: str, metric: str) -> dict[str, Any]:
    if not is_valid_run_id(run_id):
        return {
            "success": False,
            "error": "Invalid run ID.",
            "run_id": run_id,
            "metric": metric,
        }

    run_folder = RUNS_DIR / run_id
    metadata_path = run_folder / "run_metadata.json"

    if not run_folder.exists() or not run_folder.is_dir():
        return {
            "success": False,
            "error": "Run folder not found.",
            "run_id": run_id,
            "metric": metric,
            "run_folder": str(run_folder),
        }

    metadata = read_json_file(metadata_path)

    if metadata is None:
        return {
            "success": False,
            "error": "run_metadata.json not found or invalid.",
            "run_id": run_id,
            "metric": metric,
            "run_folder": str(run_folder),
        }

    heatmap_entry = find_heatmap_entry(metadata, metric)

    if heatmap_entry is None:
        return {
            "success": False,
            "error": "Heatmap has not been generated for this metric yet.",
            "run_id": run_id,
            "metric": metric,
            "run_folder": str(run_folder),
            "available_heatmaps": metadata.get("available_heatmaps", []),
        }

    relative_path = heatmap_entry.get("path")

    if not relative_path:
        return {
            "success": False,
            "error": "Heatmap entry is missing a path.",
            "run_id": run_id,
            "metric": metric,
        }

    heatmap_path = run_folder / relative_path

    if not heatmap_path.exists():
        return {
            "success": False,
            "error": "Heatmap is listed in metadata, but the PNG file was not found.",
            "run_id": run_id,
            "metric": metric,
            "relative_path": relative_path,
            "full_path": str(heatmap_path),
        }

    return {
        "success": True,
        "run_id": run_id,
        "metric": metric,
        "relative_path": relative_path,
        "full_path": str(heatmap_path),
    }


# Prints the heatmap path result as JSON so Unreal can read it later.
def main() -> None:
    parser = argparse.ArgumentParser(
        description="Get the PNG path for a generated RoadMap telemetry heatmap."
    )

    parser.add_argument(
        "--run-id",
        required=True,
        help="Run folder ID, such as run_2026-07-06_16-09-14.",
    )

    parser.add_argument(
        "--metric",
        required=True,
        help="Heatmap metric, such as bottleneck_score or avg_speed_mph.",
    )

    parser.add_argument(
        "--pretty",
        action="store_true",
        help="Print formatted JSON for easier reading in PowerShell.",
    )

    args = parser.parse_args()

    result = get_heatmap_path(args.run_id, args.metric)

    indent = 2 if args.pretty else None
    print(json.dumps(result, indent=indent))


if __name__ == "__main__":
    main()