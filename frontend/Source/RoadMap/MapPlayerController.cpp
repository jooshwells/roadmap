#include "MapPlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "RoadNetworkVisualizer.h"
#include "Blueprint/UserWidget.h"
#include "RoadEditorWidget.h"
#include "RoadToolbarWidget.h"
#include "SimControlBarWidget.h"
#include "VehicleStatsWidget.h"
#include "IntersectionInspectorWidget.h"
#include "RoadTurnLaneOptions.h"
#include "SimulationManager.h"
#include "DrawDebugHelpers.h"


void AMapPlayerController::BeginPlay()
{
	Super::BeginPlay();
	
	// Ensure the mouse cursor is visible over the map
	bShowMouseCursor = true; 
	bEnableClickEvents = true; 
	bEnableMouseOverEvents = true;

	// Built-in C++ road toolbar: draw-mode toggle plus lanes / speed limit /
	// two-way / turn lanes for new roads. Needs no UMG asset.
	if (bUseBuiltInRoadToolbar)
	{
		ActiveRoadToolbar = CreateWidget<URoadToolbarWidget>(this);
		if (ActiveRoadToolbar)
		{
			ActiveRoadToolbar->AddToViewport();
		}
	}

	// Built-in C++ sim control bar: play / pause / stop and playback speed,
	// anchored top-center like a video player. Needs no UMG asset.
	if (bUseBuiltInSimControlBar)
	{
		ActiveSimControlBar = CreateWidget<USimControlBarWidget>(this);
		if (ActiveSimControlBar)
		{
			ActiveSimControlBar->AddToViewport();
		}
	}
}

void AMapPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// Bind the "LeftClick" action to our custom function
	if (InputComponent)
	{
		InputComponent->BindAction("LeftClick", IE_Pressed, this, &AMapPlayerController::OnLeftMouseClick);

		// Right-click cancels a pending road placement. Bound directly to the
		// key (no project action mapping needed) and non-consuming so camera
		// controls on right mouse keep working.
		FInputKeyBinding& CancelBinding = InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AMapPlayerController::CancelRoadDrawing);
		CancelBinding.bConsumeInput = false;
	}
}

void AMapPlayerController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bIsDrawingMode && bHasStartNode && CachedVisualizer)
	{
		UpdateRoadPreview(DeltaTime);
	}
}

ARoadNetworkVisualizer* AMapPlayerController::ResolveVisualizer()
{
	if (!CachedVisualizer)
	{
		AActor* FoundActor = UGameplayStatics::GetActorOfClass(GetWorld(), ARoadNetworkVisualizer::StaticClass());
		CachedVisualizer = Cast<ARoadNetworkVisualizer>(FoundActor);
	}
	return CachedVisualizer;
}

ASimulationManager* AMapPlayerController::ResolveSimManager()
{
	if (!CachedSimManager)
	{
		AActor* FoundActor = UGameplayStatics::GetActorOfClass(GetWorld(), ASimulationManager::StaticClass());
		CachedSimManager = Cast<ASimulationManager>(FoundActor);
	}
	return CachedSimManager;
}

bool AMapPlayerController::IsRoadEditingAllowed()
{
	ASimulationManager* SimManager = ResolveSimManager();
	return !SimManager || !SimManager->bSimulationRunning;
}

void AMapPlayerController::NotifySimulationStarted()
{
	// Drop any editing in progress: the map is view-only until the sim stops.
	bIsDrawingMode = false;
	bHasStartNode = false;
	PreviewChainPoints.Reset();
	PreviewNewIntersections.Reset();

	if (ActiveRoadToolbar)
	{
		ActiveRoadToolbar->SyncDrawState(false);
	}

	// Close the edit panel; clicking a road reopens it read-only.
	if (ActiveRoadEditor && ActiveRoadEditor->IsInViewport())
	{
		ActiveRoadEditor->RemoveFromParent();
	}
}

void AMapPlayerController::CancelRoadDrawing()
{
	if (bIsDrawingMode && bHasStartNode)
	{
		bHasStartNode = false;
		PreviewChainPoints.Reset();
		PreviewNewIntersections.Reset();
	}
}

