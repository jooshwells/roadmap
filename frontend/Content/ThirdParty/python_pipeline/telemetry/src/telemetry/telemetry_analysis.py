"""
RoadMap telemetry analysis.

This script reads the raw C++ simulation telemetry file and creates summary CSVs
that we can use for debugging, heatmaps, and later FDOT comparison.

Main outputs:
    telemetry_outputs/run_summary.txt
    telemetry_outputs/edge_metrics.csv
    telemetry_outputs/vehicle_metrics.csv
    telemetry_outputs/od_metrics.csv
    telemetry_outputs/bottleneck_edges.csv
    telemetry_outputs/telemetry_flags.csv
    telemetry_outputs/heatmap_frames/frame_000.csv
    telemetry_outputs/heatmap_frames/all_frames.csv
    telemetry_outputs/heatmap_frames/frame_manifest.json

Run examples:
    python telemetry_analysis.py
    python telemetry_analysis.py --input simulation_output.csv --output telemetry_outputs
    python telemetry_analysis.py --input larger_run.csv --output telemetry_outputs --frame-seconds 30

Note:
    I kept this script focused on post-processing. The simulation team exports the
    raw CSV, then this script checks it and turns it into easier files to review.
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Any

import numpy as np
import pandas as pd

INPUT_FILE = Path(__file__).resolve().parents[2] / "data/simulation/large_run_with_frontend.csv"
OUTPUT_DIR = Path(__file__).resolve().parents[2] / "outputs/telemetry"

MPS_TO_MPH = 2.2369362921
LOW_SPEED_MPS = 2.2352          # about 5 mph
BAD_ACCEL_LIMIT = 20.0          # anything over +/-20 m/s^2 is probably a bug/spike
BAD_JUMP_M = 200.0              # suspicious position jump on the same edge
MIN_EDGE_SAMPLES = 5            # keeps tiny sample edges from dominating bottlenecks
DEFAULT_FRAME_SECONDS = 30.0     # used for animated/replay heatmap frames
BOTTLENECK_WAIT_REFERENCE_S = 30.0  # 30 seconds fills the stopped-time part of the index

REQUIRED_COLS = [
    "Time", "VehicleID", "EdgeID", "LaneIndex",
    "Speed_mps", "Accel_mps2", "Pos_m",
    "RouteIndex", "WaitTime_s", "OriginID", "DestID"
]

# In the current telemetry export all required columns should be numeric.
NUMERIC_COLS = REQUIRED_COLS.copy()


# Loading and basic checks

def load_telemetry(csv_file: str | Path) -> pd.DataFrame:
    """Load telemetry and make sure the required columns exist."""
    csv_file = Path(csv_file)
    if not csv_file.exists():
        raise FileNotFoundError(f"Could not find telemetry file: {csv_file}")

    df = pd.read_csv(csv_file)

    missing = [col for col in REQUIRED_COLS if col not in df.columns]
    if missing:
        raise ValueError(f"Missing required columns: {missing}")

    # Convert bad strings to NaN instead of crashing immediately. The validation
    # step reports them so we know the CSV has a data quality problem.
    for col in NUMERIC_COLS:
        df[col] = pd.to_numeric(df[col], errors="coerce")

    # Keep the original CSV row number to make debugging with the simulation team easier.
    df.insert(0, "SourceRow", np.arange(2, len(df) + 2))
    return df


def validate_telemetry(df: pd.DataFrame) -> list[str]:
    """
    Return high-level validation problems.

    These checks tell us whether the whole file looks safe to trust. Row-level
    problems are written separately to telemetry_flags.csv.
    """
    issues: list[str] = []

    if df.empty:
        issues.append("telemetry file has no rows")
        return issues

    missing = [col for col in REQUIRED_COLS if col not in df.columns]
    if missing:
        issues.append(f"missing required columns: {missing}")
        return issues

    null_cols = [col for col in REQUIRED_COLS if df[col].isna().any()]
    if null_cols:
        issues.append(f"null or non-numeric values found in: {null_cols}")

    if (df["Time"] < 0).any():
        issues.append("negative time values found")
    if (df["Speed_mps"] < 0).any():
        issues.append("negative speed values found")
    if (df["LaneIndex"] < 0).any():
        issues.append("negative lane index values found")
    if (df["Pos_m"] < 0).any():
        issues.append("negative position values found")
    if (df["WaitTime_s"] < 0).any():
        issues.append("negative wait time values found")

    # This checks the actual file order. The simulation should not write a later
    # vehicle sample and then go backward in time for that same vehicle.
    file_order_time_diff = df.groupby("VehicleID", sort=False)["Time"].diff()
    if (file_order_time_diff < 0).any():
        issues.append("time moves backward for at least one vehicle in file order")

    return issues


def add_derived_columns(df: pd.DataFrame) -> pd.DataFrame:
    """Add helper columns used by the metric builders."""
    df = df.sort_values(["VehicleID", "Time", "SourceRow"]).copy()

    df["Speed_mph"] = df["Speed_mps"] * MPS_TO_MPH

    # WaitTime_s appears to be cumulative per vehicle, so the diff tells us how
    # much new wait time was added at this exact sample.
    wait_delta = df.groupby("VehicleID")["WaitTime_s"].diff()
    df["WaitDelta_s"] = wait_delta.fillna(df["WaitTime_s"]).clip(lower=0)

    # These columns help catch sim issues like teleporting, time bugs, or bad edge transitions.
    df["TimeDelta_s"] = df.groupby("VehicleID")["Time"].diff()
    df["PosDelta_m"] = df.groupby("VehicleID")["Pos_m"].diff()
    df["PrevEdgeID"] = df.groupby("VehicleID")["EdgeID"].shift(1)
    df["SameEdgeAsPrevious"] = df["EdgeID"] == df["PrevEdgeID"]

    # EdgeEntry is True when a vehicle first appears on an edge. This is a better
    # count for volume/flow than just counting every telemetry row.
    df["EdgeEntry"] = df["PrevEdgeID"].isna() | (df["EdgeID"] != df["PrevEdgeID"])
    return df


# Full-run and per-road measurements

def calculate_bottleneck_index(
    average_stopped_time_s: pd.Series,
    low_speed_ratio: pd.Series,
    stopped_ratio: pd.Series,
) -> pd.Series:
    """Build RoadMap's 0-100 screening index from per-entry measurements."""
    # A fixed 30-second reference keeps the same meaning between runs. Values
    # above it stay at the top of this part instead of making the score unlimited.
    stopped_time_part = (
        pd.to_numeric(average_stopped_time_s, errors="coerce")
        .fillna(0.0)
        .clip(lower=0.0, upper=BOTTLENECK_WAIT_REFERENCE_S)
        / BOTTLENECK_WAIT_REFERENCE_S
    )

    # Slow and stopped sample ratios describe how often traffic had trouble
    # moving. Keeping every part between 0 and 1 gives a final 0-100 index.
    slow_part = pd.to_numeric(low_speed_ratio, errors="coerce").fillna(0.0).clip(0.0, 1.0)
    stopped_part = pd.to_numeric(stopped_ratio, errors="coerce").fillna(0.0).clip(0.0, 1.0)
    return 100.0 * (0.50 * stopped_time_part + 0.30 * slow_part + 0.20 * stopped_part)

