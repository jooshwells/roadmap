import json
import sys
from pathlib import Path

import pandas as pd
import pytest


PROJECT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_ROOT))

import run_pipeline
from src.fdot.fdot_vs_simulation import (
    build_fdot_comparison,
    classify_geh,
    compare_fdot_to_simulation,
    summarize_fdot_comparison,
)


def sample_fdot_mapping() -> pd.DataFrame:
    return pd.DataFrame({
        "EdgeID": [1, 2, 3],
        "fdot_aadt": [2400, 2400, 2400],
        "fdot_roadway": ["Exact Road", "Review Road", "Poor Road"],
        "match_distance_m": [5.0, 8.0, 12.0],
    })


def sample_simulation_metrics() -> pd.DataFrame:
    return pd.DataFrame({
        "EdgeID": [1, 2, 3, 99],
        "estimated_flow_veh_per_hr": [100.0, 180.0, 400.0, 50.0],
    })


def make_saved_run(tmp_path: Path) -> Path:
    run_folder = tmp_path / "outputs" / "runs" / "downtown-orlando" / "run_2026-07-13_10-00-00"
    run_folder.mkdir(parents=True)
    (run_folder / "run_metadata.json").write_text(
        json.dumps({
            "run_id": run_folder.name,
            "map_name": "Downtown Orlando",
            "map_id": "downtown-orlando",
        }),
        encoding="utf-8",
    )
    sample_simulation_metrics().to_csv(run_folder / "edge_metrics.csv", index=False)
    return run_folder


@pytest.mark.parametrize(
    ("score", "expected"),
    [(0.0, "Good"), (4.99, "Good"), (5.0, "Review"), (9.99, "Review"), (10.0, "Poor")],
)
def test_classify_geh_boundaries(score, expected):
    assert classify_geh(score) == expected


def test_build_comparison_calculates_expected_categories():
    comparison = build_fdot_comparison(sample_fdot_mapping(), sample_simulation_metrics())
    results = comparison.set_index("EdgeID")

    assert results.loc[1, "fdot_estimated_veh_per_hr"] == pytest.approx(100.0)
    assert results.loc[1, "geh_score"] == pytest.approx(0.0)
    assert results.loc[1, "geh_result"] == "Good"
    assert results.loc[2, "geh_result"] == "Review"
    assert results.loc[3, "geh_result"] == "Poor"

    summary = summarize_fdot_comparison(comparison)
    assert summary["matched_edges"] == 3
    assert summary["good_edges"] == 1
    assert summary["review_edges"] == 1
    assert summary["poor_edges"] == 1


def test_build_comparison_rejects_unrelated_edge_ids():
    unrelated = sample_simulation_metrics().assign(EdgeID=[101, 102, 103, 104])
    with pytest.raises(ValueError, match="No matching EdgeIDs"):
        build_fdot_comparison(sample_fdot_mapping(), unrelated)


def test_compare_writes_csv_and_json_summary(tmp_path):
    mapping_path = tmp_path / "mapping.csv"
    metrics_path = tmp_path / "metrics.csv"
    output_path = tmp_path / "fdot" / "fdot_vs_simulation.csv"
    summary_path = tmp_path / "fdot" / "fdot_summary.json"
    sample_fdot_mapping().to_csv(mapping_path, index=False)
    sample_simulation_metrics().to_csv(metrics_path, index=False)

    summary = compare_fdot_to_simulation(
        mapping_path,
        metrics_path,
        output_path,
        summary_path,
    )

    assert output_path.exists()
    assert summary_path.exists()
    assert summary["matched_edges"] == 3
    assert json.loads(summary_path.read_text(encoding="utf-8"))["good_edges"] == 1


def test_selected_run_reports_missing_map_mapping(tmp_path, monkeypatch):
    run_folder = make_saved_run(tmp_path)
    monkeypatch.setattr(run_pipeline, "OUTPUT_DIR", tmp_path / "outputs")

    result = run_pipeline.compare_run_with_fdot(run_folder.name)

    assert result["success"] is False
    assert result["map_id"] == "downtown-orlando"
    assert "No FDOT edge mapping" in result["error"]


def test_legacy_flat_run_uses_unknown_map_id(tmp_path, monkeypatch):
    runs_folder = tmp_path / "outputs" / "runs"
    run_folder = runs_folder / "run_2026-07-13_09-00-00"
    run_folder.mkdir(parents=True)
    (run_folder / "run_metadata.json").write_text(
        json.dumps({"run_id": run_folder.name}),
        encoding="utf-8",
    )
    sample_simulation_metrics().to_csv(run_folder / "edge_metrics.csv", index=False)
    monkeypatch.setattr(run_pipeline, "OUTPUT_DIR", tmp_path / "outputs")

    result = run_pipeline.compare_run_with_fdot(run_folder.name)

    assert result["success"] is False
    assert result["map_id"] == "unknown-map"


def test_selected_run_stores_results_inside_run(tmp_path, monkeypatch):
    run_folder = make_saved_run(tmp_path)
    mapping_path = tmp_path / "downtown_mapping.csv"
    sample_fdot_mapping().to_csv(mapping_path, index=False)
    monkeypatch.setattr(run_pipeline, "OUTPUT_DIR", tmp_path / "outputs")

    result = run_pipeline.compare_run_with_fdot(run_folder.name, str(mapping_path))

    assert result["success"] is True
    assert (run_folder / "fdot" / "fdot_vs_simulation.csv").exists()
    assert (run_folder / "fdot" / "fdot_summary.json").exists()

    metadata = json.loads((run_folder / "run_metadata.json").read_text(encoding="utf-8"))
    assert metadata["fdot_validation"]["status"] == "complete"
    assert metadata["fdot_validation"]["summary"]["matched_edges"] == 3
