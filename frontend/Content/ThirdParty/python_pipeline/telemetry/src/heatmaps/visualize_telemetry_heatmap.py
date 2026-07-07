"""
RoadMap telemetry heatmap generator.

This script takes the edge metrics from the simulation and draws map-style
heatmaps for speed, flow, wait time, and bottleneck score.

It expects network_graph.csv to have road endpoint coordinates and optionally
geometry_xy for curved roads.

Expected network_graph.csv columns:
    edge_id, source, target, length,
    source_x, source_y, target_x, target_y,
    name, ref, highway, geometry_xy

Expected edge_metrics.csv columns include:
    EdgeID, bottleneck_score, total_wait_added_s,
    estimated_flow_veh_per_hr, avg_speed_mph
"""

from pathlib import Path
import argparse
import ast
import json
import math

import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.colors as colors
import matplotlib.patheffects as path_effects
from matplotlib.collections import LineCollection
from matplotlib.lines import Line2D
from matplotlib.patches import FancyBboxPatch, Rectangle, Circle, Polygon


MAJOR_ROAD_TYPES = {"motorway", "trunk", "primary", "secondary"}

# Label the important roads, but skip neighborhood/service roads.
# Tertiary roads are included because they are usually useful connector roads.
LABELED_HIGHWAY_TYPES = {"motorway", "trunk", "primary", "secondary", "tertiary"}
MAX_ROAD_LABELS = 45

BACKGROUND_WIDTHS = {
    "motorway": 1.30,
    "trunk": 1.15,
    "primary": 1.00,
    "secondary": 0.85,
    "tertiary": 0.65,
    "residential": 0.35,
    "service": 0.25,
}

HEAT_WIDTHS = {
    "motorway": 3.20,
    "trunk": 2.90,
    "primary": 2.60,
    "secondary": 2.30,
    "tertiary": 1.85,
    "residential": 1.35,
    "service": 1.05,
}


BASE_BACKGROUND_WIDTH = 0.35
BASE_HEAT_WIDTH = 1.35


# Load the road network and telemetry metrics CSV files.
def load_files(network_path: Path, edge_metrics_path: Path):
    network_df = pd.read_csv(network_path)
    metrics_df = pd.read_csv(edge_metrics_path)
    return network_df, metrics_df


# Clean EdgeID values so the network and metrics files can join correctly.
def clean_edge_id_column(df: pd.DataFrame, column_name: str) -> pd.DataFrame:
    df = df.copy()
    df[column_name] = pd.to_numeric(df[column_name], errors="coerce")
    df = df.dropna(subset=[column_name])
    df[column_name] = df[column_name].astype(int).astype(str)
    return df


# Make sure both dataframes use the same EdgeID column name and format.
def normalize_id_columns(network_df: pd.DataFrame, metrics_df: pd.DataFrame):
    network_df = network_df.copy()
    metrics_df = metrics_df.copy()

    if "edge_id" in network_df.columns:
        network_df = network_df.rename(columns={"edge_id": "EdgeID"})

    if "EdgeID" not in network_df.columns:
        raise ValueError("network_graph.csv must contain either 'edge_id' or 'EdgeID'.")

    if "EdgeID" not in metrics_df.columns:
        raise ValueError("edge_metrics.csv must contain 'EdgeID'.")

    network_df = clean_edge_id_column(network_df, "EdgeID")
    metrics_df = clean_edge_id_column(metrics_df, "EdgeID")

    return network_df, metrics_df


# Check that the network file has the coordinate columns needed for drawing roads.
def require_network_columns(network_df: pd.DataFrame):
    required = {"source_x", "source_y", "target_x", "target_y"}
    missing = required - set(network_df.columns)

    if missing:
        raise ValueError(
            "network_graph.csv is missing required coordinate columns: "
            f"{sorted(missing)}. Rebuild network_graph.csv with the updated builder."
        )


# Clean OSM values that may come in as lists, blanks, or weird string formats.
def simplify_osm_value(value):
    if value is None:
        return None

    try:
        if pd.isna(value):
            return None
    except (TypeError, ValueError):
        pass

    if isinstance(value, (list, tuple)):
        for item in value:
            cleaned = simplify_osm_value(item)
            if cleaned:
                return cleaned
        return None

    text = str(value).strip()

    if not text or text.lower() in {"nan", "none", "null"}:
        return None

    # Handles values like ['I 4', 'I 4'] or values joined by the builder.
    if text.startswith("[") and text.endswith("]"):
        try:
            parsed = ast.literal_eval(text)
            return simplify_osm_value(parsed)
        except (SyntaxError, ValueError):
            pass

    if ";" in text:
        pieces = [piece.strip() for piece in text.split(";")]
        pieces = [piece for piece in pieces if piece and piece.lower() not in {"nan", "none", "null"}]
        return pieces[0] if pieces else None

    return text