def build_run_summary(df: pd.DataFrame) -> tuple[dict[str, Any], float]:
    """Create high-level summary values for the whole simulation run."""
    sim_start = df["Time"].min()
    sim_end = df["Time"].max()
    sim_duration_s = sim_end - sim_start
    sim_duration_hr = sim_duration_s / 3600 if sim_duration_s > 0 else np.nan

    bad_accel_rows = int((df["Accel_mps2"].abs() > BAD_ACCEL_LIMIT).sum())
    zero_speed_rows = int((df["Speed_mps"] == 0).sum())
    low_speed_rows = int((df["Speed_mps"] < LOW_SPEED_MPS).sum())

    summary: dict[str, Any] = {
        "rows": len(df),
        "vehicles": df["VehicleID"].nunique(),
        "edges_used": df["EdgeID"].nunique(),
        "origin_nodes": df["OriginID"].nunique(),
        "destination_nodes": df["DestID"].nunique(),
        "simulation_start_s": sim_start,
        "simulation_end_s": sim_end,
        "simulation_duration_s": sim_duration_s,
        "average_speed_mps": df["Speed_mps"].mean(),
        "average_speed_mph": df["Speed_mph"].mean(),
        "median_speed_mph": df["Speed_mph"].median(),
        "max_speed_mps": df["Speed_mps"].max(),
        "max_speed_mph": df["Speed_mph"].max(),
        "average_wait_time_s": df["WaitTime_s"].mean(),
        "max_wait_time_s": df["WaitTime_s"].max(),
        "total_wait_added_s": df["WaitDelta_s"].sum(),
        "rows_with_added_wait": int((df["WaitDelta_s"] > 0).sum()),
        "zero_speed_rows": zero_speed_rows,
        "zero_speed_row_percent": zero_speed_rows / len(df) * 100,
        "low_speed_rows_under_5mph": low_speed_rows,
        "low_speed_row_percent": low_speed_rows / len(df) * 100,
        "bad_accel_rows_abs_gt_20": bad_accel_rows,
        "bad_accel_row_percent": bad_accel_rows / len(df) * 100,
        "min_accel_mps2": df["Accel_mps2"].min(),
        "max_accel_mps2": df["Accel_mps2"].max(),
    }

    return summary, sim_duration_hr


