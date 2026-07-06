from __future__ import annotations

from pathlib import Path
import argparse

import matplotlib.pyplot as plt
import pandas as pd
from reportlab.lib import colors
from reportlab.lib.pagesizes import letter
from reportlab.lib.styles import getSampleStyleSheet
from reportlab.lib.units import inch
from reportlab.platypus import (
    Image,
    PageBreak,
    Paragraph,
    SimpleDocTemplate,
    Spacer,
    Table,
    TableStyle,
)

"""
fdot_validation_report.py

Create a simple PDF report for RoadMap's FDOT validation results.

This PDF is meant for humans: presentations, demos, and documentation.
Unreal should still use outputs/fdot/fdot_vs_simulation.csv to color roads.
"""

BASE_DIR = Path(__file__).resolve().parents[2]
INPUT_FILE = BASE_DIR / "outputs/fdot/fdot_vs_simulation.csv"
OUTPUT_FILE = BASE_DIR / "outputs/fdot/fdot_validation_report.pdf"
CHART_FILE = BASE_DIR / "outputs/fdot/fdot_geh_summary.png"


def money_round(value: float, decimals: int = 2) -> str:
    if pd.isna(value):
        return "N/A"
    return f"{value:,.{decimals}f}"


def make_geh_chart(df: pd.DataFrame, chart_path: Path) -> None:
    """Create a small bar chart showing GEH category counts."""
    chart_path.parent.mkdir(parents=True, exist_ok=True)

    order = ["Good", "Review", "Poor", "No Data"]
    counts = df["geh_result"].value_counts().reindex(order, fill_value=0)

    fig, ax = plt.subplots(figsize=(6.8, 3.2))
    counts.plot(kind="bar", ax=ax)
    ax.set_title("GEH validation categories")
    ax.set_xlabel("Category")
    ax.set_ylabel("Matched RoadMap edges")
    ax.tick_params(axis="x", rotation=0)
    ax.grid(axis="y", alpha=0.25)
    plt.tight_layout()
    plt.savefig(chart_path, dpi=200)
    plt.close()


def make_table(data, col_widths=None):
    table = Table(data, colWidths=col_widths, hAlign="LEFT")
    table.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#EDEDED")),
                ("TEXTCOLOR", (0, 0), (-1, 0), colors.black),
                ("FONTNAME", (0, 0), (-1, 0), "Helvetica-Bold"),
                ("FONTSIZE", (0, 0), (-1, -1), 8),
                ("GRID", (0, 0), (-1, -1), 0.25, colors.HexColor("#BFBFBF")),
                ("VALIGN", (0, 0), (-1, -1), "TOP"),
                ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, colors.HexColor("#F7F7F7")]),
            ]
        )
    )
    return table