void AMapPlayerController::UpdateRoadPreview(float DeltaTime)
{
	FVector MouseLoc;
	if (!GetMouseIntersectionOnZPlane(MouseLoc)) return;

	// The snap + crossing queries walk the whole edge set, so only recompute
	// on a short interval and when the cursor actually moved; the cached
	// chain is redrawn every frame.
	PreviewRefreshTimer -= DeltaTime;
	const bool bMoved = FVector::DistSquared2D(MouseLoc, LastPreviewQueryLoc) > FMath::Square(50.0f);
	if (PreviewRefreshTimer <= 0.0f && (bMoved || PreviewChainPoints.Num() < 2))
	{
		PreviewRefreshTimer = 0.1f;
		LastPreviewQueryLoc = MouseLoc;

		PreviewChainPoints.Reset();
		PreviewNewIntersections.Reset();
		bPreviewEndSnappedToNode = false;
		bPreviewEndOnEdge = false;

		// Where would the road end?
		FVector EndLoc = MouseLoc;
		int64 EndNodeId = -1;
		int64 SnappedNodeId, EdgeU, EdgeV;
		FVector SnappedLoc, EdgePoint;
		if (CachedVisualizer->FindClosestNode(MouseLoc, SnapRadius, SnappedLoc, SnappedNodeId))
		{
			EndLoc = SnappedLoc;
			EndNodeId = SnappedNodeId;
			bPreviewEndSnappedToNode = true;
		}
		else if (CachedVisualizer->FindClosestEdge(MouseLoc, SnapRadius, EdgePoint, EdgeU, EdgeV))
		{
			EndLoc = EdgePoint;
			bPreviewEndOnEdge = true;
		}

		// Walk the same crossing chain the real placement will use, without
		// mutating anything.
		PreviewChainPoints.Add(StartNodeLocation);
		FVector CurrentLoc = StartNodeLocation;
		int64 CurrentId = StartNodeId;
		for (int32 Guard = 0; Guard < 16; Guard++)
		{
			TArray<int64> IgnoreNodes;
			if (CurrentId != -1) IgnoreNodes.Add(CurrentId);
			if (EndNodeId != -1) IgnoreNodes.Add(EndNodeId);

			FVector CrossPoint;
			int64 CrossU, CrossV, ExistingNodeId;
			if (!CachedVisualizer->FindFirstCrossing(CurrentLoc, EndLoc, IgnoreNodes, CrossPoint, CrossU, CrossV, ExistingNodeId))
			{
				break;
			}

			PreviewChainPoints.Add(CrossPoint);
			if (ExistingNodeId == -1)
			{
				PreviewNewIntersections.Add(CrossPoint);
			}
			CurrentLoc = CrossPoint;
			CurrentId = ExistingNodeId; // -1 for a would-be split (no node yet)
		}
		PreviewChainPoints.Add(EndLoc);
	}

	// Draw the cached ghost every frame, floated slightly above the roads --
	// at deck height when an elevated layer is selected, so the preview shows
	// where the bridge will run (underpasses keep the surface ghost visible).
	const float LayerLiftCm = FMath::Max(0, CurrentDrawLayer)
		* static_cast<float>(Network::DefaultLayerHeightM) * 100.0f;
	const FVector Lift(0.0f, 0.0f, 30.0f + LayerLiftCm);
	const FColor LineColor = FColor::Cyan;
	for (int32 i = 0; i + 1 < PreviewChainPoints.Num(); i++)
	{
		DrawDebugLine(GetWorld(), PreviewChainPoints[i] + Lift, PreviewChainPoints[i + 1] + Lift, LineColor, false, 0.0f, 0, 120.0f);
	}

	if (PreviewChainPoints.Num() > 0)
	{
		// Start marker.
		DrawDebugSphere(GetWorld(), PreviewChainPoints[0] + Lift, 300.0f, 12, FColor::Cyan, false, 0.0f, 0, 40.0f);

		// End marker: green = snapping to a node, orange = will split a road,
		// white = new node in empty space.
		const FColor EndColor = bPreviewEndSnappedToNode ? FColor::Green : (bPreviewEndOnEdge ? FColor::Orange : FColor::White);
		DrawDebugSphere(GetWorld(), PreviewChainPoints.Last() + Lift, 300.0f, 12, EndColor, false, 0.0f, 0, 40.0f);
	}

	// Yellow markers where existing roads would be split into intersections.
	for (const FVector& Cross : PreviewNewIntersections)
	{
		DrawDebugSphere(GetWorld(), Cross + Lift, 250.0f, 12, FColor::Yellow, false, 0.0f, 0, 40.0f);
	}
}

