from pathlib import Path
import sys
import json
import pandas as pd

from reportlab.lib.pagesizes import letter
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, Image
from reportlab.lib.styles import getSampleStyleSheet
from reportlab.lib import colors

# Detect if we are running as a PyInstaller packaged .exe or a normal .py script
if getattr(sys, 'frozen', False):
    # RUNTIME_DIR is where the .exe physically lives on the hard drive
    RUNTIME_DIR = Path(sys.executable).parent
    # BUNDLE_DIR is the temporary folder where PyInstaller extracts your code
    BUNDLE_DIR = Path(sys._MEIPASS)
else:
    # Running normally via python.exe
    RUNTIME_DIR = Path(__file__).resolve().parent
    BUNDLE_DIR = RUNTIME_DIR

TELEMETRY_DIR = Path(__file__).resolve().parent
PYTHON_PIPELINE_DIR = TELEMETRY_DIR.parent
BASE_DIR = PYTHON_PIPELINE_DIR.parent

sys.path.append(str(TELEMETRY_DIR / "src"))

# Import sub-scripts as Python modules
from telemetry import telemetry_analysis
from heatmaps import generate_all_heatmaps

OUTPUT_DIR = TELEMETRY_DIR / "outputs"
CSV_DIR = OUTPUT_DIR / "csv"
REPORT_DIR = OUTPUT_DIR / "reports"
INPUT_DIR = TELEMETRY_DIR / "inputs"

EDGE_JSONL_PATH = PYTHON_PIPELINE_DIR / "sample_out" / "waterford_edges_orange_allroads_offline_xy.jsonl"
EDITED_EDGES_PATH = INPUT_DIR / "edited_edges.csv"


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


def load_edge_metadata() -> pd.DataFrame:
    if not EDGE_JSONL_PATH.exists():
        print(f"No edge JSONL found: {EDGE_JSONL_PATH}")
        return pd.DataFrame()

    rows = []

    with EDGE_JSONL_PATH.open("r", encoding="utf-8") as file:
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


def create_simple_outputs(simulation_csv: Path) -> None:
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

    metadata = load_edge_metadata()

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

def main() -> int:
    if len(sys.argv) < 2:
        print("ERROR: Missing simulation CSV path.")
        return 1

    simulation_csv = Path(sys.argv[1])

    if not simulation_csv.exists():
        print(f"ERROR: Simulation CSV not found: {simulation_csv}")
        return 1

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    CSV_DIR.mkdir(parents=True, exist_ok=True)
    REPORT_DIR.mkdir(parents=True, exist_ok=True)
    INPUT_DIR.mkdir(parents=True, exist_ok=True)

    # Create folders used by the telemetry pipeline
    (OUTPUT_DIR / "telemetry").mkdir(parents=True, exist_ok=True)
    (OUTPUT_DIR / "heatmaps").mkdir(parents=True, exist_ok=True)

    # 3. Call the imported module functions directly
    print("Running telemetry analysis...")
    telemetry_analysis.run_telemetry(simulation_csv)
    
    print("Generating heatmaps...")
    generate_all_heatmaps.generate()
           
    # Create the Unreal-friendly/user-friendly outputs.
    create_simple_outputs(simulation_csv)
    write_text_report(simulation_csv)
    write_pdf_report()


    print("RoadMap Python pipeline finished successfully.")

    done_file = Path(__file__).parent / "telemetry_done.txt"
    done_file.write_text("Telemetry processing complete.\n")
    
    return 0


if __name__ == "__main__":
    raise SystemExit(main())