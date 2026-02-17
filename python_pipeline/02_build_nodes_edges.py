"""
Turn a road GeoJSON (LineStrings) into a simple graph:

- nodes.jsonl : one node per unique coordinate
- edges.jsonl : one edge per road segment between consecutive coordinates

"""

from pathlib import Path
import json

import geopandas as gpd
from shapely.geometry import LineString
from pyproj import Geod

# -----------------------------
# 1) File paths
# -----------------------------
BASE_DIR = Path(r"C:\Users\danie\OneDrive\Desktop\School\Spring 2026\SD1\OSM")
OUT_DIR = BASE_DIR / "out"

INPUT_CLIPPED = OUT_DIR / "orange_major_roads_clipped.geojson"
INPUT_PATH = INPUT_CLIPPED

NODES_PATH = OUT_DIR / "nodes.jsonl"
EDGES_PATH = OUT_DIR / "edges.jsonl"

print("Reading:", INPUT_PATH)

# -----------------------------
# 2) Read GeoJSON lines
# -----------------------------
roads = gpd.read_file(INPUT_PATH)

# Safety: keep only lines
roads = roads[roads.geometry.type.isin(["LineString", "MultiLineString"])].copy()

# -----------------------------
# 3) Helper: compute length in meters on Earth
# -----------------------------
# Geod gives distances using lat/lon
geod = Geod(ellps="WGS84")

def segment_length_m(lon1, lat1, lon2, lat2) -> float:
    """
    Returns the distance (meters) between two lat/lon points on Earth.
    """
    # geod.inv returns (fwd_azimuth, back_azimuth, distance_meters)
    _, _, dist_m = geod.inv(lon1, lat1, lon2, lat2)
    return float(dist_m)

# -----------------------------
# 4) Build nodes and edges
# -----------------------------
# We'll create a unique node for every unique coordinate.
# To avoid tiny floating point differences, we round coordinates.
ROUND_DP = 7  # ~1 cm-ish precision; plenty for roads

node_id_by_coord = {}   # (lon, lat) -> node_id
nodes_list = []         # list of {"id":..., "lat":..., "lon":...}
edges_list = []         # list of {"u":..., "v":..., "length_m":..., ...}

next_node_id = 1

def get_node_id(lon, lat) -> int:
    """
    Get an integer node id for a coordinate.
    If it's new, create a node entry.
    """
    global next_node_id

    lon_r = round(float(lon), ROUND_DP)
    lat_r = round(float(lat), ROUND_DP)
    key = (lon_r, lat_r)

    if key in node_id_by_coord:
        return node_id_by_coord[key]

    # Create a new node
    node_id_by_coord[key] = next_node_id
    nodes_list.append({"id": next_node_id, "lat": lat_r, "lon": lon_r})
    next_node_id += 1
    return node_id_by_coord[key]

# Go through each road feature
for idx, row in roads.iterrows():
    geom = row.geometry

    # MultiLineString can contain multiple LineStrings
    lines = []
    if geom.geom_type == "LineString":
        lines = [geom]
    elif geom.geom_type == "MultiLineString":
        lines = list(geom.geoms)

    # Grab some common OSM-like attributes if they exist
    highway = row.get("highway", None)
    oneway = row.get("oneway", None)
    name = row.get("name", None)
    ref = row.get("ref", None)

    for line in lines:
        # Convert line into a list of coordinate pairs
        coords = list(line.coords)

        # Make edges between each pair of consecutive points
        for i in range(len(coords) - 1):
            lon1, lat1 = coords[i]
            lon2, lat2 = coords[i + 1]

            u = get_node_id(lon1, lat1)
            v = get_node_id(lon2, lat2)

            length_m = segment_length_m(lon1, lat1, lon2, lat2)

            edges_list.append({
                "u": u,
                "v": v,
                "length_m": round(length_m, 3),
                "highway": highway,
                "oneway": oneway,
                "name": name,
                "ref": ref,
            })

# -----------------------------
# 5) Save as JSON Lines (JSONL) for C team
# -----------------------------

print(f"Writing {len(nodes_list)} nodes -> {NODES_PATH}")
with open(NODES_PATH, "w", encoding="utf-8") as f:
    for n in nodes_list:
        f.write(json.dumps(n) + "\n")

print(f"Writing {len(edges_list)} edges -> {EDGES_PATH}")
with open(EDGES_PATH, "w", encoding="utf-8") as f:
    for e in edges_list:
        f.write(json.dumps(e) + "\n")

print("DONE")
print("Nodes file:", NODES_PATH)
print("Edges file:", EDGES_PATH)
