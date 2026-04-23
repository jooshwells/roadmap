#ifndef ROAD_H
#define ROAD_H

#include <cstdint>

class Road {

    public:
        // Getters
        inline std::uint64_t getDest() const      { return destId; }
        inline double getSpeedLimit() const       { return speedLimit; }
        inline int getLanes() const               { return lanes; }
        inline double getLength() const           {return length; }
        // Setters
        inline void setSpeedLimit(double sL) { speedLimit = sL; }
        inline void setLanes(int l)         { lanes = l; }

        Road(uint64_t dest, double sL, double le);
        ~Road();

    private:
        std::uint64_t destId;
        double speedLimit;
        int lanes;
        double length;
};

#endif