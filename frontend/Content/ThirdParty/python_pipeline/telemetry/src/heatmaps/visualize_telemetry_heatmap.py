"""
visualize_telemetry_heatmap.py

I use this script to turn the RoadMap telemetry results into map-style heatmaps.

The script combines two csv files:

1. This gives me the road geometry.
   Current expected columns:
       source, target, length, source_x, source_y, edge_id

2. This gives me the telemetry values that were calculated from the simulation output.
   Current expected columns include:
       EdgeID, bottleneck_score, total_wait_added_s,
       estimated_flow_veh_per_hr, avg_speed_mph

Important note:
    My network_graph.csv only stores the coordinate for the source node of each edge.
    To draw the full road segment, I look up the target node's coordinate from other
    rows in the same file.

"""

from pathlib import Path
import argparse

import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.colors as colors
from matplotlib.collections import LineCollection


def load_files(network_path: Path, edge_metrics_path: Path):
    """
    Load the road geometry file and the telemetry metrics file.

    I keep this as its own function so the script is easier to test later.
    """
    network_df = pd.read_csv(network_path)
    metrics_df = pd.read_csv(edge_metrics_path)
    return network_df, metrics_df


def clean_edge_id_column(df: pd.DataFrame, column_name: str) -> pd.DataFrame:
    """
    Clean an edge ID column so both files can be matched correctly.

    The network file sometimes stores IDs like 60390.0, while telemetry stores
    the same ID as 60390. If I compare those directly as strings, they do not
    match. I convert both to whole-number strings so they line up correctly.
    """
    df = df.copy()

    df[column_name] = pd.to_numeric(df[column_name], errors="coerce")

    # Some rows in the network file may have blank edge IDs. I drop those
    # because they cannot be matched to telemetry or drawn as telemetry roads.
    df = df.dropna(subset=[column_name])

    df[column_name] = df[column_name].astype(int).astype(str)
    return df


def normalize_id_columns(network_df: pd.DataFrame, metrics_df: pd.DataFrame):
    """
    Make the edge ID columns match between the network file and telemetry file.

    network_graph.csv uses edge_id.
    edge_metrics.csv uses EdgeID.

    After this function, both files use EdgeID with the same formatting.
    """
    network_df = network_df.copy()
    metrics_df = metrics_df.copy()

    if "edge_id" in network_df.columns:
        network_df = network_df.rename(columns={"edge_id": "EdgeID"})

    if "EdgeID" not in network_df.columns:
        raise ValueError("network_graph.csv must contain either 'edge_id' or 'EdgeID'.")

    if "EdgeID" not in metrics_df.columns:
        raise ValueError("edge_metrics.csv must contain 'EdgeID'.")

    network_df = clean_edge_id_column(network_df, "EdgeID")
    metrics_df = clean_edge_id_column(metrics_df, "EdgeID")

    return network_df, metrics_df


def add_target_coordinates(network_df: pd.DataFrame):
    """
    Add target_x and target_y columns using the node coordinates I already have.

    The current network file has source_x/source_y for each edge, but it does not
    directly include target_x/target_y. Since most target nodes also appear as a
    source node somewhere else, I build a small lookup table:

        source node id -> source_x/source_y

    Then I use the edge's target node ID to find the ending coordinate.
    """
    network_df = network_df.copy()
    
    if "target_x" in network_df.columns and "target_y" in network_df.columns:
        return network_df

    required_columns = {"source", "target", "source_x", "source_y"}
    missing = required_columns - set(network_df.columns)

    if missing:
        raise ValueError(
            f"network_graph.csv is missing required columns: {sorted(missing)}"
        )

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

    network_df = network_df.merge(
        node_lookup,
        left_on="target",
        right_on="target_lookup_id",
        how="left",
    )

    return network_df


def choose_color_settings(metric: str):
    """
    Use colors that are easy to interpret:

    Green = good
    Yellow = moderate
    Red = needs attention

    For flow, I use a neutral scale because higher traffic volume is
    not automatically a problem.
    """

    if metric == "avg_speed_mph":
        # Fast roads = green, slow roads = red
        return "RdYlGn", "Average speed (mph)", False

    if metric == "total_wait_added_s":
        # More delay = worse
        return "RdYlGn_r", "Total wait added (seconds)", True

    if metric == "estimated_flow_veh_per_hr":
        # Traffic volume is not inherently good or bad
        return "viridis", "Estimated flow (vehicles/hour)", True

    if metric == "bottleneck_score":
        # Higher bottleneck score = worse congestion
        return "RdYlGn_r", "Bottleneck score", True

    return "RdYlGn_r", metric, True


