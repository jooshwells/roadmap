"""
generate_heatmap_animation.py

I use this script to preview the replay heatmap frames before they are shown in Unreal.

The telemetry analysis script creates time-based CSV frames in:
    outputs/telemetry/heatmap_frames/

This script reads those frames, matches them to network_graph.csv by EdgeID, and
exports an animated GIF or MP4. The animation is mainly for testing and demos.
Unreal should still use the frame CSV files directly so it can color the road
meshes in-engine.

Run examples:
    python src/heatmaps/generate_heatmap_animation.py
    python src/heatmaps/generate_heatmap_animation.py --metric bottleneck_score
    python src/heatmaps/generate_heatmap_animation.py --metric avg_speed_mph --output outputs/heatmap_frames/avg_speed_replay.gif
"""

from __future__ import annotations

import argparse
from pathlib import Path

import matplotlib.colors as colors
import matplotlib.pyplot as plt
import pandas as pd
from matplotlib.animation import FuncAnimation, PillowWriter, FFMpegWriter
from matplotlib.collections import LineCollection


BASE_DIR = Path(__file__).resolve().parents[2]

DEFAULT_NETWORK = BASE_DIR / "data/network/network_graph.csv"
DEFAULT_FRAMES_DIR = BASE_DIR / "outputs/telemetry/heatmap_frames"
DEFAULT_OUTPUT_DIR = BASE_DIR / "outputs/heatmap_frames"

METRICS = [
    "bottleneck_score",
    "estimated_flow_veh_per_hr",
    "avg_speed_mph",
    "total_wait_added_s",
]


def clean_edge_id_column(df: pd.DataFrame, column_name: str) -> pd.DataFrame:
    """Convert edge IDs to clean strings so network and frame files match."""
    df = df.copy()
    df[column_name] = pd.to_numeric(df[column_name], errors="coerce")
    df = df.dropna(subset=[column_name])
    df[column_name] = df[column_name].astype(int).astype(str)
    return df


def normalize_network_ids(network_df: pd.DataFrame) -> pd.DataFrame:
    """Make sure the network file has an EdgeID column."""
    network_df = network_df.copy()

    if "edge_id" in network_df.columns:
        network_df = network_df.rename(columns={"edge_id": "EdgeID"})

    if "EdgeID" not in network_df.columns:
        raise ValueError("network_graph.csv must contain either 'edge_id' or 'EdgeID'.")

    return clean_edge_id_column(network_df, "EdgeID")


def add_target_coordinates(network_df: pd.DataFrame) -> pd.DataFrame:
    """
    Add target_x and target_y using the source coordinate lookup.

    network_graph.csv stores source_x/source_y. To draw the whole edge, this looks
    up the target node's coordinates from rows where that target is also a source.
    """
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


def prepare_network_segments(network_path: Path) -> tuple[pd.DataFrame, list[list[tuple[float, float]]]]:
    """Load the road network and build drawable line segments once."""
    network_df = pd.read_csv(network_path)
    network_df = normalize_network_ids(network_df)
    network_df = add_target_coordinates(network_df)

    rows = []
    background_segments = []
    skipped_edges = 0

    for _, row in network_df.iterrows():
        if (
            pd.isna(row["source_x"])
            or pd.isna(row["source_y"])
            or pd.isna(row["target_x"])
            or pd.isna(row["target_y"])
        ):
            skipped_edges += 1
            continue

        segment = [
            (float(row["source_x"]), float(row["source_y"])),
            (float(row["target_x"]), float(row["target_y"])),
        ]

        background_segments.append(segment)
        rows.append({"EdgeID": row["EdgeID"], "segment": segment})

    if skipped_edges:
        print(f"Skipped {skipped_edges} edges because endpoint coordinates were missing.")

    segment_df = pd.DataFrame(rows)
    print(f"Drawable road segments: {len(background_segments):,}")
    return segment_df, background_segments