void AMapPlayerController::OpenRoadEditor(const FRoadEdgeInfo& EdgeInfo, const FString& RoadName)
{
    // Retarget an already-open panel instead of stacking a second one.
    if (ActiveRoadEditor && ActiveRoadEditor->IsInViewport())
    {
        ActiveRoadEditor->UpdateRoadDisplay(EdgeInfo, RoadName);
        return;
    }

    ActiveRoadEditor = CreateWidget<URoadEditorWidget>(this);
    if (ActiveRoadEditor)
    {
        ActiveRoadEditor->UpdateRoadDisplay(EdgeInfo, RoadName);
        ActiveRoadEditor->AddToViewport(10);
    }
}

void AMapPlayerController::OpenVehicleStats(ASimulationManager* SimManager, const FVehicleIDMStats& Stats)
{
    // Retarget an already-open panel instead of stacking a second one.
    if (ActiveVehicleStats && ActiveVehicleStats->IsInViewport())
    {
        ActiveVehicleStats->InitWithStats(SimManager, Stats);
        return;
    }

    ActiveVehicleStats = CreateWidget<UVehicleStatsWidget>(this);
    if (ActiveVehicleStats)
    {
        // Relay the panel's X button to Blueprints so a follow-camera can
        // unlock when the panel closes, not only on spacebar.
        ActiveVehicleStats->OnClosed.AddWeakLambda(this, [this]()
        {
            OnVehicleStatsClosed.Broadcast();
        });
        ActiveVehicleStats->InitWithStats(SimManager, Stats);
        ActiveVehicleStats->AddToViewport(10);
    }
}

bool AMapPlayerController::TryOpenIntersectionAtCursor()
{
    // No physical hit to anchor on: project the mouse onto the ground plane.
    // Only reliable for ground-level junctions; clicks that land on geometry
    // go through TryOpenIntersectionAt with the real impact point instead.
    FVector ClickLoc;
    if (!GetMouseIntersectionOnZPlane(ClickLoc)) return false;
    return TryOpenIntersectionAt(ClickLoc, NodeInspectRadius);
}

bool AMapPlayerController::TryOpenIntersectionAt(const FVector& ClickLoc, float SearchRadiusCM)
{
    ARoadNetworkVisualizer* Visualizer = ResolveVisualizer();
    ASimulationManager* SimManager = ResolveSimManager();
    if (!Visualizer || !SimManager) return false;

    int64 NodeId;
    FVector NodeLoc;
    if (!Visualizer->FindClosestInspectableNode(ClickLoc, SearchRadiusCM, NodeLoc, NodeId)) return false;

    FIntersectionNodeInfo NodeInfo;
    if (!SimManager->GetIntersectionInfo(NodeId, NodeInfo)) return false;

    UE_LOG(LogTemp, Warning, TEXT("Inspecting intersection node %lld (%s)"), NodeId, *NodeInfo.ControlType);

    OnIntersectionClickedUI(NodeInfo);
    if (!bUseCustomIntersectionUI)
    {
        OpenIntersectionInspector(SimManager, NodeInfo);
    }
    return true;
}

void AMapPlayerController::OpenIntersectionInspector(ASimulationManager* SimManager, const FIntersectionNodeInfo& NodeInfo)
{
    // Retarget an already-open panel instead of stacking a second one.
    if (ActiveIntersectionInspector && ActiveIntersectionInspector->IsInViewport())
    {
        ActiveIntersectionInspector->InitWithInfo(SimManager, NodeInfo);
        return;
    }

    ActiveIntersectionInspector = CreateWidget<UIntersectionInspectorWidget>(this);
    if (ActiveIntersectionInspector)
    {
        ActiveIntersectionInspector->InitWithInfo(SimManager, NodeInfo);
        ActiveIntersectionInspector->AddToViewport(10);
    }
}

