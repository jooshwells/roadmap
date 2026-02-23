#ifndef VEHICLE_STATE_H
#define VEHICLE_STATE_H

class VehicleState {

    public:
        /* Write Functions */
        void accelerate(float amount); // accelerate by amount (m/s)
        void move(float distance);     // move by distance (meters)
        void setLeader(VehicleState* newLeader);
        // void update(float dt); moved to physics

        /* Read Functions */
        inline float getSpeed() const             { return m_speed; }
        inline float getPos() const               { return m_pos; }
        inline int   getCount() const             { return count; }
        inline float getDesiredSpeed() const      { return desiredSpeed; }
        inline float getAccelExp() const          { return accelExp; }
        inline float getMinGap() const            { return minGap; }
        inline float getSafeBrakePower() const    { return safeBrakePower; }
        inline float getSafeTimeHeadway() const   { return safeTimeHeadway; }
        
        inline VehicleState* getLeader() const    { return leader; }

        VehicleState(float iS, float iP, float aExp, float mA, float dS, float mG, float sB, float sTH);
        ~VehicleState();

    private:
        float m_speed;
        float m_pos; // x position as of testing with one dimension
        float accelExp; // determines how smooth acceleration is
        float maxAccel;
        float desiredSpeed; // velocity driver is trying to reach
        float minGap; // how close is this car willing to get to their leader?
        float safeBrakePower; // preferred braking force
        float safeTimeHeadway; // ideal time gap between this car and leader
    
        int id;
        inline static int count = 0;

        VehicleState* leader =nullptr;
};


#endif