def build_edge_metrics(df: pd.DataFrame, sim_duration_hr: float) -> pd.DataFrame:
    """Summarize traffic by road edge."""
    grouped = df.groupby("EdgeID")

    edge_metrics = grouped.agg(
        sample_count=("Time", "count"),
        vehicle_count=("VehicleID", "nunique"),
        edge_entry_count=("EdgeEntry", "sum"),
        avg_speed_mps=("Speed_mps", "mean"),
        median_speed_mps=("Speed_mps", "median"),
        min_speed_mps=("Speed_mps", "min"),
        max_speed_mps=("Speed_mps", "max"),
        avg_cumulative_wait_s=("WaitTime_s", "mean"),
        max_cumulative_wait_s=("WaitTime_s", "max"),
        total_wait_added_s=("WaitDelta_s", "sum"),
        avg_wait_added_per_sample_s=("WaitDelta_s", "mean"),
        avg_accel_mps2=("Accel_mps2", "mean"),
        min_accel_mps2=("Accel_mps2", "min"),
        max_accel_mps2=("Accel_mps2", "max"),
        first_seen_s=("Time", "min"),
        last_seen_s=("Time", "max"),
        lanes_used=("LaneIndex", "nunique"),
    ).reset_index()

    for col in ["avg", "median", "min", "max"]:
        edge_metrics[f"{col}_speed_mph"] = edge_metrics[f"{col}_speed_mps"] * MPS_TO_MPH

    if sim_duration_hr and sim_duration_hr > 0:
        # This is the simulated traffic volume rate. It is the closest value here
        # to what we would compare against FDOT hourly traffic counts.
        edge_metrics["estimated_flow_veh_per_hr"] = edge_metrics["edge_entry_count"] / sim_duration_hr
    else:
        edge_metrics["estimated_flow_veh_per_hr"] = np.nan

    # Divide the wait collected on a road by the number of vehicle entries. This
    # keeps a long or busy run from looking worse only because it has more data.
    safe_entry_count = edge_metrics["edge_entry_count"].replace(0, np.nan)
    edge_metrics["avg_wait_per_vehicle_s"] = (
        edge_metrics["total_wait_added_s"] / safe_entry_count
    ).fillna(0.0)

    low_speed_ratio = grouped["Speed_mps"].apply(lambda s: (s < LOW_SPEED_MPS).mean())
    zero_speed_ratio = grouped["Speed_mps"].apply(lambda s: (s == 0).mean())
    bad_accel_count = grouped["Accel_mps2"].apply(lambda s: (s.abs() > BAD_ACCEL_LIMIT).sum())

    edge_metrics = edge_metrics.merge(
        low_speed_ratio.rename("low_speed_sample_ratio").reset_index(),
        on="EdgeID",
        how="left",
    )
    edge_metrics = edge_metrics.merge(
        zero_speed_ratio.rename("zero_speed_sample_ratio").reset_index(),
        on="EdgeID",
        how="left",
    )
    edge_metrics = edge_metrics.merge(
        bad_accel_count.rename("bad_accel_count").reset_index(),
        on="EdgeID",
        how="left",
    )

    # This student-built index ranks possible trouble spots. It uses values per
    # vehicle entry so a longer or busier run does not score worse just for size.
    edge_metrics["bottleneck_score"] = calculate_bottleneck_index(
        edge_metrics["avg_wait_per_vehicle_s"],
        edge_metrics["low_speed_sample_ratio"],
        edge_metrics["zero_speed_sample_ratio"],
    )

    return edge_metrics.sort_values("EdgeID").reset_index(drop=True)


