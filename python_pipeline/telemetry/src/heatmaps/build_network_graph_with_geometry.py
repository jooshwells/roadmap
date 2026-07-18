import argparse
import csv
import json
from pathlib import Path

TELEMETRY_DIR = Path(__file__).resolve().parents[2]
PYTHON_PIPELINE_DIR = TELEMETRY_DIR.parent

DEFAULT_NODES_PATH = PYTHON_PIPELINE_DIR / "sample_out" / "waterford_nodes_orange_allroads_offline_xy.jsonl"
DEFAULT_EDGES_PATH = PYTHON_PIPELINE_DIR / "sample_out" / "waterford_edges_orange_allroads_offline_xy.jsonl"
DEFAULT_OUTPUT_PATH = TELEMETRY_DIR / "data" / "network" / "network_graph_waterford.csv"


def clean_csv_value(value):
    """Convert lists/dicts from JSON into simple CSV-friendly text."""
    if value is None:
        return ""

    if isinstance(value, list):
        return "; ".join(str(item) for item in value if item is not None)

    if isinstance(value, dict):
        return json.dumps(value)

    return value


def load_nodes(path: Path) -> dict:
    nodes = {}

    with path.open("r", encoding="utf-8") as file:
        for line in file:
            if not line.strip():
                continue

            item = json.loads(line)

            node_id = item.get("id")
            if node_id is None:
                continue

            nodes[node_id] = {
                "x": item.get("x"),
                "y": item.get("y"),
            }

    return nodes


def build_network_graph(nodes_path: Path, edges_path: Path, output_path: Path) -> None:
    nodes = load_nodes(nodes_path)

    output_path.parent.mkdir(parents=True, exist_ok=True)

    skipped_edges = 0
    written_edges = 0

    with edges_path.open("r", encoding="utf-8") as edge_file, output_path.open("w", newline="", encoding="utf-8") as out_file:
        writer = csv.DictWriter(
            out_file,
            fieldnames=[
                "edge_id",
                "source",
                "target",
                "length",
                "source_x",
                "source_y",
                "target_x",
                "target_y",
                "name",
                "ref",
                "highway",
                "geometry_xy",
            ],
        )

        writer.writeheader()

        # C++ assigns the first successfully added road edge ID 1.
        # Only increment this counter after an edge passes validation and is written.
        next_edge_id = 1

        for line in edge_file:
            if not line.strip():
                continue

            try:
                edge = json.loads(line)
            except json.JSONDecodeError:
                skipped_edges += 1
                continue

            source = edge.get("u")
            target = edge.get("v")

            source_node = nodes.get(source)
            target_node = nodes.get(target)

            if source_node is None or target_node is None:
                skipped_edges += 1
                continue

            writer.writerow({
                "edge_id": next_edge_id,
                "source": source,
                "target": target,
                "length": edge.get("length_m"),
                "source_x": source_node.get("x"),
                "source_y": source_node.get("y"),
                "target_x": target_node.get("x"),
                "target_y": target_node.get("y"),

                # These fields let the heatmap script label major roads.
                "name": clean_csv_value(edge.get("name")),
                "ref": clean_csv_value(edge.get("ref")),
                "highway": clean_csv_value(edge.get("highway")),
                "geometry_xy": json.dumps(edge.get("geometry_xy")),
            })
            written_edges += 1
            next_edge_id += 1

    print(f"Wrote network graph to: {output_path}")
    print(f"Edges written: {written_edges}")
    print(f"Edges skipped because nodes were missing: {skipped_edges}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a RoadMap network_graph CSV from node/edge JSONL files.")
    parser.add_argument("--nodes", default=DEFAULT_NODES_PATH, help="Path to nodes JSONL file.")
    parser.add_argument("--edges", default=DEFAULT_EDGES_PATH, help="Path to edges JSONL file.")
    parser.add_argument("--output", default=DEFAULT_OUTPUT_PATH, help="Output network_graph CSV path.")
    args = parser.parse_args()

    build_network_graph(
        nodes_path=Path(args.nodes),
        edges_path=Path(args.edges),
        output_path=Path(args.output),
    )


if __name__ == "__main__":
    main()