# Get the OSM road type for one road row.
def get_highway_type(row):
    return simplify_osm_value(row.get("highway"))


# Pick the best display label for a road using its name first, then its route ref.
def get_road_label(row):
    name = simplify_osm_value(row.get("name"))
    ref = simplify_osm_value(row.get("ref"))

    if name:
        return name
    if ref:
        return ref
    return None


# Check if a road is one of the major road classes.
def is_major_road(row):
    return get_highway_type(row) in MAJOR_ROAD_TYPES


# Decide if this road type should get a label on the map.
def should_label_road(row):
    return get_highway_type(row) in LABELED_HIGHWAY_TYPES


# Choose the line width based on road type and whether it is a heatmap road.
def road_width(row, heat: bool = False):
    highway = get_highway_type(row)
    if heat:
        return HEAT_WIDTHS.get(highway, BASE_HEAT_WIDTH)
    return BACKGROUND_WIDTHS.get(highway, BASE_BACKGROUND_WIDTH)


# Parse curved road geometry from the CSV when it is available.
def parse_geometry_xy(value):
    """Convert the saved geometry_xy text into a list of x/y points."""
    if value is None:
        return None

    try:
        if pd.isna(value):
            return None
    except (TypeError, ValueError):
        pass

    if isinstance(value, str):
        text = value.strip()
        if not text or text.lower() in {"nan", "none", "null"}:
            return None

        try:
            parsed = json.loads(text)
        except json.JSONDecodeError:
            try:
                parsed = ast.literal_eval(text)
            except (SyntaxError, ValueError):
                return None
    else:
        parsed = value

    if not isinstance(parsed, list):
        return None

    segment = []
    for point in parsed:
        if not isinstance(point, dict):
            continue
        x = point.get("x")
        y = point.get("y")
        if x is None or y is None:
            continue
        try:
            segment.append((float(x), float(y)))
        except (TypeError, ValueError):
            continue

    return segment if len(segment) >= 2 else None


# Get the full road shape, using curved geometry if possible or endpoints as a fallback.
def get_segment_from_row(row):
    if "geometry_xy" in row.index:
        segment = parse_geometry_xy(row.get("geometry_xy"))
        if segment:
            return segment

    return [
        (float(row["source_x"]), float(row["source_y"])),
        (float(row["target_x"]), float(row["target_y"])),
    ]


# Calculate the total length of a road segment/polyline.
def segment_length(segment):
    length = 0.0
    for (x1, y1), (x2, y2) in zip(segment[:-1], segment[1:]):
        length += math.hypot(x2 - x1, y2 - y1)
    return length


# Find a point and angle partway along a road for label placement.
def point_and_angle_at_fraction(segment, fraction=0.5):
    """Get a point and angle along a road so the label follows the road."""
    total = segment_length(segment)
    if total <= 0:
        x1, y1 = segment[0]
        x2, y2 = segment[-1]
        return (x1 + x2) / 2, (y1 + y2) / 2, math.degrees(math.atan2(y2 - y1, x2 - x1))

    target_distance = total * fraction
    walked = 0.0

    for (x1, y1), (x2, y2) in zip(segment[:-1], segment[1:]):
        part = math.hypot(x2 - x1, y2 - y1)
        if part <= 0:
            continue

        if walked + part >= target_distance:
            t = (target_distance - walked) / part
            x = x1 + (x2 - x1) * t
            y = y1 + (y2 - y1) * t
            angle = math.degrees(math.atan2(y2 - y1, x2 - x1))
            if angle > 90:
                angle -= 180
            elif angle < -90:
                angle += 180
            return x, y, angle

        walked += part

    x1, y1 = segment[-2]
    x2, y2 = segment[-1]
    angle = math.degrees(math.atan2(y2 - y1, x2 - x1))
    if angle > 90:
        angle -= 180
    elif angle < -90:
        angle += 180
    return segment[-1][0], segment[-1][1], angle


# Draw road names for important roads without labeling every neighborhood street.
def draw_road_labels(ax, network_df: pd.DataFrame):
    if "name" not in network_df.columns and "ref" not in network_df.columns:
        print("Road labels skipped because name/ref columns are missing.")
        return

    label_candidates = []

    for _, row in network_df.iterrows():
        if not should_label_road(row):
            continue

        label = get_road_label(row)
        if not label:
            continue

        if pd.isna(row["source_x"]) or pd.isna(row["source_y"]):
            continue
        if pd.isna(row["target_x"]) or pd.isna(row["target_y"]):
            continue

        segment = get_segment_from_row(row)
        length = segment_length(segment)

        # Skip tiny road segments because labels would overlap and look messy.
        if length < 200:
            continue

        label_candidates.append((label, length, segment, get_highway_type(row)))

    best_by_label = {}
    for item in label_candidates:
        label = item[0]
        if label not in best_by_label or item[1] > best_by_label[label][1]:
            best_by_label[label] = item

    best_labels = sorted(best_by_label.values(), key=lambda item: item[1], reverse=True)
    best_labels = best_labels[:MAX_ROAD_LABELS]

    for label, _, segment, highway in best_labels:
        mid_x, mid_y, angle = point_and_angle_at_fraction(segment, 0.50)

        if highway in {"motorway", "trunk"}:
            font_size = 10
            font_weight = "bold"
        elif highway in {"primary", "secondary"}:
            font_size = 9
            font_weight = "normal"
        else:
            font_size = 8
            font_weight = "normal"

        text = ax.text(
            mid_x,
            mid_y,
            label,
            fontsize=font_size,
            fontweight=font_weight,
            color="#F5F5F5",
            rotation=angle,
            rotation_mode="anchor",
            ha="center",
            va="center",
            zorder=30,
        )

        text.set_path_effects([
            path_effects.Stroke(linewidth=4.0, foreground="#000000"),
            path_effects.Normal(),
        ])

    print(f"Road labels drawn: {len(best_labels)}")