# Replay frames split the same run into smaller time windows.

def build_heatmap_frames(df: pd.DataFrame, frame_seconds: float) -> tuple[pd.DataFrame, list[pd.DataFrame]]:
    """
    Build time-based edge metrics for heatmap replay.

    The normal edge_metrics.csv summarizes the full simulation. These frame
    metrics split the same idea into smaller time windows so Unreal can replay
    how traffic changes over time. Each frame can be loaded in order and matched
    back to road segments using EdgeID.
    """
    if frame_seconds <= 0:
        raise ValueError("frame_seconds must be greater than 0")

    sim_start = float(df["Time"].min())
    sim_end = float(df["Time"].max())

    if sim_end <= sim_start:
        return pd.DataFrame(), []

    frame_rows = []
    frame_tables: list[pd.DataFrame] = []
    frame_index = 0
    frame_start = sim_start

    while frame_start <= sim_end:
        frame_end = frame_start + frame_seconds

        # Include the left side of the window and exclude the right side. This
        # avoids counting the same telemetry row in two replay frames.
        if frame_end > sim_end:
            frame_df = df[(df["Time"] >= frame_start) & (df["Time"] <= sim_end)].copy()
        else:
            frame_df = df[(df["Time"] >= frame_start) & (df["Time"] < frame_end)].copy()

        if frame_df.empty:
            frame_start = frame_end
            frame_index += 1
            continue

        frame_duration_hr = (frame_df["Time"].max() - frame_df["Time"].min()) / 3600
        if not frame_duration_hr or frame_duration_hr <= 0:
            frame_duration_hr = frame_seconds / 3600

        grouped = frame_df.groupby("EdgeID")

        frame_metrics = grouped.agg(
            sample_count=("Time", "count"),
            vehicle_count=("VehicleID", "nunique"),
            edge_entry_count=("EdgeEntry", "sum"),
            avg_speed_mps=("Speed_mps", "mean"),
            total_wait_added_s=("WaitDelta_s", "sum"),
            avg_wait_added_per_sample_s=("WaitDelta_s", "mean"),
            first_seen_s=("Time", "min"),
            last_seen_s=("Time", "max"),
        ).reset_index()

        frame_metrics["FrameIndex"] = frame_index
        frame_metrics["FrameStart_s"] = frame_start
        frame_metrics["FrameEnd_s"] = min(frame_end, sim_end)
        frame_metrics["avg_speed_mph"] = frame_metrics["avg_speed_mps"] * MPS_TO_MPH
        frame_metrics["estimated_flow_veh_per_hr"] = frame_metrics["edge_entry_count"] / frame_duration_hr

        # Replay frames use the same wait measurement as the normal heatmap.
        safe_entry_count = frame_metrics["edge_entry_count"].replace(0, np.nan)
        frame_metrics["avg_wait_per_vehicle_s"] = (
            frame_metrics["total_wait_added_s"] / safe_entry_count
        ).fillna(0.0)

        low_speed_ratio = grouped["Speed_mps"].apply(lambda s: (s < LOW_SPEED_MPS).mean())
        zero_speed_ratio = grouped["Speed_mps"].apply(lambda s: (s == 0).mean())

        frame_metrics = frame_metrics.merge(
            low_speed_ratio.rename("low_speed_sample_ratio").reset_index(),
            on="EdgeID",
            how="left",
        )
        frame_metrics = frame_metrics.merge(
            zero_speed_ratio.rename("zero_speed_sample_ratio").reset_index(),
            on="EdgeID",
            how="left",
        )

        # Replay frames use the exact same 0-100 index as the full run.
        frame_metrics["bottleneck_score"] = calculate_bottleneck_index(
            frame_metrics["avg_wait_per_vehicle_s"],
            frame_metrics["low_speed_sample_ratio"],
            frame_metrics["zero_speed_sample_ratio"],
        )

        ordered_cols = [
            "FrameIndex", "FrameStart_s", "FrameEnd_s", "EdgeID",
            "sample_count", "vehicle_count", "edge_entry_count",
            "avg_speed_mph", "estimated_flow_veh_per_hr",
            "total_wait_added_s", "avg_wait_per_vehicle_s", "low_speed_sample_ratio",
            "zero_speed_sample_ratio", "bottleneck_score",
            "first_seen_s", "last_seen_s",
        ]
        frame_metrics = frame_metrics[ordered_cols].sort_values("EdgeID").reset_index(drop=True)

        frame_tables.append(frame_metrics)
        frame_rows.append({
            "FrameIndex": frame_index,
            "FrameStart_s": frame_start,
            "FrameEnd_s": min(frame_end, sim_end),
            "Rows": len(frame_df),
            "Edges": frame_metrics["EdgeID"].nunique(),
        })

        frame_start = frame_end
        frame_index += 1

    manifest = pd.DataFrame(frame_rows)
    return manifest, frame_tables


