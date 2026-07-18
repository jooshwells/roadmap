"""Main entry point shared by the simulation and the Unreal telemetry panel.

This file saves new runs and handles the small commands sent by the C++ panel.
Most of the math and drawing work is kept in the files under ``src``.
"""

from pathlib import Path
import sys
import json
import pandas as pd
import shutil

from src.telemetry.telemetry_analysis import run_analysis
from src.telemetry.run_manager import create_run_folder, make_map_id
from src.telemetry.run_comparison import compare_saved_runs, build_heatmap_comparison_metrics
from src.heatmaps.visualize_telemetry_heatmap import load_files, plot_heatmap
from src.fdot.fdot_vs_simulation import compare_fdot_to_simulation
from src.fdot.fdot_option_a_matcher import DEFAULT_FDOT_FILE, ensure_fdot_mapping
# Builds the heatmap-ready network graph from active roadmap JSONL files.
from src.heatmaps.build_network_graph_with_geometry import build_network_graph

if getattr(sys, "frozen", False):
    TELEMETRY_DIR = Path(sys.executable).resolve().parent
else:
    TELEMETRY_DIR = Path(__file__).resolve().parent
PYTHON_PIPELINE_DIR = TELEMETRY_DIR.parent
BASE_DIR = PYTHON_PIPELINE_DIR.parent

OUTPUT_DIR = TELEMETRY_DIR / "outputs"
INPUT_DIR = TELEMETRY_DIR / "inputs"

EDGE_JSONL_PATH = PYTHON_PIPELINE_DIR / "sample_out" / "waterford_edges_orange_allroads_offline_xy.jsonl"
EDITED_EDGES_PATH = INPUT_DIR / "edited_edges.csv"

def clean_label_value(value):
    """Turn an optional road label value into clean text."""
    if value is None:
        return None

    if isinstance(value, list):
        value = [str(v).strip() for v in value if v is not None and str(v).strip()]
        return "; ".join(value) if value else None

    if pd.isna(value):
        return None

    value = str(value).strip()
    return value if value else None


def make_road_label(row) -> str:
    """Pick the best readable name available for one road."""
    name = clean_label_value(row.get("road_name"))
    ref = clean_label_value(row.get("road_ref"))
    highway = clean_label_value(row.get("highway_type"))

    if name:
        return name

    if ref:
        return ref

    if highway:
        return f"Unnamed {highway} road"

    return "Unnamed road"


def load_edge_metadata(edge_jsonl_path: Path = EDGE_JSONL_PATH) -> pd.DataFrame:
    """Read road names and IDs from the active edge JSONL file."""
    if not edge_jsonl_path.exists():
        print(f"No edge JSONL found: {edge_jsonl_path}")
        return pd.DataFrame()

    rows = []

    with edge_jsonl_path.open("r", encoding="utf-8") as file:
        for line_number, line in enumerate(file):
            if not line.strip():
                continue

            item = json.loads(line)

            rows.append({
                # The JSONL does not currently store edge_id.
                # The sim uses the line order as the EdgeID.
                "EdgeID": line_number,
                "road_name": item.get("name"),
                "road_ref": item.get("ref"),
                "highway_type": item.get("highway"),
                "length_m": item.get("length_m"),
                "source_node": item.get("u"),
                "target_node": item.get("v"),
            })

    metadata = pd.DataFrame(rows)

    if len(metadata) == 0:
        return metadata

    metadata["EdgeID"] = metadata["EdgeID"].astype(int)
    metadata["road_label"] = metadata.apply(make_road_label, axis=1)

    return metadata


