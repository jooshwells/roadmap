"""Match RoadMap network edges to the nearest FDOT AADT segments."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

import geopandas as gpd
import pandas as pd
from shapely.geometry import LineString


BASE_DIR = Path(__file__).resolve().parents[2]
DEFAULT_FDOT_FILE = BASE_DIR / "data" / "fdot" / "Annual_Average_Daily_Traffic_TDA_-2830269347537693947.geojson"
DEFAULT_NETWORK_CRS = "EPSG:32617"
DEFAULT_MATCH_DISTANCE_METERS = 30.0
DEFAULT_MAX_ANGLE_DIFFERENCE = 30.0
MINOR_ROAD_MAX_DISTANCE_METERS = 8.0
MINOR_ROAD_MAX_ANGLE_DIFFERENCE = 15.0
MINOR_ROAD_TYPES = {"residential", "unclassified", "service", "living_street"}


def _clean_value(value):
    if value is None or (not isinstance(value, (list, dict)) and pd.isna(value)):
        return None
    return value


def _parse_geometry(value, row: pd.Series) -> LineString | None:
    points = None
    if isinstance(value, str) and value.strip():
        try:
            points = json.loads(value)
        except json.JSONDecodeError:
            points = None
    elif isinstance(value, list):
        points = value

    coordinates = []
    for point in points or []:
        if isinstance(point, dict) and "x" in point and "y" in point:
            coordinates.append((float(point["x"]), float(point["y"])))
        elif isinstance(point, (list, tuple)) and len(point) >= 2:
            coordinates.append((float(point[0]), float(point[1])))

    if len(coordinates) < 2:
        endpoint_columns = ("source_x", "source_y", "target_x", "target_y")
        if all(column in row.index and pd.notna(row[column]) for column in endpoint_columns):
            coordinates = [
                (float(row["source_x"]), float(row["source_y"])),
                (float(row["target_x"]), float(row["target_y"])),
            ]

    if len(coordinates) < 2:
        return None
    return LineString(coordinates)


def load_roadmap_network(network_path: Path, network_crs: str = DEFAULT_NETWORK_CRS) -> gpd.GeoDataFrame:
    """Load a run's network_graph.csv while preserving its authoritative EdgeID."""
    network_path = Path(network_path)
    if not network_path.exists():
        raise FileNotFoundError(f"RoadMap network graph was not found: {network_path}")

    network = pd.read_csv(network_path)
    edge_id_column = "EdgeID" if "EdgeID" in network.columns else "edge_id" if "edge_id" in network.columns else None
    if edge_id_column is None:
        raise ValueError("network_graph.csv must contain EdgeID or edge_id.")

    network["EdgeID"] = pd.to_numeric(network[edge_id_column], errors="coerce")
    network["geometry"] = network.apply(
        lambda row: _parse_geometry(row.get("geometry_xy"), row),
        axis=1,
    )
    network = network.dropna(subset=["EdgeID", "geometry"]).copy()
    network["EdgeID"] = network["EdgeID"].astype(int)
    network = network.drop_duplicates(subset=["EdgeID"], keep="first")

    rename_map = {
        "source": "u",
        "target": "v",
        "length": "length_m",
    }
    for source, destination in rename_map.items():
        if source in network.columns and destination not in network.columns:
            network = network.rename(columns={source: destination})

    return gpd.GeoDataFrame(network, geometry="geometry", crs=network_crs)


def load_fdot_segments(fdot_path: Path, target_crs, county: str = "Orange") -> gpd.GeoDataFrame:
    """Load the FDOT layer, validate AADT, filter county, and project to the map CRS."""
    fdot_path = Path(fdot_path)
    if not fdot_path.exists():
        raise FileNotFoundError(f"FDOT GeoJSON was not found: {fdot_path}")

    fdot = gpd.read_file(fdot_path)
    if "AADT" not in fdot.columns:
        raise ValueError("FDOT GeoJSON must contain an AADT field.")
    if fdot.crs is None:
        raise ValueError("FDOT GeoJSON does not declare a coordinate reference system.")

    if county and "COUNTY" in fdot.columns:
        fdot = fdot[
            fdot["COUNTY"].astype(str).str.contains(county, case=False, na=False)
        ].copy()

    fdot["AADT"] = pd.to_numeric(fdot["AADT"], errors="coerce")
    fdot = fdot.dropna(subset=["AADT", "geometry"])
    return fdot.to_crs(target_crs)


