import matplotlib
matplotlib.use('Agg') 
import matplotlib.pyplot as plt
import csv
import networkx as nx

def visualize_graph_from_csv(csv_file):
    G = nx.DiGraph()
    
    # Dictionary to store exact (X, Y) coordinates for every node
    pos_dict = {} 

    try:
        with open(csv_file, mode='r') as file:
            reader = csv.reader(file)
            next(reader)  # Skip the header row

            for row in reader:
                if not row:
                    continue
                
                source = row[0]
                
                # Extract X and Y coordinates (Columns index 3 and 4)
                try:
                    source_x = float(row[3])
                    source_y = float(row[4])
                    pos_dict[source] = (source_x, source_y)
                except (IndexError, ValueError):
                    pass # Skip if coords are missing
                
                # Check if there is a target
                if len(row) > 1 and row[1].strip() != '':
                    target = row[1]
                    
                    try:
                        length = float(row[2])
                    except (IndexError, ValueError):
                        length = 1.0
                        
                    G.add_edge(source, target, weight=length)
                else:
                    G.add_node(source)
                    
    except FileNotFoundError:
        print(f"Error: The file {csv_file} was not found.")
        return

    # Safety check: ensure every node has a position in case data was malformed
    for node in G.nodes():
        if node not in pos_dict:
            pos_dict[node] = (0.0, 0.0) 

    # Increase figure size for a higher resolution canvas
    plt.figure(figsize=(24, 18), facecolor='white')

    # Optimized drawing parameters for massive networks
    nx.draw(G, pos_dict,
            with_labels=False,        # Turned off to prevent overlapping text blackouts
            node_color='red',         # Changed for contrast, though nodes will be tiny
            node_size=0.1,            # Drastically shrunk nodes
            edge_color='#333333',     # Dark gray for roads
            width=0.2,                # Very thin lines for edges
            alpha=0.5,                # Transparency so dense areas don't clump into a solid block
            arrows=False)             # Turned off arrows; they add too much visual noise

    plt.title("Geospatially Accurate Road Network", fontsize=24)
    plt.gca().set_aspect('equal', adjustable='box')
    
    # Use a higher DPI for crisp lines on zooming
    plt.savefig("network_visualization.png", bbox_inches="tight", dpi=600)
    plt.close()
    
    print("Graph generated instantly and saved to network_visualization.png")

visualize_graph_from_csv("network_graph.csv")