# Creates a small JSON summary that Unreal can read for the dashboard.
# This uses the real run summary from telemetry_analysis.py so totals are accurate.
def write_dashboard_summary(run_folder: Path, run_summary: dict) -> None:
    """Write the short summary JSON used by the Unreal overview tab."""
    edge_metrics_path = run_folder / "edge_metrics.csv"
    bottlenecks_path = run_folder / "bottleneck_edges.csv"
    summary_path = run_folder / "telemetry_summary.json"

    edge_metrics = pd.read_csv(edge_metrics_path)
    bottlenecks = pd.read_csv(bottlenecks_path)

    # Load road names and road types from the original edge JSONL when available.
    # This gives the dashboard better labels than only showing EdgeID numbers.
    # Every run stores its own edge metadata, so summaries cannot accidentally
    # borrow road names from a different map.
    metadata = load_edge_metadata(run_folder / "edges.jsonl")

    if not metadata.empty:
        bottlenecks = bottlenecks.merge(
            metadata[["EdgeID", "road_label"]],
            on="EdgeID",
            how="left",
        )
    else:
        bottlenecks["road_label"] = None

    # Gives every bottleneck a readable label.
    # If we do not have a real road name, we fall back to Road Segment <edge_id>.
    def get_dashboard_road_label(row) -> str:
        edge_id = int(row["EdgeID"])
        road_label = row.get("road_label")

        if pd.notna(road_label) and str(road_label).strip():
            return str(road_label).strip()

        return f"Road Segment {edge_id}"

    worst_bottleneck = None
    top_5_bottlenecks = []

    if not bottlenecks.empty:
        worst_row = bottlenecks.iloc[0]

        worst_bottleneck = {
            "edge_id": int(worst_row["EdgeID"]),
            "road_label": get_dashboard_road_label(worst_row),
            "bottleneck_score": float(worst_row["bottleneck_score"]),
            "avg_speed_mph": float(worst_row["avg_speed_mph"]),
            "total_wait_added_s": float(worst_row["total_wait_added_s"]),
        }

        for _, row in bottlenecks.head(5).iterrows():
            edge_id = int(row["EdgeID"])

            top_5_bottlenecks.append({
                "edge_id": edge_id,
                "road_label": get_dashboard_road_label(row),
                "bottleneck_score": float(row["bottleneck_score"]),
                "avg_speed_mph": float(row["avg_speed_mph"]),
                "total_wait_added_s": float(row["total_wait_added_s"]),
            })

    summary = {
        # Version 2 is the fixed 0-100 per-entry index. Saving the version stops
        # old cumulative scores from being treated as directly comparable.
        "bottleneck_index_version": 2,
        "total_vehicles": int(run_summary["vehicles"]),
        "simulation_duration_s": float(run_summary["simulation_duration_s"]),
        "edges_used": int(run_summary["edges_used"]),
        "average_speed_mph": float(run_summary["average_speed_mph"]),
        "total_wait_added_s": float(run_summary["total_wait_added_s"]),
        "max_wait_time_s": float(run_summary["max_wait_time_s"]),
        "worst_bottleneck": worst_bottleneck,
        "top_5_bottleneck_roads": top_5_bottlenecks,
    }

    with open(summary_path, "w", encoding="utf-8") as file:
        json.dump(summary, file, indent=4)

    print(f"Wrote dashboard summary: {summary_path}")

# Updates the run metadata after the analysis finishes.
# This helps Unreal know that the run is ready and which files belong to it.
def update_run_metadata(run_folder: Path, run_id: str) -> None:
    """Mark a saved run complete and record its available files."""
    metadata_path = run_folder / "run_metadata.json"

    if metadata_path.exists():
        with open(metadata_path, "r", encoding="utf-8") as file:
            metadata = json.load(file)
    else:
        metadata = {
            "run_id": run_id,
        }

    metadata["status"] = "analysis_complete"
    metadata["summary_path"] = "telemetry_summary.json"
    metadata["csv_path"] = "simulation_output.csv"
    metadata["edge_metrics_path"] = "edge_metrics.csv"
    metadata["bottleneck_edges_path"] = "bottleneck_edges.csv"
    metadata["vehicle_metrics_path"] = "vehicle_metrics.csv"
    metadata["od_metrics_path"] = "od_metrics.csv"
    metadata["telemetry_flags_path"] = "telemetry_flags.csv"
    metadata["heatmaps_folder"] = "heatmaps"
    metadata["fdot_folder"] = "fdot"
    metadata["available_heatmaps"] = []

    with open(metadata_path, "w", encoding="utf-8") as file:
        json.dump(metadata, file, indent=4)

    print(f"Updated run metadata: {metadata_path}")


# Returns the heatmap metrics that the Unreal telemetry panel can show.
# These names must match the columns created by telemetry_analysis.py.
def get_available_metrics() -> dict:
    """Return the heatmap choices shown in the panel."""
    return {
        "success": True,
        "metric_count": 4,
        "metrics": [
            {
                "metric": "bottleneck_score",
                "display_name": "RoadMap Bottleneck Index",
            },
            {
                "metric": "estimated_flow_veh_per_hr",
                "display_name": "Estimated Hourly Traffic Flow",
            },
            {
                "metric": "avg_speed_mph",
                "display_name": "Average Recorded Speed",
            },
            {
                "metric": "avg_wait_per_vehicle_s",
                "display_name": "Average Stopped Time per Vehicle Entry",
            },
        ],
    }


