#include "SimulationGameMode.h"
#include "MapPlayerController.h" // Include your new controller here

ASimulationGameMode::ASimulationGameMode()
{
	// Set the default player controller to our custom class
	PlayerControllerClass = AMapPlayerController::StaticClass();
}