def load_frame_files(frames_dir: Path, max_frames: int | None = None, every_nth_frame: int = 1) -> list[Path]:
    """Find frame_000.csv, frame_001.csv, etc. and optionally thin the list."""
    frame_files = sorted(
        file for file in frames_dir.glob("frame_*.csv")
        if file.stem.replace("frame_", "").isdigit()
    )
    if not frame_files:
        raise FileNotFoundError(f"No frame_###.csv files were found in: {frames_dir}")

    if every_nth_frame < 1:
        raise ValueError("every_nth_frame must be 1 or greater.")

    frame_files = frame_files[::every_nth_frame]

    if max_frames is not None and max_frames > 0:
        frame_files = frame_files[:max_frames]

    return frame_files


def choose_color_settings(metric: str):
    """Match the normal heatmap color choices."""
    if metric == "avg_speed_mph":
        return "RdYlGn", "Average speed (mph)", False, "Average Speed Replay"
    if metric == "total_wait_added_s":
        return "RdYlGn_r", "Total wait added (seconds)", True, "Wait Time Replay"
    if metric == "estimated_flow_veh_per_hr":
        return "viridis", "Estimated flow (vehicles/hour)", True, "Traffic Flow Replay"
    if metric == "bottleneck_score":
        return "RdYlGn_r", "Bottleneck score", True, "Bottleneck Replay"

    return "RdYlGn_r", metric, True, f"{metric} Replay"


def load_frame_data(
    frame_files: list[Path],
    segment_df: pd.DataFrame,
    metric: str,
) -> tuple[list[dict], float, float]:
    """Load each frame and match it to drawable road segments."""
    frame_data = []
    all_values = []

    for frame_file in frame_files:
        frame_df = pd.read_csv(frame_file)

        if "EdgeID" not in frame_df.columns:
            raise ValueError(f"{frame_file.name} is missing EdgeID.")
        if metric not in frame_df.columns:
            raise ValueError(f"{frame_file.name} is missing metric column '{metric}'.")

        frame_df = clean_edge_id_column(frame_df, "EdgeID")
        frame_df[metric] = pd.to_numeric(frame_df[metric], errors="coerce")

        merged = segment_df.merge(frame_df, on="EdgeID", how="inner")
        merged = merged.dropna(subset=[metric])

        segments = merged["segment"].tolist()
        values = merged[metric].astype(float).tolist()

        if values:
            all_values.extend(values)

        start_s = float(frame_df["FrameStart_s"].iloc[0]) if "FrameStart_s" in frame_df.columns and len(frame_df) else 0.0
        end_s = float(frame_df["FrameEnd_s"].iloc[0]) if "FrameEnd_s" in frame_df.columns and len(frame_df) else 0.0
        frame_index = int(frame_df["FrameIndex"].iloc[0]) if "FrameIndex" in frame_df.columns and len(frame_df) else len(frame_data)

        frame_data.append(
            {
                "frame_index": frame_index,
                "start_s": start_s,
                "end_s": end_s,
                "segments": segments,
                "values": values,
            }
        )

    if not all_values:
        raise ValueError("No frame values were matched to the road network.")

    value_series = pd.Series(all_values)
    return frame_data, float(value_series.min()), float(value_series.quantile(0.98))


