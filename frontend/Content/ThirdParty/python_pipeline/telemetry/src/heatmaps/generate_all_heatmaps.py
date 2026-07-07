"""
generate_all_heatmaps.py

Generates every RoadMap telemetry heatmap at once.
"""

from pathlib import Path
import argparse
import subprocess
import sys

BASE_DIR = Path(__file__).resolve().parents[2]

VISUALIZER_SCRIPT = BASE_DIR / "src/heatmaps/visualize_telemetry_heatmap_v2.py"

DEFAULT_NETWORK = BASE_DIR / "data/network/network_graph_waterford.csv"

metrics = [
    "bottleneck_score",
    "estimated_flow_veh_per_hr",
    "avg_speed_mph",
    "total_wait_added_s",
]


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate all RoadMap telemetry heatmaps.")

    parser.add_argument(
        "--network",
        default=DEFAULT_NETWORK,
        help="Path to the network graph CSV used for drawing heatmaps.",
    )

    args = parser.parse_args()

    for metric in metrics:
        print(f"Generating heatmap: {metric}")

        subprocess.run(
            [
                sys.executable,
                str(VISUALIZER_SCRIPT),
                "--metric",
                metric,
                "--network",
                str(args.network),
            ],
            check=True,
        )

    print("\nFinished generating all heatmaps.")


if __name__ == "__main__":
    main()