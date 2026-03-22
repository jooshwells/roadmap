#ifndef ROAD_H
#define ROAD_H

class Road {

    public:
        // Getters
        inline int getDest()             { return destId; }
        inline double getSpeedLimit()        { return speedLimit; }
        inline int getLanes()               { return lanes; }

        // Setters
        inline void setSpeedLimit(double sL) { speedLimit = sL; }
        inline void setLanes(int l)         { lanes = l; }

        Road(int dest, double sL, double le);
        ~Road();

    private:
        int destId;
        double speedLimit;
        int lanes;
        double length;
};

#endif