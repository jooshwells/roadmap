#ifndef VEHICLE_STATE_H
#define VEHICLE_STATE_H
#include "road.h"

#include <vector>
#include <cstdint>

struct IDMParameters {
    float accelExp;        
    float maxAccel;        
    float desiredSpeed;    
    float minGap;          
    float safeBrakePower;  
    float safeTimeHeadway; 
    float length; 
};

class VehicleState {

    public:
        /* Write Functions */
        void accelerate(float amount); 
        void move(float distance);     
        void setPos(float new_pos);
        void setLeader(VehicleState* newLeader);
        void setLane(int newLane);
        void setDesiredSpeed(float new_des_speed);
        
        // NEW: Allow physics engine to update these values
        void setAcceleration(float accel); 
        void updateWaitTime(float dt, float speedThreshold = 0.5f);

        /* Read Functions */
        inline int   getId() const                { return id; }
        inline float getSpeed() const             { return m_speed; }
        inline float getPos() const               { return m_pos; }
        inline int   getLane() const              { return m_lane; }
        
        // NEW: Telemetry getters
        inline float getAcceleration() const      { return m_acceleration; }
        inline float getWaitTime() const          { return m_waitTime; }
        inline uint64_t getOrigin() const         { return m_origin; }
        inline uint64_t getDestination() const    { return m_destination; }
        uint64_t getEdgeId() const; 

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

        // NEW: Added origin and destination IDs to constructor
        VehicleState(uint64_t originNode, uint64_t destNode, float iS, float iP, int startingLane, const IDMParameters& params);
        ~VehicleState();

        std::vector<uint64_t> currentRoute;
        uint64_t currentRouteIndex;

    private:
        float m_speed;
        float m_pos; 
        int m_lane; 

        // NEW: Telemetry state variables
        float m_acceleration = 0.0f;
        float m_waitTime = 0.0f;
        uint64_t m_origin;
        uint64_t m_destination;

        float accelExp; 
        float maxAccel;
        float desiredSpeed; 
        float minGap; 
        float safeBrakePower; 
        float safeTimeHeadway; 
        float m_length;

        Road* currentEdge = nullptr; 
    
        int id;
        inline static int count = 0;

        VehicleState* leader = nullptr;
};

#endif