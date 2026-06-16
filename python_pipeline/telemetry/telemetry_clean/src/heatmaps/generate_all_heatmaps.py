"""
generate_all_heatmaps.py

I use this script when I want to generate every RoadMap telemetry
heatmap at once instead of running the visualization script multiple times.

The script calls visualize_telemetry_heatmap.py once for each metric
that I want to visualize.
"""

from pathlib import Path
import subprocess
import sys

BASE_DIR = Path(__file__).resolve().parents[2]

VISUALIZER_SCRIPT = (
    BASE_DIR / "src/heatmaps/visualize_telemetry_heatmap.py"
)

metrics = [
    "bottleneck_score",
    "estimated_flow_veh_per_hr",
    "avg_speed_mph",
    "total_wait_added_s",
]

for metric in metrics:
    print(f"Generating heatmap: {metric}")

    subprocess.run(
        [
            sys.executable,
            str(VISUALIZER_SCRIPT),
            "--metric",
            metric,
        ],
        check=True,
    )

print("\nFinished generating all heatmaps.")