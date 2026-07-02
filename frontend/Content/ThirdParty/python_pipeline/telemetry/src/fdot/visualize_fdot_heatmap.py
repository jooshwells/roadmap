from pathlib import Path
import argparse

import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.colors as colors
from matplotlib.collections import LineCollection

"""
visualize_fdot_heatmap.py

Create a PNG validation heatmap using GEH values from fdot_vs_simulation.csv.

This is mainly for reports and presentation slides. Unreal should use the CSV
itself and color the actual road meshes by EdgeID.
"""

BASE_DIR = Path(__file__).resolve().parents[2]


def load_files(network_path: Path, fdot_validation_path: Path):
    network_df = pd.read_csv(network_path)
    validation_df = pd.read_csv(fdot_validation_path)
    return network_df, validation_df


def clean_edge_id_column(df: pd.DataFrame, column_name: str) -> pd.DataFrame:
    df = df.copy()
    df[column_name] = pd.to_numeric(df[column_name], errors="coerce")
    df = df.dropna(subset=[column_name])
    df[column_name] = df[column_name].astype(int).astype(str)
    return df


def normalize_id_columns(network_df: pd.DataFrame, validation_df: pd.DataFrame):
    network_df = network_df.copy()
    validation_df = validation_df.copy()

    if "edge_id" in network_df.columns:
        network_df = network_df.rename(columns={"edge_id": "EdgeID"})

    if "EdgeID" not in network_df.columns:
        raise ValueError("network_graph.csv must contain either 'edge_id' or 'EdgeID'.")

    if "EdgeID" not in validation_df.columns:
        raise ValueError("fdot_vs_simulation.csv must contain 'EdgeID'.")

    if "geh_score" not in validation_df.columns:
        raise ValueError("fdot_vs_simulation.csv must contain 'geh_score'. Run fdot_vs_simulation.py first.")

    network_df = clean_edge_id_column(network_df, "EdgeID")
    validation_df = clean_edge_id_column(validation_df, "EdgeID")
    return network_df, validation_df


def add_target_coordinates(network_df: pd.DataFrame):
    network_df = network_df.copy()

    required_columns = {"source", "target", "source_x", "source_y"}
    missing = required_columns - set(network_df.columns)
    if missing:
        raise ValueError(f"network_graph.csv is missing required columns: {sorted(missing)}")

    node_lookup = (
        network_df[["source", "source_x", "source_y"]]
        .dropna()
        .drop_duplicates(subset=["source"])
        .rename(
            columns={
                "source": "target_lookup_id",
                "source_x": "target_x",
                "source_y": "target_y",
            }
        )
    )

    return network_df.merge(
        node_lookup,
        left_on="target",
        right_on="target_lookup_id",
        how="left",
    )


def build_segments(network_df: pd.DataFrame, validation_df: pd.DataFrame):
    network_df, validation_df = normalize_id_columns(network_df, validation_df)
    network_df = add_target_coordinates(network_df)

    merged = network_df.merge(validation_df[["EdgeID", "geh_score"]], on="EdgeID", how="left")

    background_segments = []
    heat_segments = []
    heat_values = []
    skipped_edges = 0

    for _, row in merged.iterrows():
        if pd.isna(row["source_x"]) or pd.isna(row["source_y"]):
            skipped_edges += 1
            continue
        if pd.isna(row["target_x"]) or pd.isna(row["target_y"]):
            skipped_edges += 1
            continue

        segment = [
            (float(row["source_x"]), float(row["source_y"])),
            (float(row["target_x"]), float(row["target_y"])),
        ]
        background_segments.append(segment)

        if not pd.isna(row.get("geh_score")):
            heat_segments.append(segment)
            heat_values.append(float(row["geh_score"]))

    if skipped_edges > 0:
        print(f"Skipped {skipped_edges} edges because endpoint coordinates were missing.")

    print(f"Drawable road segments: {len(background_segments)}")
    print(f"GEH-colored segments: {len(heat_segments)}")

    return background_segments, heat_segments, heat_values


def plot_validation_heatmap(network_df: pd.DataFrame, validation_df: pd.DataFrame, output_path: Path):
    background_segments, heat_segments, heat_values = build_segments(network_df, validation_df)

    fig, ax = plt.subplots(figsize=(18, 11), facecolor="#171717")
    ax.set_facecolor("#171717")

    if background_segments:
        background = LineCollection(
            background_segments,
            colors="#A0A0A0",
            linewidths=0.40,
            alpha=0.60,
        )
        ax.add_collection(background)

    if heat_segments:
        values = pd.Series(heat_values)

        # GEH thresholds: below 5 is good, 5-10 is review, 10+ is poor.
        # Values above 15 are clipped so one extreme edge does not flatten the map.
        norm = colors.Normalize(vmin=0, vmax=max(15, values.quantile(0.98)))

        heat_lines = LineCollection(
            heat_segments,
            cmap="RdYlGn_r",
            norm=norm,
            linewidths=1.4,
        )
        heat_lines.set_array(values.to_numpy())
        ax.add_collection(heat_lines)

        colorbar = fig.colorbar(heat_lines, ax=ax)
        colorbar.set_label("GEH score (lower is better)", color="white")
        colorbar.ax.yaxis.set_tick_params(color="white")
        plt.setp(colorbar.ax.get_yticklabels(), color="white")
    else:
        print("Warning: no GEH segments were matched. Check EdgeID values.")

    ax.autoscale()
    ax.set_aspect("equal", adjustable="box")
    ax.axis("off")
    ax.set_title("FDOT Validation Heatmap", fontsize=22, color="white", pad=20)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    plt.tight_layout()
    plt.savefig(output_path, dpi=300, facecolor=fig.get_facecolor())
    plt.close()
    print(f"Saved heatmap to: {output_path}")


def main():
    parser = argparse.ArgumentParser(description="Create a RoadMap FDOT GEH validation heatmap.")
    parser.add_argument(
        "--network",
        default=BASE_DIR / "data/network/network_graph.csv",
        help="Path to network graph CSV.",
    )
    parser.add_argument(
        "--fdot-validation",
        default=BASE_DIR / "outputs/fdot/fdot_vs_simulation.csv",
        help="Path to FDOT validation CSV with GEH values.",
    )
    parser.add_argument(
        "--output",
        default=BASE_DIR / "outputs/fdot/fdot_validation_heatmap.png",
        help="Output PNG filename.",
    )
    args = parser.parse_args()

    network_df, validation_df = load_files(Path(args.network), Path(args.fdot_validation))
    plot_validation_heatmap(network_df, validation_df, Path(args.output))


if __name__ == "__main__":
    main()
