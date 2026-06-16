#pragma once


#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SplineComponent.h"
#include "RoadActor.generated.h"

UCLASS()
class SD1TEST_API ARoadActor : public AActor
{
    GENERATED_BODY()

public:
    ARoadActor();

    // Mathematical path of the road segment
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Road")
	USplineComponent* RoadSpline;

	// Exposes a slot in the Unreal UI to select the road mesh asset, which will be used to visually represent the road along the spline
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Road Visuals")
	UStaticMesh* RoadStaticMesh;

	UPROPERTY(Transient, VisibleAnywhere, Category = "Road Visuals")
	TArray<class USplineMeshComponent*> ActiveSplineMeshes;
	

	// Initialize the nodes and deforms the mesh between them to create the road segment
	void InitializeRoad(FVector StartPos, FVector EndPos);

};