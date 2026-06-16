from pathlib import Path
import pandas as pd

"""
fdot_vs_simulation.py

I use this script to compare RoadMap simulation flow estimates
against FDOT traffic counts.

The goal is not to get a perfect match, but to see whether the
simulation produces reasonable traffic patterns compared to
real-world roadway volumes.
"""

BASE_DIR = Path(__file__).resolve().parents[2]

print("Loading FDOT mapping...")
fdot = pd.read_csv(
    BASE_DIR / "outputs/fdot/fdot_edge_mapping_option_a.csv"
)

print("Loading simulation metrics...")
sim = pd.read_csv(
    BASE_DIR / "outputs/telemetry/edge_metrics.csv"
)

# Keep only RoadMap edges that successfully matched to FDOT
fdot = fdot[fdot["fdot_aadt"].notna()].copy()

print(f"FDOT matched edges: {len(fdot):,}")
print(f"Simulation edges:   {len(sim):,}")

# Join FDOT mapping and simulation metrics using EdgeID
comparison = fdot.merge(
    sim,
    on="EdgeID",
    how="inner"
)

print(f"Joined edges:       {len(comparison):,}")

if len(comparison) == 0:
    raise ValueError(
        "No matching EdgeIDs were found between FDOT results and telemetry results."
    )

# FDOT AADT is vehicles per day.
# This gives a rough average hourly traffic estimate.
comparison["fdot_estimated_veh_per_hr"] = comparison["fdot_aadt"] / 24

# Positive values mean the simulation predicted more traffic than FDOT.
# Negative values mean the simulation predicted less traffic than FDOT.
comparison["difference_per_hr"] = (
    comparison["estimated_flow_veh_per_hr"]
    - comparison["fdot_estimated_veh_per_hr"]
)

comparison["percent_error_per_hr"] = (
    comparison["difference_per_hr"]
    / comparison["fdot_estimated_veh_per_hr"]
) * 100

# Save full comparison table
comparison.to_csv(
    BASE_DIR / "outputs/fdot/fdot_vs_simulation.csv",
    index=False
)

print("\nSaved: outputs/fdot/fdot_vs_simulation.csv")

print("\nSample Results")
print(
    comparison[
        [
            "EdgeID",
            "fdot_aadt",
            "fdot_estimated_veh_per_hr",
            "estimated_flow_veh_per_hr",
            "difference_per_hr",
            "percent_error_per_hr"
        ]
    ].head(10)
)

print("\nPercent Error Summary")
print(comparison["percent_error_per_hr"].describe())