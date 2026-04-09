#include "road.h"
#include <cstdint>

// Added edgeId (eId) to the initializer list
Road::Road(uint64_t eId, uint64_t dest, double dist, double sL, int l) 
    : edgeId(eId), destId(dest), length(dist), speedLimit(sL), lanes(l) {}

Road::~Road() {}