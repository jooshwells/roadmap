import json
import sys
from pathlib import Path

import pandas as pd
import pytest


PROJECT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_ROOT))

from src.telemetry.run_comparison import compare_saved_runs


def make_run(tmp_path: Path, name: str, map_id: str, speed: float, wait: float, duration: float) -> Path:
    folder = tmp_path / map_id / name
    folder.mkdir(parents=True)
    (folder / "run_metadata.json").write_text(json.dumps({
        "run_id": name,
        "created_at": "2026-07-15_12-00-00",
        "map_id": map_id,
        "map_name": "Test Map",
    }), encoding="utf-8")
    (folder / "telemetry_summary.json").write_text(json.dumps({
        "total_vehicles": 10,
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

    assert metric_by_id(result, "average_speed_mph")["status"] == "improved"
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
