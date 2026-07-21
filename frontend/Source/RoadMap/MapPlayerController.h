#pragma once
#include "Blueprint/UserWidget.h" 
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SimulationManager.h"
#include "MapPlayerController.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnVehicleStatsClosed);

UCLASS()
class ROADMAP_API AMapPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	// updated for turn lanes, speed limit, and vertical layer (0 ground,
	// +1 overpass, -1 underpass -- newly drawn roads become bridges/tunnels)
	UFUNCTION(BlueprintCallable, Category = "Map Editor")
    void SetDrawMode(bool bEnable, int32 InLanes, bool bTwoWay, float InSpeedLimit, FString InTurnLanes, int32 InLayer = 0);

	// Fired when a vehicle is clicked, in case you want a custom Blueprint
	// widget. The built-in C++ panel (VehicleStatsWidget) opens automatically
	// either way unless bUseCustomVehicleStatsUI is set.
	UFUNCTION(BlueprintImplementableEvent, Category = "UI")
	void OnVehicleClickedUI(FVehicleIDMStats VehicleStats);

	// Set true if you implement OnVehicleClickedUI with your own widget and
	// don't want the built-in C++ vehicle stats panel to open.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	bool bUseCustomVehicleStatsUI = false;

	// Fired when the built-in vehicle stats panel is closed with its X
	// button. Bind in Blueprints (e.g. the camera pawn) to release a
	// vehicle-follow camera lock alongside the spacebar shortcut.
	UPROPERTY(BlueprintAssignable, Category = "UI")
	FOnVehicleStatsClosed OnVehicleStatsClosed;

	// Fired when a road is clicked outside draw mode, in case you want a
	// custom Blueprint widget. The built-in C++ panel (RoadEditorWidget)
	// opens automatically either way unless bUseCustomRoadEditorUI is set.
	UFUNCTION(BlueprintImplementableEvent, Category = "UI")
	void OnRoadClickedUI(FRoadEdgeInfo EdgeInfo);

	// Set true if you implement OnRoadClickedUI with your own widget and
	// don't want the built-in C++ road editor panel to open.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	bool bUseCustomRoadEditorUI = false;

	// Spawns the built-in C++ road-drawing toolbar (lanes / speed / turn
	// lanes / two-way + draw toggle) at BeginPlay. Turn off if your own
	// widget drives SetDrawMode instead.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	bool bUseBuiltInRoadToolbar = true;

	// Spawns the built-in C++ sim control bar (play / pause / stop + playback
	// speed) top-center at BeginPlay. Turn off if your own UI drives
	// ASimulationManager instead.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	bool bUseBuiltInSimControlBar = true;

	// Applies edited lanes / speed limit / turn lanes to the road everywhere:
	// visual network, JSONL files, and the live simulation.
	UFUNCTION(BlueprintCallable, Category = "Map Editor")
	bool ApplyRoadEdit(const FRoadEdgeInfo& EditedInfo, bool bBothDirections);

	// Deletes the road everywhere: visual network, JSONL files, and the live
	// simulation (vehicles on it despawn, routes through it are repaired).
	// bBothDirections also removes the opposite edge of a two-way street.
	UFUNCTION(BlueprintCallable, Category = "Map Editor")
	bool DeleteRoad(const FRoadEdgeInfo& EdgeInfo, bool bBothDirections);

	// Road edits (draw / property changes / delete) are only allowed while
	// the simulation is stopped. The sim rebuilds its network from the JSONL
	// files on every start, so pre-run edits reach it without any live-graph
	// surgery; while cars are moving the map is view-only.
	UFUNCTION(BlueprintPure, Category = "Map Editor")
	bool IsRoadEditingAllowed();

	// Called by ASimulationManager::StartSimulation: exits draw mode, drops
	// any armed start node, and closes the edit panel so it reopens read-only.
	void NotifySimulationStarted();

