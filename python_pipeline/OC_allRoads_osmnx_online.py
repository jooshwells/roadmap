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
# This script builds the Orange County driving graph
# directly from OpenStreetMap using OSMnx.
#
# Main things this file does:
# 1. Downloads the driveable road network from OSM
# 2. Keeps extra OSM tags we need for simulation
#    like turn lanes, lighting, traffic lights, and stop signs
# 3. Simplifies the graph so we do not keep every small bend node
# 4. Projects lat/lon into x,y meter coordinates for Unreal
# 5. Exports nodes and edges as JSONL files
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

# Convert miles per hour into meters per second.
# The simulation uses meters, so I convert speeds here before exporting.
def mph_to_mps(mph):
    return mph * 0.44704


# OSM maxspeed values are not always stored the same way.
# Examples can be "45 mph", "45", "80 km/h", or even a list.
# This function tries to pull the number out and convert it to m/s.
def parse_maxspeed_to_mps(val):
    if val is None:
        return None

    # If simplification merged multiple roads, the value can become a list.
    # I just use the first value that can actually be parsed.
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

    # OSM can store km/h in some cases, so handle that too.
    if "km" in s:
        return num / 3.6

    # If no unit is listed, I am assuming mph since this is Florida.
    return mph_to_mps(num)


# Reads a lane count from OSM.
# OSM values are usually simple like "2", but this also handles merged lists.
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


# JSON does not allow NaN values.
# This turns NaN into None so json.dumps writes it as null.
def clean_json_value(val):
    if val is None:
        return None

    try:
        if math.isnan(val):
            return None
    except (TypeError, ValueError):
        pass

    # Pandas/numpy values sometimes need to be converted back to normal Python values.
    if hasattr(val, "item"):
        try:
            return val.item()
        except Exception:
            pass

    return val


# Cleans every value in a dictionary before writing a JSON line.
def clean_json_obj(obj):
    return {k: clean_json_value(v) for k, v in obj.items()}


# When OSMnx simplifies a road, it keeps the road shape as geometry.
# This turns that geometry into x,y points that Unreal can use for splines.
def geometry_to_points(geom):
    if geom is None:
        return None

    if isinstance(geom, LineString):
        return [{"x": float(x), "y": float(y)} for x, y in geom.coords]

    if isinstance(geom, MultiLineString):
        pts = []

        for part in geom.geoms:
            coords = list(part.coords)

            # Avoid duplicating the same point when two line pieces connect.
            if pts and coords:
                last = pts[-1]
                if last["x"] == float(coords[0][0]) and last["y"] == float(coords[0][1]):
                    coords = coords[1:]

            for x, y in coords:
                pts.append({"x": float(x), "y": float(y)})

        return pts if pts else None

    return None


# When OSMnx merges multiple edges into one simplified edge, some tags can become lists.
# For things like turn lanes and lighting, I keep the first non-empty value found.
def first_non_null(values):
    for val in values:
        if val is not None:
            try:
                if pd.notna(val):
                    return val
            except Exception:
                return val
    return None


DEFAULT_SPEED_MPS = mph_to_mps(35)
DEFAULT_LANES = 1


# ---------------------------------------------------
# 2.5) Tell OSMnx to keep extra OSM tags
# ---------------------------------------------------
# OSMnx only keeps a default list of tags. These tags are needed for our
# simulation, so they need to be added before the graph is downloaded.
extra_way_tags = [
    "turn:lanes",
    "turn:lanes:forward",
    "turn:lanes:backward",
    "lit",
]

# highway on nodes is where OSM stores things like traffic_signals and stop signs.
extra_node_tags = [
    "highway",
]

for tag in extra_way_tags:
    if tag not in ox.settings.useful_tags_way:
        ox.settings.useful_tags_way.append(tag)

for tag in extra_node_tags:
    if tag not in ox.settings.useful_tags_node:
        ox.settings.useful_tags_node.append(tag)


# ---------------------------------------------------
# 3) Download road graph from OSMnx
# ---------------------------------------------------
print("Downloading road network for:", PLACE_NAME)

# simplify=False is important because I want to inspect and clean the raw graph first.
# After that, I simplify it myself in a controlled step.
G = ox.graph_from_place(
    PLACE_NAME,
    network_type="drive",
    simplify=False,
)

print("Original graph:")
print("  Nodes:", G.number_of_nodes())
print("  Edges:", G.number_of_edges())


# ---------------------------------------------------
# 3.5) Check if OSMnx loaded the extra tags
# ---------------------------------------------------
# This is mostly a sanity check. If these are all 0, then either OSM does not
# have the data in this area or OSMnx was not told to keep the tags correctly.
turn_count = 0
turn_forward_count = 0
turn_backward_count = 0
lit_count = 0
signal_count = 0
stop_count = 0

for u, v, key, data in G.edges(keys=True, data=True):
    if data.get("turn:lanes") is not None:
        turn_count += 1
    if data.get("turn:lanes:forward") is not None:
        turn_forward_count += 1
    if data.get("turn:lanes:backward") is not None:
        turn_backward_count += 1
    if data.get("lit") is not None:
        lit_count += 1

for node_id, data in G.nodes(data=True):
    if data.get("highway") == "traffic_signals":
        signal_count += 1
    elif data.get("highway") == "stop":
        stop_count += 1

print("Raw OSMnx tag check before simplification:")
print("  turn:lanes:", turn_count)
print("  turn:lanes:forward:", turn_forward_count)
print("  turn:lanes:backward:", turn_backward_count)
print("  lit:", lit_count)
print("  traffic signals:", signal_count)
print("  stop signs:", stop_count)