def build_line_segments(network_df: pd.DataFrame, metrics_df: pd.DataFrame, metric: str):
    """
    Build the road line segments that matplotlib will draw.

    background_segments:
        Every road segment that can be drawn from the network file.
        These are shown lightly in gray.

    heat_segments:
        Road segments that also have telemetry data for the selected metric.
        These are colored using the heatmap scale.

    heat_values:
        The actual metric values used to color each telemetry road segment.
    """
    network_df, metrics_df = normalize_id_columns(network_df, metrics_df)
    network_df = add_target_coordinates(network_df)

    merged = network_df.merge(metrics_df, on="EdgeID", how="left")

    background_segments = []
    heat_segments = []
    heat_values = []

    skipped_edges = 0

    for _, row in merged.iterrows():
        # If either endpoint is missing coordinates, I cannot draw this edge.
        if pd.isna(row["source_x"]) or pd.isna(row["source_y"]):
            skipped_edges += 1
            continue

        if pd.isna(row["target_x"]) or pd.isna(row["target_y"]):
            skipped_edges += 1
            continue

        x1 = float(row["source_x"])
        y1 = float(row["source_y"])
        x2 = float(row["target_x"])
        y2 = float(row["target_y"])

        segment = [(x1, y1), (x2, y2)]
        background_segments.append(segment)

        # A road only becomes part of the colored heatmap if it has telemetry.
        if metric in merged.columns and not pd.isna(row.get(metric)):
            heat_segments.append(segment)
            heat_values.append(float(row[metric]))

    if skipped_edges > 0:
        print(f"Skipped {skipped_edges} edges because endpoint coordinates were missing.")

    print(f"Drawable road segments: {len(background_segments)}")
    print(f"Telemetry-colored segments: {len(heat_segments)}")

    return background_segments, heat_segments, heat_values


def plot_heatmap(network_df: pd.DataFrame, metrics_df: pd.DataFrame, metric: str, output_path: Path):
    """Create and save the heatmap image."""
    if metric not in metrics_df.columns:
        raise ValueError(
            f"Metric '{metric}' was not found in edge_metrics.csv. "
            f"Available columns are: {list(metrics_df.columns)}"
        )

    cmap_name, colorbar_label, clip_high_values = choose_color_settings(metric)

    background_segments, heat_segments, heat_values = build_line_segments(
        network_df,
        metrics_df,
        metric,
    )

    fig, ax = plt.subplots(figsize=(18, 11), facecolor="#171717")
    ax.set_facecolor("#171717")

    # First I draw the full network lightly so I can see the map outline.
    if background_segments:
        background = LineCollection(
            background_segments,
            colors="#A0A0A0",
            linewidths=0.40,
            alpha=0.60,
        )
        ax.add_collection(background)

    # Then I draw the telemetry roads on top with the selected color scale.
    if heat_segments:
        values = pd.Series(heat_values)

        vmin = values.min()

        # I clip the top of some metrics at the 98th percentile so one extreme
        # road does not make every other road look the same color.
        if clip_high_values:
            vmax = values.quantile(0.98)
        else:
            vmax = values.max()

        if pd.isna(vmax) or vmax == vmin:
            vmax = vmin + 1

        norm = colors.Normalize(vmin=vmin, vmax=vmax)

        heat_lines = LineCollection(
            heat_segments,
            cmap=cmap_name,
            norm=norm,
            linewidths=1.3,
        )

        heat_lines.set_array(values.to_numpy())
        ax.add_collection(heat_lines)

        colorbar = fig.colorbar(heat_lines, ax=ax)
        colorbar.set_label(colorbar_label, color="white")
        colorbar.ax.yaxis.set_tick_params(color="white")
        plt.setp(colorbar.ax.get_yticklabels(), color="white")
    else:
        print("Warning: no telemetry segments were matched. Check EdgeID values.")

    ax.autoscale()
    ax.set_aspect("equal", adjustable="box")
    ax.axis("off")

    friendly_titles = {
        "bottleneck_score": "Road Bottleneck Hotspots",
        "total_wait_added_s": "Total Vehicle Wait Time",
        "estimated_flow_veh_per_hr": "Estimated Traffic Flow",
        "avg_speed_mph": "Average Road Speed",
    }

    title = friendly_titles.get(metric, metric)

    ax.set_title(title, fontsize=22, color="white", pad=20)

    plt.tight_layout()
    plt.savefig(output_path, dpi=300, facecolor=fig.get_facecolor())
    plt.close()

    print(f"Saved heatmap to: {output_path}")


def main():
    parser = argparse.ArgumentParser(description="Create RoadMap telemetry heatmaps.")

    BASE_DIR = Path(__file__).resolve().parents[2]

    parser.add_argument(
        "--network",
        default=BASE_DIR / "data/network/network_graph_waterford.csv",
        help="Path to network graph CSV.",
    )

    parser.add_argument(
        "--edge-metrics",
        default=BASE_DIR / "outputs/telemetry/edge_metrics.csv",
        help="Path to telemetry edge metrics CSV.",
    )

    parser.add_argument(
        "--metric",
        default="bottleneck_score",
        choices=[
            "bottleneck_score",
            "total_wait_added_s",
            "estimated_flow_veh_per_hr",
            "avg_speed_mph",
        ],
        help="Telemetry metric to visualize.",
    )

    parser.add_argument(
        "--output",
        default=None,
        help="Output PNG filename. If omitted, one is created automatically.",
    )

    args = parser.parse_args()

    output_path = (
        Path(args.output)
        if args.output
        else BASE_DIR / f"outputs/heatmaps/heatmap_{args.metric}.png"
    )

    network_df, metrics_df = load_files(
        Path(args.network),
        Path(args.edge_metrics),
    )

    plot_heatmap(network_df, metrics_df, args.metric, output_path)


if __name__ == "__main__":
    main()
