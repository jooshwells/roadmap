#include "dstarlite.h"
#include <unordered_set>

// Define Infinity once for cleaner code
const double INF = std::numeric_limits<double>::infinity();

DStarLite::DStarLite(Network* net, Node* s, Node* g, std::function<double(Node*, Node*)> hFunc) 
    : network(net), start(s), goal(g), last_start(s), km(0.0), heuristicFunc(hFunc) {
    
    // The cost to reach the goal from the goal is 0
    getState(goal).rhs = 0.0; 
    U.insert({CalculateKey(goal), goal});
}

Key DStarLite::CalculateKey(Node* s) {
    double min_val = std::min(getState(s).g, getState(s).rhs);
    return {min_val + heuristicFunc(start, s) + km, min_val};
}

void DStarLite::RemoveFromQueue(Node* s) {
    for (auto it = U.begin(); it != U.end(); ++it) {
        if (it->node == s) {
            U.erase(it);
            return;
        }
    }
}

void DStarLite::UpdateVertex(Node* u) {
    if (u != goal) {
        double min_rhs = INF;
        
        // Loop through outgoing roads to find the cheapest path forward
        for (Road& edge : u->outgoingEdges) {
            Node* succ = network->getNode(edge.getDest());
            if (!succ) continue;
            
            // Using length as the cost. 
            // (You could also use edge.getLength() / edge.getSpeedLimit() for time)
            double cost = edge.getDynamicCost() + getState(succ).g; 
            if (cost < min_rhs) {
                min_rhs = cost;
            }
        }
        getState(u).rhs = min_rhs;;
    }

    RemoveFromQueue(u);

    // If the node is inconsistent, it needs to be evaluated in the queue
    if (getState(u).g != getState(u).rhs) {
        U.insert({CalculateKey(u), u});
    }
}

void DStarLite::ComputeShortestPath() {
    while (!U.empty()) {
        QueueElement top = *U.begin();
        Node* u = top.node;
        Key k_old = top.key;
        Key k_new = CalculateKey(u);

        // Break if the start is fully consistent and optimal
        if (CalculateKey(start) < k_old && getState(start).rhs == getState(start).g) {
            break; 
        }

        U.erase(U.begin()); // Pop the top element

        if (k_old < k_new) {
            // Node needs to be re-evaluated with its new higher cost
            U.insert({k_new, u});
        } 
        else if (getState(u).g > getState(u).rhs) {
            // Overconsistent: We found a better path
            getState(u).g = getState(u).rhs;
            
            // Alert all nodes pointing INTO this node that a better path exists
            for (int predId : u->incomingEdgeNodeIds) {
                Node* pred = network->getNode(predId);
                if (pred) UpdateVertex(pred);
            }
        } 
        else {
            // Underconsistent: A path got blocked or worsened
            getState(u).g = INF;
            UpdateVertex(u);
            
            // Alert all nodes pointing INTO this node that the path is broken
            for (int predId : u->incomingEdgeNodeIds) {
                Node* pred = network->getNode(predId);
                if (pred) UpdateVertex(pred);
            }
        }
    }
}

void DStarLite::ReevaluateNode(int nodeId) {
    Node* u = network->getNode(nodeId);
    if (u) {
        UpdateVertex(u);
        ComputeShortestPath();
    }
}

void DStarLite::MoveStart(Node* new_start) {
    if (start == new_start) return;

    // Update the key modifier (km) so we don't have to recalculate the whole queue
    km += heuristicFunc(last_start, new_start);
    
    start = new_start;
    last_start = new_start;
}

std::vector<uint64_t> DStarLite::ExtractRoute(Network& map, Node* start, Node* goal) {
    std::vector<uint64_t> path;
    Node* current = start;
    
    // Safety check in case no path exists
    if (getState(start).g == std::numeric_limits<double>::infinity()) {
        return path; 
    }

    // Prevents infinite loops if the graph's g-values are temporarily 
    // inconsistent during dynamic edge weight updates.
    std::unordered_set<uint64_t> visited;

    while (current != goal && current != nullptr) {
        path.push_back(current->getId());
        visited.insert(current->getId());
        
        double min_cost = std::numeric_limits<double>::infinity();
        Node* best_next = nullptr;

        for (Road& edge : current->outgoingEdges) {
            Node* succ = map.getNode(edge.getDest()); 
            if (!succ) continue;

            // Cycle prevention: Do not step back to a node we already visited
            if (visited.find(succ->getId()) != visited.end()) {
                continue; 
            }

            // Note: Ensure this cost matches the exact heuristic/weight used in ComputeShortestPath
            double cost = edge.getDynamicCost() + getState(succ).g;
            
            if (cost < min_cost) {
                min_cost = cost;
                best_next = succ;
            }
        }

        // We hit a dead end or a cycle trap before reaching the goal
        if (!best_next) {
            return path; // Return the partial path safely
        }
        
        current = best_next;
    }
    
    // Only append the final destination if we actually arrived there
    if (current == goal) {
        path.push_back(goal->getId());
    }
    
    return path;
}