# Build the shared green-yellow-orange-red traffic color palette.
def make_roadmap_traffic_cmap(high_values_are_bad: bool = True):
    """Use one traffic-light color palette for all heatmaps."""
    green_to_bad = ["#00884A", "#38C172", "#EAF76E", "#FFB347", "#FF3B30"]
    palette = green_to_bad if high_values_are_bad else list(reversed(green_to_bad))

    name = "roadmap_high_bad" if high_values_are_bad else "roadmap_high_good"

    return colors.LinearSegmentedColormap.from_list(
        name,
        palette,
        N=256,
    )


# Convert metric column names into readable titles.
def metric_display_name(metric: str) -> str:
    names = {
        "bottleneck_score": "Bottleneck Score",
        "total_wait_added_s": "Total Wait Added",
        "estimated_flow_veh_per_hr": "Estimated Flow",
        "avg_speed_mph": "Average Speed",
    }
    return names.get(metric, metric.replace("_", " ").title())


# Format large numbers so the summary box stays readable.
def compact_number(value):
    if value is None or pd.isna(value):
        return "N/A"
    value = float(value)
    if abs(value) >= 1_000_000:
        return f"{value / 1_000_000:.1f}M"
    if abs(value) >= 1_000:
        return f"{value / 1_000:.1f}K"
    if abs(value) >= 10:
        return f"{value:.0f}"
    return f"{value:.2f}"




# Return the unit text used for each telemetry metric.
def metric_value_unit(metric: str) -> str:
    """Units shown in the small summary box."""
    if metric == "avg_speed_mph":
        return "mph"
    if metric == "estimated_flow_veh_per_hr":
        return "veh/hr"
    if metric == "total_wait_added_s":
        return "sec"
    if metric == "bottleneck_score":
        return "index"
    return ""


# Format a number and add its unit when needed.
def compact_number_with_unit(value, unit):
    number = compact_number(value)
    return f"{number} {unit}" if unit else number


# Format colorbar tick numbers in a short readable way.
def compact_axis_number(value):
    """Make the colorbar numbers shorter and easier to read."""
    if value is None or pd.isna(value):
        return ""
    value = float(value)
    if abs(value) >= 1_000_000:
        return f"{value / 1_000_000:.1f}M"
    if abs(value) >= 1_000:
        return f"{value / 1_000:.1f}K"
    if abs(value) >= 10:
        return f"{value:.0f}"
    if abs(value) >= 1:
        return f"{value:.1f}"
    return f"{value:.2f}"


# Round color scale maximums up to clean values like 100, 500, or 2K.
def nice_round_up(value):
    """Round the top of the color scale to a cleaner number."""
    if value is None or pd.isna(value):
        return value

    value = float(value)
    if value <= 0:
        return 1.0

    exponent = math.floor(math.log10(value))
    base = 10 ** exponent
    scaled = value / base

    if scaled <= 1:
        nice = 1
    elif scaled <= 2:
        nice = 2
    elif scaled <= 5:
        nice = 5
    else:
        nice = 10

    return nice * base


# Round color scale minimums down to cleaner values.
def nice_round_down(value):
    """Round the bottom of the color scale to a cleaner number."""
    if value is None or pd.isna(value):
        return value

    value = float(value)
    if value <= 0:
        return 0.0

    exponent = math.floor(math.log10(value))
    base = 10 ** exponent
    scaled = value / base

    if scaled >= 5:
        nice = 5
    elif scaled >= 2:
        nice = 2
    else:
        nice = 1

    return nice * base


