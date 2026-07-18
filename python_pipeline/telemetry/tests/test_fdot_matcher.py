import json
import sys
from pathlib import Path

import geopandas as gpd
import pandas as pd
import pytest
from shapely.geometry import LineString


PROJECT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_ROOT))

from src.fdot.fdot_option_a_matcher import (
    load_roadmap_network,
    match_edges_to_fdot,
)


def test_network_loader_preserves_real_edge_ids(tmp_path):
    network_path = tmp_path / "network_graph.csv"
    pd.DataFrame({
        "edge_id": [42, 77],
        "source": [1, 2],
        "target": [2, 3],
        "length": [100.0, 120.0],
        "geometry_xy": [
            json.dumps([{"x": 500000, "y": 3150000}, {"x": 500100, "y": 3150000}]),
            json.dumps([{"x": 500100, "y": 3150000}, {"x": 500220, "y": 3150000}]),
        ],
    }).to_csv(network_path, index=False)

    network = load_roadmap_network(network_path)

    assert network["EdgeID"].tolist() == [42, 77]
    assert network["u"].tolist() == [1, 2]
    assert network["length_m"].tolist() == [100.0, 120.0]
    assert all(network.geometry.geom_type == "LineString")


def test_network_loader_falls_back_to_endpoints(tmp_path):
    network_path = tmp_path / "network_graph.csv"
    pd.DataFrame({
        "EdgeID": [5],
        "source_x": [500000.0],
        "source_y": [3150000.0],
        "target_x": [500050.0],
        "target_y": [3150000.0],
    }).to_csv(network_path, index=False)

    network = load_roadmap_network(network_path)

    assert network.iloc[0].geometry.length == pytest.approx(50.0)


def test_nearest_match_respects_distance_and_edge_id():
    roads = gpd.GeoDataFrame(
        {"EdgeID": [12, 34]},
        geometry=[
            LineString([(0, 0), (100, 0)]),
            LineString([(0, 100), (100, 100)]),
        ],
        crs="EPSG:32617",
    )
    fdot = gpd.GeoDataFrame(
        {"FID": [1], "AADT": [24000], "ROADWAY": ["TEST ROAD"]},
        geometry=[LineString([(0, 5), (100, 5)])],
        crs="EPSG:32617",
    )

    matched = match_edges_to_fdot(roads, fdot, max_distance_meters=30)
    by_edge = matched.set_index("EdgeID")

    assert by_edge.loc[12, "fdot_aadt"] == 24000
    assert by_edge.loc[12, "match_distance_m"] == pytest.approx(5.0)
    assert pd.isna(by_edge.loc[34, "fdot_aadt"])


def test_parallel_candidate_beats_perpendicular_zero_distance_crossing():
    roads = gpd.GeoDataFrame(
        {"EdgeID": [91], "highway": ["tertiary"]},
        geometry=[LineString([(0, 0), (100, 0)])],
        crs="EPSG:32617",
    )
    fdot = gpd.GeoDataFrame(
        {
            "FID": [1, 2],
            "AADT": [99999, 15000],
            "ROADWAY": ["CROSSING ROAD", "PARALLEL ROAD"],
        },
        geometry=[
            LineString([(50, -50), (50, 50)]),
            LineString([(0, 8), (100, 8)]),
        ],
        crs="EPSG:32617",
    )

    matched = match_edges_to_fdot(roads, fdot, max_distance_meters=30)

    assert matched.iloc[0]["fdot_aadt"] == 15000
    assert matched.iloc[0]["match_distance_m"] == pytest.approx(8.0)
    assert matched.iloc[0]["match_angle_difference_deg"] == pytest.approx(0.0)


def test_minor_road_does_not_inherit_distant_parallel_corridor_count():
    roads = gpd.GeoDataFrame(
        {"EdgeID": [92], "highway": ["residential"]},
        geometry=[LineString([(0, 0), (100, 0)])],
        crs="EPSG:32617",
    )
    fdot = gpd.GeoDataFrame(
        {"FID": [3], "AADT": [50000]},
        geometry=[LineString([(0, 20), (100, 20)])],
        crs="EPSG:32617",
    )

    matched = match_edges_to_fdot(roads, fdot, max_distance_meters=30)

    assert pd.isna(matched.iloc[0]["fdot_aadt"])