def write_heatmap_frames(
    df: pd.DataFrame,
    output_dir: str | Path,
    frame_seconds: float = DEFAULT_FRAME_SECONDS,
) -> tuple[pd.DataFrame, pd.DataFrame]:
    """Write replay heatmap frames for Unreal or a Python preview."""
    output_dir = Path(output_dir)
    frames_dir = output_dir / "heatmap_frames"
    frames_dir.mkdir(parents=True, exist_ok=True)

    manifest, frame_tables = build_heatmap_frames(df, frame_seconds)

    all_frames = pd.concat(frame_tables, ignore_index=True) if frame_tables else pd.DataFrame()

    for frame_index, frame_df in enumerate(frame_tables):
        frame_df.to_csv(frames_dir / f"frame_{frame_index:03d}.csv", index=False)

    manifest.to_csv(frames_dir / "frame_manifest.csv", index=False)
    all_frames.to_csv(frames_dir / "all_frames.csv", index=False)

    # JSON is useful for Unreal because it can read one manifest and know which
    # frame files exist without guessing filenames.
    manifest_json = {
        "frame_seconds": frame_seconds,
        "frame_count": len(frame_tables),
        "frames": [
            {
                "frame_index": int(row.FrameIndex),
                "frame_start_s": float(row.FrameStart_s),
                "frame_end_s": float(row.FrameEnd_s),
                "file": f"frame_{int(row.FrameIndex):03d}.csv",
            }
            for row in manifest.itertuples(index=False)
        ],
    }

    import json
    (frames_dir / "frame_manifest.json").write_text(
        json.dumps(manifest_json, indent=2),
        encoding="utf-8",
    )

    return manifest, all_frames


# Vehicle, trip, and data-quality outputs