# Pick color scale limits that work for both short and long simulation runs.
def choose_visual_range(values: pd.Series, metric: str):
    """Pick a color scale that still works for short and long simulations."""
    values = pd.Series(values).dropna().astype(float)
    if values.empty:
        return 0.0, 1.0

    if metric == "avg_speed_mph":
        # Keep speed maps consistent between runs. Anything over 80 mph is just drawn as the top green color.
        return 0.0, 80.0

    vmin = 0.0

    if len(values) < 40:
        raw_vmax = values.max()
    elif metric == "estimated_flow_veh_per_hr":
        raw_vmax = values.quantile(0.95)
    else:
        raw_vmax = values.quantile(0.98)

    if pd.isna(raw_vmax) or raw_vmax <= 0:
        raw_vmax = values.max()

    vmax = nice_round_up(raw_vmax)
    return vmin, vmax if vmax > vmin else vmin + 1


# Apply the same simple five-tick style to every colorbar.
def set_clean_colorbar_ticks(colorbar, vmin, vmax, tick_count=5):
    """Use five simple tick marks on every colorbar."""
    if pd.isna(vmin) or pd.isna(vmax) or vmax <= vmin:
        return

    ticks = [vmin + (vmax - vmin) * i / (tick_count - 1) for i in range(tick_count)]
    colorbar.set_ticks(ticks)
    colorbar.set_ticklabels([compact_axis_number(tick) for tick in ticks])


# Shorten long road names so they fit in the summary card.
def shorten_text(value, max_length=28):
    if value is None:
        return "Unknown"
    text = str(value).strip()
    if not text:
        return "Unknown"
    return text if len(text) <= max_length else text[: max_length - 1] + "…"


# Choose the summary label for the most important road in the selected metric.
def metric_extreme_label(metric: str) -> str:
    if metric == "avg_speed_mph":
        return "Slowest Road"
    if metric == "estimated_flow_veh_per_hr":
        return "Highest Flow Road"
    if metric == "total_wait_added_s":
        return "Longest Wait Road"
    if metric == "bottleneck_score":
        return "Most Congested Road"
    return "Most Extreme Road"


# Find the road with the highest or lowest value for the selected metric.
def get_extreme_road_info(merged_df: pd.DataFrame, metric: str):
    """Find the road shown in the summary box for this metric."""
    if metric not in merged_df.columns:
        return "Unknown", None

    valid = merged_df.dropna(subset=[metric]).copy()
    if valid.empty:
        return "Unknown", None

    if metric == "avg_speed_mph":
        row = valid.loc[valid[metric].idxmin()]
    else:
        row = valid.loc[valid[metric].idxmax()]

    label = get_road_label(row)
    if not label:
        label = simplify_osm_value(row.get("ref"))
    if not label:
        label = "Unnamed road"

    return shorten_text(label), float(row[metric])


# Build the rows shown in the upper-left simulation summary card.
def summary_rows_for_metric(metric, heat_values, background_count, heat_count, merged_df):
    """Build the lines shown in the summary card."""
    values = pd.Series(heat_values)
    unit = metric_value_unit(metric)
    extreme_road, extreme_value = get_extreme_road_info(merged_df, metric)
    top_count = int((values >= values.quantile(0.95)).sum()) if metric != "avg_speed_mph" else int((values <= values.quantile(0.05)).sum())

    rows = [
        ("Road Segments", f"{background_count:,}"),
        ("Analyzed Segments", f"{heat_count:,}"),
        (metric_extreme_label(metric), extreme_road),
    ]

    if metric == "avg_speed_mph":
        rows.extend([
            ("Lowest Speed", compact_number_with_unit(values.min(), unit)),
            ("Average Speed", compact_number_with_unit(values.mean(), unit)),
        ])
    elif metric == "estimated_flow_veh_per_hr":
        rows.extend([
            ("Peak Flow", compact_number_with_unit(values.max(), unit)),
            ("Average Flow", compact_number_with_unit(values.mean(), unit)),
        ])
    elif metric == "total_wait_added_s":
        rows.extend([
            ("Peak Wait", compact_number_with_unit(values.max(), unit)),
            ("Average Wait", compact_number_with_unit(values.mean(), unit)),
        ])
    elif metric == "bottleneck_score":
        # The bottleneck score is internal, so showing the road name is clearer than showing raw score values.
        rows.append(("Top 5% Hotspots", f"{top_count:,} segments"))
    else:
        rows.extend([
            ("Peak Value", compact_number_with_unit(values.max(), unit)),
            ("Average Value", compact_number_with_unit(values.mean(), unit)),
        ])

    return rows


# Get the midpoint of a road segment.
def segment_midpoint(segment):
    x, y, _ = point_and_angle_at_fraction(segment, 0.50)
    return x, y


# Zoom the map around the road network to avoid large empty margins.
def set_tight_map_bounds(ax, segments, pad_ratio=0.035):
    """Zoom the image to the road network instead of leaving a lot of empty space."""
    xs = []
    ys = []
    for segment in segments:
        for x, y in segment:
            xs.append(x)
            ys.append(y)

    if not xs or not ys:
        return

    min_x, max_x = min(xs), max(xs)
    min_y, max_y = min(ys), max(ys)

    width = max_x - min_x
    height = max_y - min_y
    pad = max(width, height) * pad_ratio

    ax.set_xlim(min_x - pad, max_x + pad)
    ax.set_ylim(min_y - pad, max_y + pad)


