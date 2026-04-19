from pathlib import Path
import json
import math
import re

import pandas as pd
import osmnx as ox
from shapely.geometry import LineString, MultiLineString

# ---------------------------------------------------
# ONLINE VERSION
#
# This version replaces Old_motorway_full_pipeline_no_osmnx.py as
# it is a full rework using osmnx
#
# This version pulls the road network straight from OSM
# using OSMnx.
#
# It also does 2 important extra things:
# 1. Converts lat/lon into projected x,y coordinates
#    so Unreal can place roads more easily
# 2. Saves the curve points of each road segment
#    so Unreal can make curved splines instead of
#    straight roads only
# ---------------------------------------------------


# ---------------------------------------------------
# 1) File paths / area
# ---------------------------------------------------
BASE_DIR = Path(r"C:\Users\danie\OneDrive\Desktop\School\Spring 2026\SD1\OSM")

PLACE_NAME = "Orange County, Florida, USA"

OUT_NODES = BASE_DIR / "out" / "nodes_orange_allroads_online_xy.jsonl"
OUT_EDGES = BASE_DIR / "out" / "edges_orange_allroads_online_xy.jsonl"


# ---------------------------------------------------
# 2) Helper functions
# ---------------------------------------------------

# convert mph to meters per second
def mph_to_mps(mph):
    return mph * 0.44704


# tries to read maxspeed values from OSM
# examples: "45 mph", "60", "80 km/h"
def parse_maxspeed_to_mps(val):
    if val is None:
        return None

    # sometimes merged roads store values in a list
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

    # if units are km/h, convert from km/h
    if "km" in s:
        return num / 3.6

    # otherwise assume mph
    return mph_to_mps(num)


# tries to read lane count from OSM
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


# makes values safe for json export
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


# cleans a whole dictionary before export
def clean_json_obj(obj):
    return {k: clean_json_value(v) for k, v in obj.items()}


# turns shapely geometry into list of x,y points
# Unreal can use this list for spline points
def geometry_to_points(geom):
    if geom is None:
        return None

    # normal case: one line
    if isinstance(geom, LineString):
        return [{"x": float(x), "y": float(y)} for x, y in geom.coords]

    # sometimes geometry comes as multiple joined lines
    if isinstance(geom, MultiLineString):
        pts = []

        for part in geom.geoms:
            coords = list(part.coords)

            # avoid repeating the same point twice at joins
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
# 3) Download road graph from OSMnx
# ---------------------------------------------------
print("Downloading road network for:", PLACE_NAME)

# simplify=False first so we can do our own steps after
G = ox.graph_from_place(
    PLACE_NAME,
    network_type="drive",
    simplify=False
)

print("Original graph:")
print("  Nodes:", G.number_of_nodes())
print("  Edges:", G.number_of_edges())


# ---------------------------------------------------
# 4) Add cleaner attributes we want to export
# ---------------------------------------------------
for u, v, key, data in G.edges(keys=True, data=True):
    # OSMnx already gives road length in meters
    length = data.get("length", 0.0)
    data["length_m"] = float(length) if length is not None else 0.0

    # read speed if available, else use default
    speed = parse_maxspeed_to_mps(data.get("maxspeed"))
    data["speed_mps"] = float(speed) if speed is not None else DEFAULT_SPEED_MPS

    # read lanes if available, else use default
    lanes = parse_lanes(data.get("lanes"))
    data["lanes"] = int(lanes) if lanes is not None else DEFAULT_LANES


# ---------------------------------------------------
# 5) Simplify graph
#
# This removes useless bend nodes for the simulation
# graph, but OSMnx still keeps the curve geometry on
# the merged edge, which is what we want for Unreal.
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
#
# Before projection:
# x = longitude
# y = latitude
# ---------------------------------------------------
latlon_map = {}

for osmid, data in H_geo.nodes(data=True):
    latlon_map[osmid] = {
        "lon": float(data["x"]),
        "lat": float(data["y"]),
    }


# ---------------------------------------------------
# 7) Project graph into x,y meters
#
# This is much better for Unreal than using degrees.
# ---------------------------------------------------
H = ox.project_graph(H_geo)

print("Projected CRS:", H.graph.get("crs"))


# ---------------------------------------------------
# 8) Convert graph into GeoDataFrames
#
# This makes exporting easier.
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

        # original geographic coordinates
        "lon": original_geo["lon"],
        "lat": original_geo["lat"],

        # projected coordinates in meters
        "x": float(row["x"]),
        "y": float(row["y"]),
    }

    nodes_out.append(clean_json_obj(node_dict))


# ---------------------------------------------------
# 10) Export edges
# ---------------------------------------------------
edges_out = []

for (u, v, key), row in edges_gdf.iterrows():
    # get the full curve shape of this road
    geom_points = geometry_to_points(row.get("geometry"))

    # if geometry is missing, at least use start/end
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

        # this is the big new field for Unreal splines
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