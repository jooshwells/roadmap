#ifndef ROAD_H
#define ROAD_H

#include <cstdint>

class Road {

    public:
        // Getters
        inline std::uint64_t getEdgeId() const    { return edgeId; } // NEW
        inline std::uint64_t getDest() const      { return destId; }
        inline double getSpeedLimit() const       { return speedLimit; }
        inline int getLanes() const               { return lanes; }
        inline double getLength() const           { return length; }
        
        // Setters
        inline void setSpeedLimit(double sL) { speedLimit = sL; }
        inline void setLanes(int l)          { lanes = l; }
        
        // volume tracking and dynamic cost for pathfinding
        void addVehicle() { currentVolume++; }
        void removeVehicle() { if (currentVolume > 0) currentVolume--; }
        int getCurrentVolume() const { return currentVolume; }
        double getDynamicCost() const;

        // Updated constructor signature
        Road(uint64_t eId, uint64_t dest, double le, double sl, int l); 
        ~Road();

    private:
        uint64_t edgeId; // NEW: The unique identifier for this road segment
        uint64_t destId;
        double length;
        double speedLimit;
        int lanes;
        int currentVolume = 0;
};

#endif