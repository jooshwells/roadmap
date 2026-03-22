#include "node.h"
#include <GeographicLib/LocalCartesian.hpp>

Node::Node(std::uint64_t initId, double iLon, double iLat) : id(initId), lon(iLon), lat(iLat) 
{
    projectNodes();
}

Node::Node() {}

void Node::projectNodes()
{
    GeographicLib::LocalCartesian proj(MAP_ORIGIN_LAT, MAP_ORIGIN_LON, 0);
    proj.Forward(lat, lon, 0, x, y, z); // 0 should be replaced by true geographic height in the future
}

Node::~Node() {}
