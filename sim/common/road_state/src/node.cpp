#include "node.h"

Node::Node(std::uint64_t initId, double iLon, double iLat, double iX, double iY, const std::string& typeStr) 
    : id(initId), lon(iLon), lat(iLat), x(iX), y(iY), z(0.0)
{
    // Parse intersection type from the JSON data
    if (typeStr == "TRAFFIC_LIGHT") {
        type = Node::TRAFFIC_LIGHT;
    } 
    else if (typeStr == "FOUR_WAY_STOP") {
        type = Node::FOUR_WAY_STOP;
    } 
    else {
        type = Node::PASS_THROUGH;
    }
}

Node::Node() {}

Node::~Node() {}