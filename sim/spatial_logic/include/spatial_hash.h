#ifndef SPATIAL_HASH_H
#define SPATIAL_HASH_H

#include <unordered_map>
#include <vector>
#include <algorithm>
#include "vehicle_state.h"
#include "road.h"
#include "network.h"

class VehicleSpatialHash {
public:
    void rebuild(const std::vector<VehicleState*>& activeVehicles);

    VehicleState* getLeader(VehicleState* vhcl, int targetLane, Network* network);
    VehicleState* getFollower(VehicleState* vhcl, int targetLane, Network* network);
    std::vector<VehicleState*> getVehiclesOnRoad(Road* road);

private:
    std::unordered_map<Road*, std::vector<std::vector<VehicleState*>>> edgeBuckets;
};

#endif