def build_vehicle_metrics(df: pd.DataFrame) -> pd.DataFrame:
    """Summarize each vehicle's trip behavior."""
    grouped = df.groupby("VehicleID")

    vehicle_metrics = grouped.agg(
        sample_count=("Time", "count"),
        origin_id=("OriginID", "first"),
        dest_id=("DestID", "first"),
        first_seen_s=("Time", "min"),
        last_seen_s=("Time", "max"),
        edges_visited=("EdgeID", "nunique"),
        edge_entries=("EdgeEntry", "sum"),
        route_index_max=("RouteIndex", "max"),
        avg_speed_mps=("Speed_mps", "mean"),
        median_speed_mps=("Speed_mps", "median"),
        max_speed_mps=("Speed_mps", "max"),
        avg_cumulative_wait_s=("WaitTime_s", "mean"),
        max_cumulative_wait_s=("WaitTime_s", "max"),
        total_wait_added_s=("WaitDelta_s", "sum"),
        avg_wait_added_per_sample_s=("WaitDelta_s", "mean"),
        final_pos_m=("Pos_m", "last"),
    ).reset_index()

    vehicle_metrics["trip_time_s"] = vehicle_metrics["last_seen_s"] - vehicle_metrics["first_seen_s"]
    vehicle_metrics["avg_speed_mph"] = vehicle_metrics["avg_speed_mps"] * MPS_TO_MPH
    vehicle_metrics["median_speed_mph"] = vehicle_metrics["median_speed_mps"] * MPS_TO_MPH
    vehicle_metrics["max_speed_mph"] = vehicle_metrics["max_speed_mps"] * MPS_TO_MPH

    return vehicle_metrics


def build_od_metrics(vehicle_metrics: pd.DataFrame) -> pd.DataFrame:
    """Summarize trips by origin/destination pair."""
    return vehicle_metrics.groupby(["origin_id", "dest_id"]).agg(
        vehicle_count=("VehicleID", "count"),
        avg_trip_time_s=("trip_time_s", "mean"),
        median_trip_time_s=("trip_time_s", "median"),
        avg_speed_mph=("avg_speed_mph", "mean"),
        avg_max_cumulative_wait_s=("max_cumulative_wait_s", "mean"),
        max_cumulative_wait_s=("max_cumulative_wait_s", "max"),
        avg_total_wait_added_s=("total_wait_added_s", "mean"),
        total_wait_added_s=("total_wait_added_s", "sum"),
    ).reset_index()


def build_flags(df: pd.DataFrame) -> pd.DataFrame:
    """Create a row-level file of values that look suspicious."""
    problem_masks = {
        "negative speed": df["Speed_mps"] < 0,
        "unrealistic acceleration": df["Accel_mps2"].abs() > BAD_ACCEL_LIMIT,
        "negative lane index": df["LaneIndex"] < 0,
        "negative wait time": df["WaitTime_s"] < 0,
        "negative position": df["Pos_m"] < 0,
        "large position jump on same edge": df["SameEdgeAsPrevious"].fillna(False) & (df["PosDelta_m"].abs() > BAD_JUMP_M),
        "time did not increase": df["TimeDelta_s"].notna() & (df["TimeDelta_s"] <= 0),
    }

    any_problem = np.logical_or.reduce(list(problem_masks.values()))
    flags = df.loc[any_problem].copy()

    if flags.empty:
        return flags

    reasons = []
    for idx in flags.index:
        row_reasons = [reason for reason, mask in problem_masks.items() if bool(mask.loc[idx])]
        reasons.append("; ".join(row_reasons))

    flags["FlagReason"] = reasons
    return flags.sort_values(["VehicleID", "Time", "SourceRow"])