def make_animation(
    network_path: Path,
    frames_dir: Path,
    metric: str,
    output_path: Path,
    fps: int,
    dpi: int,
    max_frames: int | None,
    every_nth_frame: int,
) -> None:
    """Create and save the animated replay heatmap."""
    if metric not in METRICS:
        raise ValueError(f"Unsupported metric '{metric}'. Choose from: {METRICS}")

    output_path.parent.mkdir(parents=True, exist_ok=True)

    frame_files = load_frame_files(frames_dir, max_frames=max_frames, every_nth_frame=every_nth_frame)
    print(f"Replay frames found: {len(frame_files)}")

    segment_df, background_segments = prepare_network_segments(network_path)
    frame_data, vmin, vmax = load_frame_data(frame_files, segment_df, metric)

    cmap_name, colorbar_label, clip_high_values, title = choose_color_settings(metric)

    # Average speed should use the true max so high speeds stay green. The other
    # metrics are clipped at p98 so one extreme edge does not wash out the map.
    if not clip_high_values:
        all_values = [value for frame in frame_data for value in frame["values"]]
        vmax = max(all_values)

    if vmax <= vmin:
        vmax = vmin + 1

    norm = colors.Normalize(vmin=vmin, vmax=vmax)

    fig, ax = plt.subplots(figsize=(18, 11), facecolor="#171717")
    ax.set_facecolor("#171717")

    if background_segments:
        background = LineCollection(
            background_segments,
            colors="#A0A0A0",
            linewidths=0.35,
            alpha=0.45,
        )
        ax.add_collection(background)

    heat_lines = LineCollection(
        [],
        cmap=cmap_name,
        norm=norm,
        linewidths=1.4,
    )
    ax.add_collection(heat_lines)

    colorbar = fig.colorbar(heat_lines, ax=ax)
    colorbar.set_label(colorbar_label, color="white")
    colorbar.ax.yaxis.set_tick_params(color="white")
    plt.setp(colorbar.ax.get_yticklabels(), color="white")

    ax.autoscale()
    ax.set_aspect("equal", adjustable="box")
    ax.axis("off")

    title_text = ax.set_title(title, fontsize=22, color="white", pad=20)

    def update(frame_number: int):
        frame = frame_data[frame_number]
        heat_lines.set_segments(frame["segments"])
        heat_lines.set_array(pd.Series(frame["values"]).to_numpy())
        title_text.set_text(
            f"{title} | Frame {frame['frame_index']:03d} | "
            f"{frame['start_s']:.0f}-{frame['end_s']:.0f} sec"
        )
        return heat_lines, title_text

    animation = FuncAnimation(
        fig,
        update,
        frames=len(frame_data),
        interval=1000 / fps,
        blit=False,
        repeat=True,
    )

    suffix = output_path.suffix.lower()
    if suffix == ".gif":
        writer = PillowWriter(fps=fps)
    elif suffix == ".mp4":
        writer = FFMpegWriter(fps=fps)
    else:
        raise ValueError("Output file must end with .gif or .mp4")

    plt.tight_layout()
    animation.save(output_path, writer=writer, dpi=dpi)
    plt.close(fig)

    print(f"Saved replay animation to: {output_path}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Create an animated RoadMap heatmap replay.")

    parser.add_argument(
        "--network",
        default=DEFAULT_NETWORK,
        help="Path to network_graph.csv.",
    )
    parser.add_argument(
        "--frames-dir",
        default=DEFAULT_FRAMES_DIR,
        help="Folder containing frame_###.csv files from telemetry_analysis.py.",
    )
    parser.add_argument(
        "--metric",
        default="bottleneck_score",
        choices=METRICS,
        help="Metric to animate.",
    )
    parser.add_argument(
        "--output",
        default=None,
        help="Output .gif or .mp4 file. If omitted, a default GIF path is used.",
    )
    parser.add_argument(
        "--fps",
        type=int,
        default=2,
        help="Animation speed in frames per second.",
    )
    parser.add_argument(
        "--dpi",
        type=int,
        default=120,
        help="Output resolution. Use 120 for quick tests, 200+ for presentation exports.",
    )
    parser.add_argument(
        "--max-frames",
        type=int,
        default=None,
        help="Optional limit for quick previews. Leave blank to render every frame.",
    )
    parser.add_argument(
        "--every-nth-frame",
        type=int,
        default=1,
        help="Optional frame skipping for faster previews, such as 2 to use every other frame.",
    )

    args = parser.parse_args()

    output_path = (
        Path(args.output)
        if args.output
        else DEFAULT_OUTPUT_DIR / f"heatmap_replay_{args.metric}.gif"
    )

    make_animation(
        network_path=Path(args.network),
        frames_dir=Path(args.frames_dir),
        metric=args.metric,
        output_path=output_path,
        fps=args.fps,
        dpi=args.dpi,
        max_frames=args.max_frames,
        every_nth_frame=args.every_nth_frame,
    )


if __name__ == "__main__":
    main()
