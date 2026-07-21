import json
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_ROOT))

from src.telemetry.run_manager import (
    MAX_SAVED_RUNS,
    cleanup_old_runs,
    create_run_folder,
    make_map_id,
)


def write_run(runs_root: Path, run_number: int, map_name: str | None) -> Path:
    run_folder = runs_root / f"run_2026-07-12_12-00-{run_number:02d}"
    run_folder.mkdir(parents=True)
    metadata = {"run_id": run_folder.name}
    if map_name is not None:
        metadata["map_name"] = map_name
        metadata["map_id"] = make_map_id(map_name)
    (run_folder / "run_metadata.json").write_text(
        json.dumps(metadata),
        encoding="utf-8",
    )
    return run_folder


def test_make_map_id_is_stable_and_filesystem_friendly():
    assert make_map_id("Downtown Orlando") == "downtown-orlando"
    assert make_map_id("  Waterford (Default)  ") == "waterford-default"
    assert make_map_id("---") == "unknown-map"


def test_create_run_folder_records_map_identity(tmp_path):
    _, run_folder = create_run_folder(tmp_path, "Downtown Orlando")

    assert run_folder.parent.name == "downtown-orlando"
    metadata = json.loads(
        (run_folder / "run_metadata.json").read_text(encoding="utf-8")
    )
    assert metadata["map_name"] == "Downtown Orlando"
    assert metadata["map_id"] == "downtown-orlando"


def test_cleanup_keeps_latest_ten_runs_for_each_map(tmp_path):
    runs_root = tmp_path / "runs"
    runs_root.mkdir()

    waterford_runs = [
        write_run(runs_root, index, "Waterford")
        for index in range(MAX_SAVED_RUNS + 2)
    ]
    downtown_runs = [
        write_run(runs_root, index + 20, "Downtown Orlando")
        for index in range(3)
    ]
    legacy_run = write_run(runs_root, 30, None)

    cleanup_old_runs(runs_root, "Waterford")

    assert not waterford_runs[0].exists()
    assert not waterford_runs[1].exists()
    assert all(folder.exists() for folder in waterford_runs[2:])
    assert all(folder.exists() for folder in downtown_runs)
    assert legacy_run.exists()


def test_new_runs_are_grouped_by_map(tmp_path):
    _, waterford = create_run_folder(tmp_path, "Waterford Lakes")
    _, downtown = create_run_folder(tmp_path, "Downtown Orlando")

    assert waterford.parent == tmp_path / "runs" / "waterford-lakes"
    assert downtown.parent == tmp_path / "runs" / "downtown-orlando"