protected:
	// Runs when the game starts
	virtual void BeginPlay() override;

	// Draws the ghost-road preview while a start node is armed
	virtual void Tick(float DeltaTime) override;

	// Where we bind our input keys
	virtual void SetupInputComponent() override;

	// The function that fires when we click
	void OnLeftMouseClick();

	// Splits edge U->V (both directions of a two-way street) at Point across
	// the visual network, the JSONL files, and the live sim backend.
	// Returns the new node's id, or -1 on failure.
	int64 SplitEdgeEverywhere(int64 U, int64 V, FVector Point);

	// Creates one road segment between two nodes (forward + reverse when
	// two-way): JSONL files, visual network, and live sim. ToId of -1 creates
	// a new node at ToLoc; the resulting end node id is returned.
	int64 CreateRoadPiece(class ASimulationManager* SimManager, int64 FromId, FVector FromLoc, int64 ToId, FVector ToLoc);

	// Finds (and caches) the road visualizer in the world.
	class ARoadNetworkVisualizer* ResolveVisualizer();

	// Finds (and caches) the simulation manager in the world.
	class ASimulationManager* ResolveSimManager();

	// Opens (or retargets) the built-in C++ road editor panel.
	void OpenRoadEditor(const FRoadEdgeInfo& EdgeInfo, const FString& RoadName = FString());

	// Opens (or retargets) the built-in C++ vehicle stats panel; it re-polls
	// SimManager while open so the readouts stay live.
	void OpenVehicleStats(class ASimulationManager* SimManager, const FVehicleIDMStats& Stats);

	// The built-in road editor panel, when open.
	UPROPERTY()
	class URoadEditorWidget* ActiveRoadEditor = nullptr;

	// The built-in vehicle stats panel, when open.
	UPROPERTY()
	class UVehicleStatsWidget* ActiveVehicleStats = nullptr;

	// The built-in road-drawing toolbar, when spawned.
	UPROPERTY()
	class URoadToolbarWidget* ActiveRoadToolbar = nullptr;

	// The built-in sim control bar, when spawned.
	UPROPERTY()
	class USimControlBarWidget* ActiveSimControlBar = nullptr;

	// Right-click while placing a road: abandon the armed start node.
	void CancelRoadDrawing();

	// Recomputes the cached ghost-road chain (throttled) and draws it.
	void UpdateRoadPreview(float DeltaTime);

	// --- Ghost-road preview state (valid while bHasStartNode) ---
	// Chain of points the road would take: start, each crossing, end.
	TArray<FVector> PreviewChainPoints;
	// Subset of chain points where an existing road would be split.
	TArray<FVector> PreviewNewIntersections;
	bool bPreviewEndSnappedToNode = false;
	bool bPreviewEndOnEdge = false;
	float PreviewRefreshTimer = 0.0f;
	FVector LastPreviewQueryLoc = FVector(TNumericLimits<float>::Max());

private:
	bool bIsDrawingMode = false;
	int32 CurrentDrawLanes = 2; // Default from UI

	bool bIsTwoWayStreet = false;

	bool bHasStartNode = false;
	int64 StartNodeId = -1;
	FVector StartNodeLocation;

	// Helper to find exactly where the mouse is on the Z=0 plane
	bool GetMouseIntersectionOnZPlane(FVector& OutIntersection);

	// How close the mouse needs to be to an intersection to snap (e.g., 2000 = 20 meters)
	float SnapRadius = 2000.0f;

	// Cached reference to your visualizer
	class ARoadNetworkVisualizer* CachedVisualizer;

	// Cached reference to the simulation manager (for the editing gate).
	class ASimulationManager* CachedSimManager = nullptr;

	float CurrentDrawSpeedLimit = 20.0f; // default around 45 mph
    FString CurrentDrawTurnLanes;   // string for turn lanes
    int32 CurrentDrawLayer = 0;     // vertical layer for new roads (0 = ground)
};