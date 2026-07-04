import json
import math
from pathlib import Path


# This test file checks the JSONL files created by the online OSMnx pipeline.
# It does not rebuild the graph. It only checks the output files after the script runs.
BASE_DIR = Path(__file__).resolve().parents[1]

# Change this if your output folder has a different name.
OUT_DIR = BASE_DIR / "out"

# These match the current online output file names.
NODES_FILE = OUT_DIR / "nodes_orange_allroads_online_xy.jsonl"
EDGES_FILE = OUT_DIR / "edges_orange_allroads_online_xy.jsonl"


# Loads a JSONL file, where each line is its own JSON object.
def load_jsonl(path):
    records = []

    with open(path, "r", encoding="utf-8") as file:
        for line_number, line in enumerate(file, start=1):
            line = line.strip()

            if line:
                try:
                    records.append(json.loads(line))
                except json.JSONDecodeError as error:
                    raise AssertionError(f"Invalid JSON on line {line_number} in {path}") from error

    return records


# Recursively checks if any value is NaN.
# This matters because NaN is not valid JSON for the C++ side.
def has_nan(value):
    if isinstance(value, float):
        return math.isnan(value)

    if isinstance(value, dict):
        return any(has_nan(v) for v in value.values())

    if isinstance(value, list):
        return any(has_nan(v) for v in value)

    return False


# Small helper so tests can check if a field has at least one real value.
def count_non_null(records, field_name):
    return sum(1 for record in records if record.get(field_name) is not None)


def test_output_files_exist():
    assert NODES_FILE.exists(), f"Missing nodes file: {NODES_FILE}"
    assert EDGES_FILE.exists(), f"Missing edges file: {EDGES_FILE}"


def test_nodes_file_is_not_empty():
    nodes = load_jsonl(NODES_FILE)
    assert len(nodes) > 0


def test_edges_file_is_not_empty():
    edges = load_jsonl(EDGES_FILE)
    assert len(edges) > 0


def test_node_required_fields_exist():
    nodes = load_jsonl(NODES_FILE)

    # traffic_control was added for traffic lights and stop signs.
    required_fields = {"id", "lon", "lat", "x", "y", "traffic_control"}

    for node in nodes:
        assert required_fields.issubset(node.keys())


def test_edge_required_fields_exist():
    edges = load_jsonl(EDGES_FILE)

    # These are the fields the simulation/Unreal side expects to be present.
    required_fields = {
        "u",
        "v",
        "length_m",
        "speed_mps",
        "lanes",
        "oneway",
        "highway",
        "geometry_xy",
        "turn_lanes",
        "turn_lanes_forward",
        "turn_lanes_backward",
        "lit",
    }

    for edge in edges:
        assert required_fields.issubset(edge.keys())


def test_edges_reference_existing_nodes():
    nodes = load_jsonl(NODES_FILE)
    edges = load_jsonl(EDGES_FILE)

    node_ids = {node["id"] for node in nodes}

    for edge in edges:
        assert edge["u"] in node_ids
        assert edge["v"] in node_ids


def test_edge_lengths_are_positive():
    edges = load_jsonl(EDGES_FILE)

    for edge in edges:
        assert edge["length_m"] > 0


def test_speeds_are_positive():
    edges = load_jsonl(EDGES_FILE)

    for edge in edges:
        assert edge["speed_mps"] > 0


def test_lanes_are_positive():
    edges = load_jsonl(EDGES_FILE)

    for edge in edges:
        assert edge["lanes"] >= 1


def test_no_nan_values_in_nodes_or_edges():
    nodes = load_jsonl(NODES_FILE)
    edges = load_jsonl(EDGES_FILE)

    for node in nodes:
        assert not has_nan(node), f"NaN found in node: {node}"

    for edge in edges:
        assert not has_nan(edge), f"NaN found in edge: {edge}"


def test_geometry_xy_has_valid_points():
    edges = load_jsonl(EDGES_FILE)

    for edge in edges:
        points = edge["geometry_xy"]

        assert isinstance(points, list)
        assert len(points) >= 2

        for point in points:
            assert "x" in point
            assert "y" in point
            assert isinstance(point["x"], (int, float))
            assert isinstance(point["y"], (int, float))


def test_traffic_control_values_are_valid():
    nodes = load_jsonl(NODES_FILE)

    # Right now the pipeline only exports signal, stop, or null.
    # This catches typos like "signals" or "stops".
    allowed_values = {None, "signal", "stop"}

    for node in nodes:
        assert node.get("traffic_control") in allowed_values


def test_turn_lane_fields_are_strings_or_null():
    edges = load_jsonl(EDGES_FILE)

    turn_fields = [
        "turn_lanes",
        "turn_lanes_forward",
        "turn_lanes_backward",
    ]

    for edge in edges:
        for field in turn_fields:
            value = edge.get(field)
            assert value is None or isinstance(value, str)


def test_lit_field_is_string_or_null():
    edges = load_jsonl(EDGES_FILE)

    for edge in edges:
        value = edge.get("lit")
        assert value is None or isinstance(value, str)


def test_some_osm_extra_tags_were_exported():
    nodes = load_jsonl(NODES_FILE)
    edges = load_jsonl(EDGES_FILE)

    # This confirms the new OSM tags are not just fields full of null values.
    # If this fails, the graph may not be keeping useful_tags_way/useful_tags_node.
    assert count_non_null(edges, "turn_lanes") > 0
    assert count_non_null(edges, "lit") > 0
    assert count_non_null(nodes, "traffic_control") > 0