# Finds the folder where saved telemetry runs are stored.
def get_runs_dir() -> Path:
    """Return the root folder that contains map run folders."""
    return OUTPUT_DIR / "runs"


def iter_run_folders() -> list[Path]:
    """Find both legacy flat runs and map-organized runs."""
    runs_dir = get_runs_dir()
    if not runs_dir.exists():
        return []
    return sorted(
        (folder for folder in runs_dir.glob("**/run_*") if folder.is_dir()),
        key=lambda folder: folder.name,
        reverse=True,
    )


def find_run_folder(run_id: str) -> Path | None:
    """Resolve a unique run ID without exposing map folder paths to Unreal."""
    if not run_id or Path(run_id).name != run_id:
        return None
    for folder in iter_run_folders():
        if folder.name == run_id:
            return folder
    return None


# Safely loads JSON from a file. If the file is missing or broken, return an empty dict.
def load_json_file(path: Path) -> dict:
    """Load a JSON object and return an empty object for a missing file."""
    if not path.exists():
        return {}

    try:
        with path.open("r", encoding="utf-8") as file:
            return json.load(file)
    except Exception:
        return {}


# Lists saved run folders so Unreal can populate the telemetry panel.
def list_saved_runs() -> dict:
    """Build the saved-run list returned to the telemetry panel."""
    runs_dir = get_runs_dir()

    if not runs_dir.exists():
        return {
            "success": True,
            "run_count": 0,
            "runs": [],
        }

    runs = []

    for run_folder in iter_run_folders():

        metadata = load_json_file(run_folder / "run_metadata.json")
        summary = load_json_file(run_folder / "telemetry_summary.json")

        runs.append({
            "run_id": metadata.get("run_id", run_folder.name),
            "status": metadata.get("status", "unknown"),
            "folder": str(run_folder),
            "map_name": metadata.get("map_name"),
            "created_at": metadata.get("created_at"),
            "total_vehicles": summary.get("total_vehicles"),
            "average_speed_mph": summary.get("average_speed_mph"),
        })

    return {
        "success": True,
        "run_count": len(runs),
        "runs": runs,
    }


# Gets metadata and summary values for one saved run.
def get_run_details(run_id: str) -> dict:
    """Return the saved summary for one selected run."""
    run_folder = find_run_folder(run_id)

    if run_folder is None:
        return {
            "success": False,
            "error": "Run folder not found.",
            "run_id": run_id,
            "run_folder": "",
        }

    metadata = load_json_file(run_folder / "run_metadata.json")
    summary = load_json_file(run_folder / "telemetry_summary.json")

    return {
        "success": True,
        "run_id": run_id,
        "run_folder": str(run_folder),
        "metadata": metadata,
        "summary": summary,
    }


def compare_runs(baseline_run_id: str, comparison_run_id: str) -> dict:
    """Compare two saved runs after checking that both exist."""
    """Compare two saved runs after resolving their folders safely."""
    if baseline_run_id == comparison_run_id:
        return {"success": False, "error": "Choose two different runs to compare."}

    baseline_folder = find_run_folder(baseline_run_id)
    comparison_folder = find_run_folder(comparison_run_id)
    if baseline_folder is None or comparison_folder is None:
        return {"success": False, "error": "One or both saved run folders could not be found."}

    try:
        return compare_saved_runs(baseline_folder, comparison_folder)
    except (FileNotFoundError, ValueError, KeyError, pd.errors.ParserError) as error:
        return {"success": False, "error": str(error)}


