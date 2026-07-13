from pathlib import Path
import sys
import json
import pandas as pd
import shutil

from src.telemetry.telemetry_analysis import run_analysis
from src.telemetry.run_manager import create_run_folder
from src.heatmaps.visualize_telemetry_heatmap import load_files, plot_heatmap
from src.fdot.fdot_vs_simulation import compare_fdot_to_simulation
# Builds the heatmap-ready network graph from active roadmap JSONL files.
from src.heatmaps.build_network_graph_with_geometry import build_network_graph

from reportlab.lib.pagesizes import letter
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, Image
from reportlab.lib.styles import getSampleStyleSheet
from reportlab.lib import colors


if getattr(sys, "frozen", False):
    TELEMETRY_DIR = Path(sys.executable).resolve().parent
else:
    TELEMETRY_DIR = Path(__file__).resolve().parent
PYTHON_PIPELINE_DIR = TELEMETRY_DIR.parent
BASE_DIR = PYTHON_PIPELINE_DIR.parent

TELEMETRY_SCRIPT = TELEMETRY_DIR / "src" / "telemetry" / "telemetry_analysis.py"
HEATMAP_SCRIPT = TELEMETRY_DIR / "src" / "heatmaps" / "generate_all_heatmaps.py"

OUTPUT_DIR = TELEMETRY_DIR / "outputs"
CSV_DIR = OUTPUT_DIR / "csv"
REPORT_DIR = OUTPUT_DIR / "reports"
INPUT_DIR = TELEMETRY_DIR / "inputs"

EDGE_JSONL_PATH = PYTHON_PIPELINE_DIR / "sample_out" / "waterford_edges_orange_allroads_offline_xy.jsonl"
EDITED_EDGES_PATH = INPUT_DIR / "edited_edges.csv"

# Fallback network graph (bundled Waterford map) used when the sim does not pass
# a freshly generated one for the active roadmap.
DEFAULT_NETWORK_GRAPH_PATH = TELEMETRY_DIR / "data" / "network" / "network_graph_waterford.csv"

def clean_label_value(value):
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


def create_simple_outputs(simulation_csv: Path, edge_jsonl_path: Path = EDGE_JSONL_PATH) -> None:
    df = pd.read_csv(simulation_csv)

    edge_metrics = (
        df.groupby("EdgeID")
        .agg(
            avg_speed_mps=("Speed_mps", "mean"),
            avg_accel_mps2=("Accel_mps2", "mean"),
            total_wait_s=("WaitTime_s", "sum"),
            vehicle_records=("VehicleID", "count"),
            unique_vehicles=("VehicleID", "nunique"),
        )
        .reset_index()
    )

    edge_metrics["bottleneck_score"] = (
        edge_metrics["total_wait_s"] / (edge_metrics["avg_speed_mps"] + 1.0)
    )

    metadata = load_edge_metadata(edge_jsonl_path)

    if len(metadata) > 0:
        edge_metrics = edge_metrics.merge(metadata, on="EdgeID", how="left")
    else:
        edge_metrics["road_label"] = "Unknown road"

    CSV_DIR.mkdir(parents=True, exist_ok=True)

    edge_metrics.to_csv(CSV_DIR / "county_edge_metrics.csv", index=False)

    top_bottlenecks = edge_metrics.sort_values(
        "bottleneck_score",
        ascending=False
    ).head(20)

    top_bottlenecks.to_csv(CSV_DIR / "county_top_bottlenecks.csv", index=False)

    summary = pd.DataFrame([{
        "rows": len(df),
        "vehicles": df["VehicleID"].nunique(),
        "edges_used": df["EdgeID"].nunique(),
        "duration_s": df["Time"].max() - df["Time"].min(),
        "avg_speed_mps": df["Speed_mps"].mean(),
        "avg_wait_s": df["WaitTime_s"].mean(),
        "total_wait_s": df["WaitTime_s"].sum(),
    }])

    summary.to_csv(CSV_DIR / "county_simulation_summary.csv", index=False)


def write_text_report(simulation_csv: Path) -> None:
    REPORT_DIR.mkdir(parents=True, exist_ok=True)

    report_path = REPORT_DIR / "simulation_report.txt"

    report_path.write_text(
        "RoadMap Simulation Report\n"
        "=========================\n\n"
        f"Input telemetry CSV:\n{simulation_csv}\n\n"
        "Generated outputs:\n"
        "- csv/county_simulation_summary.csv\n"
        "- csv/county_edge_metrics.csv\n"
        "- csv/county_top_bottlenecks.csv\n\n"
        "Real telemetry folder was also called if available.\n"
    )

