import sys
from pathlib import Path

import pandas as pd
import pytest

PROJECT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_ROOT))

from src.heatmaps.visualize_telemetry_heatmap import filter_metrics_for_focus, normalize_focus


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


def test_unknown_focus_is_rejected():
    with pytest.raises(ValueError):
        normalize_focus("worst_3")