bool AMapPlayerController::ApplyRoadEdit(const FRoadEdgeInfo& EditedInfo, bool bBothDirections)
{
	if (!IsRoadEditingAllowed())
	{
		return false;
	}

	ARoadNetworkVisualizer* Visualizer = ResolveVisualizer();
	if (!Visualizer || EditedInfo.NodeU < 0 || EditedInfo.NodeV < 0) return false;

	// Visual network + JSONL + rebuild...
	const bool bApplied = Visualizer->UpdateRoadProperties(
		EditedInfo.NodeU, EditedInfo.NodeV, EditedInfo.Lanes, EditedInfo.SpeedLimitMps, EditedInfo.TurnLanes, EditedInfo.Layer, bBothDirections);
	if (!bApplied) return false;

	// ...then the live sim, plus replans nearby so traffic reacts to the new
	// speed/capacity.
	AActor* SimManagerActor = UGameplayStatics::GetActorOfClass(GetWorld(), ASimulationManager::StaticClass());
	if (ASimulationManager* SimManager = Cast<ASimulationManager>(SimManagerActor))
	{
		SimManager->UpdateBackendRoad(EditedInfo.NodeU, EditedInfo.NodeV, EditedInfo.Lanes, EditedInfo.SpeedLimitMps, EditedInfo.TurnLanes, bBothDirections);

		// Lane/speed edits can flip which approach yields, and lane count
		// moves the fixture off the outer lane edge; replant them.
		SimManager->RebuildTrafficControls();

		FVector ULoc, VLoc;
		if (Visualizer->GetNodeLocation(EditedInfo.NodeU, ULoc) && Visualizer->GetNodeLocation(EditedInfo.NodeV, VLoc))
		{
			const FVector Mid = (ULoc + VLoc) * 0.5f;
			const float RadiusM = FVector::Dist2D(ULoc, VLoc) / 100.0f * 0.5f + 300.0f;
			SimManager->RequestBackendReroutes(Mid, RadiusM);
		}
	}

	return true;
}

bool AMapPlayerController::DeleteRoad(const FRoadEdgeInfo& EdgeInfo, bool bBothDirections)
{
	if (!IsRoadEditingAllowed())
	{
		return false;
	}

	ARoadNetworkVisualizer* Visualizer = ResolveVisualizer();
	if (!Visualizer || EdgeInfo.NodeU < 0 || EdgeInfo.NodeV < 0) return false;

	// Grab the endpoints before the deletion possibly removes the nodes; the
	// reroute request below needs a center point.
	FVector ULoc = FVector::ZeroVector, VLoc = FVector::ZeroVector;
	const bool bHaveLocs = Visualizer->GetNodeLocation(EdgeInfo.NodeU, ULoc) && Visualizer->GetNodeLocation(EdgeInfo.NodeV, VLoc);

	// Live sim first: it needs the edge still present in its own graph, and
	// despawning/rerouting works off route node ids, not the visual network.
	AActor* SimManagerActor = UGameplayStatics::GetActorOfClass(GetWorld(), ASimulationManager::StaticClass());
	ASimulationManager* SimManager = Cast<ASimulationManager>(SimManagerActor);
	if (SimManager)
	{
		SimManager->DeleteBackendRoad(EdgeInfo.NodeU, EdgeInfo.NodeV, bBothDirections);
	}

	// Then the visual network + JSONL files + rebuild.
	if (!Visualizer->DeleteRoad(EdgeInfo.NodeU, EdgeInfo.NodeV, bBothDirections)) return false;

	// A removed approach can demote an intersection back to pass-through (or
	// re-prioritize a yield); drop the now-orphaned fixtures.
	if (SimManager)
	{
		SimManager->RebuildTrafficControls();
	}

	// Traffic near the removed road should discover detours instead of only
	// new spawns routing around it.
	if (SimManager && bHaveLocs)
	{
		const FVector Mid = (ULoc + VLoc) * 0.5f;
		const float RadiusM = FVector::Dist2D(ULoc, VLoc) / 100.0f * 0.5f + 300.0f;
		SimManager->RequestBackendReroutes(Mid, RadiusM);
	}

	return true;
}

bool AMapPlayerController::GetMouseIntersectionOnZPlane(FVector& OutIntersection)
{
	FVector WorldLocation, WorldDirection;
	if (DeprojectMousePositionToWorld(WorldLocation, WorldDirection))
	{
		if (WorldDirection.Z != 0.0f)
		{
			float t = -WorldLocation.Z / WorldDirection.Z;
			OutIntersection = WorldLocation + (WorldDirection * t);
			OutIntersection.Z = 0.0f;
			return true;
		}
	}
	return false;
}