def write_summary_file(summary: dict[str, Any], output_path: str | Path, validation_issues: list[str]) -> None:
    """Write the summary dictionary to a readable text file."""
    lines = ["ROADMAP TELEMETRY RUN SUMMARY", "=" * 32, ""]

    lines.append("Validation issues:")
    if validation_issues:
        for issue in validation_issues:
            lines.append(f"- {issue}")
    else:
        lines.append("- none")

    lines.append("")
    for key, value in summary.items():
        if isinstance(value, float):
            lines.append(f"{key}: {value:.4f}")
        else:
            lines.append(f"{key}: {value}")

    lines += [
        "",
        "How to read this:",
        "- edge_metrics.csv is the main file for heatmaps and FDOT comparison.",
        "- estimated_flow_veh_per_hr is based on edge entries per simulation hour.",
        "- avg_wait_per_vehicle_s is the added wait divided by road entries.",
        "- vehicle_metrics.csv is useful for trip-level behavior.",
        "- od_metrics.csv groups trips by origin/destination pair.",
        "- bottleneck_edges.csv highlights likely congestion locations.",
        "- telemetry_flags.csv lists suspicious rows that may point to simulation bugs.",
        "- heatmap_frames/ stores time-windowed edge metrics for replay in Unreal.",
    ]

    Path(output_path).write_text("\n".join(lines), encoding="utf-8")


# Main analysis flow

def run_analysis(
    input_file: str | Path = INPUT_FILE,
    output_dir: str | Path = OUTPUT_DIR,
    frame_seconds: float = DEFAULT_FRAME_SECONDS,
) -> dict[str, pd.DataFrame]:
    """Run the full analysis and write output files."""
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    df = load_telemetry(input_file)
    validation_issues = validate_telemetry(df)
    df = add_derived_columns(df)

    summary, sim_duration_hr = build_run_summary(df)
    edge_metrics = build_edge_metrics(df, sim_duration_hr)
    vehicle_metrics = build_vehicle_metrics(df)
    od_metrics = build_od_metrics(vehicle_metrics)
    flags = build_flags(df)

    bottlenecks = edge_metrics[edge_metrics["sample_count"] >= MIN_EDGE_SAMPLES].sort_values(
        by="bottleneck_score",
        ascending=False,
    )

    edge_metrics.to_csv(output_dir / "edge_metrics.csv", index=False)
    vehicle_metrics.to_csv(output_dir / "vehicle_metrics.csv", index=False)
    od_metrics.to_csv(output_dir / "od_metrics.csv", index=False)
    bottlenecks.to_csv(output_dir / "bottleneck_edges.csv", index=False)
    flags.to_csv(output_dir / "telemetry_flags.csv", index=False)
    write_summary_file(summary, output_dir / "run_summary.txt", validation_issues)
    frame_manifest, all_frames = write_heatmap_frames(df, output_dir, frame_seconds)

    return {
        "raw": df,
        "summary": summary,
        "edge_metrics": edge_metrics,
        "vehicle_metrics": vehicle_metrics,
        "od_metrics": od_metrics,
        "bottlenecks": bottlenecks,
        "flags": flags,
        "frame_manifest": frame_manifest,
        "all_frames": all_frames,
    }


def main() -> None:
    """Read command-line options and analyze one telemetry CSV."""
    parser = argparse.ArgumentParser(description="Analyze RoadMap telemetry CSV output.")
    parser.add_argument("--input", default=INPUT_FILE, help="Path to simulation_output.csv")
    parser.add_argument("--output", default=OUTPUT_DIR, help="Folder for output CSV files")
    parser.add_argument(
        "--frame-seconds",
        type=float,
        default=DEFAULT_FRAME_SECONDS,
        help="Length of each heatmap replay frame in seconds.",
    )
    args = parser.parse_args()

    results = run_analysis(args.input, args.output, args.frame_seconds)

    print("Telemetry analysis complete.")
    print(f"Outputs saved in: {args.output}")
    print(f"Heatmap replay frames: {len(results['frame_manifest'])}")
    print()
    print("Top 10 bottleneck edges:")
    cols = [
        "EdgeID", "edge_entry_count", "estimated_flow_veh_per_hr", "avg_speed_mph",
        "total_wait_added_s", "low_speed_sample_ratio", "bottleneck_score",
    ]
    print(results["bottlenecks"][cols].head(10).to_string(index=False))

    if not results["flags"].empty:
        print()
        print(f"Warning: {len(results['flags'])} suspicious rows were written to telemetry_flags.csv")


if __name__ == "__main__":
    main()
