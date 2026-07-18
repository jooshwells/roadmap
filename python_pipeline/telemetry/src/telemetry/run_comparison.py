"""Compare two saved RoadMap telemetry runs from the same map."""

from __future__ import annotations

import json
from pathlib import Path

import pandas as pd


COMPARABLE_HEATMAP_METRICS = {
    "avg_speed_mph": ("Average Recorded Speed", "mph", "neutral", 0.5),
    "avg_wait_per_vehicle_s": ("Average Stopped Time per Vehicle Entry", "seconds", "lower", 0.25),
    "bottleneck_score": ("RoadMap Bottleneck Index", "index", "lower", 1.0),
    "estimated_flow_veh_per_hr": ("Estimated Hourly Traffic Flow", "vehicles/hour", "neutral", 50.0),
}

# Small helpers used by both the summary and comparison heatmaps


# Add the new road wait value when comparing a run saved by an older build.
def _add_average_wait_metric(metrics: pd.DataFrame) -> pd.DataFrame:
    """Build average wait from total wait and road entries when needed."""
    metrics = metrics.copy()
    if "avg_wait_per_vehicle_s" in metrics.columns:
        return metrics
    needed = {"total_wait_added_s", "edge_entry_count"}
    if needed.issubset(metrics.columns):
        total_wait = pd.to_numeric(metrics["total_wait_added_s"], errors="coerce")
        entries = pd.to_numeric(metrics["edge_entry_count"], errors="coerce")
        entries = entries.where(entries != 0)
        metrics["avg_wait_per_vehicle_s"] = (total_wait / entries).fillna(0.0)
    return metrics

def _load_json(path: Path) -> dict:
    """Load one required saved-run JSON file."""
    if not path.exists():
        raise FileNotFoundError(f"Required comparison file was not found: {path}")
    with path.open("r", encoding="utf-8") as file:
        return json.load(file)


def _number(value, default: float = 0.0) -> float:
    """Convert saved values to numbers without stopping on blank data."""
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def _percent_change(baseline: float, comparison: float) -> float | None:
    """Calculate percent change while handling a zero baseline."""
    if abs(baseline) < 1e-9:
        return 0.0 if abs(comparison) < 1e-9 else None
    return round((comparison - baseline) / abs(baseline) * 100.0, 2)


def _metric(
    metric_id: str,
    label: str,
    baseline: float,
    comparison: float,
    unit: str,
    better: str = "neutral",
    tolerance: float = 0.0,
) -> dict:
    """Build one easy-to-read comparison result."""
    delta = comparison - baseline
    status = "informational"
    if better != "neutral":
        if abs(delta) <= tolerance:
            status = "little_change"
        else:
            improved = delta > 0 if better == "higher" else delta < 0
            status = "improved" if improved else "worsened"

    explanations = {
        "improved": f"{label} moved in a favorable direction.",
        "worsened": f"{label} moved in an unfavorable direction.",
        "little_change": f"{label} stayed about the same.",
        "informational": f"{label} is shown for context and is not automatically good or bad.",
    }
    return {
        "id": metric_id,
        "label": label,
        "baseline": round(baseline, 3),
        "comparison": round(comparison, 3),
        "delta": round(delta, 3),
        "percent_change": _percent_change(baseline, comparison),
        "unit": unit,
        "status": status,
        "explanation": explanations[status],
    }


def _map_identity(metadata: dict, run_folder: Path) -> str:
    """Return the saved map ID, including support for older runs."""
    return str(metadata.get("map_id") or metadata.get("map_name") or run_folder.parent.name).strip().casefold()


def _road_identity(run_folder: Path) -> pd.DataFrame:
    """Match a run's EdgeIDs to stable directed node pairs and display names."""
    network_path = run_folder / "network_graph.csv"
    if not network_path.exists():
        raise FileNotFoundError(f"Required comparison file was not found: {network_path}")

    network = pd.read_csv(network_path)
    edge_column = "EdgeID" if "EdgeID" in network.columns else "edge_id" if "edge_id" in network.columns else None
    if edge_column is None or not {"source", "target"}.issubset(network.columns):
        raise ValueError("The selected runs do not contain the road identity data needed for comparison.")

    def label(row) -> str:
        for column in ("name", "ref"):
            value = str(row.get(column) or "").strip()
            if value and value.casefold() != "nan":
                return value
        highway = str(row.get("highway") or "").strip()
        return f"Unnamed {highway} road" if highway and highway.casefold() != "nan" else "Unnamed road"

    identity = pd.DataFrame({
        "EdgeID": pd.to_numeric(network[edge_column], errors="coerce"),
        "source": pd.to_numeric(network["source"], errors="coerce"),
        "target": pd.to_numeric(network["target"], errors="coerce"),
        "road_name": network.apply(label, axis=1),
    }).dropna(subset=["EdgeID", "source", "target"])
    for column in ("EdgeID", "source", "target"):
        identity[column] = identity[column].astype(int)
    return identity.drop_duplicates("EdgeID")