# ---------------------------------------------------
# 4) Add cleaner attributes we want to export
# ---------------------------------------------------
for u, v, key, data in G.edges(keys=True, data=True):
    # OSMnx already gives road length in meters.
    length = data.get("length", 0.0)
    data["length_m"] = float(length) if length is not None else 0.0

    # Use the OSM maxspeed when available, otherwise use a basic default.
    speed = parse_maxspeed_to_mps(data.get("maxspeed"))
    data["speed_mps"] = float(speed) if speed is not None else DEFAULT_SPEED_MPS

    # Use the OSM lane count when available, otherwise assume one lane.
    lanes = parse_lanes(data.get("lanes"))
    data["lanes"] = int(lanes) if lanes is not None else DEFAULT_LANES


# ---------------------------------------------------
# 4.5) Detect traffic controls before simplification
# ---------------------------------------------------
# Traffic lights and stop signs are stored as OSM nodes. I save them before
# simplification because simplification can remove some original OSM nodes.
traffic_control_map = {}

for node_id, data in G.nodes(data=True):
    highway_tag = data.get("highway")
    traffic_control = None

    if highway_tag == "traffic_signals":
        traffic_control = "signal"
    elif highway_tag == "stop":
        traffic_control = "stop"
    elif highway_tag == "give_way":
        traffic_control = "yield"

    traffic_control_map[node_id] = traffic_control


# ---------------------------------------------------
# 5) Simplify graph
# ---------------------------------------------------
# This removes extra bend nodes from the graph topology.
# The actual road curves are still kept in the edge geometry for Unreal.
H_geo = ox.simplification.simplify_graph(
    G,
    track_merged=True,
    edge_attr_aggs={
        "length": sum,
        "length_m": sum,
        "speed_mps": min,
        "lanes": min,
        "turn:lanes": first_non_null,
        "turn:lanes:forward": first_non_null,
        "turn:lanes:backward": first_non_null,
        "lit": first_non_null,
    },
)

print("Simplified graph:")
print("  Nodes:", H_geo.number_of_nodes())
print("  Edges:", H_geo.number_of_edges())


# ---------------------------------------------------
# 5.5) Apply traffic controls to simplified nodes
# ---------------------------------------------------
# This keeps traffic control info on nodes that survive simplification.
# Some stop signs/signals can still be simplified away, but this keeps the
# data when the original node remains as a graph node.
for node_id, data in H_geo.nodes(data=True):
    data["traffic_control"] = traffic_control_map.get(node_id)


# ---------------------------------------------------
# 5.6) Check tags after simplification
# ---------------------------------------------------
turn_count_after = 0
turn_forward_count_after = 0
turn_backward_count_after = 0
lit_count_after = 0
signal_count_after = 0
stop_count_after = 0

for u, v, key, data in H_geo.edges(keys=True, data=True):
    if data.get("turn:lanes") is not None:
        turn_count_after += 1
    if data.get("turn:lanes:forward") is not None:
        turn_forward_count_after += 1
    if data.get("turn:lanes:backward") is not None:
        turn_backward_count_after += 1
    if data.get("lit") is not None:
        lit_count_after += 1

for node_id, data in H_geo.nodes(data=True):
    if data.get("traffic_control") == "signal":
        signal_count_after += 1
    elif data.get("traffic_control") == "stop":
        stop_count_after += 1
    elif data.get("traffic_control") == "yield": # <-- NEW
        yield_count_after += 1

print("Tag check after simplification:")
print("  turn:lanes:", turn_count_after)
print("  turn:lanes:forward:", turn_forward_count_after)
print("  turn:lanes:backward:", turn_backward_count_after)
print("  lit:", lit_count_after)
print("  traffic signals:", signal_count_after)
print("  stop signs:", stop_count_after)
print("  yield signs:", yield_count_after)


# ---------------------------------------------------
# 6) Save original lat/lon before projection
# ---------------------------------------------------
# Before projection, OSMnx stores x as longitude and y as latitude.
# I save these so the output has both lat/lon and projected x/y.
latlon_map = {}

for osmid, data in H_geo.nodes(data=True):
    latlon_map[osmid] = {
        "lon": float(data["x"]),
        "lat": float(data["y"]),
    }


# ---------------------------------------------------
# 7) Project graph into x,y meters
# ---------------------------------------------------
# Unreal works much better with meter-based x,y values than longitude/latitude.
H = ox.project_graph(H_geo)

print("Projected CRS:", H.graph.get("crs"))


# ---------------------------------------------------
# 8) Convert graph into GeoDataFrames
# ---------------------------------------------------
# GeoDataFrames make it easier to loop through nodes and edges for export.
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

        # Original geographic coordinates.
        "lon": original_geo["lon"],
        "lat": original_geo["lat"],

        # Projected coordinates in meters.
        "x": float(row["x"]),
        "y": float(row["y"]),

        # Intersection control for the simulation team.
        # Possible values right now: "signal", "stop", or null.
        "traffic_control": clean_json_value(row.get("traffic_control")),
    }

    nodes_out.append(clean_json_obj(node_dict))


# ---------------------------------------------------
# 10) Export edges
# ---------------------------------------------------
edges_out = []

for (u, v, key), row in edges_gdf.iterrows():
    # Get the full curve shape of this road segment.
    geom_points = geometry_to_points(row.get("geometry"))

    # If geometry is missing for some reason, at least export start and end points.
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

        # Basic OSM road tags we already used before.
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

        # New simulation-related tags.
        # These come directly from OSM when available. If OSM does not have them,
        # they are exported as null instead of being guessed.
        "turn_lanes": clean_json_value(row.get("turn:lanes")),
        "turn_lanes_forward": clean_json_value(row.get("turn:lanes:forward")),
        "turn_lanes_backward": clean_json_value(row.get("turn:lanes:backward")),
        "lit": clean_json_value(row.get("lit")),

        # Full curve points for Unreal splines.
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
