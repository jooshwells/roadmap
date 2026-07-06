"""
FDOT to RoadMap EdgeID matcher - Option A

Goal:
Assign one FDOT AADT value to each RoadMap/OSM edge that is closest to an FDOT AADT segment.

Important assumption:
The simulation team's EdgeID is 1-indexed and follows the same order as the edges JSONL file.
That means:
    first edge in JSONL  -> EdgeID 1
    second edge in JSONL -> EdgeID 2
    etc.

Inputs:
    Annual_Average_Daily_Traffic_TDA_-2830269347537693947.geojson
    edges_orange_allroads_offline_xy.jsonl

Outputs:
    fdot_edge_mapping_option_a.csv
    fdot_edge_mapping_option_a.geojson

Notes:
    - This uses geometry matching, not road names.
    - Each RoadMap edge gets the nearest FDOT AADT segment if it is within MATCH_DISTANCE_METERS.
    - Start with 30 meters. If too many edges are unmatched, try 50 or 75.
"""

import json
from pathlib import Path

import geopandas as gpd
import pandas as pd
from shapely.geometry import LineString


# -----------------------------
# File paths
# -----------------------------

BASE_DIR = Path(__file__).resolve().parents[2]

FDOT_FILE = BASE_DIR / "data/fdot/Annual_Average_Daily_Traffic_TDA_-2830269347537693947.geojson"
EDGES_FILE = BASE_DIR / "data/network/edges_orange_allroads_offline_xy.jsonl"

OUTPUT_CSV = BASE_DIR / "outputs/fdot/fdot_edge_mapping_option_a.csv"
OUTPUT_GEOJSON = BASE_DIR / "outputs/fdot/fdot_edge_mapping_option_a.geojson"

# Using relative paths makes the script work no matter where it is launched from.
# This is helpful now that the telemetry project has been split into data,
# outputs, and source code folders.


# -----------------------------
# Settings
# -----------------------------

# Your OSMnx projected x/y values look like UTM Zone 17N meters.
# If the FDOT/OSM layers look offset in QGIS, try changing this to "EPSG:26917".
OSM_XY_CRS = "EPSG:32617"

# Maximum distance allowed between one RoadMap edge and the nearest FDOT AADT segment.
# Start strict. Increase later if too many edges are unmatched.
MATCH_DISTANCE_METERS = 30


# -----------------------------
# Helper functions
# -----------------------------

def load_roadmap_edges(edges_path: Path) -> gpd.GeoDataFrame:
    """
    Load RoadMap exported edges JSONL and create one LineString geometry per edge.

    The simulation team confirmed EdgeID is 1-indexed based on the edge import order.
    So we create EdgeID using the line number in the JSONL file.
    """
    rows = []

    with edges_path.open("r", encoding="utf-8") as f:
        for edge_id, line in enumerate(f, start=1):
            edge = json.loads(line)

            # geometry_xy gives the real road shape, not just a straight line from u to v.
            xy_points = edge.get("geometry_xy")

            # Fallback: skip bad geometry rows instead of crashing.
            if not xy_points or len(xy_points) < 2:
                continue

            coords = [(pt["x"], pt["y"]) for pt in xy_points]
            geometry = LineString(coords)

            rows.append(
                {
                    "EdgeID": edge_id,
                    "u": edge.get("u"),
                    "v": edge.get("v"),
                    "length_m": edge.get("length_m"),
                    "speed_mps": edge.get("speed_mps"),
                    "lanes": edge.get("lanes"),
                    "oneway": edge.get("oneway"),
                    "highway": edge.get("highway"),
                    "name": edge.get("name"),
                    "ref": edge.get("ref"),
                    "geometry": geometry,
                }
            )

    return gpd.GeoDataFrame(rows, geometry="geometry", crs=OSM_XY_CRS)


