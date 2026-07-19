import json
import sys
from pathlib import Path

import pandas as pd
import pytest


PROJECT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_ROOT))

from src.telemetry.run_comparison import compare_saved_runs, build_heatmap_comparison_metrics
from run_pipeline import get_saved_comparison_heatmap_paths


def make_run(
    tmp_path: Path,
    name: str,
    map_id: str,
    speed: float,
    wait: float,
    duration: float,
    vehicles: int = 10,
    index_version: int = 2,
) -> Path:
    folder = tmp_path / map_id / name
    folder.mkdir(parents=True)
    (folder / "run_metadata.json").write_text(json.dumps({
        "run_id": name,
        "created_at": "2026-07-15_12-00-00",
        "map_id": map_id,
        "map_name": "Test Map",
    }), encoding="utf-8")
    (folder / "telemetry_summary.json").write_text(json.dumps({
        "bottleneck_index_version": index_version,
        "total_vehicles": vehicles,
        "simulation_duration_s": duration,
        "edges_used": 2,
        "average_speed_mph": speed,
        "total_wait_added_s": wait,
        "max_wait_time_s": wait / 2,
        "worst_bottleneck": {"bottleneck_score": wait / 10},
    }), encoding="utf-8")
    pd.DataFrame({
        "EdgeID": [1, 2],
        "avg_speed_mph": [speed, speed - 5],
        "total_wait_added_s": [wait, 0],
        "edge_entry_count": [2, 1],
        "bottleneck_score": [wait / 10, 0],
        "estimated_flow_veh_per_hr": [100, 50],
    }).to_csv(folder / "edge_metrics.csv", index=False)
    pd.DataFrame({
        "edge_id": [1, 2],
        "source": [10, 20],
        "target": [20, 30],
        "name": ["Main Street", ""],
        "ref": ["", "SR 50"],
        "highway": ["primary", "secondary"],
    }).to_csv(folder / "network_graph.csv", index=False)
    return folder


def metric_by_id(result: dict, metric_id: str) -> dict:
    return next(metric for metric in result["metrics"] if metric["id"] == metric_id)


def test_comparison_reports_improvement_and_road_changes(tmp_path):
    baseline = make_run(tmp_path, "run_one", "test-map", speed=40, wait=20, duration=120)
    comparison = make_run(tmp_path, "run_two", "test-map", speed=50, wait=10, duration=120)

    result = compare_saved_runs(baseline, comparison)

    assert metric_by_id(result, "average_speed_mph")["status"] == "informational"
    assert metric_by_id(result, "wait_per_vehicle_s")["status"] == "improved"
    assert result["top_road_changes"][0]["road_name"] == "Main Street"
    assert result["top_road_changes"][0]["status"] == "improved"
    assert result["road_coverage"]["shared_coverage_percent"] == 100.0


def test_comparison_rejects_runs_from_different_maps(tmp_path):
    baseline = make_run(tmp_path, "run_one", "map-one", speed=40, wait=20, duration=120)
    comparison = make_run(tmp_path, "run_two", "map-two", speed=50, wait=10, duration=120)

    with pytest.raises(ValueError, match="same map"):
        compare_saved_runs(baseline, comparison)


def test_short_or_different_duration_runs_include_plain_warning(tmp_path):
    baseline = make_run(tmp_path, "run_one", "test-map", speed=40, wait=20, duration=10)
    comparison = make_run(tmp_path, "run_two", "test-map", speed=40, wait=20, duration=120)

    result = compare_saved_runs(baseline, comparison)

    assert result["preliminary"]
    assert any("shorter than 60 seconds" in warning for warning in result["warnings"])
    assert any("different durations" in warning for warning in result["warnings"])


def test_different_vehicle_totals_warn_about_demand(tmp_path):
    """A large demand change can explain speed or stopped-time differences."""
    baseline = make_run(tmp_path, "run_one", "test-map", speed=40, wait=20, duration=120, vehicles=10)
    comparison = make_run(tmp_path, "run_two", "test-map", speed=45, wait=15, duration=120, vehicles=20)

    result = compare_saved_runs(baseline, comparison)

    assert any("different vehicle totals" in warning.lower() for warning in result["warnings"])


def test_different_bottleneck_versions_are_not_treated_as_equal(tmp_path):
    """Old cumulative index scores should not be compared with the 0-100 index."""
    baseline = make_run(
        tmp_path, "run_old", "test-map", speed=40, wait=20, duration=120, index_version=1
    )
    comparison = make_run(
        tmp_path, "run_new", "test-map", speed=40, wait=20, duration=120, index_version=2
    )

    result = compare_saved_runs(baseline, comparison)
    assert any("different roadmap bottleneck-index versions" in warning.lower() for warning in result["warnings"])

    with pytest.raises(ValueError, match="different RoadMap bottleneck-index versions"):
        build_heatmap_comparison_metrics(baseline, comparison, "bottleneck_score")