def generate_comparison_heatmaps(
    baseline_run_id: str,
    comparison_run_id: str,
    metric: str,
    focus: str = "all",
) -> dict:
    """Generate both source maps and a stable road-pair change map."""
    if baseline_run_id == comparison_run_id:
        return {"success": False, "error": "Choose two different runs to compare."}
    baseline_folder = find_run_folder(baseline_run_id)
    comparison_folder = find_run_folder(comparison_run_id)
    if baseline_folder is None or comparison_folder is None:
        return {"success": False, "error": "One or both saved run folders could not be found."}

    try:
        baseline_result = generate_single_heatmap(baseline_run_id, metric, focus)
        comparison_result = generate_single_heatmap(comparison_run_id, metric, focus)
        if not baseline_result.get("success"):
            return baseline_result
        if not comparison_result.get("success"):
            return comparison_result

        change_metrics, context = build_heatmap_comparison_metrics(
            baseline_folder,
            comparison_folder,
            metric,
        )
        comparison_network = pd.read_csv(comparison_folder / "network_graph.csv")
        change_stem = f"comparison_{baseline_run_id}_{metric}"
        if focus != "all":
            change_stem += f"_{focus}"
        change_path = comparison_folder / "heatmaps" / f"{change_stem}.svg"
        plot_heatmap(
            comparison_network,
            change_metrics,
            "comparison_delta",
            change_path,
            focus=focus,
        )
        return {
            "success": True,
            "baseline_run_id": baseline_run_id,
            "comparison_run_id": comparison_run_id,
            "metric": metric,
            "focus": focus,
            "baseline_path": baseline_result["heatmap_path"],
            "comparison_path": comparison_result["heatmap_path"],
            "change_path": str(change_path),
            "shared_roads": context["shared_roads"],
        }
    except (FileNotFoundError, ValueError, KeyError, pd.errors.ParserError) as error:
        return {"success": False, "error": str(error)}


def delete_saved_run(run_id: str) -> dict:
    """Delete one resolved run folder without accepting arbitrary paths."""
    run_folder = find_run_folder(run_id)
    if run_folder is None:
        return {"success": False, "error": "Run folder not found."}
    try:
        runs_root = get_runs_dir().resolve()
        resolved_run = run_folder.resolve()
        if not resolved_run.is_relative_to(runs_root) or not resolved_run.name.startswith("run_"):
            return {"success": False, "error": "The selected run path is not safe to delete."}
        shutil.rmtree(resolved_run)
        return {"success": True, "run_id": run_id}
    except OSError as error:
        return {"success": False, "error": f"Could not delete the selected run: {error}"}


# Returns the expected heatmap SVG path for one run and metric.
def get_heatmap_path(run_id: str, metric: str) -> dict:
    """Return an existing SVG heatmap path for one run and metric."""
    run_folder = find_run_folder(run_id)

    if run_folder is None:
        return {
            "success": False,
            "error": "Run folder not found.",
            "run_id": run_id,
            "metric": metric,
            "run_folder": "",
        }

    heatmap_path = run_folder / "heatmaps" / f"heatmap_{metric}.svg"

    if not heatmap_path.exists():
        return {
            "success": False,
            "error": "Heatmap has not been generated for this metric yet.",
            "run_id": run_id,
            "metric": metric,
            "run_folder": str(run_folder),
            "expected_path": str(heatmap_path),
        }

    return {
        "success": True,
        "run_id": run_id,
        "metric": metric,
        "relative_path": f"heatmaps/heatmap_{metric}.svg",
        "full_path": str(heatmap_path),
    }


# Adds a generated heatmap path to run_metadata.json so Unreal knows where it is.
def add_available_heatmap_to_metadata(run_folder: Path, metric: str, output_path: Path, focus: str = "all") -> None:
    """Record a newly created heatmap in the run metadata file."""
    metadata_path = run_folder / "run_metadata.json"
    metadata = load_json_file(metadata_path)

    if not metadata:
        metadata = {
            "run_id": run_folder.name,
        }

    available_heatmaps = metadata.get("available_heatmaps", [])
    heatmap_entry = {
        "metric": metric,
        "focus": focus,
        "path": output_path.relative_to(run_folder).as_posix(),
    }

    # Replace older string entries and previous entries for this metric.
    available_heatmaps = [
        item for item in available_heatmaps
        if not (
            item == metric and focus == "all"
            or (
                isinstance(item, dict)
                and item.get("metric") == metric
                and item.get("focus", "all") == focus
            )
        )
    ]
    available_heatmaps.append(heatmap_entry)

    metadata["available_heatmaps"] = available_heatmaps
    metadata["heatmaps_folder"] = "heatmaps"

    with metadata_path.open("w", encoding="utf-8") as file:
        json.dump(metadata, file, indent=4)


# Generates one heatmap for one saved run instead of generating every metric automatically.
def heatmap_file_stem(metric: str, focus: str) -> str:
    """Build a consistent heatmap filename from its options."""
    return f"heatmap_{metric}" if focus == "all" else f"heatmap_{metric}_{focus}"


