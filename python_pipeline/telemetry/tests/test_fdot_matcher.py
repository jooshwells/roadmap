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
    ensure_fdot_mapping,
    has_geographic_coordinates,
    load_fdot_segments,
    load_roadmap_network,
    match_edges_to_fdot,
)
from src.fdot import fdot_option_a_matcher


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


def _write_nodes(path: Path, longitude: float, latitude: float) -> None:
    path.write_text(
        json.dumps({"id": 1, "lon": longitude, "lat": latitude}) + "\n",
        encoding="utf-8",
    )


def test_geographic_coordinate_check_skips_synthetic_maps(tmp_path):
    real_nodes = tmp_path / "real_nodes.jsonl"
    synthetic_nodes = tmp_path / "synthetic_nodes.jsonl"
    _write_nodes(real_nodes, -81.38, 28.54)
    _write_nodes(synthetic_nodes, 0.0, 0.0)

    assert has_geographic_coordinates(real_nodes)
    assert not has_geographic_coordinates(synthetic_nodes)


def test_fdot_geojson_loads_without_gdal_file_reader(tmp_path, monkeypatch):
    fdot_path = tmp_path / "fdot.geojson"
    fdot_path.write_text(json.dumps({
        "type": "FeatureCollection",
        "features": [{
            "type": "Feature",
            "properties": {"AADT": 12000, "COUNTY": "Orange"},
            "geometry": {
                "type": "LineString",
                "coordinates": [[-81.38, 28.54], [-81.379, 28.54]],
            },
        }],
    }), encoding="utf-8")

    monkeypatch.setattr(gpd, "read_file", lambda *args, **kwargs: pytest.fail("GDAL reader used"))
    segments = load_fdot_segments(fdot_path, "EPSG:32617")

    assert len(segments) == 1
    assert segments.iloc[0]["AADT"] == 12000
    assert segments.crs.to_string() == "EPSG:32617"


def test_ensure_mapping_generates_then_reuses_matching_cache(tmp_path, monkeypatch):
    network_path = tmp_path / "network_graph.csv"
    nodes_path = tmp_path / "nodes.jsonl"
    fdot_path = tmp_path / "fdot.geojson"
    cache_dir = tmp_path / "cache"
    first_run_path = tmp_path / "run_one" / "fdot_edge_mapping.csv"
    second_run_path = tmp_path / "run_two" / "fdot_edge_mapping.csv"
    network_path.write_text("EdgeID\n1\n", encoding="utf-8")
    fdot_path.write_text("fdot-v1", encoding="utf-8")
    _write_nodes(nodes_path, -81.38, 28.54)
    calls = []

    def fake_create_fdot_mapping(**kwargs):
        calls.append(kwargs)
        Path(kwargs["output_path"]).write_text("EdgeID,fdot_aadt\n1,12000\n", encoding="utf-8")
        return {"total_edges": 1, "matched_edges": 1}

    monkeypatch.setattr(fdot_option_a_matcher, "create_fdot_mapping", fake_create_fdot_mapping)

    generated = ensure_fdot_mapping(
        network_path, nodes_path, fdot_path, cache_dir, first_run_path, "test-map"
    )
    reused = ensure_fdot_mapping(
        network_path, nodes_path, fdot_path, cache_dir, second_run_path, "test-map"
    )

    assert generated["status"] == "generated"
    assert reused["status"] == "reused"
    assert len(calls) == 1
    assert first_run_path.read_text(encoding="utf-8") == second_run_path.read_text(encoding="utf-8")


def test_ensure_mapping_regenerates_when_map_changes(tmp_path, monkeypatch):
    network_path = tmp_path / "network_graph.csv"
    nodes_path = tmp_path / "nodes.jsonl"
    fdot_path = tmp_path / "fdot.geojson"
    cache_dir = tmp_path / "cache"
    network_path.write_text("EdgeID\n1\n", encoding="utf-8")
    fdot_path.write_text("fdot-v1", encoding="utf-8")
    _write_nodes(nodes_path, -81.38, 28.54)
    calls = []

    def fake_create_fdot_mapping(**kwargs):
        calls.append(kwargs)
        Path(kwargs["output_path"]).write_text(f"version-{len(calls)}", encoding="utf-8")
        return {"matched_edges": len(calls)}

    monkeypatch.setattr(fdot_option_a_matcher, "create_fdot_mapping", fake_create_fdot_mapping)

    ensure_fdot_mapping(
        network_path, nodes_path, fdot_path, cache_dir,
        tmp_path / "run_one" / "mapping.csv", "test-map",
    )
    network_path.write_text("EdgeID\n1\n2\n", encoding="utf-8")
    result = ensure_fdot_mapping(
        network_path, nodes_path, fdot_path, cache_dir,
        tmp_path / "run_two" / "mapping.csv", "test-map",
    )

    assert result["status"] == "generated"
    assert len(calls) == 2


def test_ensure_mapping_skips_map_without_real_coordinates(tmp_path, monkeypatch):
    network_path = tmp_path / "network_graph.csv"
    nodes_path = tmp_path / "nodes.jsonl"
    fdot_path = tmp_path / "fdot.geojson"
    output_path = tmp_path / "run" / "mapping.csv"
    network_path.write_text("EdgeID\n1\n", encoding="utf-8")
    fdot_path.write_text("fdot-v1", encoding="utf-8")
    _write_nodes(nodes_path, 0.0, 0.0)

    def unexpected_match(**kwargs):
        pytest.fail("Synthetic maps should not run the FDOT spatial matcher.")

    monkeypatch.setattr(fdot_option_a_matcher, "create_fdot_mapping", unexpected_match)
    result = ensure_fdot_mapping(
        network_path, nodes_path, fdot_path, tmp_path / "cache", output_path, "grid-city"
    )

    assert result["status"] == "skipped"
    assert not output_path.exists()