# Draw the small dashboard-style summary card.
def add_stats_card(ax, metric, heat_values, background_count, heat_count, merged_df):
    """Draw the summary card in the upper-left corner."""
    if not heat_values:
        return

    title = "Simulation Summary"
    rows = summary_rows_for_metric(
        metric,
        heat_values,
        background_count,
        heat_count,
        merged_df,
    )

    # Separate text calls make the spacing look cleaner than one big multiline string.
    x0, y0 = 0.018, 0.965
    card_width = 0.215
    card_height = 0.082 + (0.029 * len(rows))

    card = FancyBboxPatch(
        (x0 - 0.004, y0 - card_height),
        card_width,
        card_height,
        transform=ax.transAxes,
        boxstyle="round,pad=0.008,rounding_size=0.004",
        facecolor="#0B1220",
        edgecolor="#334155",
        linewidth=1.1,
        alpha=0.92,
        zorder=58,
    )
    ax.add_patch(card)

    ax.text(
        x0,
        y0 - 0.004,
        title,
        transform=ax.transAxes,
        ha="left",
        va="top",
        fontsize=10.5,
        fontweight="bold",
        color="#F9FAFB",
        zorder=60,
    )

    ax.text(
        x0,
        y0 - 0.033,
        metric_display_name(metric),
        transform=ax.transAxes,
        ha="left",
        va="top",
        fontsize=8.2,
        color="#94A3B8",
        zorder=60,
    )

    y = y0 - 0.066
    for label, value in rows:
        ax.text(
            x0,
            y,
            label,
            transform=ax.transAxes,
            ha="left",
            va="top",
            fontsize=8.3,
            color="#CBD5E1",
            zorder=60,
        )
        ax.text(
            x0 + card_width - 0.014,
            y,
            str(value),
            transform=ax.transAxes,
            ha="right",
            va="top",
            fontsize=8.7,
            fontweight="bold",
            color="#FFFFFF",
            zorder=60,
        )
        y -= 0.029


# Add extra glow to the roads with the strongest heatmap values.
def add_value_weighted_glow(ax, heat_segments, heat_widths, values, cmap_name, norm, metric):
    """Add extra glow to the roads that matter most for the selected metric."""
    if not heat_segments or len(values) == 0:
        return

    series = pd.Series(values)

    # For speed, slow roads should pop. For the other metrics, high values should pop.
    if metric == "avg_speed_mph":
        levels = [
            (series.quantile(0.25), "below", 2.6, 0.08),
            (series.quantile(0.15), "below", 4.6, 0.10),
            (series.quantile(0.08), "below", 6.8, 0.13),
        ]
    else:
        levels = [
            (series.quantile(0.75), "above", 2.4, 0.08),
            (series.quantile(0.90), "above", 4.6, 0.10),
            (series.quantile(0.97), "above", 7.2, 0.13),
        ]

    for threshold, direction, width_boost, alpha in levels:
        if pd.isna(threshold):
            continue

        selected_segments = []
        selected_widths = []
        selected_values = []

        for segment, width, value in zip(heat_segments, heat_widths, values):
            if direction == "above" and value >= threshold:
                selected_segments.append(segment)
                selected_widths.append(width + width_boost)
                selected_values.append(value)
            elif direction == "below" and value <= threshold:
                selected_segments.append(segment)
                selected_widths.append(width + width_boost)
                selected_values.append(value)

        if not selected_segments:
            continue

        glow = LineCollection(
            selected_segments,
            cmap=cmap_name,
            norm=norm,
            linewidths=selected_widths,
            alpha=alpha,
            capstyle="round",
            joinstyle="round",
            zorder=3,
        )
        glow.set_array(pd.Series(selected_values).to_numpy())
        ax.add_collection(glow)


# Optionally draw numbered markers on the worst bottlenecks.
def add_top_bottleneck_markers(ax, heat_segments, heat_values, metric, max_markers=5):
    """Optional numbered markers for the worst roads."""
    if metric not in {"bottleneck_score", "total_wait_added_s"}:
        return
    if not heat_segments or not heat_values:
        return

    ranked = sorted(
        zip(heat_segments, heat_values),
        key=lambda item: item[1],
        reverse=True,
    )[:max_markers]

    for index, (segment, _) in enumerate(ranked, start=1):
        x, y = segment_midpoint(segment)
        circle = Circle(
            (x, y),
            radius=42,
            facecolor="#FFFFFF",
            edgecolor="#111827",
            linewidth=1.4,
            zorder=70,
        )
        ax.add_patch(circle)
        ax.text(
            x,
            y,
            str(index),
            ha="center",
            va="center",
            fontsize=8,
            fontweight="bold",
            color="#111827",
            zorder=71,
        )