def _compare_roads(baseline_folder: Path, comparison_folder: Path, limit: int = 8) -> tuple[list[dict], dict]:
    """Find the shared roads with the largest useful changes."""
    baseline = pd.read_csv(baseline_folder / "edge_metrics.csv")
    comparison = pd.read_csv(comparison_folder / "edge_metrics.csv")
    baseline = _add_average_wait_metric(baseline)
    comparison = _add_average_wait_metric(comparison)
    required = {"EdgeID", "avg_speed_mph", "avg_wait_per_vehicle_s", "bottleneck_score"}
    if not required.issubset(baseline.columns) or not required.issubset(comparison.columns):
        raise ValueError("The selected runs do not contain the road metrics needed for comparison.")

    baseline["EdgeID"] = pd.to_numeric(baseline["EdgeID"], errors="coerce")
    comparison["EdgeID"] = pd.to_numeric(comparison["EdgeID"], errors="coerce")
    baseline = baseline.dropna(subset=["EdgeID"]).copy()
    comparison = comparison.dropna(subset=["EdgeID"]).copy()
    baseline["EdgeID"] = baseline["EdgeID"].astype(int)
    comparison["EdgeID"] = comparison["EdgeID"].astype(int)

    columns = ["EdgeID", "avg_speed_mph", "avg_wait_per_vehicle_s", "bottleneck_score"]
    if "estimated_flow_veh_per_hr" in baseline.columns and "estimated_flow_veh_per_hr" in comparison.columns:
        columns.append("estimated_flow_veh_per_hr")
    baseline = baseline[columns].merge(_road_identity(baseline_folder), on="EdgeID", how="inner")
    comparison = comparison[columns].merge(_road_identity(comparison_folder), on="EdgeID", how="inner")
    merged = baseline.merge(
        comparison, on=["source", "target"], how="inner", suffixes=("_baseline", "_comparison")
    )
    merged["road_name"] = merged["road_name_comparison"].fillna(merged["road_name_baseline"])
    merged["road_name"] = merged["road_name"].fillna("Unnamed road")

    for metric_name in ("avg_speed_mph", "avg_wait_per_vehicle_s", "bottleneck_score"):
        merged[f"{metric_name}_delta"] = (
            merged[f"{metric_name}_comparison"] - merged[f"{metric_name}_baseline"]
        )
    # Put unlike units on similar rough scales before choosing which road
    # changes to show. The status itself is decided below from stopped time and
    # the RoadMap index; recorded speed stays useful context.
    merged["change_score"] = (
        merged["avg_speed_mph_delta"].abs() / 5.0
        + merged["avg_wait_per_vehicle_s_delta"].abs()
        + merged["bottleneck_score_delta"].abs() / 10.0
    )
    merged = merged.sort_values("change_score", ascending=False).head(limit)

    roads = []
    for _, row in merged.iterrows():
        speed_delta = _number(row["avg_speed_mph_delta"])
        wait_delta = _number(row["avg_wait_per_vehicle_s_delta"])
        bottleneck_delta = _number(row["bottleneck_score_delta"])
        has_worse_signal = bottleneck_delta > 1.0 or wait_delta > 1.0
        has_better_signal = bottleneck_delta < -1.0 or wait_delta < -1.0
        if has_worse_signal and has_better_signal:
            status = "mixed_change"
        elif has_worse_signal:
            status = "worsened"
        elif has_better_signal:
            status = "improved"
        else:
            status = "little_change"

        roads.append({
            "edge_id": int(row["EdgeID_comparison"]),
            "road_name": str(row["road_name"]),
            "baseline_speed_mph": round(_number(row["avg_speed_mph_baseline"]), 2),
            "comparison_speed_mph": round(_number(row["avg_speed_mph_comparison"]), 2),
            "speed_delta_mph": round(speed_delta, 2),
            "baseline_wait_s": round(_number(row["avg_wait_per_vehicle_s_baseline"]), 2),
            "comparison_wait_s": round(_number(row["avg_wait_per_vehicle_s_comparison"]), 2),
            "wait_delta_s": round(wait_delta, 2),
            "baseline_bottleneck_score": round(_number(row["bottleneck_score_baseline"]), 3),
            "comparison_bottleneck_score": round(_number(row["bottleneck_score_comparison"]), 3),
            "bottleneck_delta": round(bottleneck_delta, 3),
            "status": status,
        })

    baseline_pairs = set(zip(baseline["source"], baseline["target"]))
    comparison_pairs = set(zip(comparison["source"], comparison["target"]))
    baseline_count = len(baseline_pairs)
    comparison_count = len(comparison_pairs)
    shared_count = len(baseline_pairs & comparison_pairs)
    coverage_base = max(baseline_count, comparison_count, 1)
    return roads, {
        "baseline_roads": baseline_count,
        "comparison_roads": comparison_count,
        "shared_roads": shared_count,
        "shared_coverage_percent": round(shared_count / coverage_base * 100.0, 2),
    }


