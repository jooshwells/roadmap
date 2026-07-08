from pathlib import Path
import sys
import json
import pandas as pd
import shutil
import io

from contextlib import redirect_stdout

from src.telemetry.telemetry_analysis import run_analysis
from src.telemetry.run_manager import create_run_folder
from src.heatmaps.visualize_telemetry_heatmap import load_files, plot_heatmap

from reportlab.lib.pagesizes import letter
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, Image
from reportlab.lib.styles import getSampleStyleSheet
from reportlab.lib import colors


# Finds the real telemetry folder.
# In a PyInstaller one-file EXE, __file__ points to a temporary _MEI folder,
# so we use sys.executable to find the folder where run_pipeline.exe lives.
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

# Runs a telemetry panel helper command and keeps only its final output line.
# This prevents normal heatmap log messages from confusing Unreal's JSON reader.
def run_panel_helper(helper_main) -> int:
    output_buffer = io.StringIO()

    with redirect_stdout(output_buffer):
        return_code = helper_main()

    output_lines = [
        line.strip()
        for line in output_buffer.getvalue().splitlines()
        if line.strip()
    ]

    if output_lines:
        print(output_lines[-1])

    return return_code if return_code is not None else 0

# Handles commands from Unreal and decides what the pipeline should do.
# Normal simulation runs still use: run_pipeline.exe simulation_output.csv
# Telemetry panel commands use: run_pipeline.exe --panel-command list-runs
def main() -> int:
    if len(sys.argv) >= 3 and sys.argv[1] == "--panel-command":
        command = sys.argv[2]

        # Remove the EXE routing arguments before calling the selected helper script.
        # This leaves only arguments that belong to that telemetry panel command.
        sys.argv = [sys.argv[0]] + sys.argv[3:]

        if command == "list-metrics":
            from src.telemetry.list_available_metrics import main as list_metrics_main
            return run_panel_helper(list_metrics_main)

        if command == "list-runs":
            from src.telemetry.list_runs import main as list_runs_main
            return run_panel_helper(list_runs_main)

        if command == "get-run-details":
            from src.telemetry.get_run_details import main as get_run_details_main
            return run_panel_helper(get_run_details_main)

        if command == "get-heatmap-path":
            from src.telemetry.get_heatmap_path import main as get_heatmap_path_main
            return run_panel_helper(get_heatmap_path_main)

        if command == "generate-heatmap":
            from src.heatmaps.generate_selected_heatmap import main as generate_heatmap_main
            return run_panel_helper(generate_heatmap_main)

        print(json.dumps({
            "success": False,
            "error": f"Unknown telemetry panel command: {command}"
        }))

        return 1

    if len(sys.argv) < 2:
        print("ERROR: Missing simulation CSV path.")
        return 1

    simulation_csv = Path(sys.argv[1])

    if not simulation_csv.exists():
        print(f"ERROR: Simulation CSV not found: {simulation_csv}")
        return 1

    # Optional positional args passed by the Unreal frontend:
    #   argv[2] = network graph CSV generated by the sim for the active roadmap
    #   argv[3] = edge JSONL of the active roadmap used for road labels
    # Both fall back to bundled defaults when omitted.
    network_path = Path(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_NETWORK_GRAPH_PATH
    edge_jsonl_path = Path(sys.argv[3]) if len(sys.argv) > 3 else EDGE_JSONL_PATH

    print(f"Network graph: {network_path}")
    print(f"Edge metadata JSONL: {edge_jsonl_path}")

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    CSV_DIR.mkdir(parents=True, exist_ok=True)
    REPORT_DIR.mkdir(parents=True, exist_ok=True)
    INPUT_DIR.mkdir(parents=True, exist_ok=True)

    # Create folders used by the telemetry pipeline.
    (OUTPUT_DIR / "telemetry").mkdir(parents=True, exist_ok=True)
    (OUTPUT_DIR / "heatmaps").mkdir(parents=True, exist_ok=True)

    # Create a new folder for this simulation run.
    # This keeps the current telemetry results separate from older runs.
    run_id, run_folder = create_run_folder(OUTPUT_DIR)

    print(f"Created telemetry run: {run_id}")
    print(f"Run folder: {run_folder}")

    # Save a copy of the raw simulation CSV inside this run folder.
    # This lets us keep the original telemetry data that belongs to each run.
    saved_simulation_csv = run_folder / "simulation_output.csv"
    shutil.copy2(simulation_csv, saved_simulation_csv)

    print(f"Saved simulation CSV: {saved_simulation_csv}")

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

    # Heatmaps are now generated on demand from the telemetry panel.
    # We intentionally do not call generate_heatmaps() here because every run should not create every heatmap automatically.
    # generate_heatmaps(run_folder, network_path)

    done_file = TELEMETRY_DIR / "telemetry_done.txt"
    done_file.write_text("Telemetry processing complete.\n", encoding="utf-8")
    print(f"Wrote done file: {done_file}")

    print("RoadMap Python pipeline finished successfully.")

    return 0

if __name__ == "__main__":
    raise SystemExit(main())