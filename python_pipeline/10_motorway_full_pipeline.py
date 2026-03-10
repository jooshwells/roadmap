"""
10_motorway_full_pipeline.py

Goal:
- Build a directed road graph from orange_motorways.geojson
- Simplify it (straight edges are fine for now)
- Export JSONL nodes + edges for the C++ simulation

What "simplify" means here:
- If a node is just a bend point (degree = 2), we remove it
- We merge the two edges into one longer edge
- We keep intersections and endpoints (degree != 2)

"""

from pathlib import Path
import json
import re

import geopandas as gpd
import networkx as nx
from pyproj import Geod


# -----------------------------
# 1) Paths (my project folder)
# -----------------------------
BASE_DIR = Path(r"C:\Users\danie\OneDrive\Desktop\School\Spring 2026\SD1\OSM")
IN_PATH = BASE_DIR / "out" / "orange_motorways.geojson"

OUT_NODES = BASE_DIR / "out" / "nodes_motorways_simplified.jsonl"
OUT_EDGES = BASE_DIR / "out" / "edges_motorways_simplified.jsonl"


# -----------------------------
# 2) Helpers for distance + tags
# -----------------------------
geod = Geod(ellps="WGS84")

def distance_m(lon1, lat1, lon2, lat2) -> float:
    """Distance in meters between two lon/lat points."""
    _, _, dist = geod.inv(lon1, lat1, lon2, lat2)
    return float(dist)

def mph_to_mps(mph: float) -> float:
    return mph * 0.44704

def parse_maxspeed_to_mps(val):
    """Try to parse maxspeed like '65 mph' or '100 km/h'. Return m/s or None."""
    if val is None:
        return None
    s = str(val).lower()
    m = re.search(r"\d+(\.\d+)?", s)
    if not m:
        return None
    num = float(m.group())
    if "km" in s:
        return num / 3.6
    # If there is no unit, I assume mph (normal for US OSM)
    return mph_to_mps(num)

def parse_lanes(val):
    """Try to parse lanes like '3' or '2;3'. Return int or None."""
    if val is None:
        return None
    m = re.search(r"\d+", str(val))
    if not m:
        return None
    return int(m.group())

def oneway_status(val):
    """
    Return:
    - "no"  -> two-way
    - "yes" -> forward only
    - "-1"  -> reverse only
    """
    if val is None:
        return "no"
    s = str(val).strip().lower()
    if s in ["yes", "1", "true", "t"]:
        return "yes"
    if s in ["-1", "reverse"]:
        return "-1"
    return "no"


# Defaults for motorways if tags are missing
DEFAULT_SPEED_MPS = mph_to_mps(65)
DEFAULT_LANES = 3


# -----------------------------
# 3) Build a directed graph (NetworkX DiGraph)
# -----------------------------
print("Reading:", IN_PATH)
roads = gpd.read_file(IN_PATH)
roads = roads[roads.geometry.type.isin(["LineString", "MultiLineString"])].copy()

G = nx.DiGraph()

for feature_id, row in roads.iterrows():
    geom = row.geometry

    # Must-haves
    highway = row.get("highway")
    one = oneway_status(row.get("oneway"))
    speed = parse_maxspeed_to_mps(row.get("maxspeed")) or DEFAULT_SPEED_MPS
    lanes = parse_lanes(row.get("lanes")) or DEFAULT_LANES

    # Nice-to-haves
    name = row.get("name")
    ref = row.get("ref")
    bridge = row.get("bridge")
    tunnel = row.get("tunnel")
    layer = row.get("layer")
    surface = row.get("surface")
    access = row.get("access")
    motor_vehicle = row.get("motor_vehicle")
    service = row.get("service")

    # Some features are MultiLineString, so I handle each part
    lines = [geom] if geom.geom_type == "LineString" else list(geom.geoms)

    for line in lines:
        coords = list(line.coords)

        for i in range(len(coords) - 1):
            lon1, lat1 = coords[i]
            lon2, lat2 = coords[i + 1]

            u = (round(lon1, 7), round(lat1, 7))
            v = (round(lon2, 7), round(lat2, 7))

            # store node coordinates on the node itself
            G.add_node(u, lon=u[0], lat=u[1])
            G.add_node(v, lon=v[0], lat=v[1])

            length = distance_m(lon1, lat1, lon2, lat2)

            edge_data = {
                "length_m": float(length),
                "speed_mps": float(speed),
                "lanes": int(lanes),
                "oneway": one,
                "highway": highway,

                # nice-to-haves
                "name": name,
                "ref": ref,
                "bridge": bridge,
                "tunnel": tunnel,
                "layer": layer,
                "surface": surface,
                "access": access,
                "motor_vehicle": motor_vehicle,
                "service": service,

                # debug
                "source_feature_id": int(feature_id),
            }

            # Add edges depending on oneway
            if one == "yes":
                G.add_edge(u, v, **edge_data)
            elif one == "-1":
                G.add_edge(v, u, **edge_data)
            else:
                G.add_edge(u, v, **edge_data)
                G.add_edge(v, u, **edge_data)