def generate_single_heatmap(run_id: str, metric: str, focus: str = "all") -> dict:
    """Generate one SVG heatmap and its display JSON for a saved run."""
    run_folder = find_run_folder(run_id)

    if run_folder is None:
        return {
            "success": False,
            "error": "Run folder not found.",
            "run_id": run_id,
            "metric": metric,
            "run_folder": "",
        }

    edge_metrics_path = run_folder / "edge_metrics.csv"
    network_path = run_folder / "network_graph.csv"

    if not edge_metrics_path.exists():
        return {
            "success": False,
            "error": "edge_metrics.csv not found for this run.",
            "run_id": run_id,
            "metric": metric,
            "edge_metrics_path": str(edge_metrics_path),
        }

    if not network_path.exists():
        return {
            "success": False,
            "error": "network_graph.csv not found for this run.",
            "run_id": run_id,
            "metric": metric,
            "network_path": str(network_path),
        }

    heatmap_dir = run_folder / "heatmaps"
    heatmap_dir.mkdir(parents=True, exist_ok=True)

    output_path = heatmap_dir / f"{heatmap_file_stem(metric, focus)}.svg"

    network_df, metrics_df = load_files(network_path, edge_metrics_path)

    plot_heatmap(
        network_df,
        metrics_df,
        metric,
        output_path,
        focus=focus,
    )

    add_available_heatmap_to_metadata(run_folder, metric, output_path, focus)

    return {
        "success": True,
        "run_id": run_id,
        "metric": metric,
        "heatmap_path": str(output_path),
    }


def get_fdot_mapping_candidates(run_folder: Path, metadata: dict) -> list[Path]:
    """List the map-specific FDOT mapping files that may fit this run."""
    """Return map-specific mapping locations in preferred lookup order."""
    map_id = get_run_map_id(run_folder, metadata)
    candidates = [
        run_folder / "fdot" / "fdot_edge_mapping.csv",
        run_folder / "fdot" / "fdot_edge_mapping_option_a.csv",
    ]

    if map_id:
        candidates.extend([
            TELEMETRY_DIR / "data" / "fdot" / map_id / "fdot_edge_mapping.csv",
            TELEMETRY_DIR / "data" / "fdot" / map_id / "fdot_edge_mapping_option_a.csv",
        ])

    candidates.extend([
        TELEMETRY_DIR / "data" / "fdot" / "fdot_edge_mapping.csv",
        TELEMETRY_DIR / "data" / "fdot" / "fdot_edge_mapping_option_a.csv",
    ])
    return candidates


def get_run_map_id(run_folder: Path, metadata: dict) -> str:
    """Return a real map ID without treating the legacy runs folder as a map."""
    metadata_map_id = str(metadata.get("map_id") or "").strip()
    if metadata_map_id:
        return metadata_map_id
    if run_folder.parent == get_runs_dir():
        return "unknown-map"
    return run_folder.parent.name


