#ifndef VEHICLE_STATE_H
#define VEHICLE_STATE_H
#include "road_state/include/road.h"

// add struct for diff "types" of drivers, easier to pass in args
// can add in initial speed and pos later, exlucde for ease of testign for now
struct IDMParameters {
    float accelExp;        // delta: acceleration exponent
    float maxAccel;        // a: max acceleration
    float desiredSpeed;    // v0: desired speed
    float minGap;          // s0: minimum gap
    float safeBrakePower;  // b: comfortable braking deceleration
    float safeTimeHeadway; // T: safe time headway
    float length; // vehicle length m
};

class VehicleState {

    public:
        /* Write Functions */
        void accelerate(float amount); // accelerate by amount (m/s)
        void move(float distance);     // move by distance (meters)
        void setLeader(VehicleState* newLeader);
        void setLane(int newLane);

        /* Read Functions */
        inline float getSpeed() const             { return m_speed; }
        inline float getPos() const               { return m_pos; }
        inline int   getLane() const              { return m_lane; }

        inline int   getCount() const             { return count; }
        inline float getDesiredSpeed() const      { return desiredSpeed; }
        inline float getAccelExp() const          { return accelExp; }
        inline float getMinGap() const            { return minGap; }
        inline float getSafeBrakePower() const    { return safeBrakePower; }
        inline float getSafeTimeHeadway() const   { return safeTimeHeadway; }
        inline VehicleState* getLeader() const    { return leader; }
        inline float getMaxAccel() const          { return maxAccel; }
        inline float getLength() const            { return m_length;}

        Road* getCurrentEdge() const;
        void setCurrentEdge(Road* edge);

        // now takes struct for remaining args AND starting lane number
        VehicleState(float iS, float iP, int startingLane, const IDMParameters& params);
        ~VehicleState();

    private:
        float m_speed;
        float m_pos; // x position as of testing with one dimension
        int m_lane; // keep track of lane number

        float accelExp; // determines how smooth acceleration is
        float maxAccel;
        float desiredSpeed; // velocity driver is trying to reach
        float minGap; // how close is this car willing to get to their leader?
        float safeBrakePower; // preferred braking force
        float safeTimeHeadway; // ideal time gap between this car and leader
        float m_length;

        Road* currentEdge = nullptr; //tracks curr road
    
        int id;
        inline static int count = 0;

        VehicleState* leader =nullptr;
};


#endif