def line_direction_degrees(geometry: LineString) -> float:
    """Return an undirected 0-180 degree bearing using the line endpoints."""
    start = geometry.coords[0]
    end = geometry.coords[-1]
    return math.degrees(math.atan2(end[1] - start[1], end[0] - start[0])) % 180.0


def angle_difference_degrees(first: LineString, second: LineString) -> float:
    """Return the smallest direction difference for undirected road lines."""
    difference = abs(line_direction_degrees(first) - line_direction_degrees(second))
    return min(difference, 180.0 - difference)


def match_edges_to_fdot(
    roadmap_edges: gpd.GeoDataFrame,
    fdot_segments: gpd.GeoDataFrame,
    max_distance_meters: float = DEFAULT_MATCH_DISTANCE_METERS,
    max_angle_difference: float = DEFAULT_MAX_ANGLE_DIFFERENCE,
) -> gpd.GeoDataFrame:
    """Return one nearest FDOT result for every RoadMap edge."""
    if max_distance_meters <= 0:
        raise ValueError("max_distance_meters must be greater than zero.")
    if roadmap_edges.crs is None or fdot_segments.crs is None:
        raise ValueError("Both RoadMap and FDOT geometries must declare a CRS.")
    if roadmap_edges.crs != fdot_segments.crs:
        fdot_segments = fdot_segments.to_crs(roadmap_edges.crs)
    if not 0 < max_angle_difference <= 90:
        raise ValueError("max_angle_difference must be between 0 and 90 degrees.")

    fdot_columns = [
        "FID", "YEAR_", "ROADWAY", "DESC_FRM", "DESC_TO", "AADT",
        "AADTFLG", "KFCTR", "KFLG", "DFCTR", "DFLG",
        "BEGIN_POST", "END_POST", "Shape_Leng", "geometry",
    ]
    fdot_columns = [column for column in fdot_columns if column in fdot_segments.columns]
    fdot_small = fdot_segments[fdot_columns].reset_index(drop=True)
    spatial_index = fdot_small.sindex
    matched_rows = []

    # Distance alone is unsafe at intersections: a perpendicular street has
    # distance zero to a counted corridor. Require similar road direction and
    # then choose the closest aligned FDOT segment.
    for _, road in roadmap_edges.iterrows():
        road_geometry = road.geometry
        highway_type = str(_clean_value(road.get("highway")) or "").split(";")[0].strip().lower()
        is_minor_road = not highway_type or highway_type in MINOR_ROAD_TYPES
        road_distance_limit = (
            min(max_distance_meters, MINOR_ROAD_MAX_DISTANCE_METERS)
            if is_minor_road else max_distance_meters
        )
        road_angle_limit = (
            min(max_angle_difference, MINOR_ROAD_MAX_ANGLE_DIFFERENCE)
            if is_minor_road else max_angle_difference
        )
        candidate_positions = spatial_index.query(
            road_geometry.buffer(road_distance_limit),
            predicate="intersects",
        )
        best_candidate = None
        best_score = None

        for position in candidate_positions:
            fdot_row = fdot_small.iloc[int(position)]
            fdot_geometry = fdot_row.geometry
            distance = float(road_geometry.distance(fdot_geometry))
            angle_difference = angle_difference_degrees(road_geometry, fdot_geometry)
            if distance > road_distance_limit or angle_difference > road_angle_limit:
                continue

            # Distance remains the primary quality measure. The small angular
            # penalty breaks ties between nearby parallel/crossing candidates.
            score = distance + angle_difference * 0.25
            if best_score is None or score < best_score:
                best_score = score
                best_candidate = (fdot_row, distance, angle_difference)

        result = road.to_dict()
        if best_candidate is not None:
            fdot_row, distance, angle_difference = best_candidate
            for column in fdot_columns:
                if column != "geometry":
                    result[column] = fdot_row[column]
            result["match_distance_m"] = distance
            result["match_angle_difference_deg"] = angle_difference
        else:
            for column in fdot_columns:
                if column != "geometry":
                    result[column] = pd.NA
            result["match_distance_m"] = pd.NA
            result["match_angle_difference_deg"] = pd.NA
        matched_rows.append(result)

    matched = gpd.GeoDataFrame(matched_rows, geometry="geometry", crs=roadmap_edges.crs)

    matched = matched.rename(columns={
        "FID": "fdot_fid",
        "YEAR_": "fdot_year",
        "ROADWAY": "fdot_roadway",
        "DESC_FRM": "fdot_from",
        "DESC_TO": "fdot_to",
        "AADT": "fdot_aadt",
        "AADTFLG": "fdot_aadt_flag",
        "KFCTR": "fdot_k_factor_percent",
        "KFLG": "fdot_k_factor_flag",
        "DFCTR": "fdot_d_factor_percent",
        "DFLG": "fdot_d_factor_flag",
        "BEGIN_POST": "fdot_begin_post",
        "END_POST": "fdot_end_post",
        "Shape_Leng": "fdot_shape_length",
    })
    if "index_right" in matched.columns:
        matched = matched.drop(columns=["index_right"])
    return matched


