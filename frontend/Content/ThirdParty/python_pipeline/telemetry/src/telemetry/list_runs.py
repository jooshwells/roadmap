from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


# Adds the telemetry folder to Python's import path when this file is run directly.
def add_telemetry_root_to_path() -> Path:
    telemetry_root = Path(__file__).resolve().parents[2]

    if str(telemetry_root) not in sys.path:
        sys.path.insert(0, str(telemetry_root))

    return telemetry_root


TELEMETRY_ROOT = add_telemetry_root_to_path()
RUNS_DIR = TELEMETRY_ROOT / "outputs" / "runs"


# Safely reads a JSON file and returns None if the file is missing or broken.
def read_json_file(json_path: Path) -> dict[str, Any] | None:
    if not json_path.exists():
        return None

    try:
        with json_path.open("r", encoding="utf-8") as file:
            return json.load(file)
    except json.JSONDecodeError:
        return None


# Gets the metadata for one run folder and fills in fallback values if needed.
def build_run_entry(run_folder: Path) -> dict[str, Any]:
    metadata_path = run_folder / "run_metadata.json"
    summary_path = run_folder / "telemetry_summary.json"

    metadata = read_json_file(metadata_path) or {}
    summary = read_json_file(summary_path) or {}

    run_id = metadata.get("run_id", run_folder.name)

    return {
        "run_id": run_id,
        "status": metadata.get("status", "unknown"),
        "run_folder": str(run_folder),
        "metadata_path": str(metadata_path) if metadata_path.exists() else None,
        "summary_path": str(summary_path) if summary_path.exists() else None,
        "available_heatmaps": metadata.get("available_heatmaps", []),
        "created_at": metadata.get("created_at"),
        "total_vehicles": summary.get("total_vehicles"),
        "simulation_duration_s": summary.get("simulation_duration_s"),
        "edges_used": summary.get("edges_used"),
        "average_speed_mph": summary.get("average_speed_mph"),
        "total_wait_added_s": summary.get("total_wait_added_s"),
        "max_wait_time_s": summary.get("max_wait_time_s"),
        "worst_bottleneck": summary.get("worst_bottleneck"),
    }


# Returns all saved runs, newest first.
def list_saved_runs(limit: int | None = None) -> list[dict[str, Any]]:
    if not RUNS_DIR.exists():
        return []

    run_folders = [
        folder
        for folder in RUNS_DIR.iterdir()
        if folder.is_dir() and folder.name.startswith("run_")
    ]

    run_folders.sort(key=lambda folder: folder.name, reverse=True)

    if limit is not None:
        run_folders = run_folders[:limit]

    return [build_run_entry(folder) for folder in run_folders]


# Prints the saved runs as JSON so Unreal can read the output later.
def main() -> None:
    parser = argparse.ArgumentParser(
        description="List saved telemetry runs for the RoadMap telemetry panel."
    )

    parser.add_argument(
        "--limit",
        type=int,
        default=None,
        help="Optional max number of runs to return.",
    )

    parser.add_argument(
        "--pretty",
        action="store_true",
        help="Print formatted JSON for easier reading in PowerShell.",
    )

    args = parser.parse_args()

    runs = list_saved_runs(limit=args.limit)

    output = {
        "runs_dir": str(RUNS_DIR),
        "run_count": len(runs),
        "runs": runs,
    }

    indent = 2 if args.pretty else None
    print(json.dumps(output, indent=indent))


if __name__ == "__main__":
    main()