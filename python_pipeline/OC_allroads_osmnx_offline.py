from pathlib import Path
import json
import math
import re

import pandas as pd
import osmnx as ox
from shapely.geometry import LineString, MultiLineString

# ---------------------------------------------------
# OFFLINE VERSION
#
# This version replaces Old_motorway_full_pipeline_no_osmnx.py as
# it is a full rework using osmnx
#
# This version reads from a local .osm.bz2 file
# instead of downloading from OSM online.
#
# It still does the same 2 important extra things:
# 1. Converts lat/lon into projected x,y coordinates
# 2. Saves the curve points of each road segment
#    for Unreal spline generation
# ---------------------------------------------------


# ---------------------------------------------------
# 1) File paths
# ---------------------------------------------------
BASE_DIR = Path(r"C:\Users\danie\OneDrive\Desktop\School\Spring 2026\SD1\OSM")

XML_PATH = BASE_DIR / "orange_drive_roads_clean-260208.osm.bz2"

OUT_NODES = BASE_DIR / "out" / "nodes_orange_allroads_offline_xy.jsonl"
OUT_EDGES = BASE_DIR / "out" / "edges_orange_allroads_offline_xy.jsonl"


# ---------------------------------------------------
# 2) Helper functions
# ---------------------------------------------------

# convert mph to meters per second
def mph_to_mps(mph):
    return mph * 0.44704


# reads maxspeed from OSM text
def parse_maxspeed_to_mps(val):
    if val is None:
        return None

    if isinstance(val, list):
        for item in val:
            parsed = parse_maxspeed_to_mps(item)
            if parsed is not None:
                return parsed
        return None

    s = str(val).lower()
    m = re.search(r"\d+(\.\d+)?", s)

    if not m:
        return None

    num = float(m.group())

    if "km" in s:
        return num / 3.6

    return mph_to_mps(num)


# reads lane count
def parse_lanes(val):
    if val is None:
        return None

    if isinstance(val, list):
        for item in val:
            parsed = parse_lanes(item)
            if parsed is not None:
                return parsed
        return None

    m = re.search(r"\d+", str(val))

    if not m:
        return None

    return int(m.group())


# make values safe for json
def clean_json_value(val):
    if val is None:
        return None

    try:
        if math.isnan(val):
            return None
    except (TypeError, ValueError):
        pass

    if hasattr(val, "item"):
        try:
            return val.item()
        except Exception:
            pass

    return val


# clean a whole object before writing
def clean_json_obj(obj):
    return {k: clean_json_value(v) for k, v in obj.items()}


# turns edge geometry into ordered x,y points
def geometry_to_points(geom):
    if geom is None:
        return None

    if isinstance(geom, LineString):
        return [{"x": float(x), "y": float(y)} for x, y in geom.coords]

    if isinstance(geom, MultiLineString):
        pts = []

        for part in geom.geoms:
            coords = list(part.coords)

            if pts and coords:
                last = pts[-1]
                if last["x"] == float(coords[0][0]) and last["y"] == float(coords[0][1]):
                    coords = coords[1:]

            for x, y in coords:
                pts.append({"x": float(x), "y": float(y)})

        return pts if pts else None

    return None


DEFAULT_SPEED_MPS = mph_to_mps(35)
DEFAULT_LANES = 1


# ---------------------------------------------------
# 3) Read local XML graph
# ---------------------------------------------------
print("Reading local file:", XML_PATH)

# simplify=False first so we control simplification later
G = ox.graph_from_xml(
    XML_PATH,
    simplify=False,
    retain_all=True,
)

print("Original graph:")
print("  Nodes:", G.number_of_nodes())
print("  Edges:", G.number_of_edges())


# ---------------------------------------------------
# 4) Add cleaned attributes we want
# ---------------------------------------------------
for u, v, key, data in G.edges(keys=True, data=True):
    length = data.get("length", 0.0)
    data["length_m"] = float(length) if length is not None else 0.0

    speed = parse_maxspeed_to_mps(data.get("maxspeed"))
    data["speed_mps"] = float(speed) if speed is not None else DEFAULT_SPEED_MPS

    lanes = parse_lanes(data.get("lanes"))
    data["lanes"] = int(lanes) if lanes is not None else DEFAULT_LANES


