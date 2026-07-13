"""Compare one RoadMap telemetry run with an existing FDOT edge mapping."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
import pandas as pd


BASE_DIR = Path(__file__).resolve().parents[2]
DEFAULT_FDOT_MAPPING_FILE = BASE_DIR / "outputs" / "fdot" / "fdot_edge_mapping_option_a.csv"
DEFAULT_SIM_METRICS_FILE = BASE_DIR / "outputs" / "telemetry" / "edge_metrics.csv"
DEFAULT_OUTPUT_FILE = BASE_DIR / "outputs" / "fdot" / "fdot_vs_simulation.csv"

FDOT_REQUIRED_COLUMNS = {"EdgeID", "fdot_aadt"}
SIM_REQUIRED_COLUMNS = {"EdgeID", "estimated_flow_veh_per_hr"}


def classify_geh(score: float) -> str:
    """Turn a GEH score into the standard RoadMap review category."""
    if pd.isna(score):
        return "No Data"
    if score < 5:
        return "Good"
    if score < 10:
        return "Review"
    return "Poor"


def _load_csv(path: Path, description: str) -> pd.DataFrame:
    if not path.exists():
        raise FileNotFoundError(f"{description} was not found: {path}")
    return pd.read_csv(path)


def _require_columns(dataframe: pd.DataFrame, required: set[str], description: str) -> None:
    missing = sorted(required.difference(dataframe.columns))
    if missing:
        raise ValueError(f"{description} is missing required columns: {', '.join(missing)}")


def _normalize_edge_ids(dataframe: pd.DataFrame) -> pd.DataFrame:
    result = dataframe.copy()
    result["EdgeID"] = pd.to_numeric(result["EdgeID"], errors="coerce")
    result = result.dropna(subset=["EdgeID"])
    result["EdgeID"] = result["EdgeID"].astype(int)
    return result


def build_fdot_comparison(
    fdot_mapping: pd.DataFrame,
    simulation_metrics: pd.DataFrame,
    hourly_factor: float = 1.0 / 24.0,
) -> pd.DataFrame:
    """Return edge-level FDOT validation values without writing files."""
    _require_columns(fdot_mapping, FDOT_REQUIRED_COLUMNS, "FDOT mapping")
    _require_columns(simulation_metrics, SIM_REQUIRED_COLUMNS, "Simulation metrics")

    if hourly_factor <= 0:
        raise ValueError("hourly_factor must be greater than zero.")

    fdot = _normalize_edge_ids(fdot_mapping)
    simulation = _normalize_edge_ids(simulation_metrics)

    fdot["fdot_aadt"] = pd.to_numeric(fdot["fdot_aadt"], errors="coerce")
    simulation["estimated_flow_veh_per_hr"] = pd.to_numeric(
        simulation["estimated_flow_veh_per_hr"],
        errors="coerce",
    )

    fdot = fdot.dropna(subset=["fdot_aadt"])
    simulation = simulation.dropna(subset=["estimated_flow_veh_per_hr"])

    # A mapping is expected to contain at most one FDOT match per RoadMap edge.
    # Keeping the nearest row prevents duplicate edges from inflating the summary.
    if "match_distance_m" in fdot.columns:
        fdot["match_distance_m"] = pd.to_numeric(fdot["match_distance_m"], errors="coerce")
        fdot = fdot.sort_values("match_distance_m", na_position="last")
    fdot = fdot.drop_duplicates(subset=["EdgeID"], keep="first")
    simulation = simulation.drop_duplicates(subset=["EdgeID"], keep="first")

    comparison = fdot.merge(simulation, on="EdgeID", how="inner", suffixes=("_fdot", "_simulation"))
    if comparison.empty:
        raise ValueError(
            "No matching EdgeIDs were found between the FDOT mapping and this telemetry run. "
            "Confirm that the mapping was generated from the same map and edge ordering."
        )

    # AADT represents vehicles per day. The default 1/24 conversion is a rough
    # average hour and can later be replaced by a map-specific time-of-day factor.
    comparison["fdot_estimated_veh_per_hr"] = comparison["fdot_aadt"] * hourly_factor
    comparison["difference_per_hr"] = (
        comparison["estimated_flow_veh_per_hr"]
        - comparison["fdot_estimated_veh_per_hr"]
    )
    comparison["percent_error_per_hr"] = np.where(
        comparison["fdot_estimated_veh_per_hr"] > 0,
        comparison["difference_per_hr"]
        / comparison["fdot_estimated_veh_per_hr"]
        * 100.0,
        np.nan,
    )

    simulated_volume = comparison["estimated_flow_veh_per_hr"]
    observed_volume = comparison["fdot_estimated_veh_per_hr"]
    denominator = simulated_volume + observed_volume
    comparison["geh_score"] = np.where(
        denominator > 0,
        np.sqrt(2.0 * (simulated_volume - observed_volume) ** 2 / denominator),
        np.nan,
    )
    comparison["geh_result"] = comparison["geh_score"].apply(classify_geh)
    return comparison.sort_values("geh_score", ascending=False, na_position="last")


def summarize_fdot_comparison(comparison: pd.DataFrame) -> dict:
    """Create JSON-safe values for the telemetry panel and run metadata."""
    counts = comparison["geh_result"].value_counts()
    valid_geh = comparison["geh_score"].dropna()
    valid_error = comparison["percent_error_per_hr"].dropna()
    matched_edges = int(len(comparison))
    good_edges = int(counts.get("Good", 0))

    return {
        "matched_edges": matched_edges,
        "good_edges": good_edges,
        "review_edges": int(counts.get("Review", 0)),
        "poor_edges": int(counts.get("Poor", 0)),
        "no_data_edges": int(counts.get("No Data", 0)),
        "good_percent": round(good_edges / matched_edges * 100.0, 2) if matched_edges else 0.0,
        "mean_geh": round(float(valid_geh.mean()), 3) if not valid_geh.empty else None,
        "median_geh": round(float(valid_geh.median()), 3) if not valid_geh.empty else None,
        "mean_absolute_percent_error": (
            round(float(valid_error.abs().mean()), 2) if not valid_error.empty else None
        ),
        "hourly_conversion_note": "FDOT AADT multiplied by 1/24 (rough average hour).",
    }


def compare_fdot_to_simulation(
    fdot_mapping_path: Path,
    simulation_metrics_path: Path,
    output_path: Path,
    summary_path: Path | None = None,
    hourly_factor: float = 1.0 / 24.0,
) -> dict:
    """Run comparison, save the edge table and summary, and return the summary."""
    fdot_mapping_path = Path(fdot_mapping_path)
    simulation_metrics_path = Path(simulation_metrics_path)
    output_path = Path(output_path)
    summary_path = Path(summary_path) if summary_path else output_path.with_name("fdot_summary.json")

    fdot = _load_csv(fdot_mapping_path, "FDOT edge mapping")
    simulation = _load_csv(simulation_metrics_path, "Simulation edge metrics")
    comparison = build_fdot_comparison(fdot, simulation, hourly_factor)
    summary = summarize_fdot_comparison(comparison)
    summary.update({
        "mapping_path": str(fdot_mapping_path),
        "simulation_metrics_path": str(simulation_metrics_path),
        "comparison_path": str(output_path),
    })

    output_path.parent.mkdir(parents=True, exist_ok=True)
    comparison.to_csv(output_path, index=False)
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    return summary


def main() -> None:
    parser = argparse.ArgumentParser(description="Compare one RoadMap run with FDOT AADT data.")
    parser.add_argument("--fdot-mapping", type=Path, default=DEFAULT_FDOT_MAPPING_FILE)
    parser.add_argument("--simulation-metrics", type=Path, default=DEFAULT_SIM_METRICS_FILE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT_FILE)
    parser.add_argument("--summary", type=Path)
    parser.add_argument("--hourly-factor", type=float, default=1.0 / 24.0)
    args = parser.parse_args()

    summary = compare_fdot_to_simulation(
        args.fdot_mapping,
        args.simulation_metrics,
        args.output,
        args.summary,
        args.hourly_factor,
    )
    print(json.dumps({"success": True, "summary": summary}, indent=2))


if __name__ == "__main__":
    main()
