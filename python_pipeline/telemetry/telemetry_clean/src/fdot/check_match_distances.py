from pathlib import Path
import pandas as pd
import matplotlib.pyplot as plt

"""
check_match_distances.py

I use this script to check how far RoadMap edges were from the
FDOT segment they matched to.

This helps validate whether the geometry matching distance is
reasonable or if the matching threshold should be adjusted.
"""

BASE_DIR = Path(__file__).resolve().parents[2]

df = pd.read_csv(
    BASE_DIR / "outputs/fdot/fdot_edge_mapping_option_a.csv"
)

# Keep only matched rows
# Only keep RoadMap edges that successfully matched to FDOT.
df = df[df["fdot_aadt"].notna()]

print("\nMatch Distance Statistics")
print(df["match_distance_m"].describe())

# Histogram
plt.figure(figsize=(10,6))
plt.hist(df["match_distance_m"], bins=30)

plt.title("FDOT Match Distance Validation")
plt.xlabel("Match Distance (meters)")
plt.ylabel("Number of Edges")

plt.grid(True)
plt.tight_layout()

plt.savefig(
    BASE_DIR / "outputs/fdot/match_distance_histogram.png",
    dpi=300
)
print(
    f"\nSaved: {BASE_DIR / 'outputs/fdot/match_distance_histogram.png'}"
)
#plt.show()

print("0-5m  :", ((df["match_distance_m"] <= 5)).sum())
print("5-10m :", ((df["match_distance_m"] > 5) & (df["match_distance_m"] <= 10)).sum())
print("10-20m:", ((df["match_distance_m"] > 10) & (df["match_distance_m"] <= 20)).sum())
print("20-30m:", ((df["match_distance_m"] > 20) & (df["match_distance_m"] <= 30)).sum())