# Choose the palette direction and colorbar label for the selected metric.
def choose_color_settings(metric: str):
    # Same traffic palette for all maps. Reverse it when higher values should look better.
    if metric == "avg_speed_mph":
        return make_roadmap_traffic_cmap(high_values_are_bad=False), "Average speed (mph)", False

    if metric == "estimated_flow_veh_per_hr":
        return make_roadmap_traffic_cmap(high_values_are_bad=False), "Estimated flow (vehicles/hour)", True

    if metric == "total_wait_added_s":
        return make_roadmap_traffic_cmap(high_values_are_bad=True), "Total wait added (seconds)", True

    if metric == "bottleneck_score":
        return make_roadmap_traffic_cmap(high_values_are_bad=True), "Bottleneck score", True

    return make_roadmap_traffic_cmap(high_values_are_bad=True), metric, True


# Join network roads to telemetry metrics and prepare line segments for drawing.
def build_line_segments(network_df: pd.DataFrame, metrics_df: pd.DataFrame, metric: str):
    network_df, metrics_df = normalize_id_columns(network_df, metrics_df)
    require_network_columns(network_df)

    merged = network_df.merge(metrics_df, on="EdgeID", how="left")

    background_segments = []
    background_widths = []
    heat_segments = []
    heat_widths = []
    heat_values = []
    skipped_edges = 0

    for _, row in merged.iterrows():
        if pd.isna(row["source_x"]) or pd.isna(row["source_y"]):
            skipped_edges += 1
            continue
        if pd.isna(row["target_x"]) or pd.isna(row["target_y"]):
            skipped_edges += 1
            continue

        segment = get_segment_from_row(row)
        background_segments.append(segment)
        background_widths.append(road_width(row, heat=False))

        if metric in merged.columns and not pd.isna(row.get(metric)):
            heat_segments.append(segment)
            heat_widths.append(road_width(row, heat=True))
            heat_values.append(float(row[metric]))

    if skipped_edges > 0:
        print(f"Skipped {skipped_edges} edges because endpoint coordinates were missing.")

    print(f"Drawable road segments: {len(background_segments)}")
    print(f"Telemetry-colored segments: {len(heat_segments)}")

    return background_segments, background_widths, heat_segments, heat_widths, heat_values, merged



# Draw optional water, park, or building polygons under the roads.
def draw_geojson_polygons(ax, geojson_path, facecolor, edgecolor, alpha, zorder):
    """Draw optional water/park/building GeoJSON layers if they are provided."""
    if not geojson_path:
        return

    geojson_path = Path(geojson_path)

    if not geojson_path.exists():
        print(f"Optional layer skipped, file not found: {geojson_path}")
        return

    with open(geojson_path, "r", encoding="utf-8") as file:
        data = json.load(file)

    features = data.get("features", [])
    drawn = 0

    for feature in features:
        geometry = feature.get("geometry", {})
        geometry_type = geometry.get("type")
        coordinates = geometry.get("coordinates")

        if not coordinates:
            continue

        if geometry_type == "Polygon":
            polygons = [coordinates]
        elif geometry_type == "MultiPolygon":
            polygons = coordinates
        else:
            continue

        for polygon in polygons:
            if not polygon:
                continue

            exterior_ring = polygon[0]

            try:
                points = [(float(x), float(y)) for x, y in exterior_ring]
            except (TypeError, ValueError):
                continue

            patch = Polygon(
                points,
                closed=True,
                facecolor=facecolor,
                edgecolor=edgecolor,
                linewidth=0.25,
                alpha=alpha,
                zorder=zorder,
            )

            ax.add_patch(patch)
            drawn += 1

    print(f"Optional layer drawn: {geojson_path.name} polygons={drawn}")