void AMapPlayerController::SetDrawMode(bool bEnable, int32 InLanes, bool bTwoWay, float InSpeedLimit, FString InTurnLanes, int32 InLayer)
{
	if (bEnable && !IsRoadEditingAllowed())
	{
		bEnable = false;
	}

	bIsDrawingMode = bEnable;
    CurrentDrawLanes = InLanes;
    bIsTwoWayStreet = bTwoWay;
    CurrentDrawSpeedLimit = InSpeedLimit;
    CurrentDrawTurnLanes = InTurnLanes;
    CurrentDrawLayer = InLayer;
    bHasStartNode = false;
    PreviewChainPoints.Reset();
    PreviewNewIntersections.Reset();

	if (bEnable)
	{
		// Find the visualizer in the world when draw mode starts
		ResolveVisualizer();

		// Drawing and editing at once is confusing; close the edit panel.
		if (ActiveRoadEditor && ActiveRoadEditor->IsInViewport())
		{
			ActiveRoadEditor->RemoveFromParent();
		}
	}
}

void AMapPlayerController::OnLeftMouseClick()
{
    if (!bIsDrawingMode)
    {
        // 1. Check if the input action is firing at all
        UE_LOG(LogTemp, Warning, TEXT("=== CLICK REGISTERED ==="));

        FHitResult HitResult;
        bool bHit = GetHitResultUnderCursor(ECC_Visibility, false, HitResult);

        if (bHit)
        {
            AActor* HitActor = HitResult.GetActor();

            // 2. Check WHAT the raycast actually hit
            FString HitName = HitActor ? HitActor->GetName() : TEXT("Unknown Actor");
            UE_LOG(LogTemp, Warning, TEXT("Raycast Hit: %s"), *HitName);

            ARoadNetworkVisualizer* ClickedVisualizer = Cast<ARoadNetworkVisualizer>(HitActor);
            if (ClickedVisualizer)
            {
                // 3. Confirm we recognized it as your specific visualizer class
                UE_LOG(LogTemp, Warning, TEXT("Successfully cast to RoadNetworkVisualizer."));

                int32 HitInstanceIndex = HitResult.Item;

                // 4. Check the instance index. Only road HISM hits carry a
                // meaningful edge instance; node caps and the junction
                // pavement mesh belong to the same actor but describe an
                // intersection, not a road.
                UE_LOG(LogTemp, Warning, TEXT("Hit Instance Index: %d"), HitInstanceIndex);

                const bool bRoadComponentHit = HitResult.GetComponent() == ClickedVisualizer->RoadHISM;
                if (bRoadComponentHit && HitInstanceIndex != INDEX_NONE)
                {
                    int64 EdgeId = ClickedVisualizer->GetEdgeIdFromHitItem(HitInstanceIndex);
                    UE_LOG(LogTemp, Warning, TEXT("SUCCESS! Edge ID: %lld"), EdgeId);

                    // Open the road-edit UI with this edge's current values.
                    FRoadEdgeInfo EdgeInfo;
                    if (EdgeId != -1 && ClickedVisualizer->GetEdgeInfo(EdgeId, EdgeInfo))
                    {
                        OnRoadClickedUI(EdgeInfo);
                        if (!bUseCustomRoadEditorUI)
                        {
                            // Retrieve the road name and pass both parameters to C++
                            FString RoadName = ClickedVisualizer->GetRoadNameFromEdgeId(EdgeId);
                            OpenRoadEditor(EdgeInfo, RoadName);
                        }
                        return;
                    }
                }

                // Junction pavement / node cap: inspect the intersection
                // here. Anchor the search on the actual impact point -- the
                // Z-plane projection lands elsewhere for elevated junctions
                // -- and widen the radius: the click provably hit junction
                // geometry, whose pavement can span well past 15 m on wide
                // multi-node intersections.
                if (TryOpenIntersectionAt(HitResult.ImpactPoint, NodeInspectRadius * 2.0f))
                {
                    return;
                }
            }
            // Check if we clicked the Simulation Manager (Vehicles)
            ASimulationManager* SimManager = Cast<ASimulationManager>(HitActor);
            if (SimManager)
            {
                // Verify we actually clicked the vehicle instances, not just the actor root
                if (HitResult.Item != INDEX_NONE)
                {
                    FVehicleIDMStats Stats;
                    if (SimManager->GetVehicleStatsFromInstance(HitResult.Item, Stats))
                    {
                        UE_LOG(LogTemp, Warning, TEXT("Clicked Vehicle ID: %d"), Stats.VehicleID);

                        // Fire the event to open the Widget in Blueprints!
                        OnVehicleClickedUI(Stats);

                        // Built-in live panel (speed / acceleration / driver
                        // profile) unless the project uses its own widget.
                        if (!bUseCustomVehicleStatsUI)
                        {
                            OpenVehicleStats(SimManager, Stats);
                        }
                    }
                }
                return; // End execution since we found a vehicle
            }
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("Raycast fired, but hit absolutely nothing."));
        }

        // Nothing solid resolved the click: sign/signal fixtures have no
        // collision (the ray sails through them) and clicks beside a node
        // land on empty ground, so snap to the nearest node and open the
        // intersection inspector if one is close enough.
        TryOpenIntersectionAtCursor();
        return;
    }
    else
    {
        // Belt-and-braces: if the sim started while draw mode was somehow
        // still on, exit it instead of mutating the network under moving cars.
        if (!IsRoadEditingAllowed())
        {
            NotifySimulationStarted();
            return;
        }

        if (!CachedVisualizer) return;

        FVector ClickedLocation;
        if (GetMouseIntersectionOnZPlane(ClickedLocation))
        {
            if (!bHasStartNode)
            {
                // === CLICK 1: START ON THE EXISTING NETWORK (NODE OR EDGE) ===
                int64 SnappedNodeId;
                FVector SnappedLoc;
                int64 EdgeU, EdgeV;
                FVector EdgePoint;

                if (CachedVisualizer->FindClosestNode(ClickedLocation, SnapRadius, SnappedLoc, SnappedNodeId))
                {
                    StartNodeId = SnappedNodeId;
                    StartNodeLocation = SnappedLoc;
                    bHasStartNode = true;
                }
                else if (CachedVisualizer->FindClosestEdge(ClickedLocation, SnapRadius, EdgePoint, EdgeU, EdgeV))
                {
                    // Clicked mid-road: split the edge there and start from the new node.
                    const int64 NewNodeId = SplitEdgeEverywhere(EdgeU, EdgeV, EdgePoint);
                    if (NewNodeId != -1)
                    {
                        StartNodeId = NewNodeId;
                        StartNodeLocation = EdgePoint;
                        bHasStartNode = true;
                    }
                }
            }
            else
            {
                // === CLICK 2: PLACE END NODE (NODE, EDGE SPLIT, OR EMPTY SPACE) ===
                int64 EndNodeId = -1;
                FVector EndNodeLoc = ClickedLocation;

                int64 SnappedNodeId;
                FVector SnappedLoc;
                int64 EdgeU, EdgeV;
                FVector EdgePoint;
                if (CachedVisualizer->FindClosestNode(ClickedLocation, SnapRadius, SnappedLoc, SnappedNodeId))
                {
                    EndNodeId = SnappedNodeId;
                    EndNodeLoc = SnappedLoc;
                }
                else if (CachedVisualizer->FindClosestEdge(ClickedLocation, SnapRadius, EdgePoint, EdgeU, EdgeV))
                {
                    // Ending mid-road: split the edge so the new road tees into it.
                    const int64 NewNodeId = SplitEdgeEverywhere(EdgeU, EdgeV, EdgePoint);
                    if (NewNodeId != -1)
                    {
                        EndNodeId = NewNodeId;
                        EndNodeLoc = EdgePoint;
                    }
                }

                if (EndNodeId != -1 && EndNodeId == StartNodeId)
                {
                    return;
                }

                // Find the Simulation Manager to update the live backend graph
                AActor* SimManagerActor = UGameplayStatics::GetActorOfClass(GetWorld(), ASimulationManager::StaticClass());
                ASimulationManager* SimManager = Cast<ASimulationManager>(SimManagerActor);

                // Walk from the start toward the end, splitting the drawn road at
                // every existing road it crosses so real intersections form there.
                int64 CurrentId = StartNodeId;
                FVector CurrentLoc = StartNodeLocation;
                int32 CrossingsMade = 0;
                const int32 MaxCrossings = 16;

                while (CrossingsMade < MaxCrossings)
                {
                    TArray<int64> IgnoreNodes;
                    IgnoreNodes.Add(CurrentId);
                    if (EndNodeId != -1) IgnoreNodes.Add(EndNodeId);

                    FVector CrossPoint;
                    int64 CrossU, CrossV, ExistingNodeId;
                    if (!CachedVisualizer->FindFirstCrossing(CurrentLoc, EndNodeLoc, IgnoreNodes, CrossPoint, CrossU, CrossV, ExistingNodeId))
                    {
                        break;
                    }

                    int64 MidNodeId;
                    if (ExistingNodeId != -1)
                    {
                        // The crossing lands on an existing node: route through it.
                        MidNodeId = ExistingNodeId;
                    }
                    else
                    {
                        MidNodeId = SplitEdgeEverywhere(CrossU, CrossV, CrossPoint);
                        if (MidNodeId == -1) break;
                    }
                    if (MidNodeId == CurrentId) break;

                    CreateRoadPiece(SimManager, CurrentId, CurrentLoc, MidNodeId, CrossPoint);
                    CurrentId = MidNodeId;
                    CurrentLoc = CrossPoint;
                    CrossingsMade++;
                }

                // Final piece to the clicked end point (creates the node if free).
                CreateRoadPiece(SimManager, CurrentId, CurrentLoc, EndNodeId, EndNodeLoc);

                // One rebuild picks up everything: curved-quality geometry,
                // junction pavement at the new intersections, and setbacks.
                CachedVisualizer->RefreshRoadVisuals();

                if (SimManager)
                {
                    // Plant stop signs / signals at any intersection the drawn
                    // road just created (the visual network's node controls
                    // were refreshed as each piece was committed).
                    SimManager->RebuildTrafficControls();

                    // Let traffic already driving nearby replan onto the new
                    // connection (new spawns pick it up automatically).
                    const FVector EditCenter = (StartNodeLocation + EndNodeLoc) * 0.5f;
                    const float EditRadiusM = FVector::Dist2D(StartNodeLocation, EndNodeLoc) / 100.0f * 0.5f + 300.0f;
                    SimManager->RequestBackendReroutes(EditCenter, EditRadiusM);
                }

                // Reset for the next road segment
                bHasStartNode = false;
                PreviewChainPoints.Reset();
                PreviewNewIntersections.Reset();
            }
        }
    }
}

