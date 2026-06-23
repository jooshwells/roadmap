"""
test_telemetry_analysis.py

Unit tests for my RoadMap telemetry analysis script.

These tests use a tiny fake telemetry dataset instead of the full simulation CSV.
That makes the tests fast, easy to understand, and safe to run every time I
change the telemetry code.

The goal is not to prove the whole simulator is perfect. The goal is to make
sure my Python post-processing code still:
    - loads the expected telemetry columns,
    - calculates derived values like speed in mph and added wait time,
    - creates edge, vehicle, and origin/destination metrics,
    - catches suspicious telemetry values,
    - writes all expected output files.
"""

import sys
from pathlib import Path

# The telemetry code now lives in src/telemetry, so I add that
# folder to the import path before running the tests.
PROJECT_ROOT = Path(__file__).resolve().parents[1]

sys.path.insert(
    0,
    str(PROJECT_ROOT / "src" / "telemetry")
)

import pandas as pd
import pytest

from telemetry_analysis import (
    load_telemetry,
    validate_telemetry,
    add_derived_columns,
    build_run_summary,
    build_edge_metrics,
    build_vehicle_metrics,
    build_od_metrics,
    build_flags,
    run_analysis,
)


def sample_df():
    """
    Create a small sample telemetry table for testing.

    This is intentionally simple:
    - Vehicle 1 moves from edge 100 to edge 101.
    - Vehicle 2 stays on edge 100 and briefly stops.
    - WaitTime_s increases for both vehicles, so I can test wait deltas.
    """
    return pd.DataFrame(
        [
            {
                "SourceRow": 0,
                "Time": 0.0,
                "VehicleID": 1,
                "EdgeID": 100,
                "LaneIndex": 0,
                "Speed_mps": 10.0,
                "Accel_mps2": 0.5,
                "Pos_m": 0.0,
                "RouteIndex": 0,
                "WaitTime_s": 0.0,
                "OriginID": 1,
                "DestID": 9,
            },
            {
                "SourceRow": 0,
                "Time": 1.0,
                "VehicleID": 1,
                "EdgeID": 100,
                "LaneIndex": 0,
                "Speed_mps": 11.0,
                "Accel_mps2": 1.0,
                "Pos_m": 11.0,
                "RouteIndex": 0,
                "WaitTime_s": 0.0,
                "OriginID": 1,
                "DestID": 9,
            },
            {
                "SourceRow": 0,
                "Time": 2.0,
                "VehicleID": 1,
                "EdgeID": 101,
                "LaneIndex": 0,
                "Speed_mps": 2.0,
                "Accel_mps2": -2.0,
                "Pos_m": 3.0,
                "RouteIndex": 1,
                "WaitTime_s": 1.0,
                "OriginID": 1,
                "DestID": 9,
            },
            {
                "SourceRow": 0,
                "Time": 0.0,
                "VehicleID": 2,
                "EdgeID": 100,
                "LaneIndex": 1,
                "Speed_mps": 8.0,
                "Accel_mps2": 0.0,
                "Pos_m": 0.0,
                "RouteIndex": 0,
                "WaitTime_s": 0.0,
                "OriginID": 2,
                "DestID": 9,
            },
            {
                "SourceRow": 0,
                "Time": 1.0,
                "VehicleID": 2,
                "EdgeID": 100,
                "LaneIndex": 1,
                "Speed_mps": 0.0,
                "Accel_mps2": -3.0,
                "Pos_m": 4.0,
                "RouteIndex": 0,
                "WaitTime_s": 1.0,
                "OriginID": 2,
                "DestID": 9,
            },
        ]
    )


def test_load_telemetry_checks_required_columns(tmp_path):
    """The loader should reject files that do not have the required columns."""
    bad_file = tmp_path / "bad.csv"
    pd.DataFrame({"Time": [0], "VehicleID": [1]}).to_csv(bad_file, index=False)

    with pytest.raises(ValueError):
        load_telemetry(bad_file)


def test_validate_good_telemetry_has_no_issues():
    """My clean sample data should not trigger validation errors."""
    issues = validate_telemetry(sample_df())
    assert issues == []


def test_derived_wait_delta_is_added():
    """Derived columns should include added wait time and speed in mph."""
    df = add_derived_columns(sample_df())

    assert "WaitDelta_s" in df.columns
    assert df["WaitDelta_s"].sum() == 2.0
    assert "Speed_mph" in df.columns


def test_run_summary_basic_counts():
    """The run summary should count rows, vehicles, and used edges correctly."""
    df = add_derived_columns(sample_df())
    summary, duration_hr = build_run_summary(df)

    assert summary["rows"] == 5
    assert summary["vehicles"] == 2
    assert summary["edges_used"] == 2
    assert duration_hr > 0


def test_edge_metrics_are_created():
    """Edge metrics should include the main columns used for heatmaps and FDOT work."""
    df = add_derived_columns(sample_df())
    _, duration_hr = build_run_summary(df)
    edge_metrics = build_edge_metrics(df, duration_hr)

    assert set(edge_metrics["EdgeID"]) == {100, 101}
    assert "avg_speed_mph" in edge_metrics.columns
    assert "bottleneck_score" in edge_metrics.columns

    # These are newer fields used by the updated heatmap and future FDOT comparison.
    assert "edge_entry_count" in edge_metrics.columns
    assert "estimated_flow_veh_per_hr" in edge_metrics.columns
    assert "low_speed_sample_ratio" in edge_metrics.columns
    assert "bad_accel_count" in edge_metrics.columns


def test_vehicle_and_od_metrics_are_created():
    """Vehicle and OD metrics should summarize trips by vehicle and route pair."""
    df = add_derived_columns(sample_df())
    vehicle_metrics = build_vehicle_metrics(df)
    od_metrics = build_od_metrics(vehicle_metrics)

    assert len(vehicle_metrics) == 2
    assert "trip_time_s" in vehicle_metrics.columns
    assert od_metrics["vehicle_count"].sum() == 2


def test_flags_catch_bad_values():
    """The flag builder should catch bad physics values before we trust the output."""
    df = sample_df()

    # I manually create two bad rows to make sure the flag system catches them.
    df.loc[0, "Accel_mps2"] = 999.0
    df.loc[1, "Speed_mps"] = -1.0

    df = add_derived_columns(df)
    flags = build_flags(df)

    assert len(flags) >= 2
    assert flags["FlagReason"].str.contains("unrealistic acceleration").any()
    assert flags["FlagReason"].str.contains("negative speed").any()


def test_run_analysis_writes_output_files(tmp_path):
    """The full analysis function should write every expected output file."""
    input_file = tmp_path / "simulation_output.csv"
    output_dir = tmp_path / "telemetry_outputs"
    df = sample_df().drop(columns=["SourceRow"])
    df.to_csv(input_file, index=False)

    results = run_analysis(input_file, output_dir)

    assert (output_dir / "edge_metrics.csv").exists()
    assert (output_dir / "vehicle_metrics.csv").exists()
    assert (output_dir / "od_metrics.csv").exists()
    assert (output_dir / "bottleneck_edges.csv").exists()
    assert (output_dir / "telemetry_flags.csv").exists()
    assert (output_dir / "run_summary.txt").exists()
    assert "edge_metrics" in results