def mapping_columns(matched: pd.DataFrame) -> list[str]:
    preferred = [
        "EdgeID", "u", "v", "name", "ref", "highway", "length_m",
        "fdot_aadt", "fdot_roadway", "fdot_from", "fdot_to", "fdot_year",
        "fdot_aadt_flag", "fdot_k_factor_percent", "fdot_k_factor_flag",
        "fdot_d_factor_percent", "fdot_d_factor_flag",
        "match_distance_m", "match_angle_difference_deg",
    ]
    return [column for column in preferred if column in matched.columns]


def create_fdot_mapping(
    network_path: Path,
    fdot_path: Path,
    output_path: Path,
    max_distance_meters: float = DEFAULT_MATCH_DISTANCE_METERS,
    max_angle_difference: float = DEFAULT_MAX_ANGLE_DIFFERENCE,
    network_crs: str = DEFAULT_NETWORK_CRS,
    county: str = "Orange",
) -> dict:
    """Generate a map-specific CSV and return its quality summary."""
    roadmap = load_roadmap_network(network_path, network_crs)
    fdot = load_fdot_segments(fdot_path, roadmap.crs, county)
    matched = match_edges_to_fdot(
        roadmap,
        fdot,
        max_distance_meters,
        max_angle_difference,
    )

    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    matched[mapping_columns(matched)].to_csv(output_path, index=False)

    matched_mask = matched["fdot_aadt"].notna()
    distances = matched.loc[matched_mask, "match_distance_m"].dropna()
    angles = matched.loc[matched_mask, "match_angle_difference_deg"].dropna()
    total_edges = int(len(matched))
    matched_edges = int(matched_mask.sum())
    return {
        "network_path": str(network_path),
        "fdot_path": str(fdot_path),
        "output_path": str(output_path),
        "total_edges": total_edges,
        "matched_edges": matched_edges,
        "unmatched_edges": total_edges - matched_edges,
        "matched_percent": round(matched_edges / total_edges * 100.0, 2) if total_edges else 0.0,
        "mean_match_distance_m": round(float(distances.mean()), 2) if not distances.empty else None,
        "max_match_distance_m": round(float(distances.max()), 2) if not distances.empty else None,
        "mean_angle_difference_deg": round(float(angles.mean()), 2) if not angles.empty else None,
        "max_angle_difference_deg": round(float(angles.max()), 2) if not angles.empty else None,
        "distance_limit_m": float(max_distance_meters),
        "angle_limit_deg": float(max_angle_difference),
        "minor_road_distance_limit_m": MINOR_ROAD_MAX_DISTANCE_METERS,
        "minor_road_angle_limit_deg": MINOR_ROAD_MAX_ANGLE_DIFFERENCE,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Match a RoadMap network_graph.csv to FDOT AADT segments.")
    parser.add_argument("--network", type=Path, required=True)
    parser.add_argument("--fdot", type=Path, default=DEFAULT_FDOT_FILE)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--max-distance", type=float, default=DEFAULT_MATCH_DISTANCE_METERS)
    parser.add_argument("--max-angle", type=float, default=DEFAULT_MAX_ANGLE_DIFFERENCE)
    parser.add_argument("--network-crs", default=DEFAULT_NETWORK_CRS)
    parser.add_argument("--county", default="Orange")
    args = parser.parse_args()

    summary = create_fdot_mapping(
        args.network,
        args.fdot,
        args.output,
        args.max_distance,
        args.max_angle,
        args.network_crs,
        args.county,
    )
    print(json.dumps({"success": True, "summary": summary}, indent=2))


if __name__ == "__main__":
    main()
