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


# Safely reads a JSON file and returns None if the file is missing or invalid.
def read_json_file(json_path: Path) -> dict[str, Any] | None:
    if not json_path.exists():
        return None

    try:
        with json_path.open("r", encoding="utf-8") as file:
            return json.load(file)
    except json.JSONDecodeError:
        return None


# Checks that the run ID looks like one of our normal run folder names.
def is_valid_run_id(run_id: str) -> bool:
    return run_id.startswith("run_") and ".." not in run_id and "/" not in run_id and "\\" not in run_id


# Builds the full details response for one saved simulation run.
def get_run_details(run_id: str) -> dict[str, Any]:
    if not is_valid_run_id(run_id):
        return {
            "success": False,
            "error": "Invalid run ID.",
            "run_id": run_id,
        }

    run_folder = RUNS_DIR / run_id

    if not run_folder.exists() or not run_folder.is_dir():
        return {
            "success": False,
            "error": "Run folder not found.",
            "run_id": run_id,
            "run_folder": str(run_folder),
        }

    metadata_path = run_folder / "run_metadata.json"
    summary_path = run_folder / "telemetry_summary.json"

    metadata = read_json_file(metadata_path)
    summary = read_json_file(summary_path)

    return {
        "success": True,
        "run_id": run_id,
        "run_folder": str(run_folder),
        "metadata_path": str(metadata_path) if metadata_path.exists() else None,
        "summary_path": str(summary_path) if summary_path.exists() else None,
        "metadata": metadata,
        "summary": summary,
    }


# Prints one run's full details as JSON so Unreal can read it later.
def main() -> None:
    parser = argparse.ArgumentParser(
        description="Get full telemetry details for one saved RoadMap simulation run."
    )

    parser.add_argument(
        "--run-id",
        required=True,
        help="Run folder ID, such as run_2026-07-06_16-09-14.",
    )

    parser.add_argument(
        "--pretty",
        action="store_true",
        help="Print formatted JSON for easier reading in PowerShell.",
    )

    args = parser.parse_args()

    result = get_run_details(args.run_id)

    indent = 2 if args.pretty else None
    print(json.dumps(result, indent=indent))


if __name__ == "__main__":
    main()