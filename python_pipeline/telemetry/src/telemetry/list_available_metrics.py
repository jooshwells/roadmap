from __future__ import annotations

import argparse
import json


# Returns the heatmap metrics that users can generate from the telemetry panel.
def get_available_metrics() -> list[dict[str, str]]:
    return [
        {
            "metric": "bottleneck_score",
            "display_name": "Bottleneck Score",
        },
        {
            "metric": "estimated_flow_veh_per_hr",
            "display_name": "Estimated Traffic Flow",
        },
        {
            "metric": "avg_speed_mph",
            "display_name": "Average Speed",
        },
        {
            "metric": "total_wait_added_s",
            "display_name": "Total Wait Time",
        },
    ]


# Prints the available heatmap metrics as JSON so Unreal can read them later.
def main() -> None:
    parser = argparse.ArgumentParser(
        description="List the available RoadMap telemetry heatmap metrics."
    )

    parser.add_argument(
        "--pretty",
        action="store_true",
        help="Print formatted JSON for easier reading in PowerShell.",
    )

    args = parser.parse_args()

    metrics = get_available_metrics()

    output = {
        "metric_count": len(metrics),
        "metrics": metrics,
    }

    indent = 2 if args.pretty else None
    print(json.dumps(output, indent=indent))


if __name__ == "__main__":
    main()