def compare_run_with_fdot(run_id: str, mapping_path: str | None = None) -> dict:
    """Compare a selected saved run and store all results inside its fdot folder."""
    run_folder = find_run_folder(run_id)
    if run_folder is None:
        return {
            "success": False,
            "error": "Run folder not found.",
            "run_id": run_id,
        }

    edge_metrics_path = run_folder / "edge_metrics.csv"
    if not edge_metrics_path.exists():
        return {
            "success": False,
            "error": "edge_metrics.csv not found for this run.",
            "run_id": run_id,
            "edge_metrics_path": str(edge_metrics_path),
        }

    metadata_path = run_folder / "run_metadata.json"
    metadata = load_json_file(metadata_path)
    candidates = get_fdot_mapping_candidates(run_folder, metadata)

    if mapping_path:
        selected_mapping = Path(mapping_path).expanduser()
    else:
        selected_mapping = next((path for path in candidates if path.exists()), None)

    if selected_mapping is None or not selected_mapping.exists():
        return {
            "success": False,
            "error": (
                "No FDOT edge mapping is available for this map. Generate or copy a mapping "
                "for the selected map before running FDOT comparison."
            ),
            "run_id": run_id,
            "map_id": get_run_map_id(run_folder, metadata),
            "searched_paths": [str(path) for path in candidates],
        }

    fdot_folder = run_folder / "fdot"
    comparison_path = fdot_folder / "fdot_vs_simulation.csv"
    summary_path = fdot_folder / "fdot_summary.json"
    telemetry_summary = load_json_file(run_folder / "telemetry_summary.json")
    simulation_duration_seconds = telemetry_summary.get("simulation_duration_s")

    summary = compare_fdot_to_simulation(
        selected_mapping,
        edge_metrics_path,
        comparison_path,
        summary_path,
        simulation_duration_seconds,
    )

    network_path = run_folder / "network_graph.csv"
    if not network_path.exists():
        return {
            "success": False,
            "error": "network_graph.csv not found for this run; the FDOT comparison map could not be generated.",
            "run_id": run_id,
            "network_path": str(network_path),
        }

    heatmap_dir = run_folder / "heatmaps"
    heatmap_dir.mkdir(parents=True, exist_ok=True)
    heatmap_path = heatmap_dir / "heatmap_fdot_geh_score.svg"

    network_df = pd.read_csv(network_path)
    network_edge_column = "edge_id" if "edge_id" in network_df.columns else "EdgeID"
    if network_edge_column not in network_df.columns:
        return {
            "success": False,
            "error": "network_graph.csv does not contain an edge ID column; FDOT coverage could not be calculated.",
            "run_id": run_id,
            "network_path": str(network_path),
        }

    total_road_directions = int(
        pd.to_numeric(network_df[network_edge_column], errors="coerce").dropna().nunique()
    )
    compared_road_directions = int(summary.get("matched_edges", 0))
    summary["total_road_directions"] = total_road_directions
    summary["unmatched_road_directions"] = max(0, total_road_directions - compared_road_directions)
    summary["coverage_percent"] = round(
        compared_road_directions / total_road_directions * 100.0,
        2,
    ) if total_road_directions else 0.0
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")

    comparison_df = pd.read_csv(comparison_path)
    fdot_metrics = comparison_df[["EdgeID", "geh_score", "geh_result"]].copy()
    fdot_metrics = fdot_metrics.rename(columns={"geh_score": "fdot_geh_score"})
    plot_heatmap(
        network_df,
        fdot_metrics,
        "fdot_geh_score",
        heatmap_path,
        focus="all",
    )
    add_available_heatmap_to_metadata(run_folder, "fdot_geh_score", heatmap_path, "all")
    metadata = load_json_file(metadata_path)

    metadata["fdot_folder"] = "fdot"
    metadata["fdot_validation"] = {
        "status": "complete",
        "mapping_path": str(selected_mapping),
        "comparison_path": comparison_path.relative_to(run_folder).as_posix(),
        "summary_path": summary_path.relative_to(run_folder).as_posix(),
        "heatmap_path": heatmap_path.relative_to(run_folder).as_posix(),
        "summary": summary,
    }
    metadata_path.write_text(json.dumps(metadata, indent=4), encoding="utf-8")

    return {
        "success": True,
        "run_id": run_id,
        "map_id": get_run_map_id(run_folder, metadata),
        "comparison_path": str(comparison_path),
        "summary_path": str(summary_path),
        "heatmap_path": str(heatmap_path),
        "summary": summary,
    }