int64 AMapPlayerController::SplitEdgeEverywhere(int64 U, int64 V, FVector Point)
{
    if (!CachedVisualizer) return -1;

    const int64 NewNodeId = CachedVisualizer->AllocateNodeId();

    // Visual network + JSONL files first...
    if (!CachedVisualizer->SplitEdgeForNewNode(U, V, NewNodeId, Point))
    {
        return -1;
    }

    // ...then the live simulation graph (patches vehicle routes too).
    AActor* SimManagerActor = UGameplayStatics::GetActorOfClass(GetWorld(), ASimulationManager::StaticClass());
    if (ASimulationManager* SimManager = Cast<ASimulationManager>(SimManagerActor))
    {
        SimManager->SplitBackendEdge(U, V, NewNodeId, Point);
    }

    return NewNodeId;
}

int64 AMapPlayerController::CreateRoadPiece(ASimulationManager* SimManager, int64 FromId, FVector FromLoc, int64 ToId, FVector ToLoc)
{
    const float LengthMeters = FVector::Dist2D(FromLoc, ToLoc) / 100.0f;

    // Forward direction (A -> B): files + visual network.
    const int64 FinalToId = CachedVisualizer->ExportNewRoadSegment(FromId, ToId, ToLoc, CurrentDrawLanes, CurrentDrawSpeedLimit, CurrentDrawTurnLanes, CurrentDrawLayer);

    if (SimManager)
    {
        SimManager->NotifyBackendOfNewRoad(FromId, FinalToId, ToLoc, LengthMeters, CurrentDrawLanes, CurrentDrawSpeedLimit);
    }

    // Reverse direction (B -> A) if Two-Way is checked. turn:lanes is ordered
    // in the direction of travel, so the reverse side gets the mirrored
    // string (lane order flipped, left/right swapped).
    if (bIsTwoWayStreet)
    {
        CachedVisualizer->ExportNewRoadSegment(FinalToId, FromId, FromLoc, CurrentDrawLanes, CurrentDrawSpeedLimit, RoadTurnLaneOptions::MirrorTurnLanes(CurrentDrawTurnLanes), CurrentDrawLayer);

        if (SimManager)
        {
            SimManager->NotifyBackendOfNewRoad(FinalToId, FromId, FromLoc, LengthMeters, CurrentDrawLanes, CurrentDrawSpeedLimit);
        }
    }

    return FinalToId;
}