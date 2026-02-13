#include "vehicle_state.h"
#include <memory>

int main()
{
    std::unique_ptr<VehicleState> vhcl1 = std::make_unique<VehicleState>(2.2);
    return 0;
}