# Handles commands from the Unreal telemetry panel.
# This must run before the normal simulation CSV path check.
def handle_panel_command(argv: list[str]) -> int:
    """Read one Unreal panel command and print one JSON response."""
    if "--panel-command" not in argv:
        return -1

    command_index = argv.index("--panel-command")

    if command_index + 1 >= len(argv):
        print(json.dumps({
            "success": False,
            "error": "Missing panel command.",
        }))
        return 1

    command = argv[command_index + 1]
    args = argv[command_index + 2:]

    def get_arg_value(flag: str) -> str | None:
        if flag not in args:
            return None

        flag_index = args.index(flag)

        if flag_index + 1 >= len(args):
            return None

        return args[flag_index + 1]

    try:
        if command == "list-metrics":
            result = get_available_metrics()

        elif command == "list-runs":
            result = list_saved_runs()

        elif command == "get-run-details":
            run_id = get_arg_value("--run-id")
            if not run_id:
                result = {
                    "success": False,
                    "error": "Missing --run-id.",
                }
            else:
                result = get_run_details(run_id)

        elif command == "compare-runs":
            baseline_run_id = get_arg_value("--baseline-run-id")
            comparison_run_id = get_arg_value("--comparison-run-id")
            if not baseline_run_id or not comparison_run_id:
                result = {
                    "success": False,
                    "error": "Missing --baseline-run-id or --comparison-run-id.",
                }
            else:
                result = compare_runs(baseline_run_id, comparison_run_id)

        elif command == "generate-comparison-heatmaps":
            baseline_run_id = get_arg_value("--baseline-run-id")
            comparison_run_id = get_arg_value("--comparison-run-id")
            metric = get_arg_value("--metric")
            focus = get_arg_value("--focus") or "all"
            if not baseline_run_id or not comparison_run_id or not metric:
                result = {
                    "success": False,
                    "error": "Missing baseline run, comparison run, or metric.",
                }
            else:
                result = generate_comparison_heatmaps(
                    baseline_run_id,
                    comparison_run_id,
                    metric,
                    focus,
                )

        elif command == "delete-run":
            run_id = get_arg_value("--run-id")
            result = delete_saved_run(run_id) if run_id else {
                "success": False,
                "error": "Missing --run-id.",
            }

        elif command == "get-heatmap-path":
            run_id = get_arg_value("--run-id")
            metric = get_arg_value("--metric")
            if not run_id or not metric:
                result = {
                    "success": False,
                    "error": "Missing --run-id or --metric.",
                }
            else:
                result = get_heatmap_path(run_id, metric)

        elif command == "generate-heatmap":
            run_id = get_arg_value("--run-id")
            metric = get_arg_value("--metric")
            focus = get_arg_value("--focus") or "all"
            if not run_id or not metric:
                result = {
                    "success": False,
                    "error": "Missing --run-id or --metric.",
                }
            else:
                result = generate_single_heatmap(run_id, metric, focus)

        elif command == "compare-fdot":
            run_id = get_arg_value("--run-id")
            mapping_path = get_arg_value("--mapping-path")
            if not run_id:
                result = {
                    "success": False,
                    "error": "Missing --run-id.",
                }
            else:
                result = compare_run_with_fdot(run_id, mapping_path)

        else:
            result = {
                "success": False,
                "error": f"Unknown panel command: {command}",
            }

        print(json.dumps(result))
        return 0 if result.get("success") else 1

    except Exception as error:
        print(json.dumps({
            "success": False,
            "error": str(error),
            "command": command,
        }))
        return 1



# Records the active map/network files that were saved with this run.
# This keeps old telemetry runs self-contained even after the user loads or edits another map.
def record_saved_run_inputs(
    run_folder: Path,
    network_graph_path: Path | None,
    edges_jsonl_path: Path | None,
    source_network_graph_path: Path,
    source_edges_jsonl_path: Path,
    fdot_mapping_status: dict | None = None,
) -> None:
    metadata_path = run_folder / "run_metadata.json"
    metadata = load_json_file(metadata_path)

    if not metadata:
        metadata = {
            "run_id": run_folder.name,
        }

    if network_graph_path is not None:
        metadata["network_graph_path"] = network_graph_path.name
        metadata["source_network_graph_path"] = str(source_network_graph_path)

    if edges_jsonl_path is not None:
        metadata["edges_jsonl_path"] = edges_jsonl_path.name
        metadata["source_edges_jsonl_path"] = str(source_edges_jsonl_path)

    if fdot_mapping_status is not None:
        metadata["fdot_mapping"] = fdot_mapping_status

    with metadata_path.open("w", encoding="utf-8") as file:
        json.dump(metadata, file, indent=4)