def build_report(input_file: Path = INPUT_FILE, output_file: Path = OUTPUT_FILE) -> None:
    if not input_file.exists():
        raise FileNotFoundError(f"Could not find FDOT validation file: {input_file}")

    df = pd.read_csv(input_file)

    required = {
        "EdgeID",
        "fdot_estimated_veh_per_hr",
        "estimated_flow_veh_per_hr",
        "geh_score",
        "geh_result",
    }
    missing = required - set(df.columns)
    if missing:
        raise ValueError(
            "fdot_vs_simulation.csv is missing columns needed for the report: "
            f"{sorted(missing)}. Run fdot_vs_simulation.py first."
        )

    output_file.parent.mkdir(parents=True, exist_ok=True)
    make_geh_chart(df, CHART_FILE)

    styles = getSampleStyleSheet()
    title_style = styles["Title"]
    heading_style = styles["Heading2"]
    body_style = styles["BodyText"]

    story = []
    story.append(Paragraph("RoadMap FDOT Validation Report", title_style))
    story.append(Spacer(1, 0.12 * inch))
    story.append(
        Paragraph(
            "This report compares RoadMap simulated hourly edge flow against FDOT AADT-based hourly estimates. "
            "It is meant for review and presentation. The CSV output is still the file Unreal should use for in-app validation heatmaps.",
            body_style,
        )
    )
    story.append(Spacer(1, 0.18 * inch))

    total = len(df)
    mean_geh = df["geh_score"].mean()
    median_geh = df["geh_score"].median()
    good_count = int((df["geh_result"] == "Good").sum())
    review_count = int((df["geh_result"] == "Review").sum())
    poor_count = int((df["geh_result"] == "Poor").sum())

    summary_table = [
        ["Metric", "Value"],
        ["Matched RoadMap edges reviewed", f"{total:,}"],
        ["Mean GEH", money_round(mean_geh)],
        ["Median GEH", money_round(median_geh)],
        ["Good edges (GEH < 5)", f"{good_count:,}"],
        ["Review edges (5 <= GEH < 10)", f"{review_count:,}"],
        ["Poor edges (GEH >= 10)", f"{poor_count:,}"],
    ]
    story.append(Paragraph("Summary", heading_style))
    story.append(make_table(summary_table, [3.4 * inch, 2.2 * inch]))
    story.append(Spacer(1, 0.2 * inch))

    story.append(Image(str(CHART_FILE), width=6.4 * inch, height=3.0 * inch))
    story.append(Spacer(1, 0.18 * inch))

    story.append(Paragraph("How to read GEH", heading_style))
    story.append(
        Paragraph(
            "Lower GEH means the simulation volume is closer to the FDOT-based observed volume. "
            "For this project, GEH below 5 is labeled Good, 5 to 10 is Review, and 10 or higher is Poor.",
            body_style,
        )
    )

    story.append(PageBreak())

    story.append(Paragraph("Highest GEH roads", heading_style))
    story.append(
        Paragraph(
            "These are the matched RoadMap edges where simulated hourly flow is farthest from the FDOT-based hourly estimate.",
            body_style,
        )
    )
    story.append(Spacer(1, 0.1 * inch))

    top = df.sort_values("geh_score", ascending=False).head(15).copy()
    cols = [
        "EdgeID",
        "name",
        "fdot_aadt",
        "fdot_estimated_veh_per_hr",
        "estimated_flow_veh_per_hr",
        "geh_score",
        "geh_result",
    ]
    cols = [col for col in cols if col in top.columns]

    table_data = [[
        "EdgeID",
        "Road name",
        "FDOT AADT",
        "FDOT/hr",
        "Sim/hr",
        "GEH",
        "Result",
    ]]

    for _, row in top.iterrows():
        table_data.append(
            [
                str(int(row["EdgeID"])),
                str(row.get("name", ""))[:24],
                money_round(row.get("fdot_aadt"), 0),
                money_round(row["fdot_estimated_veh_per_hr"], 1),
                money_round(row["estimated_flow_veh_per_hr"], 1),
                money_round(row["geh_score"], 2),
                str(row["geh_result"]),
            ]
        )

    story.append(
        make_table(
            table_data,
            [0.6 * inch, 1.45 * inch, 0.85 * inch, 0.8 * inch, 0.8 * inch, 0.6 * inch, 0.75 * inch],
        )
    )

    story.append(Spacer(1, 0.2 * inch))
    story.append(Paragraph("Notes", heading_style))
    story.append(
        Paragraph(
            "FDOT AADT was divided by 24 to create a rough hourly traffic estimate. "
            "That is useful for a first validation pass, but it does not represent a specific peak hour. "
            "Small test simulations may also undercount real-world traffic because they use fewer vehicles than actual daily traffic volumes.",
            body_style,
        )
    )

    doc = SimpleDocTemplate(
        str(output_file),
        pagesize=letter,
        rightMargin=0.55 * inch,
        leftMargin=0.55 * inch,
        topMargin=0.55 * inch,
        bottomMargin=0.55 * inch,
    )
    doc.build(story)


def main() -> None:
    parser = argparse.ArgumentParser(description="Create a RoadMap FDOT validation PDF report.")
    parser.add_argument(
        "--input",
        default=INPUT_FILE,
        help="Path to fdot_vs_simulation.csv after GEH has been calculated.",
    )
    parser.add_argument(
        "--output",
        default=OUTPUT_FILE,
        help="Path for the output PDF report.",
    )
    args = parser.parse_args()

    output_path = Path(args.output)
    global CHART_FILE
    CHART_FILE = output_path.with_name("fdot_geh_summary.png")

    build_report(Path(args.input), output_path)
    print(f"Saved: {output_path}")


if __name__ == "__main__":
    main()
