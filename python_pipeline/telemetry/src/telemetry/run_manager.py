"""Create saved-run folders and keep a small history for each map."""

from pathlib import Path
from datetime import datetime
import json
import shutil
import re


# This controls how many old simulation runs we keep.
# For now, we only want to keep the latest 10 runs so the user's computer
# does not slowly fill up with old telemetry files.
MAX_SAVED_RUNS = 10


# Creates a new folder for one simulation run.
# Each run gets its own folder so we do not overwrite old telemetry results.
def make_map_id(map_name: str) -> str:
    """Returns a stable, filesystem-friendly identity for a roadmap name."""
    normalized = re.sub(r"[^a-z0-9]+", "-", map_name.strip().lower()).strip("-")
    return normalized or "unknown-map"


def create_run_folder(outputs_root: Path, map_name: str) -> tuple[str, Path]:
    """Create the folders and starting metadata for one simulation run."""
    runs_root = outputs_root / "runs"
    map_runs_root = runs_root / make_map_id(map_name)
    map_runs_root.mkdir(parents=True, exist_ok=True)

    # Use the current date and time so every run folder has a unique name.
    timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    run_id = f"run_{timestamp}"
    run_folder = map_runs_root / run_id

    # Create the main run folder and the folders we will use later.
    run_folder.mkdir(parents=True, exist_ok=True)
    (run_folder / "heatmaps").mkdir(exist_ok=True)
    (run_folder / "fdot").mkdir(exist_ok=True)

    # This metadata file helps Unreal know what files belong to this run.
    metadata = {
        "run_id": run_id,
        "created_at": timestamp,
        "status": "created",
        "map_name": map_name,
        "map_id": make_map_id(map_name),
        "summary_path": "telemetry_summary.json",
        "csv_path": "simulation_output.csv",
        "heatmaps_folder": "heatmaps",
        "fdot_folder": "fdot",
        "available_heatmaps": [],
        "notes": ""
    }

    with open(run_folder / "run_metadata.json", "w", encoding="utf-8") as file:
        json.dump(metadata, file, indent=4)

    # After making a new run, clean up old runs if there are more than 10.
    cleanup_old_runs(runs_root, map_name)

    return run_id, run_folder


# Deletes the oldest run folders when we have more than the allowed amount.
# This keeps storage under control for now.
def cleanup_old_runs(runs_root: Path, map_name: str) -> None:
    """Remove the oldest runs for this map when it passes the saved limit."""
    run_folders = []
    candidates = list(runs_root.glob("run_*"))
    candidates.extend((runs_root / make_map_id(map_name)).glob("run_*"))
    for folder in candidates:
        if not folder.is_dir():
            continue
        try:
            with (folder / "run_metadata.json").open("r", encoding="utf-8") as file:
                metadata = json.load(file)
        except (OSError, json.JSONDecodeError):
            continue
        if metadata.get("map_name", "").casefold() == map_name.casefold():
            run_folders.append(folder)

    # Since the folder names include the date and time, sorting by name
    # also sorts them from oldest to newest.
    run_folders.sort(key=lambda folder: folder.name)

    while len(run_folders) > MAX_SAVED_RUNS:
        oldest_folder = run_folders.pop(0)
        shutil.rmtree(oldest_folder)