def build_heatmap_comparison_metrics(
    baseline_folder: Path,
    comparison_folder: Path,
    metric: str,
) -> tuple[pd.DataFrame, dict]:
    """Build stable road-pair deltas that the comparison heatmap can draw."""
    if metric not in COMPARABLE_HEATMAP_METRICS:
        raise ValueError(f"Unsupported comparison heatmap metric: {metric}")

    # Old runs used a cumulative bottleneck score that is not comparable with
    # the current fixed 0-100 index. Other metrics remain available for them.
    if metric == "bottleneck_score":
        baseline_summary = _load_json(Path(baseline_folder) / "telemetry_summary.json")
        comparison_summary = _load_json(Path(comparison_folder) / "telemetry_summary.json")
        baseline_version = int(_number(baseline_summary.get("bottleneck_index_version"), 1))
        comparison_version = int(_number(comparison_summary.get("bottleneck_index_version"), 1))
        if baseline_version != comparison_version:
            raise ValueError(
                "These runs use different RoadMap bottleneck-index versions. "
                "Compare another metric or create two new runs with the current build."
            )

    baseline_metadata = _load_json(Path(baseline_folder) / "run_metadata.json")
    comparison_metadata = _load_json(Path(comparison_folder) / "run_metadata.json")
    if _map_identity(baseline_metadata, Path(baseline_folder)) != _map_identity(
        comparison_metadata, Path(comparison_folder)
    ):
        raise ValueError("Runs can only be compared when they belong to the same map.")

    def prepare(run_folder: Path) -> pd.DataFrame:
        metrics = pd.read_csv(run_folder / "edge_metrics.csv")
        metrics = _add_average_wait_metric(metrics)
        if "EdgeID" not in metrics.columns or metric not in metrics.columns:
            raise ValueError(f"The selected runs do not contain {metric} road metrics.")
        metrics["EdgeID"] = pd.to_numeric(metrics["EdgeID"], errors="coerce")
        metrics[metric] = pd.to_numeric(metrics[metric], errors="coerce")
        metrics = metrics.dropna(subset=["EdgeID", metric]).copy()
        metrics["EdgeID"] = metrics["EdgeID"].astype(int)
        return metrics[["EdgeID", metric]].merge(_road_identity(run_folder), on="EdgeID", how="inner")

    baseline = prepare(Path(baseline_folder))
    comparison = prepare(Path(comparison_folder))
    merged = baseline.merge(
        comparison,
        on=["source", "target"],
        how="inner",
        suffixes=("_baseline", "_comparison"),
    )
    if merged.empty:
        raise ValueError("The selected runs do not share any road directions.")

    display_name, unit, better, tolerance = COMPARABLE_HEATMAP_METRICS[metric]
    merged["baseline_value"] = merged[f"{metric}_baseline"]
    merged["comparison_value"] = merged[f"{metric}_comparison"]
    merged["raw_delta"] = merged["comparison_value"] - merged["baseline_value"]
    merged["comparison_delta"] = (
        -merged["raw_delta"] if better == "lower" else merged["raw_delta"]
    )

    if better == "neutral":
        merged["comparison_status"] = merged["raw_delta"].apply(
            lambda value: "little_change" if abs(value) <= tolerance else "increased" if value > 0 else "decreased"
        )
    else:
        merged["comparison_status"] = merged["comparison_delta"].apply(
            lambda value: "little_change" if abs(value) <= tolerance else "improved" if value > 0 else "worsened"
        )

    result = pd.DataFrame({
        "EdgeID": merged["EdgeID_comparison"].astype(int),
        "comparison_delta": merged["comparison_delta"],
        "baseline_value": merged["baseline_value"],
        "comparison_value": merged["comparison_value"],
        "raw_delta": merged["raw_delta"],
        "comparison_status": merged["comparison_status"],
        "comparison_metric": display_name,
        "comparison_unit": unit,
        "comparison_is_neutral": better == "neutral",
    })
    return result, {
        "metric": metric,
        "display_name": display_name,
        "unit": unit,
        "neutral": better == "neutral",
        "shared_roads": int(len(result)),
    }


