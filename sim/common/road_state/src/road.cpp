#include "road.h"
#include <cstdint>

Road::Road(uint64_t dest, double sL, double le) : destId(dest), speedLimit(sL), length(le) {}
Road::~Road() {}