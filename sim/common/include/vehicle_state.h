#ifndef VEHICLE_STATE_H
#define VEHICLE_STATE_H

class VehicleState {

    public:
        /* Write Functions */
        void accelerate(float amount); // accelerate by amount (m/s)
        void move(float distance);     // move by distance (meters)
        
        /* Read Functions */
        float getSpeed() const;
        float getPos() const;

        VehicleState(float initialSpeed);
        ~VehicleState();

    private:
        float m_speed;
        float m_pos; // x position as of testing with one dimension

};


#endif