# Build the full heatmap figure and save both normal and transparent PNGs.
def plot_heatmap(
    network_df: pd.DataFrame,
    metrics_df: pd.DataFrame,
    metric: str,
    output_path: Path,
    water_geojson=None,
    parks_geojson=None,
    buildings_geojson=None,
    show_markers=False,
):
    if metric not in metrics_df.columns:
        raise ValueError(
            f"Metric '{metric}' was not found in edge_metrics.csv. "
            f"Available columns are: {list(metrics_df.columns)}"
        )

    cmap_name, colorbar_label, clip_high_values = choose_color_settings(metric)

    (
        background_segments,
        background_widths,
        heat_segments,
        heat_widths,
        heat_values,
        labeled_network_df,
    ) = build_line_segments(network_df, metrics_df, metric)

    fig, ax = plt.subplots(figsize=(20, 12), facecolor="#070B10")
    ax.set_facecolor("#070B10")

    # Optional context layers. These only draw if the file paths are passed in.
    draw_geojson_polygons(
        ax,
        water_geojson,
        facecolor="#0B2538",
        edgecolor="#143A55",
        alpha=0.55,
        zorder=0,
    )

    draw_geojson_polygons(
        ax,
        parks_geojson,
        facecolor="#102A1E",
        edgecolor="#1E4D34",
        alpha=0.35,
        zorder=0,
    )

    draw_geojson_polygons(
        ax,
        buildings_geojson,
        facecolor="#1C1F24",
        edgecolor="#2B3036",
        alpha=0.18,
        zorder=0,
    )

    # Draw a dark road outline first so the roads are easier to see.
    if background_segments:
        casing = LineCollection(
            background_segments,
            colors="#050505",
            linewidths=[width + 0.70 for width in background_widths],
            alpha=0.72,
            capstyle="round",
            joinstyle="round",
            zorder=1,
        )
        ax.add_collection(casing)

        background = LineCollection(
            background_segments,
            colors="#1C2228",
            linewidths=background_widths,
            alpha=0.34,
            capstyle="round",
            joinstyle="round",
            zorder=2,
        )
        ax.add_collection(background)

    if heat_segments:
        values = pd.Series(heat_values)
        vmin, vmax = choose_visual_range(values, metric)

        if pd.isna(vmax) or vmax == vmin:
            vmax = vmin + 1

        norm = colors.Normalize(vmin=vmin, vmax=vmax, clip=True)

        # Dark outline behind colored roads.
        heat_casing = LineCollection(
            heat_segments,
            colors="#080808",
            linewidths=[width + 0.85 for width in heat_widths],
            alpha=0.90,
            capstyle="round",
            joinstyle="round",
            zorder=4,
        )
        ax.add_collection(heat_casing)

        # Soft glow behind active roads.
        heat_glow = LineCollection(
            heat_segments,
            cmap=cmap_name,
            norm=norm,
            linewidths=[width + 3.6 for width in heat_widths],
            alpha=0.15,
            capstyle="round",
            joinstyle="round",
            zorder=3,
        )
        heat_glow.set_array(values.to_numpy())
        ax.add_collection(heat_glow)

        add_value_weighted_glow(ax, heat_segments, heat_widths, values.to_numpy(), cmap_name, norm, metric)

        heat_lines = LineCollection(
            heat_segments,
            cmap=cmap_name,
            norm=norm,
            linewidths=heat_widths,
            capstyle="round",
            joinstyle="round",
            zorder=5,
        )

        heat_lines.set_array(values.to_numpy())
        ax.add_collection(heat_lines)

        colorbar = fig.colorbar(heat_lines, ax=ax, fraction=0.035, pad=0.015)
        colorbar.set_label(colorbar_label, color="white", fontsize=11)
        set_clean_colorbar_ticks(colorbar, vmin, vmax, tick_count=5)
        colorbar.ax.tick_params(colors="white", labelsize=10)
        colorbar.outline.set_edgecolor("white")
        colorbar.outline.set_linewidth(1.2)
        plt.setp(colorbar.ax.get_yticklabels(), color="white")
    else:
        print("Warning: no telemetry segments were matched. Check EdgeID values.")

    draw_road_labels(ax, labeled_network_df)

    # Keep all four heatmaps on the same layout.
    add_route_shields(ax, labeled_network_df)
    if show_markers:
        add_top_bottleneck_markers(ax, heat_segments, heat_values, metric)
    add_stats_card(
        ax,
        metric,
        heat_values,
        background_count=len(background_segments),
        heat_count=len(heat_segments),
        merged_df=labeled_network_df,
    )

    set_tight_map_bounds(ax, background_segments, pad_ratio=0.025)
    ax.set_aspect("equal", adjustable="box")
    ax.axis("off")

    friendly_titles = {
        "bottleneck_score": "Road Bottleneck Hotspots",
        "total_wait_added_s": "Total Vehicle Wait Time",
        "estimated_flow_veh_per_hr": "Estimated Traffic Flow",
        "avg_speed_mph": "Average Road Speed",
    }

    title = friendly_titles.get(metric, metric)
    ax.set_title(
        f"RoadMap\n{title}",
        fontsize=24,
        color="white",
        weight="bold",
        pad=24,
    )

    ax.text(
        0.99,
        0.01,
        "RoadMap  •  Summer 2026",
        transform=ax.transAxes,
        ha="right",
        va="bottom",
        fontsize=9,
        color="#888888",
        zorder=40,
    )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    plt.tight_layout(pad=0.35)
    plt.savefig(output_path, dpi=300, facecolor=fig.get_facecolor(), bbox_inches="tight", pad_inches=0.06)

    transparent_output_path = output_path.with_name(output_path.stem + "_transparent" + output_path.suffix)
    plt.savefig(transparent_output_path, dpi=300, transparent=True, bbox_inches="tight", pad_inches=0.06)
    plt.close()

    print(f"Saved heatmap to: {output_path}")
    print(f"Saved transparent heatmap to: {transparent_output_path}")

