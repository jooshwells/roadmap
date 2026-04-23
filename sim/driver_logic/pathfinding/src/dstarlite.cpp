#include "dstarlite.h"

// Define Infinity once for cleaner code
const double INF = std::numeric_limits<double>::infinity();

DStarLite::DStarLite(Network* net, Node* s, Node* g, std::function<double(Node*, Node*)> hFunc) 
    : network(net), start(s), goal(g), last_start(s), km(0.0), heuristicFunc(hFunc) {
    
    // The cost to reach the goal from the goal is 0
    goal->rhs = 0.0; 
    U.insert({CalculateKey(goal), goal});
}

Key DStarLite::CalculateKey(Node* s) {
    double min_val = std::min(s->g, s->rhs);
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
            double cost = edge.getLength() + succ->g; 
            if (cost < min_rhs) {
                min_rhs = cost;
            }
        }
        u->rhs = min_rhs;
    }

    RemoveFromQueue(u);

    // If the node is inconsistent, it needs to be evaluated in the queue
    if (u->g != u->rhs) {
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
        if (CalculateKey(start) < k_old && start->rhs == start->g) {
            break; 
        }

        U.erase(U.begin()); // Pop the top element

        if (k_old < k_new) {
            // Node needs to be re-evaluated with its new higher cost
            U.insert({k_new, u});
        } 
        else if (u->g > u->rhs) {
            // Overconsistent: We found a better path
            u->g = u->rhs;
            
            // Alert all nodes pointing INTO this node that a better path exists
            for (int predId : u->incomingEdgeNodeIds) {
                Node* pred = network->getNode(predId);
                if (pred) UpdateVertex(pred);
            }
        } 
        else {
            // Underconsistent: A path got blocked or worsened
            u->g = INF;
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