print("Original graph:")
print("  Nodes:", G.number_of_nodes())
print("  Edges:", G.number_of_edges())


# -----------------------------
# 4) Simplify - Improved for directed graphs
# -----------------------------
# New rule:
# - A "bend node" usually has exactly 2 unique neighbors total
#   (neighbors = predecessors U successors)
# - If it only connects between A and B, we can merge edges:
#     A->n->B into A->B
#   and if it exists:
#     B->n->A into B->A
#
# This works even when roads are two-way (because then n has
# 2 preds and 2 succs, but still only 2 unique neighbors).

H = G.copy()

removed_nodes = 0
merged_edges = 0

changed = True
while changed:
    changed = False

    for n in list(H.nodes()):
        if n not in H:
            continue

        neighbors = set(H.predecessors(n)) | set(H.successors(n))

        # Only simplify if exactly 2 neighbors
        if len(neighbors) != 2:
            continue

        a, b = list(neighbors)

        if a == b:
            continue

        # --- Merge a -> n -> b ---
        if H.has_edge(a, n) and H.has_edge(n, b):

            d1 = H.get_edge_data(a, n)
            d2 = H.get_edge_data(n, b)

            new_len = float(d1["length_m"]) + float(d2["length_m"])

            new_data = dict(d1)
            new_data["length_m"] = new_len

            # conservative merge choice
            new_data["speed_mps"] = min(
                float(d1.get("speed_mps", DEFAULT_SPEED_MPS)),
                float(d2.get("speed_mps", DEFAULT_SPEED_MPS))
            )

            new_data["lanes"] = min(
                int(d1.get("lanes", DEFAULT_LANES)),
                int(d2.get("lanes", DEFAULT_LANES))
            )

            H.remove_edge(a, n)
            H.remove_edge(n, b)

            H.add_edge(a, b, **new_data)

            merged_edges += 1
            changed = True

        # --- Merge b -> n -> a ---
        if H.has_edge(b, n) and H.has_edge(n, a):

            d1 = H.get_edge_data(b, n)
            d2 = H.get_edge_data(n, a)

            new_len = float(d1["length_m"]) + float(d2["length_m"])

            new_data = dict(d1)
            new_data["length_m"] = new_len

            new_data["speed_mps"] = min(
                float(d1.get("speed_mps", DEFAULT_SPEED_MPS)),
                float(d2.get("speed_mps", DEFAULT_SPEED_MPS))
            )

            new_data["lanes"] = min(
                int(d1.get("lanes", DEFAULT_LANES)),
                int(d2.get("lanes", DEFAULT_LANES))
            )

            H.remove_edge(b, n)
            H.remove_edge(n, a)

            H.add_edge(b, a, **new_data)

            merged_edges += 1
            changed = True

        # Remove node if now isolated
        if H.in_degree(n) == 0 and H.out_degree(n) == 0:
            H.remove_node(n)
            removed_nodes += 1
            changed = True

print("Simplified graph:")
print("  Nodes:", H.number_of_nodes())
print("  Edges:", H.number_of_edges())
print("  Removed bend nodes:", removed_nodes)
print("  Merged edge chains:", merged_edges)


# -----------------------------
# 5) Export to JSONL for C++
# -----------------------------
# Convert node keys (lon,lat) into small integer ids (1..N)
node_to_id = {}
nodes_out = []
next_id = 1

for node in H.nodes():
    node_to_id[node] = next_id
    nodes_out.append({
        "id": next_id,
        "lon": float(H.nodes[node]["lon"]),
        "lat": float(H.nodes[node]["lat"]),
    })
    next_id += 1

edges_out = []
for u, v, data in H.edges(data=True):
    edges_out.append({
        "u": node_to_id[u],
        "v": node_to_id[v],
        "length_m": round(float(data.get("length_m", 0.0)), 3),
        "speed_mps": round(float(data.get("speed_mps", DEFAULT_SPEED_MPS)), 3),
        "lanes": int(data.get("lanes", DEFAULT_LANES)),
        "oneway": data.get("oneway"),
        "highway": data.get("highway"),

        # nice-to-haves
        "name": data.get("name"),
        "ref": data.get("ref"),
        "bridge": data.get("bridge"),
        "tunnel": data.get("tunnel"),
        "layer": data.get("layer"),
        "surface": data.get("surface"),
        "access": data.get("access"),
        "motor_vehicle": data.get("motor_vehicle"),
        "service": data.get("service"),

        # debug
        "source_feature_id": data.get("source_feature_id"),
    })

print("Writing:", OUT_NODES)
with open(OUT_NODES, "w", encoding="utf-8") as f:
    for n in nodes_out:
        f.write(json.dumps(n) + "\n")

print("Writing:", OUT_EDGES)
with open(OUT_EDGES, "w", encoding="utf-8") as f:
    for e in edges_out:
        f.write(json.dumps(e) + "\n")

print("DONE")
print("Nodes file:", OUT_NODES)
print("Edges file:", OUT_EDGES)