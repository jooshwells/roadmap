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

    plt.figure(figsize=(16, 12))

    # We now pass our custom pos_dict directly! 
    # Notice we completely removed the slow spring_layout algorithm.
    nx.draw(G, pos_dict,
            with_labels=True,
            node_color='#A0CBE2',     
            node_size=100,            # Shrunk slightly for tighter geographic mapping
            edge_color='gray',        
            linewidths=1,
            font_size=6,              # Shrunk text so it doesn't overlap on dense roads
            font_weight='bold',
            arrows=True,              
            arrowsize=8)

    plt.title("Geospatially Accurate Road Network", fontsize=16)
    plt.gca().set_aspect('equal', adjustable='box')
    plt.savefig("network_visualization.png", bbox_inches="tight", dpi=300)
    plt.close()
    
    print("Graph generated instantly and saved to network_visualization.png")

visualize_graph_from_csv("network_graph.csv")