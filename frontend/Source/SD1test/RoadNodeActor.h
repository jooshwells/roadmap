#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoadNodeActor.generated.h"

UCLASS()
class SD1TEST_API ARoadNodeActor : public AActor
{
    GENERATED_BODY()

public:
    ARoadNodeActor();

protected:
    virtual void BeginPlay() override;

public:
    UPROPERTY(VisibleAnywhere)
    UStaticMeshComponent* Mesh;
};