# ---------------------------------------------------
# 5) Simplify graph
#
# Removes extra bend nodes for the simulation graph,
# while still keeping edge geometry for road curves.
# ---------------------------------------------------
H_geo = ox.simplification.simplify_graph(
    G,
    track_merged=True,
    edge_attr_aggs={
        "length": sum,
        "length_m": sum,
        "speed_mps": min,
        "lanes": min,
    },
)

print("Simplified graph:")
print("  Nodes:", H_geo.number_of_nodes())
print("  Edges:", H_geo.number_of_edges())


# ---------------------------------------------------
# 6) Save original lat/lon before projection
# ---------------------------------------------------
latlon_map = {}

for osmid, data in H_geo.nodes(data=True):
    latlon_map[osmid] = {
        "lon": float(data["x"]),
        "lat": float(data["y"]),
    }


# ---------------------------------------------------
# 7) Project graph into x,y meters
# ---------------------------------------------------
H = ox.project_graph(H_geo)

print("Projected CRS:", H.graph.get("crs"))


# ---------------------------------------------------
# 8) Convert to GeoDataFrames
# ---------------------------------------------------
nodes_gdf, edges_gdf = ox.graph_to_gdfs(H, nodes=True, edges=True)


# ---------------------------------------------------
# 9) Export nodes
# ---------------------------------------------------
node_id_map = {}
nodes_out = []

for new_id, (osmid, row) in enumerate(nodes_gdf.iterrows(), start=1):
    node_id_map[osmid] = new_id

    original_geo = latlon_map[osmid]

    node_dict = {
        "id": new_id,

        # original geographic values
        "lon": original_geo["lon"],
        "lat": original_geo["lat"],

        # projected values in meters
        "x": float(row["x"]),
        "y": float(row["y"]),
    }

    nodes_out.append(clean_json_obj(node_dict))


# ---------------------------------------------------
# 10) Export edges
# ---------------------------------------------------
edges_out = []

for (u, v, key), row in edges_gdf.iterrows():
    geom_points = geometry_to_points(row.get("geometry"))

    # fallback if geometry is missing
    if not geom_points:
        geom_points = [
            {"x": float(nodes_gdf.loc[u]["x"]), "y": float(nodes_gdf.loc[u]["y"])},
            {"x": float(nodes_gdf.loc[v]["x"]), "y": float(nodes_gdf.loc[v]["y"])},
        ]

    edge_dict = {
        "u": node_id_map[u],
        "v": node_id_map[v],
        "length_m": round(float(row.get("length_m", row.get("length", 0.0))), 3),
        "speed_mps": round(float(row.get("speed_mps", DEFAULT_SPEED_MPS)), 3),
        "lanes": int(row.get("lanes", DEFAULT_LANES))
            if pd.notna(row.get("lanes", DEFAULT_LANES))
            else DEFAULT_LANES,
        "oneway": clean_json_value(row.get("oneway")),
        "highway": clean_json_value(row.get("highway")),
        "name": clean_json_value(row.get("name")),
        "ref": clean_json_value(row.get("ref")),
        "bridge": clean_json_value(row.get("bridge")),
        "tunnel": clean_json_value(row.get("tunnel")),
        "layer": clean_json_value(row.get("layer")),
        "surface": clean_json_value(row.get("surface")),
        "access": clean_json_value(row.get("access")),
        "motor_vehicle": clean_json_value(row.get("motor_vehicle")),
        "service": clean_json_value(row.get("service")),
        "source_feature_id": clean_json_value(row.get("osmid")),

        # full curve points for Unreal splines
        "geometry_xy": geom_points,
    }

    edges_out.append(clean_json_obj(edge_dict))


# ---------------------------------------------------
# 11) Write node file
# ---------------------------------------------------
print("Writing:", OUT_NODES)

with open(OUT_NODES, "w", encoding="utf-8") as f:
    for n in nodes_out:
        f.write(json.dumps(n, allow_nan=False) + "\n")


# ---------------------------------------------------
# 12) Write edge file
# ---------------------------------------------------
print("Writing:", OUT_EDGES)

with open(OUT_EDGES, "w", encoding="utf-8") as f:
    for e in edges_out:
        f.write(json.dumps(e, allow_nan=False) + "\n")


print("DONE")
print("Nodes file:", OUT_NODES)
print("Edges file:", OUT_EDGES)