def write_pdf_report() -> None:
    REPORT_DIR.mkdir(parents=True, exist_ok=True)

    pdf_path = REPORT_DIR / "simulation_results_report.pdf"

    summary_path = CSV_DIR / "county_simulation_summary.csv"
    bottlenecks_path = CSV_DIR / "county_top_bottlenecks.csv"

    heatmap_dir = TELEMETRY_DIR / "outputs" / "heatmaps"

    styles = getSampleStyleSheet()
    story = []

    story.append(Paragraph("RoadMap Simulation Results Report", styles["Title"]))
    story.append(Spacer(1, 12))

    story.append(Paragraph("Simulation Summary", styles["Heading2"]))

    if summary_path.exists():
        summary = pd.read_csv(summary_path).iloc[0]

        summary_data = [
            ["Metric", "Value"],
            ["Telemetry rows", f"{summary['rows']}"],
            ["Vehicles simulated", f"{summary['vehicles']}"],
            ["Edges used", f"{summary['edges_used']}"],
            ["Duration (s)", f"{summary['duration_s']:.2f}"],
            ["Average speed (m/s)", f"{summary['avg_speed_mps']:.2f}"],
            ["Average wait (s)", f"{summary['avg_wait_s']:.2f}"],
            ["Total wait (s)", f"{summary['total_wait_s']:.2f}"],
        ]

        table = Table(summary_data)
        table.setStyle(TableStyle([
            ("BACKGROUND", (0, 0), (-1, 0), colors.lightgrey),
            ("GRID", (0, 0), (-1, -1), 0.5, colors.grey),
            ("FONTNAME", (0, 0), (-1, 0), "Helvetica-Bold"),
        ]))

        story.append(table)
        story.append(Spacer(1, 16))

    story.append(Paragraph("Top Bottleneck Edges", styles["Heading2"]))

    if bottlenecks_path.exists():
        bottlenecks = pd.read_csv(bottlenecks_path).head(10)

        bottleneck_data = [["Road", "EdgeID", "Avg Speed", "Total Wait", "Score"]]

        for _, row in bottlenecks.iterrows():
            bottleneck_data.append([
                str(row.get("road_label", "Unknown road"))[:35],
                str(row.get("EdgeID", "")),
                f"{row.get('avg_speed_mps', 0):.2f}",
                f"{row.get('total_wait_s', 0):.2f}",
                f"{row.get('bottleneck_score', 0):.2f}",
            ])

        table = Table(bottleneck_data, repeatRows=1)
        table.setStyle(TableStyle([
            ("BACKGROUND", (0, 0), (-1, 0), colors.lightgrey),
            ("GRID", (0, 0), (-1, -1), 0.5, colors.grey),
            ("FONTNAME", (0, 0), (-1, 0), "Helvetica-Bold"),
            ("FONTSIZE", (0, 0), (-1, -1), 8),
        ]))

        story.append(table)
        story.append(Spacer(1, 16))

    story.append(Paragraph("Generated Heatmaps", styles["Heading2"]))

    heatmaps = [
        ("Bottleneck Score", heatmap_dir / "heatmap_bottleneck_score.png"),
        ("Average Speed", heatmap_dir / "heatmap_avg_speed_mph.png"),
        ("Total Wait", heatmap_dir / "heatmap_total_wait_added_s.png"),
        ("Estimated Flow", heatmap_dir / "heatmap_estimated_flow_veh_per_hr.png"),
    ]

    for title, path in heatmaps:
        if path.exists():
            story.append(Paragraph(title, styles["Heading3"]))
            story.append(Image(str(path), width=480, height=300))
            story.append(Spacer(1, 12))

    doc = SimpleDocTemplate(str(pdf_path), pagesize=letter)
    doc.build(story)

    print(f"PDF report written to: {pdf_path}")

# Generates all heatmap PNGs for one specific saved run.
# This is kept as a helper, but normal runs do not call it automatically anymore.
def generate_heatmaps(run_folder: Path, network_path: Path) -> None:
    # If the requested network graph is missing, fall back to the bundled default graph.
    # This keeps development testing safe while the active-roadmap graph work is still being integrated.
    if not network_path.exists():
        print(f"Network graph not found ({network_path}); falling back to bundled default.")
        network_path = DEFAULT_NETWORK_GRAPH_PATH

    # This run's edge metrics are saved inside the run folder.
    edge_metrics_path = run_folder / "edge_metrics.csv"

    # Save heatmaps inside this run so old runs keep their own images.
    heatmap_dir = run_folder / "heatmaps"

    heatmap_dir.mkdir(parents=True, exist_ok=True)

    metrics = [
        "bottleneck_score",
        "estimated_flow_veh_per_hr",
        "avg_speed_mph",
        "total_wait_added_s",
    ]

    network_df, metrics_df = load_files(network_path, edge_metrics_path)

    for metric in metrics:
        print(f"Generating heatmap: {metric}")

        output_path = heatmap_dir / f"heatmap_{metric}.png"

        plot_heatmap(
            network_df,
            metrics_df,
            metric,
            output_path,
        )

    print("Finished generating all heatmaps.")

