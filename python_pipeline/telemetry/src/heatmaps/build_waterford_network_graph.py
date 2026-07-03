import json
import csv
from pathlib import Path

TELEMETRY_DIR = Path(__file__).resolve().parents[2]
PYTHON_PIPELINE_DIR = TELEMETRY_DIR.parent

NODES_PATH = PYTHON_PIPELINE_DIR / "sample_out" / "waterford_nodes_orange_allroads_offline_xy.jsonl"
EDGES_PATH = PYTHON_PIPELINE_DIR / "sample_out" / "waterford_edges_orange_allroads_offline_xy.jsonl"

OUTPUT_PATH = TELEMETRY_DIR / "data" / "network" / "network_graph_waterford.csv"


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


def main() -> None:
    nodes = load_nodes(NODES_PATH)

    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)

    with EDGES_PATH.open("r", encoding="utf-8") as edge_file, OUTPUT_PATH.open("w", newline="", encoding="utf-8") as out_file:
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
            ],
        )

        writer.writeheader()

        for edge_id, line in enumerate(edge_file):
            if not line.strip():
                continue

            edge = json.loads(line)

            source = edge.get("u")
            target = edge.get("v")

            source_node = nodes.get(source)
            target_node = nodes.get(target)

            if source_node is None or target_node is None:
                continue

            writer.writerow({
                "edge_id": edge_id,
                "source": source,
                "target": target,
                "length": edge.get("length_m"),
                "source_x": source_node.get("x"),
                "source_y": source_node.get("y"),
                "target_x": target_node.get("x"),
                "target_y": target_node.get("y"),
            })

    print(f"Wrote Waterford network graph to: {OUTPUT_PATH}")


if __name__ == "__main__":
    main()