#include "road.h"

Road::Road(int dest, double dist, double sL, int l) : destId(dest), length(dist), speedLimit(sL), lanes(l) {}
Road::~Road() {}