#ifndef DSTARLITE_H
#define DSTARLITE_H

#include "network.h"
#include "node.h"
#include <set>
#include <functional>
#include <algorithm>
#include <limits>
#include <utility>
#include <vector>
#include <cstdint>
#include <unordered_map>

// Define the Key type for our Priority Queue
typedef std::pair<double, double> Key;

// Priority Queue Element with strict weak ordering using your unique Node IDs
struct QueueElement {
    Key key;
    Node* node;

    bool operator<(const QueueElement& other) const {
        if (key.first != other.key.first) return key.first < other.key.first;
        if (key.second != other.key.second) return key.second < other.key.second;
        
        // Strict weak ordering ensures std::set handles identical keys safely
        return node->getId() < other.node->getId(); 
    }
};

struct NodeState {
    double g = std::numeric_limits<double>::infinity();
    double rhs = std::numeric_limits<double>::infinity();
};

class DStarLite {
private:
    Network* network;
    Node* start;
    Node* goal;
    Node* last_start;
    double km; // Key modifier for when the start node moves

    std::unordered_map<uint64_t, NodeState> stateMap;
    NodeState& getState(Node* n) { return stateMap[n->getId()]; }
    
    std::set<QueueElement> U; // The Priority Queue
    
    // The injected heuristic function
    std::function<double(Node*, Node*)> heuristicFunc;

    // Internal Helpers
    Key CalculateKey(Node* s);
    void RemoveFromQueue(Node* s);
    void UpdateVertex(Node* u);

public:
    // Constructor requires the network, start/goal nodes, and your chosen heuristic
    DStarLite(Network* net, Node* s, Node* g, std::function<double(Node*, Node*)> hFunc);

    // Core pathfinding solver
    void ComputeShortestPath();

    std::vector<uint64_t> ExtractRoute(Network& map, Node* start, Node* goal);

    // Call this if a road's cost changes (e.g., traffic jam or road closure)
    // You pass the ID of the node that the changed road originates from.
    void ReevaluateNode(int nodeId);

    // Call this when your agent physically moves to a new node along the path
    void MoveStart(Node* new_start);
};

#endif // DSTARLITE_H