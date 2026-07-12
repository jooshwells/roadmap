from pathlib import Path
import argparse
import json
import sys

# When this file is run directly, Python may not know where the src folder is.
# This adds the telemetry folder to the import path so both command styles work.
TELEMETRY_DIR = Path(__file__).resolve().parents[2]

if str(TELEMETRY_DIR) not in sys.path:
    sys.path.insert(0, str(TELEMETRY_DIR))

from src.heatmaps.visualize_telemetry_heatmap import load_files, plot_heatmap


# These are the heatmap metrics the dashboard is allowed to request.
ALLOWED_METRICS = {
    "bottleneck_score",
    "estimated_flow_veh_per_hr",
    "avg_speed_mph",
    "total_wait_added_s",
}


# Finds the real telemetry folder in both development and packaged builds.
# Packaged builds use the folder containing run_pipeline.exe instead of
# PyInstaller's temporary extraction folder.
def get_telemetry_dir() -> Path:
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parent

    return Path(__file__).resolve().parents[2]


# Updates run_metadata.json after a heatmap is created.
# This lets Unreal know which heatmaps are already available for this run.
def update_available_heatmaps(run_folder: Path, metric: str, output_path: Path) -> None:
    metadata_path = run_folder / "run_metadata.json"

    if metadata_path.exists():
        with open(metadata_path, "r", encoding="utf-8") as file:
            metadata = json.load(file)
    else:
        metadata = {}

    available_heatmaps = metadata.get("available_heatmaps", [])

    heatmap_entry = {
        "metric": metric,
        "path": output_path.relative_to(run_folder).as_posix(),
    }

    # Replace older string entries and previous entries for this metric.
    available_heatmaps = [
        item for item in available_heatmaps
        if not (
            item == metric
            or (isinstance(item, dict) and item.get("metric") == metric)
        )
    ]

    available_heatmaps.append(heatmap_entry)

    metadata["available_heatmaps"] = available_heatmaps
    metadata["status"] = "heatmap_ready"

    with open(metadata_path, "w", encoding="utf-8") as file:
        json.dump(metadata, file, indent=4)


# Generates one selected heatmap for one saved run.
def generate_selected_heatmap(run_id: str, metric: str, network_path: Path | None = None) -> Path:
    telemetry_dir = get_telemetry_dir()
    run_folder = telemetry_dir / "outputs" / "runs" / run_id

    if metric not in ALLOWED_METRICS:
        raise ValueError(f"Unsupported heatmap metric: {metric}")

    if not run_folder.exists():
        raise FileNotFoundError(f"Run folder not found: {run_folder}")

    edge_metrics_path = run_folder / "edge_metrics.csv"

    if not edge_metrics_path.exists():
        raise FileNotFoundError(f"edge_metrics.csv not found for run: {run_folder}")

    if network_path is None:
        network_path = run_folder / "network_graph.csv"

    if not network_path.exists():
        raise FileNotFoundError(f"Network graph not found: {network_path}")

    heatmap_dir = run_folder / "heatmaps"
    heatmap_dir.mkdir(parents=True, exist_ok=True)

    output_path = heatmap_dir / f"heatmap_{metric}.png"

    network_df, metrics_df = load_files(network_path, edge_metrics_path)

    plot_heatmap(
        network_df,
        metrics_df,
        metric,
        output_path,
    )

    update_available_heatmaps(run_folder, metric, output_path)

    return output_path


# Reads command-line arguments and creates the requested heatmap.
# The final print is JSON so Unreal can safely read the result.
def main() -> int:
    parser = argparse.ArgumentParser(description="Generate one RoadMap heatmap for a saved run.")

    parser.add_argument(
        "--run-id",
        required=True,
        help="Run folder name, such as run_2026-07-06_14-31-16.",
    )

    parser.add_argument(
        "--metric",
        required=True,
        choices=sorted(ALLOWED_METRICS),
        help="Heatmap metric to generate.",
    )

    parser.add_argument(
        "--network",
        default=None,
        help="Optional custom network graph CSV path.",
    )

    args = parser.parse_args()

    try:
        network_path = Path(args.network) if args.network else None

        output_path = generate_selected_heatmap(
            run_id=args.run_id,
            metric=args.metric,
            network_path=network_path,
        )

        print(json.dumps({
            "success": True,
            "run_id": args.run_id,
            "metric": args.metric,
            "heatmap_path": str(output_path),
        }))

        return 0

    except Exception as error:
        print(json.dumps({
            "success": False,
            "error": str(error),
        }))

        return 1

if __name__ == "__main__":
    raise SystemExit(main())