def main() -> None:
    print("Loading RoadMap edges...")
    roadmap_edges = load_roadmap_edges(EDGES_FILE)
    print(f"Loaded {len(roadmap_edges):,} RoadMap edges")

    print("Loading FDOT AADT data...")
    fdot = gpd.read_file(FDOT_FILE)
    print(f"Loaded {len(fdot):,} FDOT segments")
    print(f"FDOT CRS: {fdot.crs}")

    # Keep only Orange County just to be safe.
    if "COUNTY" in fdot.columns:
        fdot = fdot[fdot["COUNTY"].str.contains("Orange", case=False, na=False)].copy()
        print(f"FDOT Orange County segments: {len(fdot):,}")

    # Project FDOT into the same CRS as the RoadMap edges.
    fdot = fdot.to_crs(roadmap_edges.crs)

    # Keep only the FDOT columns we need for validation.
    fdot_cols = [
        "FID",
        "YEAR_",
        "ROADWAY",
        "DESC_FRM",
        "DESC_TO",
        "AADT",
        "BEGIN_POST",
        "END_POST",
        "Shape_Leng",
        "geometry",
    ]
    fdot_cols = [col for col in fdot_cols if col in fdot.columns]
    fdot_small = fdot[fdot_cols].copy()

    print("Matching each RoadMap edge to the nearest FDOT segment...")
    matched = gpd.sjoin_nearest(
        roadmap_edges,
        fdot_small,
        how="left",
        distance_col="match_distance_m",
        max_distance=MATCH_DISTANCE_METERS,
    )

    # Sometimes GeoPandas returns more than one FDOT match for the same RoadMap edge.
    # I only want one output row per EdgeID, so I keep the closest match.
    matched = matched.sort_values(
        by=["EdgeID", "match_distance_m"],
        na_position="last"
    )

    matched = matched.drop_duplicates(
        subset=["EdgeID"],
        keep="first"
    )

    # Rename FDOT fields so the output is clearer.
    rename_map = {
        "FID": "fdot_fid",
        "YEAR_": "fdot_year",
        "ROADWAY": "fdot_roadway",
        "DESC_FRM": "fdot_from",
        "DESC_TO": "fdot_to",
        "AADT": "fdot_aadt",
        "BEGIN_POST": "fdot_begin_post",
        "END_POST": "fdot_end_post",
        "Shape_Leng": "fdot_shape_length",
    }
    matched = matched.rename(columns=rename_map)

    # Drop the spatial join index column if GeoPandas created it.
    if "index_right" in matched.columns:
        matched = matched.drop(columns=["index_right"])

    # Save outputs.
    csv_cols = [
        "EdgeID",
        "u",
        "v",
        "name",
        "ref",
        "highway",
        "length_m",
        "speed_mps",
        "lanes",
        "oneway",
        "fdot_aadt",
        "fdot_roadway",
        "fdot_from",
        "fdot_to",
        "fdot_year",
        "match_distance_m",
    ]
    csv_cols = [col for col in csv_cols if col in matched.columns]

    matched[csv_cols].to_csv(OUTPUT_CSV, index=False)
    matched.to_file(OUTPUT_GEOJSON, driver="GeoJSON")

    # Print a simple summary.
    total_edges = len(matched)
    matched_edges = matched["fdot_aadt"].notna().sum()
    unmatched_edges = total_edges - matched_edges

    print("\nDone.")
    print(f"Total RoadMap edges: {total_edges:,}")
    print(f"Matched to FDOT:     {matched_edges:,}")
    print(f"Unmatched:           {unmatched_edges:,}")
    print(f"Match distance used: {MATCH_DISTANCE_METERS} meters")
    print(f"\nSaved: {OUTPUT_CSV}")
    print(f"Saved: {OUTPUT_GEOJSON}")

    print("\nSample matched rows:")
    sample_cols = ["EdgeID", "name", "ref", "fdot_aadt", "fdot_roadway", "match_distance_m"]
    sample_cols = [col for col in sample_cols if col in matched.columns]
    print(matched[sample_cols].dropna(subset=["fdot_aadt"]).head(10))


if __name__ == "__main__":
    main()