def compare_saved_runs(baseline_folder: Path, comparison_folder: Path) -> dict:
    """Return user-facing summary and road changes for two compatible runs."""
    baseline_folder = Path(baseline_folder)
    comparison_folder = Path(comparison_folder)
    baseline_metadata = _load_json(baseline_folder / "run_metadata.json")
    comparison_metadata = _load_json(comparison_folder / "run_metadata.json")
    baseline_summary = _load_json(baseline_folder / "telemetry_summary.json")
    comparison_summary = _load_json(comparison_folder / "telemetry_summary.json")

    baseline_map = _map_identity(baseline_metadata, baseline_folder)
    comparison_map = _map_identity(comparison_metadata, comparison_folder)
    if not baseline_map or baseline_map != comparison_map:
        raise ValueError("Runs can only be compared when they belong to the same map.")

    baseline_vehicles = _number(baseline_summary.get("total_vehicles"))
    comparison_vehicles = _number(comparison_summary.get("total_vehicles"))
    baseline_wait = _number(baseline_summary.get("total_wait_added_s"))
    comparison_wait = _number(comparison_summary.get("total_wait_added_s"))
    baseline_wait_per_vehicle = baseline_wait / baseline_vehicles if baseline_vehicles else 0.0
    comparison_wait_per_vehicle = comparison_wait / comparison_vehicles if comparison_vehicles else 0.0
    baseline_bottleneck = _number((baseline_summary.get("worst_bottleneck") or {}).get("bottleneck_score"))
    comparison_bottleneck = _number((comparison_summary.get("worst_bottleneck") or {}).get("bottleneck_score"))
    baseline_index_version = int(_number(baseline_summary.get("bottleneck_index_version"), 1))
    comparison_index_version = int(_number(comparison_summary.get("bottleneck_index_version"), 1))
    same_index_version = baseline_index_version == comparison_index_version

    metrics = [
        _metric("average_speed_mph", "Average recorded speed", _number(baseline_summary.get("average_speed_mph")), _number(comparison_summary.get("average_speed_mph")), "mph"),
        _metric("wait_per_vehicle_s", "Average stopped time per vehicle", baseline_wait_per_vehicle, comparison_wait_per_vehicle, "seconds", "lower", 0.25),
        _metric("maximum_wait_s", "Highest stopped time for one vehicle", _number(baseline_summary.get("max_wait_time_s")), _number(comparison_summary.get("max_wait_time_s")), "seconds", "lower", 0.5),
        _metric(
            "worst_bottleneck_score",
            "Highest RoadMap bottleneck index" if same_index_version else "RoadMap bottleneck index (different versions)",
            baseline_bottleneck,
            comparison_bottleneck,
            "index",
            "lower" if same_index_version else "neutral",
            1.0,
        ),
        _metric("total_vehicles", "Vehicles", baseline_vehicles, comparison_vehicles, "vehicles"),
        _metric("edges_used", "Road directions used", _number(baseline_summary.get("edges_used")), _number(comparison_summary.get("edges_used")), "directions"),
        _metric("duration_s", "Simulation duration", _number(baseline_summary.get("simulation_duration_s")), _number(comparison_summary.get("simulation_duration_s")), "seconds"),
    ]
    roads, coverage = _compare_roads(baseline_folder, comparison_folder)

    warnings = []
    baseline_duration = _number(baseline_summary.get("simulation_duration_s"))
    comparison_duration = _number(comparison_summary.get("simulation_duration_s"))
    if min(baseline_duration, comparison_duration) < 60.0:
        warnings.append("One or both runs are shorter than 60 seconds, so the comparison is preliminary.")
    if max(baseline_duration, comparison_duration, 1.0) > max(min(baseline_duration, comparison_duration), 1.0) * 1.25:
        warnings.append("The runs have noticeably different durations; compare totals with caution.")
    if max(baseline_vehicles, comparison_vehicles, 1.0) > max(min(baseline_vehicles, comparison_vehicles), 1.0) * 1.20:
        warnings.append(
            "The runs have noticeably different vehicle totals. Speed and stopped-time changes may reflect different demand, not only road changes."
        )
    if baseline_index_version != comparison_index_version:
        warnings.append(
            "The runs use different RoadMap bottleneck-index versions, so their index values should not be compared."
        )
    if coverage["shared_coverage_percent"] < 70.0:
        warnings.append("Fewer than 70% of road directions are shared. The map may have changed between runs.")

    return {
        "success": True,
        "baseline": {
            "run_id": baseline_metadata.get("run_id", baseline_folder.name),
            "created_at": baseline_metadata.get("created_at", ""),
            "map_name": baseline_metadata.get("map_name", baseline_map),
        },
        "comparison": {
            "run_id": comparison_metadata.get("run_id", comparison_folder.name),
            "created_at": comparison_metadata.get("created_at", ""),
            "map_name": comparison_metadata.get("map_name", comparison_map),
        },
        "metrics": metrics,
        "top_road_changes": roads,
        "road_coverage": coverage,
        "preliminary": bool(warnings),
        "warnings": warnings,
    }
