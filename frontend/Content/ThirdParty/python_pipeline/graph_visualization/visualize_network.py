"""
This file takes in a csv called network_graph.csv, and produces
a png visualizing the graph. The csv may be obtained from the 
visualizeNetworkForPython function of the Network class in the sim
project.
"""

import matplotlib
matplotlib.use('Agg') 
import matplotlib.pyplot as plt
import csv
import networkx as nx

def visualize_graph_from_csv(csv_file):
    # --- BULLETPROOF PRESENTATION STYLING GLOBALS ---
    bg_color = '#1A1C23'  # Deep slate/dark gray background
    
    # Force Matplotlib to use this background everywhere by default
    plt.rcParams['figure.facecolor'] = bg_color
    plt.rcParams['axes.facecolor'] = bg_color
    plt.rcParams['savefig.facecolor'] = bg_color

    G = nx.DiGraph()
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
                    
                    # Prevent self-cycles by checking if source and target are different
                    if source != target:
                        try:
                            length = float(row[2])
                        except (IndexError, ValueError):
                            length = 1.0
                            
                        G.add_edge(source, target, weight=length)
                    else:
                        G.add_node(source)
                else:
                    G.add_node(source)
                    
    except FileNotFoundError:
        print(f"Error: The file {csv_file} was not found.")
        return

    # Safety check: ensure every node has a position in case data was malformed
    for node in G.nodes():
        if node not in pos_dict:
            pos_dict[node] = (0.0, 0.0) 

    # Create figure and get axis
    fig = plt.figure(figsize=(24, 18))
    ax = plt.gca()

    # Optimized drawing parameters for presentations
    nx.draw(G, pos_dict,
            with_labels=False,        
            node_color='#00FF9D',     
            node_size=0.15,           
            edge_color='#8A91A6',     
            width=0.25,               
            alpha=0.6,                
            arrows=False,
            ax=ax)                    

    # --- CRITICAL FIX ---
    # nx.draw() completely hides the axis and its background color. 
    # We must turn it back on, set the color, and hide the borders/ticks manually.
    ax.set_axis_on()
    ax.set_facecolor(bg_color)
    
    # Hide ticks and tick labels
    ax.tick_params(left=False, bottom=False, labelleft=False, labelbottom=False)
    
    # Hide the square border (spines) around the plot
    for spine in ax.spines.values():
        spine.set_visible(False)

    # Change title color to white to contrast with the dark background
    plt.title("Geospatially Accurate Road Network", fontsize=28, color='white', pad=20)
    ax.set_aspect('equal', adjustable='box')
    
    # Save image (rcparams handles the facecolor automatically now)
    plt.savefig("network_visualization.png", bbox_inches="tight", dpi=600)
    plt.close()
    
    print("Presentation-ready graph generated and saved to network_visualization.png")

visualize_graph_from_csv("network_graph.csv")