def main() -> int:
    """Save and analyze a new simulation run."""
    # Panel commands are used by Unreal widgets for saved-run browsing and on-demand heatmaps.
    # They do not use a simulation CSV, so handle them before the normal run mode.
    panel_result = handle_panel_command(sys.argv[1:])

    if panel_result != -1:
        return panel_result

    if len(sys.argv) < 2:
        print("ERROR: Missing simulation CSV path.")
        return 1

    simulation_csv = Path(sys.argv[1])

    if not simulation_csv.exists():
        print(f"ERROR: Simulation CSV not found: {simulation_csv}")
        return 1

    # Optional positional args passed by the Unreal frontend:
    #   argv[2] = active roadmap nodes JSONL
    #   argv[3] = active roadmap edges JSONL
    #   argv[4] = active roadmap display name
    nodes_jsonl_path = Path(sys.argv[2]) if len(sys.argv) > 2 else None
    edge_jsonl_path = Path(sys.argv[3]) if len(sys.argv) > 3 else EDGE_JSONL_PATH
    map_name = sys.argv[4].strip() if len(sys.argv) > 4 else "Unknown map"

    print(f"Active nodes JSONL: {nodes_jsonl_path}")
    print(f"Active edges JSONL: {edge_jsonl_path}")
    print(f"Active roadmap: {map_name}")

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    INPUT_DIR.mkdir(parents=True, exist_ok=True)

    # Create folders used by the telemetry pipeline
    (OUTPUT_DIR / "telemetry").mkdir(parents=True, exist_ok=True)
    (OUTPUT_DIR / "heatmaps").mkdir(parents=True, exist_ok=True)

    # Create a new folder for this simulation run.
    # This keeps the current telemetry results separate from older runs.
    run_id, run_folder = create_run_folder(OUTPUT_DIR, map_name)

    print(f"Created telemetry run: {run_id}")
    print(f"Run folder: {run_folder}")

    # Save a copy of the raw simulation CSV inside this run folder.
    # This lets us keep the original telemetry data that belongs to each run.
    saved_simulation_csv = run_folder / "simulation_output.csv"
    shutil.copy2(simulation_csv, saved_simulation_csv)

    print(f"Saved simulation CSV: {saved_simulation_csv}")

    # Build the heatmap-ready network graph directly from the same active
    # node and edge JSONL files used by the C++ simulation.
    saved_network_graph = None

    if (
        nodes_jsonl_path is not None
        and nodes_jsonl_path.exists()
        and edge_jsonl_path.exists()
    ):
        saved_network_graph = run_folder / "network_graph.csv"

        build_network_graph(
            nodes_path=nodes_jsonl_path,
            edges_path=edge_jsonl_path,
            output_path=saved_network_graph,
        )

        print(f"Built network_graph.csv: {saved_network_graph}")
    else:
        print("WARNING: Could not build network_graph.csv.")
        print(f"Nodes JSONL: {nodes_jsonl_path}")
        print(f"Edges JSONL: {edge_jsonl_path}")

    # Save the active edge JSONL inside this run folder.
    # The heatmap mainly uses network_graph.csv, but edges.jsonl preserves map/road context.
    saved_edges_jsonl = None

    if edge_jsonl_path.exists():
        saved_edges_jsonl = run_folder / "edges.jsonl"
        shutil.copy2(edge_jsonl_path, saved_edges_jsonl)
        print(f"Saved edges.jsonl: {saved_edges_jsonl}")
    else:
        print(f"WARNING: Edge JSONL was not found and was not saved: {edge_jsonl_path}")

    # FDOT mappings use this run's exact EdgeIDs. A fingerprinted cache avoids
    # repeating the spatial match unless the map graph or FDOT dataset changed.
    fdot_mapping_status = None
    if saved_network_graph is not None and nodes_jsonl_path is not None:
        try:
            map_id = make_map_id(map_name)
            fdot_mapping_status = ensure_fdot_mapping(
                network_path=saved_network_graph,
                nodes_path=nodes_jsonl_path,
                fdot_path=DEFAULT_FDOT_FILE,
                cache_dir=OUTPUT_DIR / "fdot_mappings" / map_id,
                run_mapping_path=run_folder / "fdot" / "fdot_edge_mapping.csv",
                map_id=map_id,
            )
            print(f"FDOT mapping status: {fdot_mapping_status['status']}")
        except Exception as error:
            fdot_mapping_status = {
                "status": "failed",
                "reason": str(error),
            }
            # Telemetry and heatmaps are still useful if optional FDOT matching fails.
            print(f"WARNING: FDOT mapping could not be prepared: {error}")

    # Run the telemetry analysis and keep the returned results.
    # We use the summary values from this result to build the dashboard JSON.
    analysis_results = run_analysis(
        input_file=saved_simulation_csv,
        output_dir=run_folder,
    )

    # Create the dashboard-friendly JSON summary for this run.
    write_dashboard_summary(run_folder, analysis_results["summary"])

    # Mark this run as finished and list the files Unreal can read later.
    update_run_metadata(run_folder, run_id)

    # Store the map/network files that belong to this specific run.
    record_saved_run_inputs(
        run_folder=run_folder,
        network_graph_path=saved_network_graph,
        edges_jsonl_path=saved_edges_jsonl,
        source_network_graph_path=nodes_jsonl_path,
        source_edges_jsonl_path=edge_jsonl_path,
        fdot_mapping_status=fdot_mapping_status,
    )

    done_file = TELEMETRY_DIR / "telemetry_done.txt"
    done_file.write_text("Telemetry processing complete.\n", encoding="utf-8")
    print(f"Wrote done file: {done_file}")

    print("RoadMap Python pipeline finished successfully.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