# Old speed legend helper kept in case we want it later.
def add_speed_legend(ax):
    """Old speed legend helper. The colorbar is used now, but this is left here just in case."""
    legend_items = [
        ("0 – 15", "#d7191c"),
        ("15 – 25", "#fdae61"),
        ("25 – 35", "#ffffbf"),
        ("35 – 45", "#a6d96a"),
        ("45 – 70", "#1a9641"),
    ]

    handles = [
        Line2D([0], [0], color=color, lw=3, label=label)
        for label, color in legend_items
    ]

    legend = ax.legend(
        handles=handles,
        title="Speed (mph)",
        loc="upper left",
        bbox_to_anchor=(0.02, 0.98),
        frameon=True,
        facecolor="#070B10",
        edgecolor="#777777",
        fontsize=8,
        title_fontsize=9,
    )

    legend.get_title().set_color("white")

    for text in legend.get_texts():
        text.set_color("white")

# Draw simple route shields for common roads like I-4, 50, 417, and 528.
def add_route_shields(ax, network_df: pd.DataFrame):
    """Draw simple route shields for common major roads."""
    if "ref" not in network_df.columns:
        return

    refs_to_draw = {}
    allowed_refs = ["I 4", "I-4", "US 441", "441", "FL 50", "SR 50", "50", "FL 528", "SR 528", "528", "FL 417", "SR 417", "417"]

    for _, row in network_df.iterrows():
        ref = simplify_osm_value(row.get("ref"))
        if not ref:
            continue

        matched = None
        for allowed in allowed_refs:
            if allowed.lower() in ref.lower():
                matched = allowed
                break

        if matched is None:
            continue

        if pd.isna(row["source_x"]) or pd.isna(row["source_y"]):
            continue
        if pd.isna(row["target_x"]) or pd.isna(row["target_y"]):
            continue

        x = (float(row["source_x"]) + float(row["target_x"])) / 2
        y = (float(row["source_y"]) + float(row["target_y"])) / 2

        if matched not in refs_to_draw:
            refs_to_draw[matched] = (x, y)

    for ref, (x, y) in refs_to_draw.items():
        label = ref.replace("I ", "").replace("I-", "").replace("US ", "").replace("FL ", "").replace("SR ", "")

        ax.text(
            x,
            y,
            label,
            fontsize=8,
            color="#111111",
            ha="center",
            va="center",
            fontweight="bold",
            bbox=dict(
                boxstyle="round,pad=0.22",
                facecolor="white",
                edgecolor="#333333",
                linewidth=0.8,
            ),
            zorder=30,
        )
        

# Set up command-line options and run the heatmap generator.
def main():
    parser = argparse.ArgumentParser(description="Create RoadMap telemetry heatmaps.")

    BASE_DIR = Path(__file__).resolve().parents[2]

    # Use the Waterford network graph by default when this script
    # is run directly from PowerShell or VS Code.
    parser.add_argument(
        "--network",
        default=BASE_DIR / "data/network/network_graph_waterford.csv",
        help="Path to network graph CSV.",
    )

    parser.add_argument(
        "--edge-metrics",
        default=BASE_DIR / "outputs/telemetry/edge_metrics.csv",
        help="Path to telemetry edge metrics CSV.",
    )

    parser.add_argument(
        "--metric",
        default="bottleneck_score",
        choices=[
            "bottleneck_score",
            "total_wait_added_s",
            "estimated_flow_veh_per_hr",
            "avg_speed_mph",
        ],
        help="Telemetry metric to visualize.",
    )

    parser.add_argument(
        "--output",
        default=None,
        help="Output PNG filename. If omitted, one is created automatically.",
    )

    parser.add_argument(
        "--water-geojson",
        default=None,
        help="Optional GeoJSON polygon layer for water. Must use same coordinates as network_graph.csv.",
    )

    parser.add_argument(
        "--parks-geojson",
        default=None,
        help="Optional GeoJSON polygon layer for parks/green space. Must use same coordinates as network_graph.csv.",
    )

    parser.add_argument(
        "--buildings-geojson",
        default=None,
        help="Optional GeoJSON polygon layer for buildings. Must use same coordinates as network_graph.csv.",
    )

    parser.add_argument(
        "--show-markers",
        action="store_true",
        help="Optional: draw numbered circles on the top bottlenecks. Off by default for a cleaner final-product map.",
    )

    args = parser.parse_args()

    output_path = (
        Path(args.output)
        if args.output
        else BASE_DIR / f"outputs/heatmaps/heatmap_{args.metric}.png"
    )

    network_df, metrics_df = load_files(Path(args.network), Path(args.edge_metrics))
    plot_heatmap(
        network_df,
        metrics_df,
        args.metric,
        output_path,
        water_geojson=args.water_geojson,
        parks_geojson=args.parks_geojson,
        buildings_geojson=args.buildings_geojson,
        show_markers=args.show_markers,
    )


if __name__ == "__main__":
    main()