# Creates a small JSON summary that Unreal can read for the dashboard.
# This uses the real run summary from telemetry_analysis.py so totals are accurate.
def write_dashboard_summary(run_folder: Path, run_summary: dict) -> None:
    edge_metrics_path = run_folder / "edge_metrics.csv"
    bottlenecks_path = run_folder / "bottleneck_edges.csv"
    summary_path = run_folder / "telemetry_summary.json"

    edge_metrics = pd.read_csv(edge_metrics_path)
    bottlenecks = pd.read_csv(bottlenecks_path)

    # Load road names and road types from the original edge JSONL when available.
    # This gives the dashboard better labels than only showing EdgeID numbers.
    metadata = load_edge_metadata()

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
    return {
        "success": True,
        "metric_count": 4,
        "metrics": [
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
                "display_name": "Total Wait Added",
            },
        ],
    }


# Finds the folder where saved telemetry runs are stored.
def get_runs_dir() -> Path:
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
    if not path.exists():
        return {}

    try:
        with path.open("r", encoding="utf-8") as file:
            return json.load(file)
    except Exception:
        return {}


# Lists saved run folders so Unreal can populate the telemetry panel.
def list_saved_runs() -> dict:
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


# Returns the expected heatmap PNG path for one run and metric.
def get_heatmap_path(run_id: str, metric: str) -> dict:
    run_folder = find_run_folder(run_id)

    if run_folder is None:
        return {
            "success": False,
            "error": "Run folder not found.",
            "run_id": run_id,
            "metric": metric,
            "run_folder": "",
        }

    heatmap_path = run_folder / "heatmaps" / f"heatmap_{metric}.png"

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
        "relative_path": f"heatmaps/heatmap_{metric}.png",
        "full_path": str(heatmap_path),
    }


# Adds a generated heatmap path to run_metadata.json so Unreal knows where it is.
def add_available_heatmap_to_metadata(run_folder: Path, metric: str, output_path: Path, focus: str = "all") -> None:
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
    return f"heatmap_{metric}" if focus == "all" else f"heatmap_{metric}_{focus}"


def generate_single_heatmap(run_id: str, metric: str, focus: str = "all") -> dict:
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

    output_path = heatmap_dir / f"{heatmap_file_stem(metric, focus)}.png"

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

    summary = compare_fdot_to_simulation(
        selected_mapping,
        edge_metrics_path,
        comparison_path,
        summary_path,
    )

    metadata["fdot_folder"] = "fdot"
    metadata["fdot_validation"] = {
        "status": "complete",
        "mapping_path": str(selected_mapping),
        "comparison_path": comparison_path.relative_to(run_folder).as_posix(),
        "summary_path": summary_path.relative_to(run_folder).as_posix(),
        "summary": summary,
    }
    metadata_path.write_text(json.dumps(metadata, indent=4), encoding="utf-8")

    return {
        "success": True,
        "run_id": run_id,
        "map_id": get_run_map_id(run_folder, metadata),
        "comparison_path": str(comparison_path),
        "summary_path": str(summary_path),
        "summary": summary,
    }


# Handles commands from the Unreal telemetry panel.
# This must run before the normal simulation CSV path check.
def handle_panel_command(argv: list[str]) -> int:
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

    with metadata_path.open("w", encoding="utf-8") as file:
        json.dump(metadata, file, indent=4)


def main() -> int:
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
    CSV_DIR.mkdir(parents=True, exist_ok=True)
    REPORT_DIR.mkdir(parents=True, exist_ok=True)
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
    )

    # For now this uses the temporary Waterford network graph path.
    # Later, Unreal or the active-roadmap pipeline can pass in the exact active network path.
    active_network_path = TELEMETRY_DIR / "data" / "network" / "network_graph_waterford.csv"

    # Heatmaps are now generated on demand from the telemetry panel.
    # We intentionally do not call generate_heatmaps() here because every run should not create every heatmap automatically.
    # generate_heatmaps(run_folder, active_network_path)

    # Then create the Unreal-friendly/user-friendly outputs.
    # create_simple_outputs(simulation_csv, edge_jsonl_path)
    # write_text_report(simulation_csv)
    # write_pdf_report()


    done_file = TELEMETRY_DIR / "telemetry_done.txt"
    done_file.write_text("Telemetry processing complete.\n", encoding="utf-8")
    print(f"Wrote done file: {done_file}")

    print("RoadMap Python pipeline finished successfully.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