def test_low_shared_edge_coverage_warns_that_map_may_have_changed(tmp_path):
    baseline = make_run(tmp_path, "run_one", "test-map", speed=40, wait=20, duration=120)
    comparison = make_run(tmp_path, "run_two", "test-map", speed=40, wait=20, duration=120)
    metrics_path = comparison / "edge_metrics.csv"
    metrics = pd.read_csv(metrics_path)
    metrics["EdgeID"] = [20, 30]
    metrics.to_csv(metrics_path, index=False)
    graph_path = comparison / "network_graph.csv"
    graph = pd.read_csv(graph_path)
    graph["edge_id"] = [20, 30]
    graph["source"] = [20, 30]
    graph["target"] = [30, 40]
    graph.to_csv(graph_path, index=False)

    result = compare_saved_runs(baseline, comparison)

    assert result["road_coverage"]["shared_roads"] == 1
    assert any("map may have changed" in warning.lower() for warning in result["warnings"])


def test_edge_id_changes_still_match_the_same_directed_roads(tmp_path):
    baseline = make_run(tmp_path, "run_one", "test-map", speed=40, wait=20, duration=120)
    comparison = make_run(tmp_path, "run_two", "test-map", speed=50, wait=10, duration=120)

    metrics_path = comparison / "edge_metrics.csv"
    metrics = pd.read_csv(metrics_path)
    metrics["EdgeID"] = [101, 102]
    metrics.to_csv(metrics_path, index=False)
    graph_path = comparison / "network_graph.csv"
    graph = pd.read_csv(graph_path)
    graph["edge_id"] = [101, 102]
    graph.to_csv(graph_path, index=False)

    result = compare_saved_runs(baseline, comparison)

    assert result["road_coverage"]["shared_roads"] == 2
    assert result["road_coverage"]["shared_coverage_percent"] == 100.0
    assert result["top_road_changes"][0]["edge_id"] == 101


def test_heatmap_comparison_uses_favorable_metric_direction(tmp_path):
    baseline = make_run(tmp_path, "run_one", "test-map", speed=40, wait=20, duration=120)
    comparison = make_run(tmp_path, "run_two", "test-map", speed=50, wait=10, duration=120)

    speed_changes, speed_context = build_heatmap_comparison_metrics(
        baseline, comparison, "avg_speed_mph"
    )
    wait_changes, _ = build_heatmap_comparison_metrics(
        baseline, comparison, "avg_wait_per_vehicle_s"
    )

    assert speed_context["shared_roads"] == 2
    assert speed_changes.iloc[0]["comparison_delta"] > 0
    assert speed_context["neutral"]
    assert speed_changes.iloc[0]["comparison_status"] == "increased"
    assert wait_changes.iloc[0]["comparison_delta"] > 0
    assert wait_changes.iloc[0]["comparison_status"] == "improved"


def test_old_run_builds_average_wait_from_saved_columns(tmp_path):
    """A run saved before this update should still support the wait heatmap."""
    baseline = make_run(tmp_path, "run_one", "test-map", speed=40, wait=20, duration=120)
    comparison = make_run(tmp_path, "run_two", "test-map", speed=40, wait=10, duration=120)

    changes, context = build_heatmap_comparison_metrics(
        baseline, comparison, "avg_wait_per_vehicle_s"
    )

    assert context["shared_roads"] == 2
    assert changes.iloc[0]["baseline_value"] == pytest.approx(10.0)
    assert changes.iloc[0]["comparison_value"] == pytest.approx(5.0)


def test_complete_saved_comparison_maps_are_reused(tmp_path):
    baseline = make_run(tmp_path, "run_one", "test-map", speed=40, wait=20, duration=120)
    comparison = make_run(tmp_path, "run_two", "test-map", speed=50, wait=10, duration=120)
    baseline_heatmaps = baseline / "heatmaps"
    comparison_heatmaps = comparison / "heatmaps"
    baseline_heatmaps.mkdir()
    comparison_heatmaps.mkdir()

    saved_files = [
        baseline_heatmaps / "heatmap_avg_speed_mph.svg",
        baseline_heatmaps / "heatmap_avg_speed_mph.json",
        comparison_heatmaps / "heatmap_avg_speed_mph.svg",
        comparison_heatmaps / "heatmap_avg_speed_mph.json",
        comparison_heatmaps / "comparison_run_one_avg_speed_mph.svg",
        comparison_heatmaps / "comparison_run_one_avg_speed_mph.json",
    ]
    for path in saved_files:
        path.write_text("saved", encoding="utf-8")

    paths = get_saved_comparison_heatmap_paths(
        baseline, comparison, "run_one", "avg_speed_mph", "all"
    )

    assert paths is not None
    assert paths["change_path"].endswith("comparison_run_one_avg_speed_mph.svg")


def test_incomplete_saved_comparison_maps_are_not_reused(tmp_path):
    baseline = make_run(tmp_path, "run_one", "test-map", speed=40, wait=20, duration=120)
    comparison = make_run(tmp_path, "run_two", "test-map", speed=50, wait=10, duration=120)
    (baseline / "heatmaps").mkdir()
    (comparison / "heatmaps").mkdir()

    # Missing JSON files must force normal generation so Unreal gets complete data.
    (baseline / "heatmaps" / "heatmap_bottleneck_score.svg").write_text("saved", encoding="utf-8")
    (comparison / "heatmaps" / "heatmap_bottleneck_score.svg").write_text("saved", encoding="utf-8")
    (comparison / "heatmaps" / "comparison_run_one_bottleneck_score.svg").write_text(
        "saved", encoding="utf-8"
    )

    assert get_saved_comparison_heatmap_paths(
        baseline, comparison, "run_one", "bottleneck_score", "all"
    ) is None
