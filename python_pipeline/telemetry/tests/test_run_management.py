import json
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_ROOT))

import run_pipeline


def test_delete_saved_run_only_removes_the_selected_folder(tmp_path, monkeypatch):
    runs_root = tmp_path / "outputs" / "runs"
    selected = runs_root / "test-map" / "run_2026-07-16_10-00-00"
    retained = runs_root / "test-map" / "run_2026-07-16_11-00-00"
    for folder in (selected, retained):
        folder.mkdir(parents=True)
        (folder / "run_metadata.json").write_text(
            json.dumps({"run_id": folder.name, "map_id": "test-map"}),
            encoding="utf-8",
        )

    monkeypatch.setattr(run_pipeline, "OUTPUT_DIR", tmp_path / "outputs")
    result = run_pipeline.delete_saved_run(selected.name)

    assert result["success"]
    assert not selected.exists()
    assert retained.exists()


def test_delete_saved_run_rejects_unknown_run(tmp_path, monkeypatch):
    monkeypatch.setattr(run_pipeline, "OUTPUT_DIR", tmp_path / "outputs")

    result = run_pipeline.delete_saved_run("run_missing")

    assert not result["success"]
    assert "not found" in result["error"].lower()
