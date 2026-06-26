from pathlib import Path
import numpy as np
import pandas as pd

"""
fdot_vs_simulation.py

I use this script to compare RoadMap simulation flow estimates
against FDOT traffic counts.

The goal is not to get a perfect match, but to see whether the
simulation produces reasonable traffic patterns compared to
real-world roadway volumes.

New validation output:
    geh_score and geh_result

GEH is useful because it compares simulated volume against observed volume
in a way that is easier to review than percent error alone.
"""

BASE_DIR = Path(__file__).resolve().parents[2]

FDOT_MAPPING_FILE = BASE_DIR / "outputs/fdot/fdot_edge_mapping_option_a.csv"
SIM_METRICS_FILE = BASE_DIR / "outputs/telemetry/edge_metrics.csv"
OUTPUT_FILE = BASE_DIR / "outputs/fdot/fdot_vs_simulation.csv"


def classify_geh(score):
    """Turn the GEH number into a simple label for maps/reports."""
    if pd.isna(score):
        return "No Data"
    if score < 5:
        return "Good"
    if score < 10:
        return "Review"
    return "Poor"


def main() -> None:
    print("Loading FDOT mapping...")
    fdot = pd.read_csv(FDOT_MAPPING_FILE)

    print("Loading simulation metrics...")
    sim = pd.read_csv(SIM_METRICS_FILE)

    # Keep only RoadMap edges that successfully matched to FDOT.
    fdot = fdot[fdot["fdot_aadt"].notna()].copy()

    print(f"FDOT matched edges: {len(fdot):,}")
    print(f"Simulation edges:   {len(sim):,}")

    # Join FDOT mapping and simulation metrics using EdgeID.
    comparison = fdot.merge(
        sim,
        on="EdgeID",
        how="inner",
    )

    print(f"Joined edges:       {len(comparison):,}")

    if len(comparison) == 0:
        raise ValueError(
            "No matching EdgeIDs were found between FDOT results and telemetry results."
        )

    # FDOT AADT is vehicles per day.
    # Dividing by 24 gives a rough average hourly traffic estimate.
    comparison["fdot_estimated_veh_per_hr"] = comparison["fdot_aadt"] / 24

    # Positive values mean the simulation predicted more traffic than FDOT.
    # Negative values mean the simulation predicted less traffic than FDOT.
    comparison["difference_per_hr"] = (
        comparison["estimated_flow_veh_per_hr"]
        - comparison["fdot_estimated_veh_per_hr"]
    )

    comparison["percent_error_per_hr"] = np.where(
        comparison["fdot_estimated_veh_per_hr"] > 0,
        (
            comparison["difference_per_hr"]
            / comparison["fdot_estimated_veh_per_hr"]
        ) * 100,
        np.nan,
    )

    # -----------------------------
    # GEH validation
    # -----------------------------
    # GEH compares simulated traffic volume to observed/real-world traffic volume.
    # Lower GEH values mean the simulation is closer to FDOT's traffic count.
    sim_volume = comparison["estimated_flow_veh_per_hr"]
    fdot_volume = comparison["fdot_estimated_veh_per_hr"]
    denominator = sim_volume + fdot_volume

    comparison["geh_score"] = np.where(
        denominator > 0,
        np.sqrt((2 * (sim_volume - fdot_volume) ** 2) / denominator),
        np.nan,
    )

    comparison["geh_result"] = comparison["geh_score"].apply(classify_geh)

    # Save full comparison table. This is the main output Unreal can use too.
    OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    comparison.to_csv(OUTPUT_FILE, index=False)

    print(f"\nSaved: {OUTPUT_FILE.relative_to(BASE_DIR)}")

    sample_cols = [
        "EdgeID",
        "fdot_aadt",
        "fdot_estimated_veh_per_hr",
        "estimated_flow_veh_per_hr",
        "difference_per_hr",
        "percent_error_per_hr",
        "geh_score",
        "geh_result",
    ]

    print("\nSample Results")
    print(comparison[sample_cols].head(10).to_string(index=False))

    print("\nPercent Error Summary")
    print(comparison["percent_error_per_hr"].describe())

    print("\nGEH Summary")
    print(comparison["geh_score"].describe())

    print("\nGEH Classification")
    print(comparison["geh_result"].value_counts())


if __name__ == "__main__":
    main()
