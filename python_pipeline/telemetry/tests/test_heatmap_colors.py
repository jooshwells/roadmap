"""Small checks for the meaning of each heatmap color scale."""

import sys
from pathlib import Path

from matplotlib.colors import to_hex


PROJECT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_ROOT))

from src.heatmaps.visualize_telemetry_heatmap import choose_color_settings


# Return the bottom and top colors used by one heatmap metric.
def color_ends(metric: str) -> tuple[str, str]:
    color_map, _, _ = choose_color_settings(metric)
    return to_hex(color_map(0.0)), to_hex(color_map(1.0))


def test_speed_uses_red_for_low_and_green_for_high():
    """Higher speed should move toward green on the legend."""
    low, high = color_ends("avg_speed_mph")
    assert low == "#ff3b30"
    assert high == "#00884a"


def test_flow_uses_green_for_low_and_red_for_high():
    """The familiar traffic scale should show flow amount, not pass or fail."""
    low, high = color_ends("estimated_flow_veh_per_hr")
    assert low == "#00884a"
    assert high == "#ff3b30"


def test_problem_screening_metrics_use_green_for_low_and_red_for_high():
    """Stopped time, the project index, and GEH place high values at red."""
    metrics = [
        "avg_wait_per_vehicle_s",
        "bottleneck_score",
        "fdot_geh_score",
    ]
    for metric in metrics:
        low, high = color_ends(metric)
        assert low == "#00884a"
        assert high == "#ff3b30"
