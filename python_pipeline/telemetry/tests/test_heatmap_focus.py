import sys
from pathlib import Path

import pandas as pd
import pytest

PROJECT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_ROOT))

from src.heatmaps.visualize_telemetry_heatmap import (
    build_interactive_road_data,
    filter_metrics_for_focus,
    normalize_focus,
    summary_rows_for_metric,
)


def sample_metrics():
    return pd.DataFrame({
        "EdgeID": range(1, 21),
        "bottleneck_score": range(20),
        "avg_speed_mph": range(20, 40),
    })


def test_all_focus_keeps_every_road():
    result = filter_metrics_for_focus(sample_metrics(), "bottleneck_score", "all")
    assert len(result) == 20


@pytest.mark.parametrize("focus,count", [("worst_25", 5), ("worst_10", 2), ("worst_5", 1)])
def test_high_bad_metric_keeps_largest_values(focus, count):
    result = filter_metrics_for_focus(sample_metrics(), "bottleneck_score", focus)
    assert len(result) == count
    assert result["bottleneck_score"].min() == 20 - count


def test_speed_focus_keeps_slowest_values():
    result = filter_metrics_for_focus(sample_metrics(), "avg_speed_mph", "worst_10")
    assert result["avg_speed_mph"].tolist() == [20, 21]


def test_summary_names_the_exact_directed_segment():
    """A repeated road name should still show which EdgeID owns the result."""
    merged = pd.DataFrame({
        "EdgeID": [10, 11],
        "name": ["Rouse Road", "Rouse Road"],
        "avg_speed_mph": [3.5, 14.0],
    })

    rows = summary_rows_for_metric(
        "avg_speed_mph",
        merged["avg_speed_mph"],
        background_count=20,
        heat_count=2,
        merged_df=merged,
        focus="worst_10",
    )

    assert rows[2] == ("Lowest Average-Speed Segment", "Rouse Road - Edge 10")
    assert rows[-1][0] == "Highlighted-Segment Average"


def test_unknown_focus_is_rejected():
    with pytest.raises(ValueError):
        normalize_focus("worst_3")


def test_interactive_roads_are_normalized_and_keep_important_direction():
    merged = pd.DataFrame([
        {
            "EdgeID": 10,
            "source_x": 0.0,
            "source_y": 0.0,
            "target_x": 10.0,
            "target_y": 5.0,
            "geometry_xy": None,
            "name": "Test Avenue",
            "ref": "SR 10",
            "highway": "primary",
            "bottleneck_score": 0.25,
        },
        {
            "EdgeID": 11,
            "source_x": 10.0,
            "source_y": 5.0,
            "target_x": 0.0,
            "target_y": 0.0,
            "geometry_xy": None,
            "name": "Test Avenue",
            "ref": "SR 10",
            "highway": "primary",
            "bottleneck_score": 0.75,
        },
    ])

    roads = build_interactive_road_data(
        merged,
        "bottleneck_score",
        (-1.0, 11.0, -1.0, 6.0),
    )

    assert len(roads) == 1
    assert roads[0]["edge_id"] == 11
    assert roads[0]["name"] == "Test Avenue"
    assert roads[0]["route_ref"] == "SR 10"
    assert roads[0]["value"] == 0.75
    assert roads[0]["points"] == [
        [0.916667, 0.857143],
        [0.083333, 0.142857],
    ]
