"""Generate the 'Grid City' starter map template.

Writes a small Manhattan-style grid (7x5 intersections, two-way streets) in the
same JSONL schema the OSM pipeline emits, so it can be loaded by
NetworkBuilder::buildNetworkFromJSONL like any exported map. A wider two-lane
avenue crosses the middle in each direction; everything else is single-lane
residential. Output lands in frontend/Content/ThirdParty/MapTemplates.

Run from anywhere:  python python_pipeline/make_grid_template.py
"""

import json
import os

COLS = 7
ROWS = 5
SPACING_M = 220.0

# Same UTM-style coordinate range as the OSM exports (absolute origin is
# irrelevant: the frontend recenters on the map's bounding box).
ORIGIN_X = 461000.0
ORIGIN_Y = 3156000.0

AVENUE_ROW = ROWS // 2  # horizontal 2-lane avenue
BOULEVARD_COL = COLS // 2  # vertical 2-lane boulevard

OUT_DIR = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "frontend", "Content", "ThirdParty", "MapTemplates",
)


def node_id(col: int, row: int) -> int:
    return row * COLS + col + 1


def node_pos(col: int, row: int) -> tuple[float, float]:
    return ORIGIN_X + col * SPACING_M, ORIGIN_Y + row * SPACING_M


def make_nodes() -> list[dict]:
    nodes = []
    for row in range(ROWS):
        for col in range(COLS):
            x, y = node_pos(col, row)
            on_avenue = row == AVENUE_ROW
            on_boulevard = col == BOULEVARD_COL
            nodes.append({
                "id": node_id(col, row),
                "lon": 0.0,
                "lat": 0.0,
                "x": x,
                "y": y,
                # Signalize the busy crossings, stop-sign the rest.
                "traffic_control": "signal" if (on_avenue and on_boulevard)
                or (on_avenue and col % 2 == 0)
                or (on_boulevard and row % 2 == 0)
                else None,
            })
    return nodes


def street_props(is_major: bool) -> dict:
    if is_major:
        return {"speed_mps": 17.9, "lanes": 2, "highway": "secondary"}
    return {"speed_mps": 13.4, "lanes": 1, "highway": "residential"}


def two_way(a: tuple[int, int], b: tuple[int, int], is_major: bool) -> list[dict]:
    """Two directed edges (the frontend's road editor exports two-ways the same way)."""
    edges = []
    for (ca, ra), (cb, rb) in ((a, b), (b, a)):
        ax, ay = node_pos(ca, ra)
        bx, by = node_pos(cb, rb)
        edges.append({
            "u": node_id(ca, ra),
            "v": node_id(cb, rb),
            "length_m": SPACING_M,
            **street_props(is_major),
            "oneway": True,
            "geometry_xy": [{"x": ax, "y": ay}, {"x": bx, "y": by}],
        })
    return edges


def make_edges() -> list[dict]:
    edges = []
    for row in range(ROWS):
        for col in range(COLS):
            if col + 1 < COLS:  # horizontal street segment
                edges += two_way((col, row), (col + 1, row), row == AVENUE_ROW)
            if row + 1 < ROWS:  # vertical street segment
                edges += two_way((col, row), (col, row + 1), col == BOULEVARD_COL)
    return edges


def write_jsonl(path: str, records: list[dict]) -> None:
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        for record in records:
            f.write(json.dumps(record) + "\n")


if __name__ == "__main__":
    os.makedirs(OUT_DIR, exist_ok=True)
    nodes, edges = make_nodes(), make_edges()
    write_jsonl(os.path.join(OUT_DIR, "Grid_City_nodes.jsonl"), nodes)
    write_jsonl(os.path.join(OUT_DIR, "Grid_City_edges.jsonl"), edges)
    print(f"Grid City: {len(nodes)} nodes, {len(edges)} directed edges -> {os.path.normpath(OUT_DIR)}")
