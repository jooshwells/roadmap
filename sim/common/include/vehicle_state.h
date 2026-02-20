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
        float getSpeed() const;
        float getPos() const;
        int getCount() const;
        VehicleState* getLeader() const;

        VehicleState(float initialSpeed, float initialPosition);
        ~VehicleState();

    private:
        float m_speed;
        float m_pos; // x position as of testing with one dimension
    
        int id;
        inline static int count = 0;

        VehicleState* leader =nullptr;
};


#endif